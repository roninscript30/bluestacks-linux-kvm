#include <stdint.h>
#include <stddef.h>
#include "mem_helpers.h"

/* ============================================================================
 * Raw Linux x86_64 Syscall Wrappers (callable directly from 64-bit Wine PE DLL)
 * ============================================================================ */

static inline int64_t sys_write(int fd, const void *buf, size_t count) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(1), "D"(fd), "S"(buf), "d"(count) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_open(const char *path, int flags, int mode) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(2), "D"(path), "S"(flags), "d"(mode) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_close(int fd) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(3), "D"(fd) : "rcx", "r11", "memory");
    return ret;
}

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

static inline int64_t sys_rt_sigaction(int sig, const void *act, void *oact, size_t sigsetsize) {
    int64_t ret;
    register size_t r10 __asm__("r10") = sigsetsize;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(13), "D"(sig), "S"(act), "d"(oact), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_rt_sigprocmask(int how, const void *set, void *oset, size_t sigsetsize) {
    int64_t ret;
    register size_t r10 __asm__("r10") = sigsetsize;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "0"(14), "D"(how), "S"(set), "d"(oset), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_ioctl(int fd, uint64_t req, uint64_t arg) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(16), "D"(fd), "S"(req), "d"(arg) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_getpid(void) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(39) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_gettid(void) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(186) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_tgkill(int tgid, int tid, int sig) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(234), "D"(tgid), "S"(tid), "d"(sig) : "rcx", "r11", "memory");
    return ret;
}

/* ============================================================================
 * Logging helper to /tmp/whv_kvm.log
 * ============================================================================ */

static int g_log_fd = -1;

static void log_init(void) {
    if (g_log_fd < 0) {
        /* O_WRONLY | O_CREAT | O_APPEND = 1 | 64 | 1024 = 1089 */
        g_log_fd = (int)sys_open("/tmp/whv_kvm.log", 1089, 0666);
    }
}

static void log_str(const char *s) {
    log_init();
    if (g_log_fd >= 0) {
        size_t len = 0;
        while (s[len]) len++;
        sys_write(g_log_fd, s, len);
    }
}

static void log_hex(const char *prefix, uint64_t val, const char *suffix) {
    char buf[128];
    int pos = 0;
    while (prefix && *prefix && pos < 80) buf[pos++] = *prefix++;
    buf[pos++] = '0';
    buf[pos++] = 'x';
    for (int i = 60; i >= 0; i -= 4) {
        int d = (val >> i) & 0xf;
        if (d || i == 0 || (pos > 2 && buf[pos - 1] != 'x')) {
            buf[pos++] = (d < 10) ? ('0' + d) : ('a' + d - 10);
        }
    }
    while (suffix && *suffix && pos < 126) buf[pos++] = *suffix++;
    buf[pos] = 0;
    log_str(buf);
}

/* ============================================================================
 * Linux KVM ioctl definitions & structs
 * ============================================================================ */

#define KVM_GET_API_VERSION       0xae00UL
#define KVM_CREATE_VM             0xae01UL
#define KVM_GET_MSR_INDEX_LIST    0xc004ae02UL
#define KVM_CHECK_EXTENSION       0xae03UL
#define KVM_GET_VCPU_MMAP_SIZE    0xae04UL
#define KVM_GET_SUPPORTED_CPUID   0xc008ae05UL

#define KVM_SET_TSS_ADDR          0xae47UL
#define KVM_SET_IDENTITY_MAP_ADDR 0x4008ae48UL
#define KVM_CREATE_VCPU           0xae41UL
#define KVM_SET_USER_MEMORY_REGION 0x4020ae46UL
#define KVM_ENABLE_CAP            0x4068ae68UL

#define KVM_RUN                   0xae80UL
#define KVM_GET_REGS              0x8090ae81UL
#define KVM_SET_REGS              0x4090ae82UL
#define KVM_GET_SREGS             0x8138ae83UL
#define KVM_SET_SREGS             0x4138ae84UL
#define KVM_TRANSLATE             0xc018ae85UL
#define KVM_INTERRUPT             0x4004ae86UL
#define KVM_GET_MSRS              0xc008ae88UL
#define KVM_SET_MSRS              0x4008ae89UL
#define KVM_GET_FPU               0x81a0ae8cUL
#define KVM_SET_FPU               0x41a0ae8dUL
#define KVM_SET_CPUID2            0x4008ae90UL
#define KVM_GET_VCPU_EVENTS       0x8040ae9fUL
#define KVM_SET_VCPU_EVENTS       0x4040aea0UL
#define KVM_GET_DEBUGREGS         0x8080aea1UL
#define KVM_SET_DEBUGREGS         0x4080aea2UL
#define KVM_GET_XCRS              0x8188aea6UL
#define KVM_SET_XCRS              0x4188aea7UL

#define KVM_CAP_X86_USER_SPACE_MSR 188
#define KVM_MSR_EXIT_REASON_INVAL   (1 << 0)
#define KVM_MSR_EXIT_REASON_UNKNOWN (1 << 1)
#define KVM_MSR_EXIT_REASON_FILTER  (1 << 2)

#define KVM_EXIT_UNKNOWN          0
#define KVM_EXIT_EXCEPTION        1
#define KVM_EXIT_IO               2
#define KVM_EXIT_HYPERCALL        3
#define KVM_EXIT_DEBUG            4
#define KVM_EXIT_HLT              5
#define KVM_EXIT_MMIO             6
#define KVM_EXIT_IRQ_WINDOW_OPEN  7
#define KVM_EXIT_SHUTDOWN         8
#define KVM_EXIT_FAIL_ENTRY       9
#define KVM_EXIT_INTR             10
#define KVM_EXIT_X86_RDMSR        29
#define KVM_EXIT_X86_WRMSR        30

#define KVM_EXIT_IO_IN            0
#define KVM_EXIT_IO_OUT           1

struct kvm_userspace_memory_region {
    uint32_t slot;
    uint32_t flags;
    uint64_t guest_phys_addr;
    uint64_t memory_size;
    uint64_t userspace_addr;
};

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

struct kvm_fpu {
    uint8_t  fpr[8][16];
    uint16_t fcw;
    uint16_t fsw;
    uint8_t  ftwx;
    uint8_t  pad1;
    uint16_t last_opcode;
    uint64_t last_ip;
    uint64_t last_dp;
    uint8_t  xmm[16][16];
    uint32_t mxcsr;
    uint32_t pad2;
};

struct kvm_debugregs {
    uint64_t db[4];
    uint64_t dr6;
    uint64_t dr7;
    uint64_t flags;
    uint64_t reserved[9];
};

struct kvm_xcr {
    uint32_t xcr;
    uint32_t reserved;
    uint64_t value;
};

struct kvm_xcrs {
    uint32_t nr_xcrs;
    uint32_t flags;
    struct kvm_xcr xcrs[16];
    uint64_t padding[16];
};

struct kvm_msr_entry {
    uint32_t index;
    uint32_t reserved;
    uint64_t data;
};

struct kvm_msrs_buf {
    uint32_t nmsrs;
    uint32_t pad;
    struct kvm_msr_entry entries[64];
};

struct kvm_cpuid_entry2 {
    uint32_t function;
    uint32_t index;
    uint32_t flags;
    uint32_t eax, ebx, ecx, edx;
    uint32_t padding[3];
};

struct kvm_cpuid2_buf {
    uint32_t nent;
    uint32_t padding;
    struct kvm_cpuid_entry2 entries[256];
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

struct kvm_translation {
    uint64_t linear_address;
    uint64_t physical_address;
    uint8_t  valid;
    uint8_t  writeable;
    uint8_t  usermode;
    uint8_t  pad[5];
};

struct kvm_enable_cap {
    uint32_t cap;
    uint32_t flags;
    uint64_t args[4];
    uint8_t  pad[64];
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
            uint64_t hardware_exit_reason;
        } hw;
        struct {
            uint64_t hardware_entry_failure_reason;
            uint32_t cpu;
        } fail_entry;
        struct {
            uint32_t exception;
            uint32_t error_code;
        } ex;
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
        struct {
            uint8_t  error;
            uint8_t  pad[7];
            uint32_t reason;
            uint32_t index;
            uint64_t data;
        } msr;
        uint8_t padding[256];
    };
    uint64_t kvm_valid_regs;
    uint64_t kvm_dirty_regs;
    union {
        struct kvm_sync_regs regs;
        uint8_t padding[2048];
    } s;
};

/* ============================================================================
 * WHV Data Structures (verified against BstkVMM.dll disassembly)
 * ============================================================================ */

struct whv_segment {
    uint64_t Base;
    uint32_t Limit;
    uint16_t Selector;
    uint16_t Attributes;
};

struct whv_table {
    uint16_t Pad[3];
    uint16_t Limit;
    uint64_t Base;
};

union whv_reg_value {
    uint64_t Reg64[2];
    uint32_t Reg32[4];
    uint16_t Reg16[8];
    uint8_t  Reg8[16];
    struct whv_segment Segment;
    struct whv_table Table;
};

struct whv_vp_exit_context {
    uint16_t ExecutionState;
    uint8_t  InstLenCr8;
    uint8_t  Reserved;
    uint32_t Reserved2;
    struct whv_segment Cs;
    uint64_t Rip;
    uint64_t Rflags;
};

/* ============================================================================
 * Internal KVM Partition & VCPU State
 * ============================================================================ */

#define MAX_VCPUS     64
#define MAX_MEMSLOTS  512

struct whv_vcpu {
    int created;
    int fd;
    struct kvm_run *run;
    volatile int tid;
    volatile int tgid;
    volatile int in_run;
    volatile int cancel_requested;
    volatile uint64_t run_seq;
    uint64_t last_seen_seq;
    uint64_t tsc_aux;
    int sync_regs_valid;
    int timer_created;
    int timer_id;
    void * volatile vbox_vcpu;
};

struct k_sigevent {
    uint64_t sigev_value;
    int32_t  sigev_signo;
    int32_t  sigev_notify;
    int32_t  sigev_tid;
    int32_t  pad[11];
};

struct k_itimerspec {
    struct { int64_t tv_sec; int64_t tv_nsec; } it_interval;
    struct { int64_t tv_sec; int64_t tv_nsec; } it_value;
};

