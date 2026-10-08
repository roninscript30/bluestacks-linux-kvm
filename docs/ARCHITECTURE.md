# Architecture & Component Guide (`docs/ARCHITECTURE.md`)

This document explains what every directory and file in `bluestacks-linux-kvm` does and how the Windows Hypervisor Platform (`WHPX`) API is translated directly to Linux `/dev/kvm`.

---

## 1. High-Level Overview

BlueStacks 5 (`HD-Player.exe`) consists of two main parts running inside Wine:
1. **The Qt Frontend & PGA Graphics Server (`HD-Player.exe`)**: Creates the window, manages user input, and runs the proprietary **PGA (Proprietary Graphics Acceleration)** server that translates Android guest GLES commands (sent over a Virtio/VMMDev shared-memory pipe) into host OpenGL calls.
2. **The Virtual Machine Monitor (`BstkVMM.dll`)**: A heavily modified fork of VirtualBox's VMM (`NEM` - Native Execution Manager) that boots the Android x86\_64 Linux kernel (`initrd_boot.img` + `Root.vhd` + `Data.vhdx`).

On Windows, `BstkVMM.dll` either uses its kernel driver (`BstkDrv_nxt.sys`) or, when Hyper-V is active, Microsoft's **Windows Hypervisor Platform (`WHPX`)** API through `WinHvPlatform.dll` and `WinHvEmulation.dll`. Under Wine the driver cannot run, so `install.sh` makes BlueStacks 5.22 take the WHPX path (via [`scripts/bstpatch.py`](../scripts/bstpatch.py)):
- `HD-Player.exe` selects its Hyper-V VM path unconditionally (normally it requires CPUID leaf `0x40000000` = `Microsoft Hv`).
- `BstkRT.dll`'s SUPLib switches to its driverless mode when the support driver cannot be opened, so `BstkVMM.dll` falls back from HM to NEM.
- `BstkVMM.dll` skips VirtualBox's "running inside a Hyper-V partition" CPUID probe and loads `WinHvPlatform.dll` directly.
- The `BlueStacksDrv_nxt` service runs a no-op driver ([`src/bstkdrv_stub.c`](../src/bstkdrv_stub.c)), since `HD-Player.exe` refuses to start the VM when the service cannot start.

Our project provides custom implementations of those DLLs that run the Android VM on Linux `/dev/kvm`. Because Wine traps `syscall` instructions executed from Windows PE code (syscall user dispatch) and services them as NT system calls, the KVM work lives in a Linux shared library (`winhv_kvm.so`) that the PE DLLs reach through Wine unix calls, the same mechanism Wine's own DLLs use.

---

## 2. File-by-File Breakdown

### `src/WinHvPlatform.c` & `src/winhv_unixlib.h`
The PE side of the bridge (`WinHvPlatform.dll`). Each `WHv*` export packs its arguments into a `winhv_call_params` block and forwards it with `__wine_unix_call_dispatcher` to `winhv_kvm.so`, which it loads from its own directory with `__wine_load_unix_lib` (both from the installed Wine's `libwinecrt0.a`, so `build.sh` needs Wine's PE import libraries). `winhv_unixlib.h` holds the shared list of forwarded functions and call IDs.

### `src/winhv_kvm.c`
The core hypervisor bridge (~2,300 lines of C), built as the Linux shared library `winhv_kvm.so` and exporting only `__wine_unix_call_funcs`.
- **libc system calls only**: Its `SIGPROF` watchdog and `SIGURG` cancel handlers can run while a thread is executing PE code, where Wine only lets syscalls made from libc's text through, so every syscall and the signal-return trampoline come from libc.
- **Partition Lifecycle (`WHvCreatePartition`, `WHvSetupPartition`, `WHvDeletePartition`)**:
  - Opens `/dev/kvm`, calls `KVM_CREATE_VM`, configures `KVM_SET_TSS_ADDR`, and initializes an internal `whv_partition` tracking structure.
- **Guest Physical Memory Mapping (`WHvMapGpaRange`, `WHvUnmapGpaRange`)**:
  - Translates WHPX GPA mappings into `KVM_SET_USER_MEMORY_REGION` slots (`0..63`), supporting dynamic sub-splitting, unmapping, and read-only ROM ranges.
- **Register Translation (`WHvGetVirtualProcessorRegisters`, `WHvSetVirtualProcessorRegisters`)**:
  - Maps 54 WHPX register IDs (`WHvX64RegisterRax`..`WHvX64RegisterXmm15`, `CR0..CR4`, `GDTR`, `IDTR`, `EFER`, `ApicBase`, `SpecCtrl`, `Tsc`, `PendingInterruption`, `InterruptState`, `XCr0`) to `KVM_GET_REGS`, `KVM_GET_SREGS`, `KVM_GET_XCAVE`, `KVM_GET_MSRS`, and `KVM_GET_VCPU_events`.
