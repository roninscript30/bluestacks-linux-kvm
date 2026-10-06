# `BstkVMM.dll` Reverse-Engineered Internals (`docs/BSTKVMM_INTERNALS.md`)

To achieve native performance without modifying `BstkVMM.dll` on disk, we disassembled `BstkVMM.dll` (`1,923,440` bytes, SHA256 `a54db2d4...`) and mapped out its internal VirtualBox-based `VMCPU` and `PDMAPIC` structures.

---

## 1. Discovering the `VMCPU` Pointer (`pVCpu`) from `WHvRunVirtualProcessor`

When `BstkVMM.dll`'s `nemR3WinRunGC` (`0x18007cb60`) calls `WHvRunVirtualProcessor`:

```c
HRESULT WHvRunVirtualProcessor(
    WHV_PARTITION_HANDLE Partition,
    UINT32 VpIndex,
    VOID* ExitContext,
    UINT32 ExitContextSizeInBytes
);
```

By inspecting the call stack (`__builtin_frame_address(0)`) inside `WHvRunVirtualProcessor`, we found that `BstkVMM.dll` stores a pointer into `VMCPU` on the caller's stack frame where `*(uint32_t *)ptr == VpIndex` at offset `+0x19900` from the base of `pVCpu`:

```c
pVCpu = (uint8_t *)stack_candidate - 0x19900;
```

We verify this pointer by checking that `*(uint64_t *)(pVCpu + 0x19140) == v->run->s.regs.regs.rip`. Once validated, `v->vbox_vcpu` is cached for the lifetime of the VCPU.

---

## 2. Key `VMCPU` Offsets in `BstkVMM.dll`

| Offset | Type | Field Name | Purpose |
| :--- | :--- | :--- | :--- |
| `pVCpu + 0xa7c8` | `uint8_t` | `fInterruptShadow` | Cleared/updated by `BstkVMM` after MMIO emulation to track `STI`/`MOV SS` shadow |
| `pVCpu + 0xfd20` | `uint8_t *` | `pvApicPageR3` | Pointer to the VCPU's 1 KB xAPIC register page (`0xfee00000`) |
| `pVCpu + 0xfd48` | `void *` | `pvApicPibR3` | Pointer to the APIC Posted-Interrupt Block (`+0x20` = pending notification word) |
| `pVCpu + 0xfd60` | `uint64_t` | `u64PendingPib0` | Pending external APIC interrupt bitmap 0 |
| `pVCpu + 0xfd68` | `uint64_t` | `u64PendingPib1` | Pending external APIC interrupt bitmap 1 |
| `pVCpu + 0xfd70` | `uint32_t` | `u32PendingLvl` | Level-triggered pending interrupt state |
| `pVCpu + 0x102b0` | `uint64_t` | `cEoi` | Diagnostic counter incremented by `apicSetEoi` (`0x18011eb60`) |
| `pVCpu + 0x19140` | `uint64_t` | `cpum.GstCtx.rip` | Guest instruction pointer synced before MMIO/PIO emulation |
| `pVCpu + 0x19148` | `uint64_t` | `cpum.GstCtx.eflags` | Guest `RFLAGS` + VirtualBox interrupt-shadow bits (`bits 22..23`) |
| `pVCpu + 0x19158` | `uint64_t` | `cpum.GstCtx.ripShadow` | Saved RIP before instruction emulation; if unchanged, `BstkVMM` advances `rip += cbInstr` |

---

## 3. Why `cpum.GstCtx.rip` Synchronization Matters

When a guest instruction triggers a `WHvRunVpExitReasonMemoryAccess` or `WHvRunVpExitReasonX64IoPortAccess` exit:
1. `BstkVMM.dll` saves the current `cpum.GstCtx.rip` (`pVCpu + 0x19140`) into `pVCpu + 0x19158`.
2. It runs its MMIO/PIO handler (either via `WHvEmulatorTryMmIoEmulation` or its internal VirtualBox IEM interpreter).
3. After the handler returns, `BstkVMM.dll` checks if `cpum.GstCtx.rip` was modified by the handler. If not, it adds the instruction length (`cbInstr`) to `cpum.GstCtx.rip` and calls `WHvSetVirtualProcessorRegisters(WHvX64RegisterRip)`.