static inline int64_t sys_timer_create(int clockid, const struct k_sigevent *sevp, int *timerid) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(222), "D"(clockid), "S"(sevp), "d"(timerid) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_timer_settime(int timerid, int flags, const struct k_itimerspec *new_val, struct k_itimerspec *old_val) {
    int64_t ret;
    register void *r10 __asm__("r10") = old_val;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(223), "D"(timerid), "S"(flags), "d"(new_val), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_timer_delete(int timerid) {
    int64_t ret;
    __asm__ volatile("syscall" : "=a"(ret) : "0"(226), "D"(timerid) : "rcx", "r11", "memory");
    return ret;
}

struct whv_memslot {
    int active;
    uint32_t slot_id;
    uint64_t gpa;
    uint64_t size;
    uint64_t hva;
    uint32_t kvm_flags;
};

struct whv_partition {
    uint32_t magic;
    volatile int lock;
    int kvm_fd;
    int vm_fd;
    size_t vcpu_mmap_size;
    uint32_t processor_count;
    uint64_t extended_vm_exits;
    uint64_t exception_exit_bitmap;
    struct whv_vcpu vcpus[MAX_VCPUS];
    struct whv_memslot slots[MAX_MEMSLOTS];
};

static inline void spin_lock(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        __asm__ volatile("pause" ::: "memory");
    }
}

static inline void spin_unlock(volatile int *lock) {
    __sync_lock_release(lock);
}

/* ============================================================================
 * Signal Handler for SIGURG (23) & SIGPROF (27) Watchdog
 * ============================================================================ */

static int g_sigurg_installed = 0;
static int g_main_tid = 0;
static struct whv_partition * volatile g_active_part = NULL;

static void sigurg_handler(int s) {
    (void)s;
}

static void sigprof_handler(int s) {
    (void)s;
    struct whv_partition *part = g_active_part;
    if (!part || part->magic != 0x57485650) return;
    for (uint32_t i = 0; i < part->processor_count && i < MAX_VCPUS; i++) {
        struct whv_vcpu *v = &part->vcpus[i];
        if (!v->created) continue;
        uint64_t seq = v->run_seq;
        if (v->in_run && seq == v->last_seen_seq && v->tid > 0 && v->tgid > 0) {
            if (v->cancel_requested || (v->run && v->run->request_interrupt_window)) {
                sys_tgkill(v->tgid, v->tid, 23 /* SIGURG */);
            }
        }
        v->last_seen_seq = seq;
    }
}

__attribute__((naked)) static void sig_restorer(void) {
    __asm__ volatile("mov $15, %rax\n\tsyscall\n\t");
}

struct k_sigaction {
    void *handler;
    uint64_t flags;
    void *restorer;
    uint64_t mask;
};

static void ensure_sigurg_handler(void) {
    if (!g_sigurg_installed) {
        struct k_sigaction sa;
        sa.handler = (void *)sigurg_handler;
        sa.flags = 0x54000000UL; /* SA_RESTORER | SA_NODEFER | SA_RESTART */
        sa.restorer = (void *)sig_restorer;
        sa.mask = 0;
        sys_rt_sigaction(23 /* SIGURG */, &sa, NULL, 8);
        sa.handler = (void *)sigprof_handler;
        sys_rt_sigaction(27 /* SIGPROF */, &sa, NULL, 8);
        g_sigurg_installed = 1;
    }
}

/* ============================================================================
 * Segment & MSR Helpers
 * ============================================================================ */

static void whv_seg_to_kvm(const struct whv_segment *src, struct kvm_segment *dst, int is_tr) {
    __builtin_memset(dst, 0, sizeof(*dst));
    dst->base = src->Base;
    dst->limit = src->Limit;
    dst->selector = src->Selector;
    uint16_t attr = src->Attributes;
    dst->type = attr & 0x0f;
    dst->s = (attr >> 4) & 1;
    dst->dpl = (attr >> 5) & 3;
    dst->present = (attr >> 7) & 1;
    dst->avl = (attr >> 12) & 1;
    dst->l = (attr >> 13) & 1;
    dst->db = (attr >> 14) & 1;
    dst->g = (attr >> 15) & 1;
    dst->unusable = dst->present ? 0 : 1;
    if (is_tr && (!dst->present || dst->type == 0)) {
        dst->type = 11;
        dst->s = 0;
        dst->present = 1;
        dst->unusable = 0;
        if (!dst->limit) dst->limit = 0xffff;
    }
}

static void kvm_seg_to_whv(const struct kvm_segment *src, struct whv_segment *dst) {
    dst->Base = src->base;
    dst->Limit = src->limit;
    dst->Selector = src->selector;
    if (src->unusable || !src->present) {
        dst->Attributes = 0;
    } else {
        dst->Attributes = (uint16_t)((src->type & 0x0f)
            | ((src->s & 1) << 4)
            | ((src->dpl & 3) << 5)
            | ((src->present & 1) << 7)
            | ((src->avl & 1) << 12)
            | ((src->l & 1) << 13)
            | ((src->db & 1) << 14)
            | ((src->g & 1) << 15));
    }
}

static uint32_t whv_reg_to_msr_index(uint32_t reg) {
    switch (reg) {
        case 0x2000: return 0x00000010; /* IA32_TSC */
        case 0x2001: return 0xc0000080; /* IA32_EFER */
        case 0x2002: return 0xc0000102; /* IA32_KERNEL_GS_BASE */
        case 0x2003: return 0x0000001b; /* IA32_APIC_BASE */
        case 0x2004: return 0x00000277; /* IA32_PAT */
        case 0x2005: return 0x00000174; /* IA32_SYSENTER_CS */
        case 0x2006: return 0x00000176; /* IA32_SYSENTER_EIP */
        case 0x2007: return 0x00000175; /* IA32_SYSENTER_ESP */
        case 0x2008: return 0xc0000081; /* IA32_STAR */
        case 0x2009: return 0xc0000082; /* IA32_LSTAR */
        case 0x200a: return 0xc0000083; /* IA32_CSTAR */
        case 0x200b: return 0xc0000084; /* IA32_FMASK */
        case 0x200d: return 0x000000fe; /* IA32_MTRRCAP */
        case 0x200e: return 0x000002ff; /* IA32_MTRR_DEF_TYPE */
        case 0x2070: return 0x00000250; /* MTRRfix64K_00000 */
        case 0x2071: return 0x00000258; /* MTRRfix16K_80000 */
        case 0x2072: return 0x00000259; /* MTRRfix16K_A0000 */
        case 0x2073: return 0x00000268; /* MTRRfix4K_C0000 */
        case 0x2074: return 0x00000269; /* MTRRfix4K_C8000 */
        case 0x2075: return 0x0000026a; /* MTRRfix4K_D0000 */
        case 0x2076: return 0x0000026b; /* MTRRfix4K_D8000 */
        case 0x2077: return 0x0000026c; /* MTRRfix4K_E0000 */
        case 0x2078: return 0x0000026d; /* MTRRfix4K_E8000 */
        case 0x2079: return 0x0000026e; /* MTRRfix4K_F0000 */
        case 0x207a: return 0x0000026f; /* MTRRfix4K_F8000 */
        case 0x207b: return 0xc0000103; /* IA32_TSC_AUX */
        default:
            if (reg >= 0x2010 && reg <= 0x201f) {
                return 0x00000200 + (reg - 0x2010);
            }
            return 0;
    }
}

/* ============================================================================
 * Exported WinHvPlatform Functions
 * ============================================================================ */