- **Execution Loop (`WHvRunVirtualProcessor`)**:
  - Enters `ioctl(vcpu_fd, KVM_RUN, 0)`.
  - Handles fast-paths in-place (CPUID, MSRs, TSC-deadline timer, ACPI PM-timer `0x408`, PIT `0x40..0x43`, UART `0x3f8`, PCI config `0xcf8/0xcfc`, string I/O `REP INS/OUTS`, and xAPIC MMIO `0xfee00000..0xfee00fff`) before returning to `BstkVMM.dll` only when device emulation is required.
- **Asynchronous Cancellation (`WHvCancelRunVirtualProcessor`)**:
  - Sets `run->immediate_exit = 1` and sends `SIGURG` via `SYS_tgkill` to kick a target VCPU thread out of `KVM_RUN` immediately when another VCPU or host I/O thread raises an interrupt.

### `src/WinHvEmulation.c`
Implements Microsoft's instruction emulator API (`WHvEmulatorCreateEmulator`, `WHvEmulatorTryIoEmulation`, `WHvEmulatorTryMmIoEmulation`).
- Because `winhv_kvm.c` already pre-decodes the guest instruction bytes and populates `IoPortAccess` / `MemoryAccess` exit contexts, `WinHvEmulation.c` acts as a zero-overhead dispatcher that invokes `BstkVMM.dll`'s registered `WHvEmulatorIoPortCallback` or `WHvEmulatorMemoryCallback` and commits read results back to the VCPU's GPRs.

### `src/ffmpeg_stub.c`
- BlueStacks spawns `ffmpeg.exe -list_devices true -f dshow -i dummy` every few seconds to enumerate webcams. Under Wine + DXVK, each `ffmpeg.exe` process initializes Vulkan/D3D11 adapters, causing periodic 50–100ms frame-time spikes.
- `ffmpeg_stub.c` compiles into a tiny 3KB `ffmpeg.exe` binary that immediately exits with `0`, completely eliminating camera-polling stutters.

### `src/bstdns.c`
- `BstkSVC.exe` (the VirtualBox COM server) calls `DnsQueryConfig(DnsConfigDnsServerList, DNS_CONFIG_FLAG_ALLOC, ...)` with an 8-byte buffer and expects a `LocalAlloc`'d `IP4_ARRAY` pointer back. Wine ignores the flag and writes the array itself whenever it fits (exactly one IPv4 DNS server), so `BstkSVC.exe` crashes while creating the `VirtualBox` object.
- `bstdns.dll` exports a `DnsQueryConfig` that implements the ALLOC flag on top of Wine's `dnsapi.dll`; `install.sh` renames `BstkSVC.exe`'s `DNSAPI.dll` import to `bstdns.dll`.

### `scripts/`
- **[`scripts/common.sh`](../scripts/common.sh)**: Shared paths sourced by every script. BlueStacks lives in the dedicated `~/.bluestacks` prefix (`BLUESTACKS_PREFIX` overrides it).
- **[`scripts/build.sh`](../scripts/build.sh)**: Compiles `WinHvPlatform.dll`, `WinHvEmulation.dll`, `vid.dll`, `bstdns.dll`, and `ffmpeg.exe` using `clang -target x86_64-pc-windows-gnu -fuse-ld=lld -O2`.
- **[`scripts/install.sh`](../scripts/install.sh)**: Checks dependencies and `/dev/kvm` permissions, creates the prefix, and installs BlueStacks: it reads the version, CDN path and Android image from the bluestacks.com web installer, downloads the full installer + image, patches them for Wine (see the README's *Wine Compatibility Fixes*) and runs the full installer silently. It then applies all 240 FPS / ROG 2 / PGA OpenGL patches to every `bluestacks.conf` instance.
- **[`scripts/bstpatch.py`](../scripts/bstpatch.py)**: Standard-library-only PE/.NET patcher used by `install.sh` (stub a void .NET method, rename an import DLL or function, mark a section writable).
- **[`scripts/launch.sh`](../scripts/launch.sh)**: Switches the host CPU power profile to `performance`, sets NVIDIA PRIME / Mesa glthread environment variables, spawns `guest-tune.sh` in the background, and launches `HD-Player.exe`.
- **[`scripts/guest-tune.sh`](../scripts/guest-tune.sh)**: Connects to the running Android VM via `HD-Adb.exe` (using a base64-encoded single-line payload) and configures the guest Linux kernel and SurfaceFlinger for 240 FPS.

