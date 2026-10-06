__declspec(dllexport) int VidGetHvPartitionId(void *h, unsigned long long *id) {
    if (id) *id = 1;
    return 1;
}
__declspec(dllexport) int VidGetPartitionProperty(void *h, int prop, unsigned long long *val) {
    if (val) *val = 0;
    return 1;
}
int __stdcall DllMain(void *hinst, unsigned int reason, void *reserved) {
    return 1;
}
