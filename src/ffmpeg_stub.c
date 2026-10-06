/*
 * Minimal no-op PE64 stub for BlueStacks ffmpeg.exe.
 * Prevents HD-Player.exe from repeatedly spawning DirectShow/DXVK enumeration
 * subprocesses every few seconds, eliminating periodic frame-time spikes.
 */
int __stdcall mainCRTStartup(void) {
    return 0;
}