Because our bridge stays inside `WHvRunVirtualProcessor` across thousands of fast-path exits (`CPUID`, `MSR`, `EOI`, `TPR`, `0x408`), `BstkVMM.dll`'s `cpum.GstCtx.rip` is **not** updated on those fast-path exits. Therefore, whenever we *do* return an exit to `BstkVMM.dll`, we sync `*(uint64_t *)(pVCpu + 0x19140) = v->run->s.regs.regs.rip` so `BstkVMM.dll` always advances the true guest RIP!

---

## 4. In-Place xAPIC Emulation (`apicSetEoi`, `apicUpdatePpr`, `apicReadMmio`)

We disassembled `BstkVMM.dll`'s APIC implementation:
- `apicReadMmio` (`0x18011e2c0`)
- `apicWriteMmio` (`0x18011f5c0`)
- `apicSetEoi` (`0x18011eb60`)
- `apicUpdatePpr` (`0x18011f560`)
- `apicClearVector` (`0x18011f150`)
- `apicSignalNextPendingIntr` (`0x18011f370`)

### xAPIC Page Layout (`pvApicPageR3 = *(uint8_t **)(pVCpu + 0xfd20)`)
- `0x080`: Task Priority Register (`TPR`)
- `0x0A0`: Processor Priority Register (`PPR`)
- `0x0B0`: End Of Interrupt (`EOI`)
- `0x0D0`: Logical Destination Register (`LDR`)
- `0x0E0`: Destination Format Register (`DFR`)
- `0x0F0`: Spurious Interrupt Vector Register (`SVR`)
- `0x100 .. 0x170`: In-Service Register (`ISR`, 8 $\times$ 16-byte slots, 256 bits)
- `0x180 .. 0x1F0`: Trigger Mode Register (`TMR`, 8 $\times$ 16-byte slots, 256 bits)
- `0x200 .. 0x270`: Interrupt Request Register (`IRR`, 8 $\times$ 16-byte slots, 256 bits)

### How Our In-Place EOI Fast-Path Works (`src/WinHvPlatform.c`)
On every guest write to `0xfee000b0` (`EOI`):
1. We scan `ISR` (`0x100..0x170`) from slot 7 down to 0 using `31 - __builtin_clz(isr)` to find the highest in-service vector `vec`.
2. We check the corresponding bit in `TMR` (`0x180 + slot * 0x10`). If the interrupt is level-triggered (`TMR` bit is set — e.g. PCI IOAPIC lines), we fall back to `BstkVMM.dll` so the IOAPIC is notified.
3. We check if any pending interrupts exist in `PIB` (`pVCpu + 0xfd48`, `0xfd60`, `0xfd68`, `0xfd70`) or `IRR` (`0x200..0x270`).
4. When the interrupt is edge-triggered (`TMR == 0`, such as the 1000 Hz TSC-deadline timer on vector `0xec` or rescheduling IPIs on `0xfd`/`0xfc`) and no other interrupt is waiting in `IRR`/`PIB`, we:
   - Clear the bit in `ISR`
   - Recompute `PPR` (`0x0A0`) from `TPR` (`0x080`) and the next highest `ISR` bit (exact match to `apicUpdatePpr` at `0x18011f560`)
   - Increment `*(uint64_t *)(pVCpu + 0x102b0)` (`cEoi`)
   - Advance `v->run->s.regs.regs.rip += insn_len` and immediately re-enter `KVM_RUN`!

This single optimization reduced user-space APIC EOI roundtrips by **>96.5%**.