__declspec(dllexport) int32_t __cdecl WHvGetCapability(
    uint32_t CapabilityCode,
    void *CapabilityBuffer,
    uint32_t CapabilityBufferSizeInBytes,
    uint32_t *WrittenSizeInBytes)
{
    if (!CapabilityBuffer) return (int32_t)0x80070057;
    __builtin_memset(CapabilityBuffer, 0, CapabilityBufferSizeInBytes);
    uint32_t written = 0;

    switch (CapabilityCode) {
        case 0x0000: /* WHvCapabilityCodeHypervisorPresent */
            if (CapabilityBufferSizeInBytes >= 4) {
                *(uint32_t *)CapabilityBuffer = 1;
                written = 4;
            }
            break;
        case 0x0001: /* WHvCapabilityCodeFeatures */
            if (CapabilityBufferSizeInBytes >= 8) {
                *(uint64_t *)CapabilityBuffer = 0;
                written = 8;
            }
            break;
        case 0x0002: /* WHvCapabilityCodeExtendedVmExits */
            if (CapabilityBufferSizeInBytes >= 8) {
                /* Bit 0 = X64CpuidExit, Bit 1 = X64MsrExit, Bit 2 = ExceptionExit */
                *(uint64_t *)CapabilityBuffer = 0x7ULL;
                written = 8;
            }
            break;
        case 0x0003: /* WHvCapabilityCodeExceptionExitBitmap */
            if (CapabilityBufferSizeInBytes >= 8) {
                *(uint64_t *)CapabilityBuffer = 0xffffffffffffffffULL;
                written = 8;
            }
            break;
        case 0x1000: /* WHvCapabilityCodeProcessorVendor */
            if (CapabilityBufferSizeInBytes >= 4) {
                *(uint32_t *)CapabilityBuffer = 0; /* AMD = 0 */
                written = 4;
            }
            break;
        case 0x1001: /* WHvCapabilityCodeProcessorFeatures */
            if (CapabilityBufferSizeInBytes >= 8) {
                *(uint64_t *)CapabilityBuffer = 0x7fffffffffffffffULL;
                written = 8;
            }
            break;
        case 0x1002: /* WHvCapabilityCodeProcessorClFlushSize */
            if (CapabilityBufferSizeInBytes >= 1) {
                *(uint8_t *)CapabilityBuffer = 6;
                written = 1;
            }
            break;
        case 0x1003: /* WHvCapabilityCodeProcessorXsaveFeatures */
            if (CapabilityBufferSizeInBytes >= 8) {
                *(uint64_t *)CapabilityBuffer = 0x7ULL;
                written = 8;
            }
            break;
        default:
            written = CapabilityBufferSizeInBytes;
            break;
    }
    if (WrittenSizeInBytes) *WrittenSizeInBytes = written;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvCreatePartition(void **Partition) {
    if (!Partition) return (int32_t)0x80070057;
    ensure_sigurg_handler();

    int kvm_fd = (int)sys_open("/dev/kvm", 2 /* O_RDWR */, 0);
    if (kvm_fd < 0) {
        log_hex("WHvCreatePartition: open(/dev/kvm) failed: ", (uint64_t)(int64_t)kvm_fd, "\n");
        return (int32_t)0x80004005;
    }
    int vm_fd = (int)sys_ioctl(kvm_fd, KVM_CREATE_VM, 0);
    if (vm_fd < 0) {
        log_hex("WHvCreatePartition: KVM_CREATE_VM failed: ", (uint64_t)(int64_t)vm_fd, "\n");
        sys_close(kvm_fd);
        return (int32_t)0x80004005;
    }
    int64_t mmap_sz = sys_ioctl(kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (mmap_sz <= 0) mmap_sz = 12288;

    /* Allocate struct whv_partition via sys_mmap (PROT_READ|PROT_WRITE=3, MAP_PRIVATE|MAP_ANONYMOUS=0x22) */
    size_t alloc_sz = (sizeof(struct whv_partition) + 4095) & ~4095ULL;
    struct whv_partition *part = (struct whv_partition *)sys_mmap(NULL, alloc_sz, 3, 0x22, -1, 0);
    if (part == (void *)-1 || !part) {
        sys_close(vm_fd);
        sys_close(kvm_fd);
        return (int32_t)0x8007000e;
    }
    __builtin_memset(part, 0, alloc_sz);
    part->magic = 0x57485650;
    part->kvm_fd = kvm_fd;
    part->vm_fd = vm_fd;
    part->vcpu_mmap_size = (size_t)mmap_sz;
    part->processor_count = 1;

    /* Set TSS and identity map addr (harmless on AMD SVM, required on Intel VMX) */
    sys_ioctl(vm_fd, KVM_SET_TSS_ADDR, 0xfffbd000UL);
    uint64_t id_addr = 0xfffbc000ULL;
    sys_ioctl(vm_fd, KVM_SET_IDENTITY_MAP_ADDR, (uint64_t)&id_addr);

    /* Enable user-space MSR exit for unknown/invalid MSRs */
    struct kvm_enable_cap cap;
    __builtin_memset(&cap, 0, sizeof(cap));
    cap.cap = KVM_CAP_X86_USER_SPACE_MSR;
    cap.args[0] = KVM_MSR_EXIT_REASON_UNKNOWN | KVM_MSR_EXIT_REASON_INVAL;
    sys_ioctl(vm_fd, KVM_ENABLE_CAP, (uint64_t)&cap);

    *Partition = part;
    log_hex("WHvCreatePartition: success part=", (uint64_t)(uintptr_t)part, "\n");
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvSetupPartition(void *Partition) {
    log_hex("WHvSetupPartition: part=", (uint64_t)(uintptr_t)Partition, "\n");
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvDeletePartition(void *Partition) {
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650) return (int32_t)0x80070057;
    log_hex("WHvDeletePartition: part=", (uint64_t)(uintptr_t)part, "\n");
    for (int i = 0; i < MAX_VCPUS; i++) {
        if (part->vcpus[i].created) {
            if (part->vcpus[i].run && part->vcpus[i].run != (void *)-1) {
                sys_munmap(part->vcpus[i].run, part->vcpu_mmap_size);
            }
            if (part->vcpus[i].fd >= 0) sys_close(part->vcpus[i].fd);
            part->vcpus[i].created = 0;
        }
    }
    if (part->vm_fd >= 0) sys_close(part->vm_fd);
    if (part->kvm_fd >= 0) sys_close(part->kvm_fd);
    part->magic = 0;
    size_t alloc_sz = (sizeof(struct whv_partition) + 4095) & ~4095ULL;
    sys_munmap(part, alloc_sz);
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvGetPartitionProperty(
    void *Partition,
    uint32_t PropertyCode,
    void *PropertyBuffer,
    uint32_t PropertyBufferSizeInBytes,
    uint32_t *WrittenSizeInBytes)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || !PropertyBuffer) return (int32_t)0x80070057;
    __builtin_memset(PropertyBuffer, 0, PropertyBufferSizeInBytes);
    if (PropertyCode == 0x1fff && PropertyBufferSizeInBytes >= 4) {
        *(uint32_t *)PropertyBuffer = part->processor_count;
    } else if (PropertyCode == 1 && PropertyBufferSizeInBytes >= 8) {
        *(uint64_t *)PropertyBuffer = part->extended_vm_exits;
    }
    if (WrittenSizeInBytes) *WrittenSizeInBytes = PropertyBufferSizeInBytes;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvSetPartitionProperty(
    void *Partition,
    uint32_t PropertyCode,
    const void *PropertyBuffer,
    uint32_t PropertyBufferSizeInBytes)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || !PropertyBuffer) return (int32_t)0x80070057;
    if (PropertyCode == 0x1fff && PropertyBufferSizeInBytes >= 4) {
        part->processor_count = *(const uint32_t *)PropertyBuffer;
    } else if (PropertyCode == 1 && PropertyBufferSizeInBytes >= 8) {
        part->extended_vm_exits = *(const uint64_t *)PropertyBuffer;
    } else if (PropertyCode == 2 && PropertyBufferSizeInBytes >= 8) {
        part->exception_exit_bitmap = *(const uint64_t *)PropertyBuffer;
    }
    return 0;
}

/* Internal helper: allocate an unused slot index in part->slots */
static int alloc_slot_index(struct whv_partition *part) {
    for (int i = 0; i < MAX_MEMSLOTS; i++) {
        if (!part->slots[i].active) {
            part->slots[i].slot_id = (uint32_t)i;
            return i;
        }
    }
    return -1;
}

/* Internal helper: unmap range [u_gpa, u_end), splitting/trimming overlapping KVM slots */
static void unmap_gpa_range_locked(struct whv_partition *part, uint64_t u_gpa, uint64_t u_size) {
    if (u_size == 0) return;
    uint64_t u_end = u_gpa + u_size;

    for (int i = 0; i < MAX_MEMSLOTS; i++) {
        if (!part->slots[i].active) continue;
        uint64_t s_gpa = part->slots[i].gpa;
        uint64_t s_size = part->slots[i].size;
        uint64_t s_end = s_gpa + s_size;
        uint64_t s_hva = part->slots[i].hva;
        uint32_t s_flags = part->slots[i].kvm_flags;

        if (s_end <= u_gpa || s_gpa >= u_end) continue;

        /* Delete current KVM slot first */
        struct kvm_userspace_memory_region reg;
        reg.slot = part->slots[i].slot_id;
        reg.flags = 0;
        reg.guest_phys_addr = s_gpa;
        reg.memory_size = 0;
        reg.userspace_addr = s_hva;
        sys_ioctl(part->vm_fd, KVM_SET_USER_MEMORY_REGION, (uint64_t)&reg);
        part->slots[i].active = 0;

        /* Left remainder: [s_gpa, u_gpa) */
        if (s_gpa < u_gpa) {
            uint64_t left_size = u_gpa - s_gpa;
            int idx = alloc_slot_index(part);
            if (idx >= 0) {
                part->slots[idx].active = 1;
                part->slots[idx].gpa = s_gpa;
                part->slots[idx].size = left_size;
                part->slots[idx].hva = s_hva;
                part->slots[idx].kvm_flags = s_flags;
                reg.slot = part->slots[idx].slot_id;
                reg.flags = s_flags;
                reg.guest_phys_addr = s_gpa;
                reg.memory_size = left_size;
                reg.userspace_addr = s_hva;
                sys_ioctl(part->vm_fd, KVM_SET_USER_MEMORY_REGION, (uint64_t)&reg);
            }
        }

        /* Right remainder: [u_end, s_end) */
        if (s_end > u_end) {
            uint64_t right_gpa = u_end;
            uint64_t right_size = s_end - u_end;
            uint64_t right_hva = s_hva + (u_end - s_gpa);
            int idx = alloc_slot_index(part);
            if (idx >= 0) {
                part->slots[idx].active = 1;
                part->slots[idx].gpa = right_gpa;
                part->slots[idx].size = right_size;
                part->slots[idx].hva = right_hva;
                part->slots[idx].kvm_flags = s_flags;
                reg.slot = part->slots[idx].slot_id;
                reg.flags = s_flags;
                reg.guest_phys_addr = right_gpa;
                reg.memory_size = right_size;
                reg.userspace_addr = right_hva;
                sys_ioctl(part->vm_fd, KVM_SET_USER_MEMORY_REGION, (uint64_t)&reg);
            }
        }
    }
}

__declspec(dllexport) int32_t __cdecl WHvMapGpaRange(
    void *Partition,
    void *SourceAddress,
    uint64_t GuestAddress,
    uint64_t SizeInBytes,
    uint32_t Flags)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || !SourceAddress || SizeInBytes == 0) {
        return (int32_t)0x80070057;
    }

    /* Flags bit 1 (0x2) = WHvMapGpaRangeFlagWrite. If not set, mark KVM_MEM_READONLY (1). */
    uint32_t kvm_flags = ((Flags & 2) == 0) ? 1U : 0U;
    uint64_t hva = (uint64_t)(uintptr_t)SourceAddress;

    spin_lock(&part->lock);
    /* Fast path: if an identical slot already exists, no-op */
    for (int i = 0; i < MAX_MEMSLOTS; i++) {
        if (part->slots[i].active &&
            part->slots[i].gpa == GuestAddress &&
            part->slots[i].size == SizeInBytes &&
            part->slots[i].hva == hva &&
            part->slots[i].kvm_flags == kvm_flags) {
            spin_unlock(&part->lock);
            return 0;
        }
    }

    unmap_gpa_range_locked(part, GuestAddress, SizeInBytes);

    int idx = alloc_slot_index(part);
    if (idx < 0) {
        spin_unlock(&part->lock);
        log_str("WHvMapGpaRange: out of memslots!\n");
        return (int32_t)0x8007000e;
    }

    struct kvm_userspace_memory_region reg;
    reg.slot = part->slots[idx].slot_id;
    reg.flags = kvm_flags;
    reg.guest_phys_addr = GuestAddress;
    reg.memory_size = SizeInBytes;
    reg.userspace_addr = hva;

    int64_t r = sys_ioctl(part->vm_fd, KVM_SET_USER_MEMORY_REGION, (uint64_t)&reg);
    if (r < 0) {
        spin_unlock(&part->lock);
        log_hex("WHvMapGpaRange FAILED gpa=", GuestAddress, "");
        log_hex(" size=", SizeInBytes, "");
        log_hex(" err=", (uint64_t)r, "\n");
        return (int32_t)0x80004005;
    }

    part->slots[idx].active = 1;
    part->slots[idx].gpa = GuestAddress;
    part->slots[idx].size = SizeInBytes;
    part->slots[idx].hva = hva;
    part->slots[idx].kvm_flags = kvm_flags;
    spin_unlock(&part->lock);
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvUnmapGpaRange(
    void *Partition,
    uint64_t GuestAddress,
    uint64_t SizeInBytes)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650) return (int32_t)0x80070057;

    spin_lock(&part->lock);
    unmap_gpa_range_locked(part, GuestAddress, SizeInBytes);
    spin_unlock(&part->lock);
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvQueryGpaRangeDirtyBitmap(
    void *Partition,
    uint64_t GuestAddress,
    uint64_t RangeSizeInBytes,
    uint64_t *Bitmap,
    uint32_t BitmapSizeInBytes)
{
    (void)Partition;
    (void)GuestAddress;
    (void)RangeSizeInBytes;
    if (Bitmap && BitmapSizeInBytes > 0) {
        __builtin_memset(Bitmap, 0xff, BitmapSizeInBytes);
    }
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvCreateVirtualProcessor(
    void *Partition,
    uint32_t VpIndex,
    uint32_t Flags)
{
    (void)Flags;
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS) {
        return (int32_t)0x80070057;
    }

    int vcpu_fd = (int)sys_ioctl(part->vm_fd, KVM_CREATE_VCPU, VpIndex);
    if (vcpu_fd < 0) {
        log_hex("WHvCreateVirtualProcessor FAILED err=", (uint64_t)(int64_t)vcpu_fd, "\n");
        return (int32_t)0x80004005;
    }

    /* PROT_READ|PROT_WRITE = 3, MAP_SHARED = 1 */
    struct kvm_run *run = (struct kvm_run *)sys_mmap(NULL, part->vcpu_mmap_size, 3, 1, vcpu_fd, 0);
    if (run == (void *)-1 || !run) {
        sys_close(vcpu_fd);
        return (int32_t)0x8007000e;
    }

    /* Configure CPUID from host KVM_GET_SUPPORTED_CPUID */
    int64_t tsc_khz = sys_ioctl(vcpu_fd, 0xaea3UL /* KVM_GET_TSC_KHZ */, 0);
    if (tsc_khz <= 0) tsc_khz = 3293800;

    struct kvm_cpuid2_buf cpuid;
    __builtin_memset(&cpuid, 0, sizeof(cpuid));
    cpuid.nent = 256;
    if (sys_ioctl(part->kvm_fd, KVM_GET_SUPPORTED_CPUID, (uint64_t)&cpuid) == 0) {
        int has_40000010 = 0;
        for (uint32_t i = 0; i < cpuid.nent; i++) {
            uint32_t fn = cpuid.entries[i].function;
            if (fn == 1) {
                /* Set APIC ID in EBX[31:24] */
                cpuid.entries[i].ebx &= 0x00ffffffU;
                cpuid.entries[i].ebx |= (VpIndex << 24);
                /* Keep Hypervisor Present (ECX[31]) for kvm-clock, clear VMX (ECX[5]) and TSC-Deadline (ECX[24]) */
                cpuid.entries[i].ecx &= ~((1U << 5) | (1U << 24));
                cpuid.entries[i].ecx |= (1U << 31);
            } else if (fn == 0x40000000U) {
                cpuid.entries[i].eax = 0x40000010U;
            } else if (fn == 0x40000001U) {
                /* Expose kvm-clock + stable_bit (keep io_delay so HPET enable delay works) */
                cpuid.entries[i].eax = (1U << 0) | (1U << 3) | (1U << 24);
                cpuid.entries[i].ebx = 0;
                cpuid.entries[i].ecx = 0;
                cpuid.entries[i].edx = 0;
            } else if (fn == 0x40000010U) {
                cpuid.entries[i].eax = (uint32_t)tsc_khz;
                cpuid.entries[i].ebx = 0;
                cpuid.entries[i].ecx = 0;
                cpuid.entries[i].edx = 0;
                has_40000010 = 1;
            } else if (fn == 0x80000001U) {
                /* Clear SVM bit (ECX[2]) */
                cpuid.entries[i].ecx &= ~(1U << 2);
            } else if (fn == 0x80000007U) {
                /* Clear HwPstate (EDX[7]) so Linux init_amd() doesn't clear CONSTANT_TSC, set Invariant TSC (EDX[8]) */
                cpuid.entries[i].edx &= ~(1U << 7);
                cpuid.entries[i].edx |= (1U << 8);
            }
        }
        if (!has_40000010 && cpuid.nent < 256) {
            struct kvm_cpuid_entry2 *e = &cpuid.entries[cpuid.nent++];
            __builtin_memset(e, 0, sizeof(*e));
            e->function = 0x40000010U;
            e->eax = (uint32_t)tsc_khz;
            e->ebx = 0;
        }
        sys_ioctl(vcpu_fd, KVM_SET_CPUID2, (uint64_t)&cpuid);
    }

    run->kvm_valid_regs = 0;
    run->kvm_dirty_regs = 0;

    part->vcpus[VpIndex].fd = vcpu_fd;
    part->vcpus[VpIndex].run = run;
    part->vcpus[VpIndex].tid = 0;
    part->vcpus[VpIndex].tgid = (int)sys_getpid();
    part->vcpus[VpIndex].in_run = 0;
    part->vcpus[VpIndex].cancel_requested = 0;
    part->vcpus[VpIndex].run_seq = 0;
    part->vcpus[VpIndex].last_seen_seq = 0;
    part->vcpus[VpIndex].sync_regs_valid = 0;
    part->vcpus[VpIndex].timer_created = 0;
    part->vcpus[VpIndex].timer_id = 0;
    part->vcpus[VpIndex].created = 1;

    if (VpIndex == 0) {
        ensure_sigurg_handler();
        g_active_part = part;
        int w_tid = g_main_tid > 0 ? g_main_tid : (int)sys_gettid();
        struct k_sigevent sev;
        __builtin_memset(&sev, 0, sizeof(sev));
        sev.sigev_signo = 27; /* SIGPROF */
        sev.sigev_notify = 4; /* SIGEV_THREAD_ID */
        sev.sigev_tid = w_tid;
        int tid_timer = 0;
        if (sys_timer_create(1 /* CLOCK_MONOTONIC */, &sev, &tid_timer) == 0) {
            struct k_itimerspec its;
            its.it_interval.tv_sec = 0;
            its.it_interval.tv_nsec = 20000000; /* 20ms watchdog check */
            its.it_value.tv_sec = 0;
            its.it_value.tv_nsec = 20000000;
            sys_timer_settime(tid_timer, 0, &its, NULL);
            part->vcpus[0].timer_id = tid_timer;
            part->vcpus[0].timer_created = 1;
        }
    }

    log_hex("WHvCreateVirtualProcessor OK vp=", VpIndex, "\n");
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvDeleteVirtualProcessor(
    void *Partition,
    uint32_t VpIndex)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS) {
        return (int32_t)0x80070057;
    }
    if (part->vcpus[VpIndex].created) {
        if (VpIndex == 0 && g_active_part == part) {
            g_active_part = NULL;
        }
        if (part->vcpus[VpIndex].timer_created) {
            sys_timer_delete(part->vcpus[VpIndex].timer_id);
            part->vcpus[VpIndex].timer_created = 0;
        }
        if (part->vcpus[VpIndex].run && part->vcpus[VpIndex].run != (void *)-1) {
            sys_munmap(part->vcpus[VpIndex].run, part->vcpu_mmap_size);
        }
        if (part->vcpus[VpIndex].fd >= 0) {
            sys_close(part->vcpus[VpIndex].fd);
        }
        part->vcpus[VpIndex].created = 0;
    }
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvTranslateGva(
    void *Partition,
    uint32_t VpIndex,
    uint64_t Gva,
    uint32_t TranslateFlags,
    void *TranslationResult,
    uint64_t *Gpa)
{
    (void)TranslateFlags;
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS || !part->vcpus[VpIndex].created) {
        return (int32_t)0x80070057;
    }
    struct whv_vcpu *vcpu = &part->vcpus[VpIndex];
    if (vcpu->sync_regs_valid && (vcpu->run->kvm_dirty_regs & 2ULL)) {
        sys_ioctl(vcpu->fd, KVM_SET_SREGS, (uint64_t)&vcpu->run->s.regs.sregs);
        vcpu->run->kvm_dirty_regs &= ~2ULL;
    }
    struct kvm_translation tr;
    __builtin_memset(&tr, 0, sizeof(tr));
    tr.linear_address = Gva;
    int64_t r = sys_ioctl(vcpu->fd, KVM_TRANSLATE, (uint64_t)&tr);
    if (r == 0 && tr.valid) {
        if (TranslationResult) *(uint64_t *)TranslationResult = 0; /* WHvTranslateGvaResultSuccess */
        if (Gpa) *Gpa = tr.physical_address;
        return 0;
    }
    if (TranslationResult) *(uint64_t *)TranslationResult = 2; /* WHvTranslateGvaResultPageNotPresent */
    if (Gpa) *Gpa = 0;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvGetVirtualProcessorRegisters(
    void *Partition,
    uint32_t VpIndex,
    const uint32_t *RegisterNames,
    uint32_t RegisterCount,
    union whv_reg_value *RegisterValues)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS || !part->vcpus[VpIndex].created) {
        return (int32_t)0x80070057;
    }
    struct whv_vcpu *vcpu = &part->vcpus[VpIndex];
    int fd = vcpu->fd;
    int use_sync = vcpu->sync_regs_valid;

    int need_regs = 0, need_sregs = 0, need_dregs = 0, need_xcrs = 0, need_fpu = 0, need_events = 0;
    struct kvm_msrs_buf msrs;
    msrs.nmsrs = 0;
    msrs.pad = 0;

    for (uint32_t i = 0; i < RegisterCount; i++) {
        uint32_t r = RegisterNames[i];
        if (r <= 0x11) need_regs = 1;
        else if (r >= 0x12 && r <= 0x1f) need_sregs = 1;
        else if (r >= 0x21 && r <= 0x26) need_dregs = 1;
        else if (r == 0x27) need_xcrs = 1;
        else if (r >= 0x1000 && r <= 0x1019) need_fpu = 1;
        else if (r >= 0x2000 && r <= 0x207b) {
            if (r == 0x2001 || r == 0x2003) {
                need_sregs = 1;
            } else if (r != 0x207b) {
                uint32_t idx = whv_reg_to_msr_index(r);
                if (idx && msrs.nmsrs < 64) {
                    msrs.entries[msrs.nmsrs].index = idx;
                    msrs.entries[msrs.nmsrs].reserved = 0;
                    msrs.entries[msrs.nmsrs].data = 0;
                    msrs.nmsrs++;
                }
            }
        } else if (r >= 0x80000000U && r <= 0x80000003U) {
            need_events = 1;
        }
    }

    struct kvm_regs regs_local;
    struct kvm_sregs sregs_local;
    struct kvm_debugregs dregs;
    struct kvm_xcrs xcrs;
    struct kvm_fpu fpu;
    struct kvm_vcpu_events events_local;

    const struct kvm_regs *regs_p = &vcpu->run->s.regs.regs;
    const struct kvm_sregs *sregs_p = &vcpu->run->s.regs.sregs;
    const struct kvm_vcpu_events *events_p = &vcpu->run->s.regs.events;

    if (!use_sync) {
        if (need_regs) sys_ioctl(fd, KVM_GET_REGS, (uint64_t)&regs_local);
        if (need_sregs) sys_ioctl(fd, KVM_GET_SREGS, (uint64_t)&sregs_local);
        if (need_events) sys_ioctl(fd, KVM_GET_VCPU_EVENTS, (uint64_t)&events_local);
        regs_p = &regs_local;
        sregs_p = &sregs_local;
        events_p = &events_local;
    }
    if (need_dregs) sys_ioctl(fd, KVM_GET_DEBUGREGS, (uint64_t)&dregs);
    if (need_xcrs) sys_ioctl(fd, KVM_GET_XCRS, (uint64_t)&xcrs);
    if (need_fpu) sys_ioctl(fd, KVM_GET_FPU, (uint64_t)&fpu);
    if (msrs.nmsrs > 0) sys_ioctl(fd, KVM_GET_MSRS, (uint64_t)&msrs);

    uint32_t msr_pos = 0;
    for (uint32_t i = 0; i < RegisterCount; i++) {
        uint32_t r = RegisterNames[i];
        union whv_reg_value *v = &RegisterValues[i];
        v->Reg64[0] = 0;
        v->Reg64[1] = 0;

        switch (r) {
            case 0x00: v->Reg64[0] = regs_p->rax; break;
            case 0x01: v->Reg64[0] = regs_p->rcx; break;
            case 0x02: v->Reg64[0] = regs_p->rdx; break;
            case 0x03: v->Reg64[0] = regs_p->rbx; break;
            case 0x04: v->Reg64[0] = regs_p->rsp; break;
            case 0x05: v->Reg64[0] = regs_p->rbp; break;
            case 0x06: v->Reg64[0] = regs_p->rsi; break;
            case 0x07: v->Reg64[0] = regs_p->rdi; break;
            case 0x08: v->Reg64[0] = regs_p->r8;  break;
            case 0x09: v->Reg64[0] = regs_p->r9;  break;
            case 0x0a: v->Reg64[0] = regs_p->r10; break;
            case 0x0b: v->Reg64[0] = regs_p->r11; break;
            case 0x0c: v->Reg64[0] = regs_p->r12; break;
            case 0x0d: v->Reg64[0] = regs_p->r13; break;
            case 0x0e: v->Reg64[0] = regs_p->r14; break;
            case 0x0f: v->Reg64[0] = regs_p->r15; break;
            case 0x10: v->Reg64[0] = regs_p->rip; break;
            case 0x11: v->Reg64[0] = regs_p->rflags; break;

            case 0x12: kvm_seg_to_whv(&sregs_p->es, &v->Segment); break;
            case 0x13: kvm_seg_to_whv(&sregs_p->cs, &v->Segment); break;
            case 0x14: kvm_seg_to_whv(&sregs_p->ss, &v->Segment); break;
            case 0x15: kvm_seg_to_whv(&sregs_p->ds, &v->Segment); break;
            case 0x16: kvm_seg_to_whv(&sregs_p->fs, &v->Segment); break;
            case 0x17: kvm_seg_to_whv(&sregs_p->gs, &v->Segment); break;
            case 0x18: kvm_seg_to_whv(&sregs_p->ldt, &v->Segment); break;
            case 0x19: kvm_seg_to_whv(&sregs_p->tr, &v->Segment); break;

            case 0x1a: /* Idtr */
                v->Table.Limit = sregs_p->idt.limit;
                v->Table.Base = sregs_p->idt.base;
                break;
            case 0x1b: /* Gdtr */
                v->Table.Limit = sregs_p->gdt.limit;
                v->Table.Base = sregs_p->gdt.base;
                break;

            case 0x1c: v->Reg64[0] = sregs_p->cr0; break;
            case 0x1d: v->Reg64[0] = sregs_p->cr2; break;
            case 0x1e: v->Reg64[0] = sregs_p->cr3; break;
            case 0x1f: v->Reg64[0] = sregs_p->cr4; break;
            case 0x20: v->Reg64[0] = vcpu->run->cr8; break;

            case 0x21: v->Reg64[0] = dregs.db[0]; break;
            case 0x22: v->Reg64[0] = dregs.db[1]; break;
            case 0x23: v->Reg64[0] = dregs.db[2]; break;
            case 0x24: v->Reg64[0] = dregs.db[3]; break;
            case 0x25: v->Reg64[0] = dregs.dr6;   break;
            case 0x26: v->Reg64[0] = dregs.dr7;   break;
            case 0x27:
                v->Reg64[0] = 1;
                for (uint32_t x = 0; x < xcrs.nr_xcrs && x < 16; x++) {
                    if (xcrs.xcrs[x].xcr == 0) {
                        v->Reg64[0] = xcrs.xcrs[x].value;
                        break;
                    }
                }
                break;

            case 0x1018: /* FpControlStatus */
                v->Reg16[0] = fpu.fcw;
                v->Reg16[1] = fpu.fsw;
                v->Reg8[4]  = fpu.ftwx;
                v->Reg8[5]  = 0;
                v->Reg16[3] = fpu.last_opcode;
                v->Reg64[1] = fpu.last_ip;
                break;
            case 0x1019: /* XmmControlStatus */
                v->Reg64[0] = fpu.last_dp;
                v->Reg32[2] = fpu.mxcsr;
                v->Reg32[3] = 0x0000ffffU;
                break;

            case 0x80000000U: /* PendingInterruption */
                if (events_p->exception.injected || events_p->exception.pending) {
                    uint64_t val = 1ULL /* InterruptionPending */
                        | (3ULL << 1)   /* InterruptionType = Exception (3) */
                        | ((events_p->exception.has_error_code ? 1ULL : 0ULL) << 4)
                        | ((uint64_t)events_p->exception.nr << 16)
                        | ((uint64_t)events_p->exception.error_code << 32);
                    v->Reg64[0] = val;
                } else if (events_p->interrupt.injected) {
                    uint64_t val = 1ULL /* InterruptionPending */
                        | ((events_p->interrupt.soft ? 4ULL : 0ULL) << 1)
                        | ((uint64_t)events_p->interrupt.nr << 16);
                    v->Reg64[0] = val;
                } else if (events_p->nmi.injected || events_p->nmi.pending) {
                    uint64_t val = 1ULL | (2ULL << 1) | (2ULL << 16);
                    v->Reg64[0] = val;
                }
                break;
            case 0x80000001U: /* InterruptState */
                v->Reg64[0] = (events_p->interrupt.shadow & 1ULL)
                    | ((events_p->nmi.masked & 1ULL) << 1);
                break;
            case 0x80000002U: /* PendingEvent */
                v->Reg64[0] = 0;
                break;
            case 0x80000004U: /* DeliverabilityNotifications */
                v->Reg64[0] = vcpu->run->request_interrupt_window ? 2ULL : 0ULL;
                break;

            default:
                if (r >= 0x1000 && r <= 0x100f) {
                    __builtin_memcpy(v, fpu.xmm[r - 0x1000], 16);
                } else if (r >= 0x1010 && r <= 0x1017) {
                    __builtin_memcpy(v, fpu.fpr[r - 0x1010], 16);
                } else if (r >= 0x2000 && r <= 0x207b) {
                    if (r == 0x2001) {
                        v->Reg64[0] = sregs_p->efer;
                    } else if (r == 0x2003) {
                        v->Reg64[0] = sregs_p->apic_base;
                    } else if (r == 0x207b) {
                        v->Reg64[0] = vcpu->tsc_aux;
                    } else {
                        uint32_t idx = whv_reg_to_msr_index(r);
                        if (idx && msr_pos < msrs.nmsrs) {
                            v->Reg64[0] = msrs.entries[msr_pos++].data;
                        }
                    }
                }
                break;
        }
    }
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvSetVirtualProcessorRegisters(
    void *Partition,
    uint32_t VpIndex,
    const uint32_t *RegisterNames,
    uint32_t RegisterCount,
    const union whv_reg_value *RegisterValues)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS || !part->vcpus[VpIndex].created) {
        return (int32_t)0x80070057;
    }
    struct whv_vcpu *vcpu = &part->vcpus[VpIndex];
    int fd = vcpu->fd;
    int use_sync = vcpu->sync_regs_valid;

    int mod_regs = 0, mod_sregs = 0, mod_dregs = 0, mod_xcrs = 0, mod_fpu = 0, mod_events = 0;
    struct kvm_msrs_buf msrs;
    msrs.nmsrs = 0;
    msrs.pad = 0;

    for (uint32_t i = 0; i < RegisterCount; i++) {
        uint32_t r = RegisterNames[i];
        if (r <= 0x11) mod_regs = 1;
        else if (r >= 0x12 && r <= 0x1f) mod_sregs = 1;
        else if (r >= 0x21 && r <= 0x26) mod_dregs = 1;
        else if (r == 0x27) mod_xcrs = 1;
        else if (r >= 0x1000 && r <= 0x1019) mod_fpu = 1;
        else if (r == 0x2001 || r == 0x2003) mod_sregs = 1;
        else if (r >= 0x80000000U && r <= 0x80000002U) mod_events = 1;
    }

    struct kvm_regs regs_local;
    struct kvm_sregs sregs_local;
    struct kvm_debugregs dregs;
    struct kvm_xcrs xcrs;
    struct kvm_fpu fpu;
    struct kvm_vcpu_events events_local;

    struct kvm_regs *regs_p = &vcpu->run->s.regs.regs;
    struct kvm_sregs *sregs_p = &vcpu->run->s.regs.sregs;
    struct kvm_vcpu_events *events_p = &vcpu->run->s.regs.events;
    struct kvm_sregs orig_sregs;

    if (!use_sync) {
        if (mod_regs) sys_ioctl(fd, KVM_GET_REGS, (uint64_t)&regs_local);
        if (mod_sregs) sys_ioctl(fd, KVM_GET_SREGS, (uint64_t)&sregs_local);
        if (mod_events) sys_ioctl(fd, KVM_GET_VCPU_EVENTS, (uint64_t)&events_local);
        regs_p = &regs_local;
        sregs_p = &sregs_local;
        events_p = &events_local;
    } else if (mod_sregs) {
        orig_sregs = *sregs_p;
    }
    if (mod_dregs) sys_ioctl(fd, KVM_GET_DEBUGREGS, (uint64_t)&dregs);
    if (mod_xcrs) sys_ioctl(fd, KVM_GET_XCRS, (uint64_t)&xcrs);
    if (mod_fpu) sys_ioctl(fd, KVM_GET_FPU, (uint64_t)&fpu);

    for (uint32_t i = 0; i < RegisterCount; i++) {
        uint32_t r = RegisterNames[i];
        const union whv_reg_value *v = &RegisterValues[i];

        switch (r) {
            case 0x00: regs_p->rax = v->Reg64[0]; break;
            case 0x01: regs_p->rcx = v->Reg64[0]; break;
            case 0x02: regs_p->rdx = v->Reg64[0]; break;
            case 0x03: regs_p->rbx = v->Reg64[0]; break;
            case 0x04: regs_p->rsp = v->Reg64[0]; break;
            case 0x05: regs_p->rbp = v->Reg64[0]; break;
            case 0x06: regs_p->rsi = v->Reg64[0]; break;
            case 0x07: regs_p->rdi = v->Reg64[0]; break;
            case 0x08: regs_p->r8  = v->Reg64[0]; break;
            case 0x09: regs_p->r9  = v->Reg64[0]; break;
            case 0x0a: regs_p->r10 = v->Reg64[0]; break;
            case 0x0b: regs_p->r11 = v->Reg64[0]; break;
            case 0x0c: regs_p->r12 = v->Reg64[0]; break;
            case 0x0d: regs_p->r13 = v->Reg64[0]; break;
            case 0x0e: regs_p->r14 = v->Reg64[0]; break;
            case 0x0f: regs_p->r15 = v->Reg64[0]; break;
            case 0x10: regs_p->rip = v->Reg64[0]; break;
            case 0x11: regs_p->rflags = v->Reg64[0] | 2ULL; break;

            case 0x12: whv_seg_to_kvm(&v->Segment, &sregs_p->es, 0); break;
            case 0x13: whv_seg_to_kvm(&v->Segment, &sregs_p->cs, 0); break;
            case 0x14: whv_seg_to_kvm(&v->Segment, &sregs_p->ss, 0); break;
            case 0x15: whv_seg_to_kvm(&v->Segment, &sregs_p->ds, 0); break;
            case 0x16: whv_seg_to_kvm(&v->Segment, &sregs_p->fs, 0); break;
            case 0x17: whv_seg_to_kvm(&v->Segment, &sregs_p->gs, 0); break;
            case 0x18: whv_seg_to_kvm(&v->Segment, &sregs_p->ldt, 0); break;
            case 0x19: whv_seg_to_kvm(&v->Segment, &sregs_p->tr, 1); break;

            case 0x1a: /* Idtr */
                sregs_p->idt.limit = v->Table.Limit;
                sregs_p->idt.base = v->Table.Base;
                break;
            case 0x1b: /* Gdtr */
                sregs_p->gdt.limit = v->Table.Limit;
                sregs_p->gdt.base = v->Table.Base;
                break;

            case 0x1c: sregs_p->cr0 = v->Reg64[0]; break;
            case 0x1d: sregs_p->cr2 = v->Reg64[0]; break;
            case 0x1e: sregs_p->cr3 = v->Reg64[0]; break;
            case 0x1f: sregs_p->cr4 = v->Reg64[0]; break;
            case 0x20:
                sregs_p->cr8 = v->Reg64[0] & 0xfULL;
                vcpu->run->cr8 = v->Reg64[0] & 0xfULL;
                break;

            case 0x21: dregs.db[0] = v->Reg64[0]; break;
            case 0x22: dregs.db[1] = v->Reg64[0]; break;
            case 0x23: dregs.db[2] = v->Reg64[0]; break;
            case 0x24: dregs.db[3] = v->Reg64[0]; break;
            case 0x25: dregs.dr6   = v->Reg64[0]; break;
            case 0x26: dregs.dr7   = v->Reg64[0]; break;
            case 0x27:
                xcrs.nr_xcrs = 1;
                xcrs.flags = 0;
                xcrs.xcrs[0].xcr = 0;
                xcrs.xcrs[0].reserved = 0;
                xcrs.xcrs[0].value = v->Reg64[0] ? v->Reg64[0] : 1ULL;
                break;

            case 0x1018: /* FpControlStatus */
                fpu.fcw = v->Reg16[0];
                fpu.fsw = v->Reg16[1];
                fpu.ftwx = v->Reg8[4];
                fpu.last_opcode = v->Reg16[3];
                fpu.last_ip = v->Reg64[1];
                break;
            case 0x1019: /* XmmControlStatus */
                fpu.last_dp = v->Reg64[0];
                fpu.mxcsr = v->Reg32[2];
                break;

            case 0x80000000U: /* PendingInterruption */
                if ((v->Reg64[0] & 1ULL) == 0) {
                    events_p->exception.injected = 0;
                    events_p->exception.pending = 0;
                    events_p->interrupt.injected = 0;
                    events_p->nmi.injected = 0;
                    events_p->nmi.pending = 0;
                } else {
                    uint32_t itype = (uint32_t)((v->Reg64[0] >> 1) & 7U);
                    uint8_t vec = (uint8_t)((v->Reg64[0] >> 16) & 0xffU);
                    uint8_t has_err = (uint8_t)((v->Reg64[0] >> 4) & 1U);
                    uint32_t err_code = (uint32_t)(v->Reg64[0] >> 32);
                    if (itype == 3 || itype == 6) {
                        events_p->exception.injected = 1;
                        events_p->exception.nr = vec;
                        events_p->exception.has_error_code = has_err;
                        events_p->exception.error_code = err_code;
                    } else if (itype == 2) {
                        events_p->nmi.injected = 1;
                    } else {
                        events_p->interrupt.injected = 1;
                        events_p->interrupt.nr = vec;
                        events_p->interrupt.soft = (itype == 4) ? 1 : 0;
                    }
                }
                break;
            case 0x80000001U: /* InterruptState */
                events_p->interrupt.shadow = (uint8_t)(v->Reg64[0] & 1U);
                events_p->nmi.masked = (uint8_t)((v->Reg64[0] >> 1) & 1U);
                break;
            case 0x80000002U: /* PendingEvent */
                if ((v->Reg64[0] & 1ULL) != 0) {
                    uint32_t etype = (uint32_t)((v->Reg64[0] >> 1) & 7U);
                    if (etype == 0) {
                        uint8_t vec = (uint8_t)((v->Reg64[0] >> 16) & 0xffU);
                        uint8_t has_err = (uint8_t)((v->Reg64[0] >> 8) & 1U);
                        uint32_t err_code = (uint32_t)(v->Reg64[0] >> 32);
                        events_p->exception.injected = 1;
                        events_p->exception.nr = vec;
                        events_p->exception.has_error_code = has_err;
                        events_p->exception.error_code = err_code;
                        if ((v->Reg64[0] & 0x200ULL) != 0) {
                            events_p->exception_has_payload = 1;
                            events_p->exception_payload = v->Reg64[1];
                        }
                    }
                }
                break;
            case 0x80000004U: /* DeliverabilityNotifications */
                vcpu->run->request_interrupt_window = (v->Reg64[0] & 2ULL) ? 1 : 0;
                break;

            default:
                if (r >= 0x1000 && r <= 0x100f) {
                    __builtin_memcpy(fpu.xmm[r - 0x1000], v, 16);
                } else if (r >= 0x1010 && r <= 0x1017) {
                    __builtin_memcpy(fpu.fpr[r - 0x1010], v, 16);
                } else if (r >= 0x2000 && r <= 0x207b) {
                    if (r == 0x2001) sregs_p->efer = v->Reg64[0];
                    if (r == 0x2003) {
                        sregs_p->apic_base = v->Reg64[0];
                        vcpu->run->apic_base = v->Reg64[0];
                    }
                    if (r == 0x207b) vcpu->tsc_aux = v->Reg64[0];
                    /* Skip IA32_TSC (0x2000), IA32_EFER (0x2001), IA32_APIC_BASE (0x2003) in KVM_SET_MSRS */
                    if (r != 0x2000 && r != 0x2001 && r != 0x2003) {
                        uint32_t idx = whv_reg_to_msr_index(r);
                        if (idx && msrs.nmsrs < 64) {
                            msrs.entries[msrs.nmsrs].index = idx;
                            msrs.entries[msrs.nmsrs].reserved = 0;
                            msrs.entries[msrs.nmsrs].data = v->Reg64[0];
                            msrs.nmsrs++;
                        }
                    }
                }
                break;
        }
    }

    if (use_sync) {
        if (mod_regs)   vcpu->run->kvm_dirty_regs |= 1ULL;
        if (mod_sregs) {
            if (__builtin_memcmp(&orig_sregs, sregs_p, offsetof(struct kvm_sregs, cr8)) != 0 ||
                orig_sregs.efer != sregs_p->efer ||
                orig_sregs.apic_base != sregs_p->apic_base) {
                vcpu->run->kvm_dirty_regs |= 2ULL;
            }
        }
        if (mod_events) vcpu->run->kvm_dirty_regs |= 4ULL;
    } else {
        if (mod_sregs)  sys_ioctl(fd, KVM_SET_SREGS, (uint64_t)sregs_p);
        if (mod_regs)   sys_ioctl(fd, KVM_SET_REGS, (uint64_t)regs_p);
        if (mod_events) sys_ioctl(fd, KVM_SET_VCPU_EVENTS, (uint64_t)events_p);
    }
    if (mod_dregs) sys_ioctl(fd, KVM_SET_DEBUGREGS, (uint64_t)&dregs);
    if (mod_xcrs)  sys_ioctl(fd, KVM_SET_XCRS, (uint64_t)&xcrs);
    if (mod_fpu)   sys_ioctl(fd, KVM_SET_FPU, (uint64_t)&fpu);
    if (msrs.nmsrs > 0) sys_ioctl(fd, KVM_SET_MSRS, (uint64_t)&msrs);

    return 0;
}

