#include <stdint.h>
#include <stddef.h>
#include "mem_helpers.h"

static inline void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t off) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = flags;
    register int64_t r8 __asm__("r8") = fd;
    register int64_t r9 __asm__("r9") = off;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(9), "D"(addr), "S"(len), "d"(prot), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return (ret < 0 && ret > -4096) ? (void *)-1 : (void *)ret;
}

static inline int64_t sys_munmap(void *addr, size_t len) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(11), "D"(addr), "S"(len) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_ioctl(int fd, uint64_t req, uint64_t arg) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(16), "D"(fd), "S"(req), "d"(arg) : "rcx", "r11", "memory");
    return ret;
}

#define KVM_RUN          0xae80UL
#define KVM_EXIT_IO      2
#define KVM_EXIT_MMIO    6
#define KVM_EXIT_IO_IN   0
#define KVM_EXIT_IO_OUT  1

struct kvm_regs {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rsp, rbp;
    uint64_t r8,  r9,  r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
};

struct kvm_segment {
    uint64_t base;
    uint32_t limit;
    uint16_t selector;
    uint8_t  type;
    uint8_t  present, dpl, db, s, l, g, avl;
    uint8_t  unusable;
    uint8_t  padding;
};

struct kvm_dtable {
    uint64_t base;
    uint16_t limit;
    uint16_t padding[3];
};

struct kvm_sregs {
    struct kvm_segment cs, ds, es, fs, gs, ss;
    struct kvm_segment tr, ldt;
    struct kvm_dtable gdt, idt;
    uint64_t cr0, cr2, cr3, cr4, cr8;
    uint64_t efer;
    uint64_t apic_base;
    uint64_t interrupt_bitmap[4];
};

struct kvm_vcpu_events {
    struct {
        uint8_t injected;
        uint8_t nr;
        uint8_t has_error_code;
        uint8_t pending;
        uint32_t error_code;
    } exception;
    struct {
        uint8_t injected;
        uint8_t nr;
        uint8_t soft;
        uint8_t shadow;
    } interrupt;
    struct {
        uint8_t injected;
        uint8_t pending;
        uint8_t masked;
        uint8_t pad;
    } nmi;
    uint32_t sipi_vector;
    uint32_t flags;
    struct {
        uint8_t smm;
        uint8_t pending;
        uint8_t smm_inside_nmi;
        uint8_t latched_init;
    } smi;
    uint8_t reserved[27];
    uint8_t exception_has_payload;
    uint64_t exception_payload;
};

struct kvm_sync_regs {
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    struct kvm_vcpu_events events;
};

struct kvm_run {
    uint8_t request_interrupt_window;
    uint8_t immediate_exit;
    uint8_t padding1[6];
    uint32_t exit_reason;
    uint8_t ready_for_interrupt_injection;
    uint8_t if_flag;
    uint16_t flags;
    uint64_t cr8;
    uint64_t apic_base;
    union {
        struct {
            uint8_t direction;
            uint8_t size;
            uint16_t port;
            uint32_t count;
            uint64_t data_offset;
        } io;
        struct {
            uint64_t phys_addr;
            uint8_t  data[8];
            uint32_t len;
            uint8_t  is_write;
        } mmio;
        uint8_t padding[256];
    };
    uint64_t kvm_valid_regs;
    uint64_t kvm_dirty_regs;
    union {
        struct kvm_sync_regs regs;
        uint8_t padding[2048];
    } s;
};

struct whv_vcpu {
    int created;
    int fd;
    struct kvm_run *run;
    volatile int tid;
    volatile int tgid;
    volatile int in_run;
    volatile int cancel_requested;
    volatile uint64_t run_seq;
    volatile uint64_t last_seen_seq;
    uint64_t tsc_aux;
    int sync_regs_valid;
    int timer_created;
    int timer_id;
    void * volatile vbox_vcpu;
};

