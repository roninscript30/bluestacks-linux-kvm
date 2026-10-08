#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# shellcheck source=scripts/common.sh
source "${ROOT_DIR}/scripts/common.sh"

# 1. Make sure BlueStacks is installed in the dedicated prefix
if [[ ! -f "${HD_PLAYER}" ]]; then
    echo "[!] BlueStacks 5 is not installed in ${WINEPREFIX}. Run ./install.sh first."
    exit 1
fi

# 2. Auto-detect instance name from bluestacks.conf if not explicitly set
if [[ -z "${INSTANCE:-}" ]]; then
    if [[ -f "${BST_CONF}" ]]; then
        if grep -q '^bst\.instance\.Nougat32\.' "${BST_CONF}" 2>/dev/null; then
            INSTANCE="Nougat32"
        else
            INSTANCE="$(grep -oE '^bst\.instance\.[^.]+\.' "${BST_CONF}" | cut -d. -f3 | head -n 1 || echo "Nougat32")"
        fi
    else
        INSTANCE="Nougat32"
    fi
fi

# 3. Ensure host CPU & laptop EC firmware are in performance mode (avoids 40% power-saver clock clamp)
powerprofilesctl set performance 2>/dev/null || true

rm -f /tmp/whv_kvm.log

# 4. Run background guest tuner once Android boots
(
    sleep 14
    "${ROOT_DIR}/scripts/guest-tune.sh" >/dev/null 2>&1 || true
) &

# 5. Auto-detect NVIDIA PRIME offload vs AMD/Intel Mesa
GPU_ENV=(
    __GL_THREADED_OPTIMIZATIONS=1
    __GL_SYNC_TO_VBLANK=0
    __GL_MaxFramesAllowed=1
    vblank_mode=0
    mesa_glthread=true
)

if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1; then
    GPU_ENV+=(
        __NV_PRIME_RENDER_OFFLOAD=1
        __GLX_VENDOR_LIBRARY_NAME=nvidia
        __VK_LAYER_NV_optimus=NVIDIA_only
        DXVK_FILTER_DEVICE_NAME="NVIDIA"
    )
fi

exec env \
    WINEPREFIX="${WINEPREFIX}" \
    WINEDEBUG=-all \
    WINEFSYNC=1 \
    WINEESYNC=1 \
    "${GPU_ENV[@]}" \
    QTWEBENGINE_DISABLE_SANDBOX=1 \
    QTWEBENGINE_CHROMIUM_FLAGS="--no-sandbox --disable-gpu --disable-gpu-compositing" \
    wine "C:\\Program Files\\BlueStacks_nxt\\HD-Player.exe" --instance "${INSTANCE}" "$@"