static void fill_vp_context(
    struct whv_vcpu *vcpu,
    struct whv_vp_exit_context *vp_ctx,
    uint8_t inst_len,
    int use_sync_regs,
    struct kvm_regs *out_regs,
    struct kvm_sregs *out_sregs)
{
    struct kvm_regs regs_local;
    struct kvm_sregs sregs_local;
    struct kvm_vcpu_events events_local;

    const struct kvm_regs *regs_p = &vcpu->run->s.regs.regs;
    const struct kvm_sregs *sregs_p = &vcpu->run->s.regs.sregs;
    const struct kvm_vcpu_events *events_p = &vcpu->run->s.regs.events;

    if (!use_sync_regs) {
        __builtin_memset(&regs_local, 0, sizeof(regs_local));
        __builtin_memset(&sregs_local, 0, sizeof(sregs_local));
        __builtin_memset(&events_local, 0, sizeof(events_local));
        sys_ioctl(vcpu->fd, KVM_GET_REGS, (uint64_t)&regs_local);
        sys_ioctl(vcpu->fd, KVM_GET_SREGS, (uint64_t)&sregs_local);
        sys_ioctl(vcpu->fd, KVM_GET_VCPU_EVENTS, (uint64_t)&events_local);
        regs_p = &regs_local;
        sregs_p = &sregs_local;
        events_p = &events_local;
    }

