/*
 * No-op stand-in for BstkDrv_nxt.sys (the BlueStacksDrv_nxt service).
 *
 * HD-Player.exe starts the BlueStacksDrv_nxt service before booting the VM and
 * crashes when that fails. The real driver needs VMX root mode, so its
 * DriverEntry fails inside Wine's winedevice.exe. This driver loads cleanly
 * but creates no device object, so BstkVMM.dll cannot open its support driver
 * and uses the Windows Hypervisor Platform (WinHvPlatform.dll -> /dev/kvm).
 */
#include <stdint.h>

#define DRIVER_OBJECT_DRIVER_UNLOAD 0x68 /* offsetof(DRIVER_OBJECT, DriverUnload) on x64 */

static void __stdcall DriverUnload(void *driver_object) {
    (void)driver_object;
}

long __stdcall DriverEntry(void *driver_object, void *registry_path) {
    (void)registry_path;
    *(void **)((uint8_t *)driver_object + DRIVER_OBJECT_DRIVER_UNLOAD) = (void *)DriverUnload;
    return 0; /* STATUS_SUCCESS */
}