/*
 * BstkVMM.dll (hmR3WhpxExitIoPortAccess @ 0x18020eaf0 and hmR3WhpxExitMemoryAccess @ 0x18020ef90)
 * passes pVCpu as Context, and skips calling hmR3WhpxImportVpContext (0x18020da40) when
 * WHvEmulatorTry*Emulation returns EmulationSuccessful (1).
 * Sync pVCpu->cpum.GstCtx.rip (+0x19140), eflags (+0x19148), and fInterruptShadow (+0xa7c8)
 * directly so hmR3WhpxEvalPendingInterrupts (0x18020ce50) sees the live IF flag and cleared shadow.
 */
static inline void sync_vbox_vcpu_context(struct whv_vcpu *vcpu, void *Context, const struct kvm_run *run) {
    if (!Context || !run) return;
    if (vcpu && vcpu->vbox_vcpu != Context) {
        vcpu->vbox_vcpu = Context;
    }
    uint8_t *pVCpu = (uint8_t *)Context;
    uint64_t rip = run->s.regs.regs.rip;
    uint32_t rflags = (uint32_t)run->s.regs.regs.rflags;
    *(uint64_t *)(pVCpu + 0x19140) = rip;
    uint32_t efl = (*(uint32_t *)(pVCpu + 0x19148) & 0xff000001U) | (rflags & 0x003fffffU);
    if (run->s.regs.events.interrupt.shadow & 1U) {
        efl |= 0x00c00000U;
        *(uint64_t *)(pVCpu + 0x19158) = rip;
        pVCpu[0xa7c8] |= 1U;
    } else {
        pVCpu[0xa7c8] &= ~1U;
    }
    *(uint32_t *)(pVCpu + 0x19148) = efl;
}

#pragma pack(push, 1)
struct whv_emu_io_info {
    uint8_t  Direction;
    uint8_t  Reserved;
    uint16_t Port;
    uint16_t AccessSize;
    uint16_t Reserved2;
    uint32_t Data;
};

struct whv_emu_mem_info {
    uint64_t GpaAddress;
    uint8_t  Direction;
    uint8_t  AccessSize;
    uint8_t  Data[8];
};
#pragma pack(pop)

typedef int32_t (__cdecl *PFN_IO_PORT_CB)(void *Context, struct whv_emu_io_info *IoAccess);
typedef int32_t (__cdecl *PFN_MEMORY_CB)(void *Context, struct whv_emu_mem_info *MemoryAccess);
typedef int32_t (__cdecl *PFN_GET_REGS_CB)(void *Context, const uint32_t *Names, uint32_t Count, void *Values);
typedef int32_t (__cdecl *PFN_SET_REGS_CB)(void *Context, const uint32_t *Names, uint32_t Count, const void *Values);
typedef int32_t (__cdecl *PFN_TRANSLATE_GVA_CB)(void *Context, uint64_t Gva, uint32_t Flags, uint32_t *Result, uint64_t *Gpa);

struct whv_emulator_callbacks {
    uint32_t Size;
    uint32_t Reserved;
    PFN_IO_PORT_CB WHvEmulatorIoPortCallback;
    PFN_MEMORY_CB WHvEmulatorMemoryCallback;
    PFN_GET_REGS_CB WHvEmulatorGetVirtualProcessorRegisters;
    PFN_SET_REGS_CB WHvEmulatorSetVirtualProcessorRegisters;
    PFN_TRANSLATE_GVA_CB WHvEmulatorTranslateGvaPage;
};

struct whv_emulator {
    struct whv_emulator_callbacks Callbacks;
};