    if (out_regs) *out_regs = *regs_p;
    if (out_sregs) *out_sregs = *sregs_p;

    uint16_t exec_state = (uint16_t)(sregs_p->cs.dpl & 3U);
    if (sregs_p->cr0 & 1ULL) exec_state |= 0x04U;         /* Cr0Pe */
    if (sregs_p->cr0 & 0x40000ULL) exec_state |= 0x08U;   /* Cr0Am */
    if (sregs_p->efer & 0x400ULL) exec_state |= 0x10U;    /* EferLma */
    if (events_p->exception.injected || events_p->exception.pending ||
        events_p->interrupt.injected || events_p->nmi.injected || events_p->nmi.pending) {
        exec_state |= 0x40U;                             /* InterruptionPending */
    }
    if (events_p->interrupt.shadow) exec_state |= 0x1000U; /* InterruptShadow */

    vp_ctx->ExecutionState = exec_state;
    vp_ctx->InstLenCr8 = (uint8_t)((inst_len & 0x0fU) | (((uint8_t)vcpu->run->cr8 & 0x0fU) << 4));
    vp_ctx->Reserved = 0;
    vp_ctx->Reserved2 = 0;
    kvm_seg_to_whv(&sregs_p->cs, &vp_ctx->Cs);
    vp_ctx->Rip = regs_p->rip;
    vp_ctx->Rflags = regs_p->rflags;
}

