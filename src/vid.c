/*
 * BstkVMM.dll's NEM init (nemR3WinInitVidIntercepts) hooks vid.dll's
 * NTDLL!NtDeviceIoControlFile import and fails with "Failed to patch
 * NtDeviceIoControlFile import in VID.DLL!" when there is none, so import it
 * even though these stubs never issue VID I/O controls.
 */
__declspec(dllimport) long __stdcall NtDeviceIoControlFile(void *, void *, void *, void *, void *, unsigned long,
                                                           void *, unsigned long, void *, unsigned long);
static void *volatile g_import_anchor;

__declspec(dllexport) int VidGetHvPartitionId(void *h, unsigned long long *id) {
    if (id) *id = 1;
    return 1;
}
__declspec(dllexport) int VidGetPartitionProperty(void *h, int prop, unsigned long long *val) {
    if (val) *val = 0;
    return 1;
}
int __stdcall DllMain(void *hinst, unsigned int reason, void *reserved) {
    g_import_anchor = (void *)NtDeviceIoControlFile;
    return 1;
}
