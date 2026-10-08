#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/common.sh
source "${ROOT_DIR}/scripts/common.sh"

chmod +x "${ROOT_DIR}/scripts/"*.sh "${ROOT_DIR}/bluestacks-kvm"
"${ROOT_DIR}/scripts/install.sh" "$@"

# Auto-launch if HD-Player.exe is present and --no-launch was not requested
for arg in "$@"; do
    [[ "${arg}" == "--no-launch" ]] && exit 0
done
if [[ -f "${HD_PLAYER}" ]]; then
    echo "[*] Launching BlueStacks 5 (KVM + GPU Accelerated)..."
    exec "${ROOT_DIR}/scripts/launch.sh"
fi