static volatile uint64_t g_stat_io = 0;
static volatile uint64_t g_stat_mmio = 0;
static volatile uint64_t g_stat_hlt = 0;
static volatile uint64_t g_stat_irqw = 0;
static volatile uint64_t g_stat_eintr = 0;
static volatile uint64_t g_mmio_eoi = 0;
static volatile uint64_t g_mmio_tpr = 0;
static volatile uint64_t g_mmio_icr = 0;
static volatile uint64_t g_mmio_tmr = 0;
static volatile uint64_t g_mmio_other = 0;
static volatile uint64_t g_mmio_last_pa = 0;

__declspec(dllexport) int32_t __cdecl WHvRunVirtualProcessor(
    void *Partition,
    uint32_t VpIndex,
    void *ExitContext,
    uint32_t ExitContextSizeInBytes)
{
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS || !part->vcpus[VpIndex].created || !ExitContext) {
        return (int32_t)0x80070057;
    }
    struct whv_vcpu *vcpu = &part->vcpus[VpIndex];
    struct kvm_run *run = vcpu->run;

    uint8_t *exit_buf = (uint8_t *)ExitContext;
    *(uint64_t *)exit_buf = 0;
    struct whv_vp_exit_context *vp_ctx = (struct whv_vp_exit_context *)(exit_buf + 8);

    if (__builtin_expect(vcpu->tid == 0, 0)) {
        vcpu->tid = (int)sys_gettid();
        vcpu->tgid = (int)sys_getpid();
        /* Ensure SIGURG (23 -> bit 22) is unblocked on this VCPU thread */
        uint64_t mask = (1ULL << 22);
        sys_rt_sigprocmask(1 /* SIG_UNBLOCK */, &mask, NULL, 8);
    }

    uint64_t seq = ++vcpu->run_seq;
    if (__builtin_expect((seq & 0x3ffffULL) == 0, 0)) {
        log_hex("STATS vp=", VpIndex, "");
        log_hex(" io=", g_stat_io, "");
        log_hex(" mmio=", g_stat_mmio, "");
        log_hex(" irqw=", g_stat_irqw, "");
        log_hex(" hlt=", g_stat_hlt, "");
        log_hex(" eoi=", g_mmio_eoi, "");
        log_hex(" icr=", g_mmio_icr, "");
        log_hex(" oth=", g_mmio_other, "\n");
    }

