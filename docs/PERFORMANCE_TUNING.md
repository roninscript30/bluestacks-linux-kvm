# Performance Analysis & 240 FPS Tuning Guide (`docs/PERFORMANCE_TUNING.md`)

When we first booted BlueStacks 5 on Linux via our WHPX-to-KVM bridge, games ran at **10 FPS**, then fluctuated wildly between **20–50 FPS**. Below is the complete root-cause analysis of every bottleneck we uncovered and how each one is fixed in this repository.

---

## 1. Host Laptop EC & CPU Power-Saver Throttling (40% Clock Clamp)

### Root Cause
On Linux laptops with AMD Ryzen (`amd-pstate-epp`) or Intel CPUs, `power-profiles-daemon` often defaults to `power-saver` (`energy_performance_preference = power`, `acpi/platform_profile = power-saver`).
- In `power-saver` mode, the CPU was clamped to **40% of max frequency (~1.3 GHz)** and the laptop's Embedded Controller (EC) capped the shared CPU+GPU thermal envelope.
- Every KVM VM-exit and OpenGL draw call took **2.5$\times$ longer** than normal.

### Fix
`scripts/launch.sh` automatically runs:
```bash
powerprofilesctl set performance 2>/dev/null || true
```
This restores full boost clocks (>4.0 GHz) and unlocks the full discrete GPU power budget.

---

## 2. Guest Linux Kernel Clocksource (`acpi_pm` $\rightarrow$ `tsc`)

### Root Cause
By default, the Android guest kernel inside BlueStacks selected `acpi_pm` (I/O port `0x408`) as its primary timekeeping clocksource.
- Every single `gettimeofday()` / `clock_gettime(CLOCK_MONOTONIC)` call in Android games, SurfaceFlinger, and the Unity/Unreal engine executed an `inl(0x408)` instruction, causing **30,000+ PIO VM-exits per second**.

### Fix
1. **In `WinHvPlatform.c`**: We advertise invariant TSC (`CPUID 0x80000007 EDX bit 8`) and hypervisor frequency leaves (`0x40000000`, `0x40000010`), and emulate port `0x408` directly in-place via `clock_gettime(CLOCK_MONOTONIC)` at 3.579545 MHz.
2. **In `scripts/guest-tune.sh`**: Once Android boots, we switch the guest kernel's active clocksource to hardware `tsc`:
   ```bash
   echo tsc > /sys/devices/system/clocksource/clocksource0/current_clocksource
   ```
   `rdtsc` executes natively inside the CPU with **zero VM-exits**, dropping port `0x408` exits from **30,000/sec to <40/sec**.

---

## 3. xAPIC EOI & TPR MMIO Exit Storms

### Root Cause
With userspace APIC emulation (`BstkVMM.dll`), every local timer tick (`0xec`), IPI (`0xfc`/`0xfd`), and task priority change triggered a guest MMIO write to `0xfee000b0` (`EOI`) or `0xfee00080` (`TPR`), exiting `KVM_RUN`, crossing the Wine MS-ABI boundary into `BstkVMM.dll`, calling `WHvEmulatorTryMmIoEmulation`, and calling `WHvSetVirtualProcessorRegisters(WHvX64RegisterRip)`.

### Fix
We reverse-engineered `BstkVMM.dll`'s `apicSetEoi` (`0x18011eb60`) and `apicUpdatePpr` (`0x18011f560`) and implemented an **in-place xAPIC fast-path** inside `src/WinHvPlatform.c` that services edge-triggered EOIs, TPR updates, LDR/DFR writes, ICR High writes, and 4-byte xAPIC register reads directly in the bridge without leaving `WHvRunVirtualProcessor`.
- Result: **>96.5% reduction** in xAPIC exits to `BstkVMM.dll`.

---

## 4. Wine `SIGPROF` Watchdog Interrupting Active Game VCPUs

### Root Cause
To prevent VCPU threads from missing wakeup signals, a 20ms (`50 Hz`) `ITIMER_PROF` timer sent `SIGURG` to all VCPUs to kick them out of `KVM_RUN`. On busy game rendering threads, this caused 50 forced VM-exits per second per VCPU.

### Fix
Updated `sigprof_handler` in `src/WinHvPlatform.c` so `SIGURG` is **only** sent to a VCPU if `v->cancel_requested` or `v->run->request_interrupt_window` is actually pending.

---

## 5. Android Guest `pagefusion` Daemon & Serial Console `printk` I/O

### Root Cause
- BlueStacks runs a guest service (`init.svc.pagefusion`) that continuously scans guest physical memory pages and hypercalls the host to deduplicate RAM, causing periodic CPU spikes.
- Verbose kernel `printk` messages were written byte-by-byte to UART port `0x3f8` (`outb`), causing thousands of PIO exits during heavy activity.

### Fix
`scripts/guest-tune.sh` automatically stops `pagefusion` (`setprop ctl.stop pagefusion`) and silences `printk` (`echo "0 0 0 0" > /proc/sys/kernel/printk`).

---

## 6. Unlocking 240 FPS in Both `bluestacks.conf` and Guest SurfaceFlinger

### Root Cause
1. **Config Schema Validation**: `HD-Player.exe` validates `bluestacks.conf` against its `iprop` schema. Writing invalid keys like `bst.instance.Nougat32.fps` causes `HD-Player.exe` to abort with `Failed to read configuration file`. Only `max_fps="240"`, `enable_high_fps="1"`, and `enable_vsync="0"` are valid schema keys.
2. **Device Profile & `bst.fps`**: Many mobile games (Free Fire, BGMI, CODM) check the device model (`ro.product.model`) and BlueStacks' internal `/data/bluestacks.prop` (`bst.fps`) before unlocking 90/120/240 FPS modes.

### Fix
- `scripts/install.sh` configures `bluestacks.conf` with `device_profile_code="rogt"` (ASUS ROG 2), `max_fps="240"`, `enable_high_fps="1"`, `enable_vsync="0"`, and `astc_decoding_mode="disabled"`.
- `scripts/guest-tune.sh` sets `setprop bst.fps 240`, `setprop debug.egl.swapinterval 0`, `service call SurfaceFlinger 1035 i32 0`, and patches `/data/bluestacks.prop` with `ASUS_Z01QD` and `bst.fps=240`.

