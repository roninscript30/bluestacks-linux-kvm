# Architecture & Component Guide (`docs/ARCHITECTURE.md`)

This document explains what every directory and file in `bluestacks-linux-kvm` does and how the Windows Hypervisor Platform (`WHPX`) API is translated directly to Linux `/dev/kvm`.

---

## 1. High-Level Overview

BlueStacks 5 (`HD-Player.exe`) consists of two main parts running inside Wine:
1. **The Qt Frontend & PGA Graphics Server (`HD-Player.exe`)**: Creates the window, manages user input, and runs the proprietary **PGA (Proprietary Graphics Acceleration)** server that translates Android guest GLES commands (sent over a Virtio/VMMDev shared-memory pipe) into host OpenGL calls.
2. **The Virtual Machine Monitor (`BstkVMM.dll`)**: A heavily modified fork of VirtualBox's VMM (`NEM` - Native Execution Manager) that boots the Android x86\_64 Linux kernel (`initrd_boot.img` + `Root.vhd` + `Data.vhdx`).

When `BstkVMM.dll` detects that its Windows kernel driver (`BstkDrv.sys`) is unavailable, it falls back to Microsoft's **Windows Hypervisor Platform (`WHPX`)** API by loading `WinHvPlatform.dll` and `WinHvEmulation.dll`.

Our project provides custom implementations of those DLLs that execute **native Linux syscalls (`open`, `ioctl`, `mmap`, `clock_gettime`, `tgkill`) directly from 64-bit Windows PE code** to run the Android VM on Linux `/dev/kvm`.

---

## 2. File-by-File Breakdown

### `src/WinHvPlatform.c` & `src/WinHvPlatform.def`
The core hypervisor bridge (~2,300 lines of standalone C, no libc/CRT dependencies).
- **Direct Linux Syscall Wrapper**: Uses inline `asm volatile ("syscall")` to invoke Linux kernel system calls directly from MS-ABI functions without going through Wine's `ntdll.dll`.
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

### `src/WinHvEmulation.c` & `src/WinHvEmulation.def`
Implements Microsoft's instruction emulator API (`WHvEmulatorCreateEmulator`, `WHvEmulatorTryIoEmulation`, `WHvEmulatorTryMmIoEmulation`).
- Because `WinHvPlatform.c` already pre-decodes the guest instruction bytes and populates `IoPortAccess` / `MemoryAccess` exit contexts, `WinHvEmulation.c` acts as a zero-overhead dispatcher that invokes `BstkVMM.dll`'s registered `WHvEmulatorIoPortCallback` or `WHvEmulatorMemoryCallback` and commits read results back to the VCPU's GPRs.

### `src/ffmpeg_stub.c`
- BlueStacks spawns `ffmpeg.exe -list_devices true -f dshow -i dummy` every few seconds to enumerate webcams. Under Wine + DXVK, each `ffmpeg.exe` process initializes Vulkan/D3D11 adapters, causing periodic 50–100ms frame-time spikes.
- `ffmpeg_stub.c` compiles into a tiny 3KB `ffmpeg.exe` binary that immediately exits with `0`, completely eliminating camera-polling stutters.

### `scripts/`
- **[`scripts/build.sh`](../scripts/build.sh)**: Compiles `WinHvPlatform.dll`, `WinHvEmulation.dll`, `vid.dll`, and `ffmpeg.exe` using `clang -target x86_64-pc-windows-gnu -fuse-ld=lld -O2`.
- **[`scripts/install.sh`](../scripts/install.sh)**: Auto-detects dependencies, `/dev/kvm` permissions, Wine prefixes, `HD-Player.exe`, and `bluestacks.conf` instances, then applies all 240 FPS / ROG 2 / PGA OpenGL patches.
- **[`scripts/launch.sh`](../scripts/launch.sh)**: Switches the host CPU power profile to `performance`, sets NVIDIA PRIME / Mesa glthread environment variables, spawns `guest-tune.sh` in the background, and launches `HD-Player.exe`.
- **[`scripts/guest-tune.sh`](../scripts/guest-tune.sh)**: Connects to the running Android VM via `HD-Adb.exe` (using a base64-encoded single-line payload) and configures the guest Linux kernel and SurfaceFlinger for 240 FPS.

