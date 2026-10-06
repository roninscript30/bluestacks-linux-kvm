#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

chmod +x "${ROOT_DIR}/scripts/"*.sh "${ROOT_DIR}/bluestacks-kvm"
"${ROOT_DIR}/scripts/install.sh" "$@"

# Auto-launch if HD-Player.exe is present and --no-launch was not requested
if [[ "${1:-}" != "--no-launch" ]]; then
    WINEPREFIX="${WINEPREFIX:-${HOME}/.wine}"
    if [[ -f "${WINEPREFIX}/drive_c/Program Files/BlueStacks_nxt/HD-Player.exe" ]] || \
       [[ -n "$(find "${HOME}" -maxdepth 5 -name "HD-Player.exe" 2>/dev/null | head -n 1 || true)" ]]; then
        echo "[*] Launching BlueStacks 5 (KVM + GPU Accelerated)..."
        exec "${ROOT_DIR}/scripts/launch.sh"
    fi
fi