run_again:
    vcpu->in_run = 1;
    run->immediate_exit = 0;
    __sync_synchronize();

    if (vcpu->cancel_requested) {
        vcpu->cancel_requested = 0;
        run->immediate_exit = 0;
        vcpu->in_run = 0;
        fill_vp_context(vcpu, vp_ctx, 0, vcpu->sync_regs_valid, NULL, NULL);
        *(uint32_t *)exit_buf = 0x2001U; /* WHvRunVpExitReasonCanceled */
        return 0;
    }

    run->kvm_valid_regs = 0x7ULL;
    int64_t r = sys_ioctl(vcpu->fd, KVM_RUN, 0);
    vcpu->in_run = 0;
    run->immediate_exit = 0;
    vcpu->sync_regs_valid = 1;

    if (r < 0) {
        /* -4 = -EINTR */
        g_stat_eintr++;
        vcpu->cancel_requested = 0;
        fill_vp_context(vcpu, vp_ctx, 0, 1, NULL, NULL);
        if (run->request_interrupt_window && run->ready_for_interrupt_injection) {
            run->request_interrupt_window = 0;
            *(uint32_t *)exit_buf = 7U; /* WHvRunVpExitReasonX64InterruptWindow */
            return 0;
        }
        *(uint32_t *)exit_buf = 0x2001U; /* WHvRunVpExitReasonCanceled */
        return 0;
    }

    struct kvm_regs regs;
    struct kvm_sregs sregs;

    switch (run->exit_reason) {
        case KVM_EXIT_IO: {
            g_stat_io++;
            if (run->io.port == 0x80U) {
                if (run->io.direction == KVM_EXIT_IO_IN) {
                    *((uint8_t *)run + run->io.data_offset) = 0;
                }
                goto run_again;
            }
            fill_vp_context(vcpu, vp_ctx, 0, 1, &regs, &sregs);
            *(uint32_t *)exit_buf = 2U; /* WHvRunVpExitReasonX64IoPortAccess */
            *(uint32_t *)(exit_buf + 0x30) = 0;
            /* Store vcpu pointer at exit_buf + 0x34 (InstructionBytes[0..7]) for WinHvEmulation.dll */
            *(struct whv_vcpu **)(exit_buf + 0x34) = vcpu;
            *(uint64_t *)(exit_buf + 0x3c) = 0;
            uint32_t acc = (run->io.direction == KVM_EXIT_IO_OUT) ? 1U : 0U;
            acc |= ((uint32_t)(run->io.size & 7U) << 1);
            if (run->io.count > 1) acc |= 0x30U;
            *(uint32_t *)(exit_buf + 0x44) = acc;
            *(uint16_t *)(exit_buf + 0x48) = run->io.port;
            *(uint16_t *)(exit_buf + 0x4a) = 0;
            *(uint32_t *)(exit_buf + 0x4c) = 0;
            *(uint64_t *)(exit_buf + 0x50) = regs.rax;
            *(uint64_t *)(exit_buf + 0x58) = regs.rcx;
            *(uint64_t *)(exit_buf + 0x60) = regs.rsi;
            *(uint64_t *)(exit_buf + 0x68) = regs.rdi;
            kvm_seg_to_whv(&sregs.ds, (struct whv_segment *)(exit_buf + 0x70));
            kvm_seg_to_whv(&sregs.es, (struct whv_segment *)(exit_buf + 0x80));
            return 0;
        }
        case KVM_EXIT_MMIO: {
            g_stat_mmio++;
            uint64_t pa = run->mmio.phys_addr;
            if ((pa & 0xfffffffffffff000ULL) == 0xfee00000ULL && run->mmio.len == 4U && vcpu->vbox_vcpu != NULL) {
                uint8_t *pVCpu = (uint8_t *)vcpu->vbox_vcpu;
                uint8_t *xapic = *(uint8_t * const *)(pVCpu + 0xfd20);
                uint32_t off = (uint32_t)(pa & 0xff0U);
                if (xapic != NULL) {
                    if (!run->mmio.is_write) {
                        if (off == 0x020U || off == 0x030U || off == 0x080U || off == 0x0a0U ||
                            off == 0x0d0U || off == 0x0e0U || off == 0x0f0U || off == 0x280U ||
                            off == 0x310U || (off >= 0x320U && off <= 0x380U) || off == 0x3e0U) {
                            *(uint32_t *)run->mmio.data = *(const uint32_t *)(xapic + off);
                            goto run_again;
                        } else if (off == 0x300U) {
                            *(uint32_t *)run->mmio.data = *(const uint32_t *)(xapic + 0x300U) & ~0x1000U;
                            goto run_again;
                        }
                    } else {
                        uint32_t val = *(const uint32_t *)run->mmio.data;
                        if (off == 0x310U) {
                            *(uint32_t *)(xapic + 0x310U) = val & 0xff000000U;
                            goto run_again;
                        } else if (off == 0x0d0U) {
                            *(uint32_t *)(xapic + 0x0d0U) = val & 0xff000000U;
                            goto run_again;
                        } else if (off == 0x0e0U) {
                            *(uint32_t *)(xapic + 0x0e0U) = val | 0x0fffffffU;
                            goto run_again;
                        } else if (off == 0x080U && (val & 0xffffff00U) == 0U) {
                            uint8_t new_tpr = (uint8_t)val;
                            uint8_t old_tpr = xapic[0x80U];
                            if (new_tpr == old_tpr) {
                                goto run_again;
                            }
                            uint8_t *pib = *(uint8_t * const *)(pVCpu + 0xfd48);
                            int no_pending = (*(const uint64_t *)(pVCpu + 0xfd60) == 0ULL &&
                                              *(const uint64_t *)(pVCpu + 0xfd68) == 0ULL &&
                                              *(const uint32_t *)(pVCpu + 0xfd70) == 0U &&
                                              pib != NULL && *(const uint32_t *)(pib + 0x20U) == 0U);
                            if (no_pending) {
                                uint32_t irr_any = 0;
                                for (int i = 0; i < 8; i++) {
                                    irr_any |= *(const uint32_t *)(xapic + 0x200U + (uint32_t)i * 0x10U);
                                }
                                if (irr_any == 0U || (new_tpr & 0xf0U) >= (old_tpr & 0xf0U)) {
                                    xapic[0x80U] = new_tpr;
                                    uint32_t cur_isrv = 0;
                                    for (int i = 7; i >= 0; i--) {
                                        uint32_t v = *(const uint32_t *)(xapic + 0x100U + (uint32_t)i * 0x10U);
                                        if (v != 0U) {
                                            cur_isrv = ((uint32_t)i << 5) | (31U - (uint32_t)__builtin_clz(v));
                                            break;
                                        }
                                    }
                                    uint8_t isr_prio = (uint8_t)(cur_isrv & 0xf0U);
                                    xapic[0xa0U] = ((new_tpr & 0xf0U) >= isr_prio) ? new_tpr : isr_prio;
                                    run->cr8 = (uint64_t)(new_tpr >> 4);
                                    run->s.regs.sregs.cr8 = (uint64_t)(new_tpr >> 4);
                                    goto run_again;
                                }
                            }
                        } else if (off == 0x0b0U && val == 0U &&
                                   (*(const uint32_t *)(xapic + 0xf0U) & 0x100U) != 0U &&
                                   (*(const uint32_t *)(xapic + 0x350U) & 0x4000U) == 0U &&
                                   *(const uint64_t *)(pVCpu + 0xfd60) == 0ULL &&
                                   *(const uint64_t *)(pVCpu + 0xfd68) == 0ULL &&
                                   *(const uint32_t *)(pVCpu + 0xfd70) == 0U) {
                            uint8_t *pib = *(uint8_t * const *)(pVCpu + 0xfd48);
                            if (pib != NULL && *(const uint32_t *)(pib + 0x20U) == 0U) {
                                int isr_idx = -1;
                                uint32_t isr_val = 0;
                                for (int i = 7; i >= 0; i--) {
                                    uint32_t v = *(const uint32_t *)(xapic + 0x100U + (uint32_t)i * 0x10U);
                                    if (v != 0U) {
                                        isr_idx = i;
                                        isr_val = v;
                                        break;
                                    }
                                }
                                if (isr_idx >= 0) {
                                    uint32_t bit = 31U - (uint32_t)__builtin_clz(isr_val);
                                    uint32_t tmr_val = *(const uint32_t *)(xapic + 0x180U + (uint32_t)isr_idx * 0x10U);
                                    if ((tmr_val & (1U << bit)) == 0U) {
                                        uint32_t irr_any = 0;
                                        for (int i = 0; i < 8; i++) {
                                            irr_any |= *(const uint32_t *)(xapic + 0x200U + (uint32_t)i * 0x10U);
                                        }
                                        if (irr_any == 0U) {
                                            __sync_fetch_and_and((uint32_t *)(xapic + 0x100U + (uint32_t)isr_idx * 0x10U), ~(1U << bit));
                                            uint32_t new_isrv = 0;
                                            for (int i = 7; i >= 0; i--) {
                                                uint32_t v = *(const uint32_t *)(xapic + 0x100U + (uint32_t)i * 0x10U);
                                                if (v != 0U) {
                                                    new_isrv = ((uint32_t)i << 5) | (31U - (uint32_t)__builtin_clz(v));
                                                    break;
                                                }
                                            }
                                            uint8_t tpr = xapic[0x80U];
                                            uint8_t isr_prio = (uint8_t)(new_isrv & 0xf0U);
                                            xapic[0xa0U] = ((tpr & 0xf0U) >= isr_prio) ? tpr : isr_prio;
                                            (*(uint64_t *)(pVCpu + 0x102b0))++;
                                            goto run_again;
                                        }
                                    }
                                } else {
                                    (*(uint64_t *)(pVCpu + 0x102b0))++;
                                    goto run_again;
                                }
                            }
                        }
                    }
                }
            }
            if (pa == 0xfee000b0ULL) g_mmio_eoi++;
            else if (pa == 0xfee00080ULL) g_mmio_tpr++;
            else if (pa == 0xfee00300ULL || pa == 0xfee00310ULL) g_mmio_icr++;
            else if (pa == 0xfee00380ULL || pa == 0xfee00390ULL) g_mmio_tmr++;
            else { g_mmio_other++; g_mmio_last_pa = pa; }
            fill_vp_context(vcpu, vp_ctx, 0, 1, NULL, NULL);
            *(uint32_t *)exit_buf = 1U; /* WHvRunVpExitReasonMemoryAccess */
            *(uint32_t *)(exit_buf + 0x30) = 0;
            /* Store vcpu pointer at exit_buf + 0x34 (InstructionBytes[0..7]) for WinHvEmulation.dll */
            *(struct whv_vcpu **)(exit_buf + 0x34) = vcpu;
            *(uint64_t *)(exit_buf + 0x3c) = 0;
            uint32_t acc = run->mmio.is_write ? 1U : 0U;
            *(uint32_t *)(exit_buf + 0x44) = acc;
            *(uint64_t *)(exit_buf + 0x48) = pa;
            *(uint64_t *)(exit_buf + 0x50) = 0;
            return 0;
        }
        case KVM_EXIT_HLT: {
            g_stat_hlt++;
            fill_vp_context(vcpu, vp_ctx, 1, 1, NULL, NULL);
            *(uint32_t *)exit_buf = 8U; /* WHvRunVpExitReasonX64Halt */
            return 0;
        }
        case KVM_EXIT_IRQ_WINDOW_OPEN: {
            g_stat_irqw++;
            run->request_interrupt_window = 0;
            fill_vp_context(vcpu, vp_ctx, 0, 1, NULL, NULL);
            *(uint32_t *)exit_buf = 7U; /* WHvRunVpExitReasonX64InterruptWindow */
            return 0;
        }
        case KVM_EXIT_X86_RDMSR:
        case KVM_EXIT_X86_WRMSR: {
            int is_write = (run->exit_reason == KVM_EXIT_X86_WRMSR) ? 1 : 0;
            uint32_t msr_idx = run->msr.index;
            uint64_t msr_data = run->msr.data;
            /* Complete the MSR exit in KVM first so complete_userspace_io is cleared */
            run->msr.error = 0;
            run->msr.data = (!is_write && msr_idx == 0xc0010015U) ? (1ULL << 24) : 0;
            run->immediate_exit = 1;
            sys_ioctl(vcpu->fd, KVM_RUN, 0);
            run->immediate_exit = 0;
            if (!is_write && msr_idx == 0xc0010015U) {
                goto run_again;
            }
            fill_vp_context(vcpu, vp_ctx, 2, 1, &regs, &sregs);
            /* Restore RIP to before the 2-byte RDMSR/WRMSR instruction so BstkVMM can advance it by 2 */
            vp_ctx->Rip -= 2;
            run->s.regs.regs.rip = vp_ctx->Rip;
            run->kvm_dirty_regs |= 1ULL;

            *(uint32_t *)exit_buf = 0x1000U; /* WHvRunVpExitReasonX64MsrAccess */
            *(uint32_t *)(exit_buf + 0x30) = is_write ? 1U : 0U;
            *(uint32_t *)(exit_buf + 0x34) = msr_idx;
            *(uint64_t *)(exit_buf + 0x38) = (uint32_t)msr_data;
            *(uint64_t *)(exit_buf + 0x40) = (uint32_t)(msr_data >> 32);
            return 0;
        }
        case KVM_EXIT_SHUTDOWN: {
            log_str("WHvRunVirtualProcessor: KVM_EXIT_SHUTDOWN (Triple Fault)\n");
            fill_vp_context(vcpu, vp_ctx, 0, 1, NULL, NULL);
            *(uint32_t *)exit_buf = 3U; /* WHvRunVpExitReasonUnrecoverableException */
            return 0;
        }
        default: {
            log_hex("WHvRunVirtualProcessor: other exit_reason=", run->exit_reason, "\n");
            fill_vp_context(vcpu, vp_ctx, 0, 1, NULL, NULL);
            *(uint32_t *)exit_buf = 0x2001U; /* WHvRunVpExitReasonCanceled */
            return 0;
        }
    }
}

