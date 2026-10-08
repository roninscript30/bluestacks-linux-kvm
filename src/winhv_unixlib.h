/*
 * Interface between the PE DLLs (WinHvPlatform.dll, WinHvEmulation.dll) and
 * winhv_kvm.so, the Linux side of the bridge.
 *
 * The PE side loads winhv_kvm.so from its own directory with Wine's
 * __wine_load_unix_lib() (from the installed Wine's libwinecrt0.a) and calls
 * into it with __wine_unix_call_dispatcher(), the mechanism Wine's own DLLs
 * use to reach Unix code.
 */
#ifndef WINHV_UNIXLIB_H
#define WINHV_UNIXLIB_H

#include <stddef.h>
#include <stdint.h>

#define WINHV_UNIXLIB_NAME "winhv_kvm.so"

/* WHv* functions forwarded verbatim from WinHvPlatform.dll to winhv_kvm.so */
#define WINHV_FORWARDED_FUNCS(X)                       \
    X(WHvGetCapability)                                \
    X(WHvCreatePartition)                              \
    X(WHvSetupPartition)                               \
    X(WHvDeletePartition)                              \
    X(WHvGetPartitionProperty)                         \
    X(WHvSetPartitionProperty)                         \
    X(WHvMapGpaRange)                                  \
    X(WHvUnmapGpaRange)                                \
    X(WHvQueryGpaRangeDirtyBitmap)                     \
    X(WHvCreateVirtualProcessor)                       \
    X(WHvDeleteVirtualProcessor)                       \
    X(WHvTranslateGva)                                 \
    X(WHvGetVirtualProcessorRegisters)                 \
    X(WHvSetVirtualProcessorRegisters)                 \
    X(WHvRunVirtualProcessor)                          \
    X(WHvCancelRunVirtualProcessor)                    \
    X(WHvRequestInterrupt)                             \
    X(WHvGetVirtualProcessorInterruptControllerState2) \
    X(WHvSetVirtualProcessorInterruptControllerState2)

#define WINHV_FUNC_ID(name) func_##name,
enum winhv_unix_func {
    func_process_attach,
    func_vcpu_run_once,
    WINHV_FORWARDED_FUNCS(WINHV_FUNC_ID)
    func_count
};
#undef WINHV_FUNC_ID

/* Arguments of a unix call: the PE function's (at most six) integer/pointer arguments. */
struct winhv_call_params {
    uint64_t args[6];
    int32_t ret; /* HRESULT, or the -errno result of unix_vcpu_run_once */
};

typedef int32_t winhv_ntstatus;
typedef winhv_ntstatus (*winhv_unix_entry)(void *params);

#ifdef _WIN32

typedef uint64_t winhv_unixlib_handle;

struct winhv_unicode_string {
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t *Buffer;
};

/* Provided by the installed Wine's libwinecrt0.a (resolved from ntdll at runtime). */
extern winhv_ntstatus(__stdcall *__wine_unix_call_dispatcher)(winhv_unixlib_handle, unsigned int, void *);
extern winhv_ntstatus __stdcall __wine_load_unix_lib(const struct winhv_unicode_string *name, uint64_t *lib,
                                                     winhv_unixlib_handle *handle);

__declspec(dllimport) uint32_t __stdcall GetModuleFileNameW(void *module, wchar_t *filename, uint32_t size);
__declspec(dllimport) void __stdcall OutputDebugStringA(const char *message);

static winhv_unixlib_handle winhv_unixlib;

/* Load winhv_kvm.so from the directory `module` (this DLL) was loaded from. */
static int winhv_load_unixlib(void *module) {
    static const wchar_t so_name[] = L"" WINHV_UNIXLIB_NAME;
    wchar_t path[4 + 260 + sizeof(so_name) / sizeof(wchar_t)] = L"\\??\\";
    uint32_t len = GetModuleFileNameW(module, path + 4, 260);
    if (!len || len >= 260) return 0;

    uint32_t pos = 4 + len;
    while (pos > 4 && path[pos - 1] != L'\\') pos--;
    for (uint32_t i = 0; i < sizeof(so_name) / sizeof(wchar_t); i++) path[pos + i] = so_name[i];

    struct winhv_unicode_string name;
    name.Buffer = path;
    name.Length = (uint16_t)((pos + sizeof(so_name) / sizeof(wchar_t) - 1) * sizeof(wchar_t));
    name.MaximumLength = (uint16_t)(name.Length + sizeof(wchar_t));
    uint64_t lib;
    if (__wine_load_unix_lib(&name, &lib, &winhv_unixlib)) {
        OutputDebugStringA("WHPX bridge: failed to load " WINHV_UNIXLIB_NAME " next to the DLL\n");
        return 0;
    }
    return 1;
}

static inline int32_t winhv_unix_call(enum winhv_unix_func code, struct winhv_call_params *params) {
    params->ret = (int32_t)0x80004005; /* E_FAIL if the call does not run */
    if (__wine_unix_call_dispatcher(winhv_unixlib, code, params)) return (int32_t)0x80004005;
    return params->ret;
}

#endif /* _WIN32 */

#endif /* WINHV_UNIXLIB_H */
