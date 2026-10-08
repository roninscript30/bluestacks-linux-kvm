/*
 * WinHvPlatform.dll: PE side of the WHPX -> /dev/kvm bridge.
 *
 * Every export forwards its arguments unchanged to the implementation in
 * winhv_kvm.so (winhv_kvm.c) through a Wine unix call; see winhv_unixlib.h.
 */
#include "mem_helpers.h"
#include "winhv_unixlib.h"

#define U64(x) ((uint64_t)(uintptr_t)(x))

#define FORWARD(name, ...)                                                    \
    do {                                                                      \
        struct winhv_call_params params = {{__VA_ARGS__}, 0};                 \
        return winhv_unix_call(func_##name, &params);                         \
    } while (0)

__declspec(dllexport) int32_t __cdecl WHvGetCapability(uint32_t CapabilityCode, void *CapabilityBuffer,
                                                       uint32_t CapabilityBufferSizeInBytes,
                                                       uint32_t *WrittenSizeInBytes) {
    FORWARD(WHvGetCapability, CapabilityCode, U64(CapabilityBuffer), CapabilityBufferSizeInBytes,
            U64(WrittenSizeInBytes));
}

__declspec(dllexport) int32_t __cdecl WHvCreatePartition(void **Partition) {
    FORWARD(WHvCreatePartition, U64(Partition));
}

__declspec(dllexport) int32_t __cdecl WHvSetupPartition(void *Partition) {
    FORWARD(WHvSetupPartition, U64(Partition));
}

__declspec(dllexport) int32_t __cdecl WHvDeletePartition(void *Partition) {
    FORWARD(WHvDeletePartition, U64(Partition));
}

__declspec(dllexport) int32_t __cdecl WHvGetPartitionProperty(void *Partition, uint32_t PropertyCode,
                                                              void *PropertyBuffer, uint32_t PropertyBufferSizeInBytes,
                                                              uint32_t *WrittenSizeInBytes) {
    FORWARD(WHvGetPartitionProperty, U64(Partition), PropertyCode, U64(PropertyBuffer), PropertyBufferSizeInBytes,
            U64(WrittenSizeInBytes));
}

__declspec(dllexport) int32_t __cdecl WHvSetPartitionProperty(void *Partition, uint32_t PropertyCode,
                                                              const void *PropertyBuffer,
                                                              uint32_t PropertyBufferSizeInBytes) {
    FORWARD(WHvSetPartitionProperty, U64(Partition), PropertyCode, U64(PropertyBuffer), PropertyBufferSizeInBytes);
}

__declspec(dllexport) int32_t __cdecl WHvMapGpaRange(void *Partition, void *SourceAddress, uint64_t GuestAddress,
                                                     uint64_t SizeInBytes, uint32_t Flags) {
    FORWARD(WHvMapGpaRange, U64(Partition), U64(SourceAddress), GuestAddress, SizeInBytes, Flags);
}

__declspec(dllexport) int32_t __cdecl WHvUnmapGpaRange(void *Partition, uint64_t GuestAddress, uint64_t SizeInBytes) {
    FORWARD(WHvUnmapGpaRange, U64(Partition), GuestAddress, SizeInBytes);
}

__declspec(dllexport) int32_t __cdecl WHvQueryGpaRangeDirtyBitmap(void *Partition, uint64_t GuestAddress,
                                                                  uint64_t RangeSizeInBytes, uint64_t *Bitmap,
                                                                  uint32_t BitmapSizeInBytes) {
    FORWARD(WHvQueryGpaRangeDirtyBitmap, U64(Partition), GuestAddress, RangeSizeInBytes, U64(Bitmap),
            BitmapSizeInBytes);
}

__declspec(dllexport) int32_t __cdecl WHvCreateVirtualProcessor(void *Partition, uint32_t VpIndex, uint32_t Flags) {
    FORWARD(WHvCreateVirtualProcessor, U64(Partition), VpIndex, Flags);
}

__declspec(dllexport) int32_t __cdecl WHvDeleteVirtualProcessor(void *Partition, uint32_t VpIndex) {
    FORWARD(WHvDeleteVirtualProcessor, U64(Partition), VpIndex);
}

__declspec(dllexport) int32_t __cdecl WHvTranslateGva(void *Partition, uint32_t VpIndex, uint64_t Gva,
                                                      uint32_t TranslateFlags, void *TranslationResult,
                                                      uint64_t *Gpa) {
    FORWARD(WHvTranslateGva, U64(Partition), VpIndex, Gva, TranslateFlags, U64(TranslationResult), U64(Gpa));
}

__declspec(dllexport) int32_t __cdecl WHvGetVirtualProcessorRegisters(void *Partition, uint32_t VpIndex,
                                                                      const uint32_t *RegisterNames,
                                                                      uint32_t RegisterCount, void *RegisterValues) {
    FORWARD(WHvGetVirtualProcessorRegisters, U64(Partition), VpIndex, U64(RegisterNames), RegisterCount,
            U64(RegisterValues));
}

__declspec(dllexport) int32_t __cdecl WHvSetVirtualProcessorRegisters(void *Partition, uint32_t VpIndex,
                                                                      const uint32_t *RegisterNames,
                                                                      uint32_t RegisterCount,
                                                                      const void *RegisterValues) {
    FORWARD(WHvSetVirtualProcessorRegisters, U64(Partition), VpIndex, U64(RegisterNames), RegisterCount,
            U64(RegisterValues));
}

__declspec(dllexport) int32_t __cdecl WHvRunVirtualProcessor(void *Partition, uint32_t VpIndex, void *ExitContext,
                                                             uint32_t ExitContextSizeInBytes) {
    FORWARD(WHvRunVirtualProcessor, U64(Partition), VpIndex, U64(ExitContext), ExitContextSizeInBytes);
}

__declspec(dllexport) int32_t __cdecl WHvCancelRunVirtualProcessor(void *Partition, uint32_t VpIndex, uint32_t Flags) {
    FORWARD(WHvCancelRunVirtualProcessor, U64(Partition), VpIndex, Flags);
}

__declspec(dllexport) int32_t __cdecl WHvRequestInterrupt(void *Partition, const void *Interrupt,
                                                          uint32_t InterruptControlSize) {
    FORWARD(WHvRequestInterrupt, U64(Partition), U64(Interrupt), InterruptControlSize);
}

__declspec(dllexport) int32_t __cdecl WHvGetVirtualProcessorInterruptControllerState2(void *Partition,
                                                                                      uint32_t VpIndex, void *State,
                                                                                      uint32_t StateSize,
                                                                                      uint32_t *WrittenSize) {
    FORWARD(WHvGetVirtualProcessorInterruptControllerState2, U64(Partition), VpIndex, U64(State), StateSize,
            U64(WrittenSize));
}

__declspec(dllexport) int32_t __cdecl WHvSetVirtualProcessorInterruptControllerState2(void *Partition,
                                                                                      uint32_t VpIndex,
                                                                                      const void *State,
                                                                                      uint32_t StateSize) {
    FORWARD(WHvSetVirtualProcessorInterruptControllerState2, U64(Partition), VpIndex, U64(State), StateSize);
}

int __stdcall _DllMainCRTStartup(void *hinstDLL, uint32_t fdwReason, void *lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == 1 /* DLL_PROCESS_ATTACH */) {
        if (!winhv_load_unixlib(hinstDLL)) return 0;
        struct winhv_call_params params = {{0}, 0};
        winhv_unix_call(func_process_attach, &params);
    }
    return 1;
}