__declspec(dllexport) int32_t __cdecl WHvCancelRunVirtualProcessor(
    void *Partition,
    uint32_t VpIndex,
    uint32_t Flags)
{
    (void)Flags;
    struct whv_partition *part = (struct whv_partition *)Partition;
    if (!part || part->magic != 0x57485650 || VpIndex >= MAX_VCPUS || !part->vcpus[VpIndex].created) {
        return (int32_t)0x80070057;
    }
    struct whv_vcpu *vcpu = &part->vcpus[VpIndex];
    vcpu->cancel_requested = 1;
    if (vcpu->run) {
        vcpu->run->immediate_exit = 1;
    }
    __sync_synchronize();
    int tid = vcpu->tid;
    int tgid = vcpu->tgid;
    if (vcpu->in_run && tid > 0 && tgid > 0) {
        sys_tgkill(tgid, tid, 23 /* SIGURG */);
    }
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvRequestInterrupt(
    void *Partition,
    const void *Interrupt,
    uint32_t InterruptControlSize)
{
    (void)Partition;
    (void)Interrupt;
    (void)InterruptControlSize;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvGetVirtualProcessorInterruptControllerState2(
    void *Partition,
    uint32_t VpIndex,
    void *State,
    uint32_t StateSize,
    uint32_t *WrittenSize)
{
    (void)Partition;
    (void)VpIndex;
    if (State && StateSize > 0) __builtin_memset(State, 0, StateSize);
    if (WrittenSize) *WrittenSize = StateSize;
    return 0;
}

__declspec(dllexport) int32_t __cdecl WHvSetVirtualProcessorInterruptControllerState2(
    void *Partition,
    uint32_t VpIndex,
    const void *State,
    uint32_t StateSize)
{
    (void)Partition;
    (void)VpIndex;
    (void)State;
    (void)StateSize;
    return 0;
}

int __stdcall _DllMainCRTStartup(void *hinstDLL, uint32_t fdwReason, void *lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;
    if (fdwReason == 1 /* DLL_PROCESS_ATTACH */) {
        g_main_tid = (int)sys_gettid();
        /* Ensure SIGPROF (27 -> bit 26) is unblocked on the main thread */
        uint64_t mask = (1ULL << 26);
        sys_rt_sigprocmask(1 /* SIG_UNBLOCK */, &mask, NULL, 8);
    }
    return 1;
}
