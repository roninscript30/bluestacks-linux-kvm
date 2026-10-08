/*
 * DnsQueryConfig shim for BstkSVC.exe (imported as bstdns.dll instead of DNSAPI.dll).
 *
 * VirtualBox's HostDnsServiceWin calls DnsQueryConfig(DnsConfigDnsServerList,
 * DNS_CONFIG_FLAG_ALLOC, ..., &pIp4Array, &cb) with cb = sizeof(void *) and
 * expects a LocalAlloc'd IP4_ARRAY pointer back. Wine ignores the ALLOC flag
 * and writes the array itself into the 8-byte pointer slot whenever it fits
 * (exactly one IPv4 DNS server), so BstkSVC dereferences garbage and the
 * VirtualBox COM object creation fails with ERROR_NOACCESS.
 */
#include <stdint.h>

#define DNS_CONFIG_FLAG_ALLOC 0x1UL
#define ERROR_MORE_DATA       234L

__declspec(dllimport) void *__stdcall LoadLibraryA(const char *name);
__declspec(dllimport) void *__stdcall GetProcAddress(void *module, const char *name);
__declspec(dllimport) void *__stdcall LocalAlloc(uint32_t flags, uint64_t bytes);
__declspec(dllimport) void *__stdcall LocalFree(void *mem);

typedef long (__stdcall *dns_query_config_fn)(int, unsigned long, const uint16_t *, void *, void *,
                                              unsigned long *);

__declspec(dllexport) long __stdcall DnsQueryConfig(int config, unsigned long flag, const uint16_t *adapter,
                                                    void *reserved, void *buffer, unsigned long *len) {
    static dns_query_config_fn real;
    if (!real) {
        real = (dns_query_config_fn)GetProcAddress(LoadLibraryA("dnsapi.dll"), "DnsQueryConfig");
        if (!real) return 127; /* ERROR_PROC_NOT_FOUND */
    }
    if (!(flag & DNS_CONFIG_FLAG_ALLOC)) return real(config, flag, adapter, reserved, buffer, len);
    if (!buffer || !len) return 87; /* ERROR_INVALID_PARAMETER */

    flag &= ~DNS_CONFIG_FLAG_ALLOC;
    unsigned long size = 0;
    long ret = real(config, flag, adapter, reserved, 0, &size);
    if (ret != 0 && ret != ERROR_MORE_DATA) return ret;
    if (size == 0) return ret ? ret : 13; /* ERROR_INVALID_DATA */

    void *mem = LocalAlloc(0x40 /* LPTR */, size);
    if (!mem) return 14; /* ERROR_OUTOFMEMORY */
    ret = real(config, flag, adapter, reserved, mem, &size);
    if (ret != 0) {
        LocalFree(mem);
        return ret;
    }
    *(void **)buffer = mem;
    *len = size;
    return 0;
}

int __stdcall _DllMainCRTStartup(void *hinstDLL, uint32_t fdwReason, void *lpvReserved) {
    (void)hinstDLL;
    (void)fdwReason;
    (void)lpvReserved;
    return 1;
}