__declspec(dllexport) int32_t __cdecl WHvEmulatorCreateEmulator(
    const struct whv_emulator_callbacks *Callbacks,
    void **Emulator)
{
    if (!Callbacks || !Emulator) return (int32_t)0x80070057;
    struct whv_emulator *emu = (struct whv_emulator *)sys_mmap(NULL, 4096, 3, 0x22, -1, 0);
    if (emu == (void *)-1 || !emu) return (int32_t)0x8007000e;
    emu->Callbacks = *Callbacks;
    *Emulator = emu;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvEmulatorDestroyEmulator(void *Emulator) {
    if (!Emulator) return (int32_t)0x80070057;
    sys_munmap(Emulator, 4096);
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvEmulatorTryIoEmulation(
    void *Emulator,
    void *Context,
    const void *VpContext,
    const void *IoInstructionContext,
    uint32_t *EmulatorReturnStatus)
{
    (void)VpContext;
    struct whv_emulator *emu = (struct whv_emulator *)Emulator;
    if (!emu || !IoInstructionContext || !EmulatorReturnStatus) return (int32_t)0x80070057;

    struct whv_vcpu *vcpu = *(struct whv_vcpu * const *)((const uint8_t *)IoInstructionContext + 4);
    if (!vcpu || !vcpu->run) {
        *EmulatorReturnStatus = 0;
        return 0;
    }

    struct kvm_run *run = vcpu->run;
    while (1) {
        uint8_t *base = (uint8_t *)run + run->io.data_offset;
        for (uint32_t i = 0; i < run->io.count; i++) {
            struct whv_emu_io_info io;
            io.Direction = (run->io.direction == KVM_EXIT_IO_OUT) ? 1 : 0;
            io.Reserved = 0;
            io.Port = run->io.port;
            io.AccessSize = run->io.size;
            io.Reserved2 = 0;
            io.Data = 0;

            uint8_t *ptr = base + i * run->io.size;
            if (io.Direction == 1) {
                __builtin_memcpy(&io.Data, ptr, run->io.size);
            }
            emu->Callbacks.WHvEmulatorIoPortCallback(Context, &io);
            if (io.Direction == 0) {
                __builtin_memcpy(ptr, &io.Data, run->io.size);
            }
        }

        if (run->io.direction == KVM_EXIT_IO_IN || run->io.count > 1) {
            run->kvm_valid_regs = 0x7ULL;
            run->immediate_exit = 1;
            int64_t r = sys_ioctl(vcpu->fd, KVM_RUN, 0);
            run->immediate_exit = 0;
            if (r == 0 && run->exit_reason == KVM_EXIT_IO) {
                continue;
            }
        }
        break;
    }

    sync_vbox_vcpu_context(vcpu, Context, run);
    *EmulatorReturnStatus = 1; /* EmulationSuccessful */
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvEmulatorTryMmioEmulation(
    void *Emulator,
    void *Context,
    const void *VpContext,
    const void *MmioInstructionContext,
    uint32_t *EmulatorReturnStatus)
{
    (void)VpContext;
    struct whv_emulator *emu = (struct whv_emulator *)Emulator;
    if (!emu || !MmioInstructionContext || !EmulatorReturnStatus) return (int32_t)0x80070057;

    struct whv_vcpu *vcpu = *(struct whv_vcpu * const *)((const uint8_t *)MmioInstructionContext + 4);
    if (!vcpu || !vcpu->run) {
        *EmulatorReturnStatus = 0;
        return 0;
    }

    struct kvm_run *run = vcpu->run;
    while (1) {
        struct whv_emu_mem_info mem;
        __builtin_memset(&mem, 0, sizeof(mem));
        mem.GpaAddress = run->mmio.phys_addr;
        mem.Direction = run->mmio.is_write ? 1 : 0;
        mem.AccessSize = (uint8_t)run->mmio.len;
        if (mem.AccessSize > 8) mem.AccessSize = 8;

        if (mem.Direction == 1) {
            __builtin_memcpy(mem.Data, run->mmio.data, mem.AccessSize);
        }
        emu->Callbacks.WHvEmulatorMemoryCallback(Context, &mem);
        if (mem.Direction == 0) {
            __builtin_memcpy(run->mmio.data, mem.Data, mem.AccessSize);
            run->kvm_valid_regs = 0x7ULL;
            run->immediate_exit = 1;
            int64_t r = sys_ioctl(vcpu->fd, KVM_RUN, 0);
            run->immediate_exit = 0;
            if (r == 0 && run->exit_reason == KVM_EXIT_MMIO) {
                continue;
            }
        }
        break;
    }

    sync_vbox_vcpu_context(vcpu, Context, run);
    *EmulatorReturnStatus = 1; /* EmulationSuccessful */
    return 0;
}

int __stdcall _DllMainCRTStartup(void *hinstDLL, uint32_t fdwReason, void *lpvReserved) {
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return 1;
}
