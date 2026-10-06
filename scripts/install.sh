#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"

echo "================================================================"
echo "  BlueStacks 5 on Linux (Wine + KVM + GPU Acceleration) Setup"
echo "================================================================"

# 1. Check & install required packages (wine, clang, lld) if missing
install_deps() {
    local missing=()
    command -v wine >/dev/null 2>&1 || missing+=("wine")
    command -v clang >/dev/null 2>&1 || missing+=("clang")
    command -v base64 >/dev/null 2>&1 || missing+=("coreutils")

    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "[*] Installing missing dependencies: ${missing[*]}..."
        if command -v apt-get >/dev/null 2>&1; then
            sudo apt-get update -y
            sudo apt-get install -y wine64 wine clang lld mingw-w64-x86-64-dev
        elif command -v dnf >/dev/null 2>&1; then
            sudo dnf install -y wine clang lld mingw64-headers
        elif command -v pacman >/dev/null 2>&1; then
            sudo pacman -Sy --noconfirm wine clang lld mingw-w64-gcc
        else
            echo "[!] Please install manually: ${missing[*]}"
            exit 1
        fi
    fi
}

# 2. Check /dev/kvm access
check_kvm() {
    if [[ ! -e /dev/kvm ]]; then
        echo "[!] /dev/kvm not found. Enable AMD-V / Intel VT-x in BIOS."
        exit 1
    fi
    if [[ ! -r /dev/kvm || ! -w /dev/kvm ]]; then
        echo "[*] Granting /dev/kvm access to ${USER}..."
        sudo usermod -aG kvm "${USER}" 2>/dev/null || true
        sudo chmod 0666 /dev/kvm
    fi
}

# 3. Auto-detect Wine prefix & BlueStacks installation
detect_bluestacks() {
    local candidates=(
        "${WINEPREFIX:-}"
        "${HOME}/.wine"
        "${HOME}/.local/share/wineprefixes/bluestacks"
    )
    for prefix in "${candidates[@]}"; do
        [[ -z "${prefix}" ]] && continue
        if [[ -f "${prefix}/drive_c/Program Files/BlueStacks_nxt/HD-Player.exe" ]]; then
            export WINEPREFIX="${prefix}"
            echo "[+] Detected BlueStacks 5 in WINEPREFIX=${WINEPREFIX}"
            return 0
        fi
    done

    # Fallback: search home directory for HD-Player.exe
    local found_exe
    found_exe="$(find "${HOME}" -maxdepth 6 -name "HD-Player.exe" 2>/dev/null | head -n 1 || true)"
    if [[ -n "${found_exe}" ]]; then
        export WINEPREFIX="$(echo "${found_exe}" | sed 's|/drive_c/.*||')"
        echo "[+] Auto-detected BlueStacks 5 at ${found_exe} (WINEPREFIX=${WINEPREFIX})"
        return 0
    fi

    # If not installed yet, look for BlueStacks installer .exe in Downloads / HOME
    export WINEPREFIX="${WINEPREFIX:-${HOME}/.wine}"
    echo "[*] Initializing Wine prefix at ${WINEPREFIX}..."
    WINEDEBUG=-all wineboot -u 2>/dev/null || true
}

install_deps
check_kvm
detect_bluestacks

WINEPREFIX="${WINEPREFIX:-${HOME}/.wine}"
SYS32_DIR="${WINEPREFIX}/drive_c/windows/system32"
BST_PROG_DIR="${WINEPREFIX}/drive_c/Program Files/BlueStacks_nxt"
BST_CONF="${WINEPREFIX}/drive_c/ProgramData/BlueStacks_nxt/bluestacks.conf"
DESKTOP_DIR="${HOME}/.local/share/applications/wine/Programs"

# 4. Build WHPX-to-KVM bridge DLLs & ffmpeg stub
chmod +x "${ROOT_DIR}/scripts/"*.sh "${ROOT_DIR}/bluestacks-kvm"
"${ROOT_DIR}/scripts/build.sh"

# 5. Install WinHvPlatform.dll, WinHvEmulation.dll, vid.dll into system32
echo "[*] Installing WHPX-to-KVM bridge DLLs into ${SYS32_DIR}..."
mkdir -p "${SYS32_DIR}"
cp -f "${DIST_DIR}/WinHvPlatform.dll" "${SYS32_DIR}/WinHvPlatform.dll"
cp -f "${DIST_DIR}/WinHvEmulation.dll" "${SYS32_DIR}/WinHvEmulation.dll"
cp -f "${DIST_DIR}/vid.dll" "${SYS32_DIR}/vid.dll"

# 6. If BlueStacks is not installed yet, check for an installer .exe
if [[ ! -f "${BST_PROG_DIR}/HD-Player.exe" ]]; then
    INSTALLER_EXE="$(find "${HOME}/Downloads" "${HOME}" -maxdepth 2 -iname "BlueStacksInstaller*.exe" 2>/dev/null | head -n 1 || true)"
    if [[ -n "${INSTALLER_EXE}" ]]; then
        echo "[*] Found BlueStacks installer: ${INSTALLER_EXE}"
        echo "[*] Running installer under Wine (with KVM bridge pre-installed)..."
        WINEDEBUG=-all WINEPREFIX="${WINEPREFIX}" wine "${INSTALLER_EXE}" || true
    else
        echo "[!] BlueStacks 5 is not installed in ${WINEPREFIX} yet."
        echo "    WHPX-to-KVM DLLs are installed in ${SYS32_DIR}."
        echo "    Run: WINEPREFIX=\"${WINEPREFIX}\" wine BlueStacksInstaller.exe, then re-run ./install.sh"
    fi
fi

# 7. Replace ffmpeg.exe with no-op stub (eliminates periodic DXVK camera-polling stutters)
if [[ -d "${BST_PROG_DIR}" ]]; then
    echo "[*] Installing no-op ffmpeg.exe stub (eliminates DXVK camera-polling stutters)..."
    if [[ -f "${BST_PROG_DIR}/ffmpeg.exe" && ! -f "${BST_PROG_DIR}/ffmpeg.exe.bak" ]]; then
        cp -f "${BST_PROG_DIR}/ffmpeg.exe" "${BST_PROG_DIR}/ffmpeg.exe.bak"
    fi
    cp -f "${DIST_DIR}/ffmpeg.exe" "${BST_PROG_DIR}/ffmpeg.exe"
fi

# 8. Auto-detect all instances in bluestacks.conf & apply 240 FPS + ROG 2 + PGA/GL config
set_conf_key() {
    local key="$1"
    local val="$2"
    local file="$3"
    if grep -q "^${key}=" "${file}"; then
        sed -i "s|^${key}=.*|${key}=\"${val}\"|" "${file}"
    else
        echo "${key}=\"${val}\"" >> "${file}"
    fi
}

DEFAULT_INSTANCE="Nougat32"
if [[ -f "${BST_CONF}" ]]; then
    cp -f "${BST_CONF}" "${BST_CONF}.bak"
    sed -i "/\.fps=/d" "${BST_CONF}"

    set_conf_key "bst.enable_adb_access" "1" "${BST_CONF}"
    set_conf_key "bst.feature.rooting" "1" "${BST_CONF}"
    set_conf_key "bst.prefer_dedicated_gpu" "1" "${BST_CONF}"

    # Find all configured instances (e.g., Nougat32, Nougat64, Pie64, Rvc64)
    mapfile -t INSTANCES < <(grep -oE '^bst\.instance\.[^.]+\.' "${BST_CONF}" | cut -d. -f3 | sort -u)
    if [[ ${#INSTANCES[@]} -eq 0 ]]; then
        INSTANCES=("Nougat32")
    fi

    for inst in "${INSTANCES[@]}"; do
        echo "[*] Applying 240 FPS + ASUS ROG 2 + PGA/GL configuration to instance: ${inst}..."
        set_conf_key "bst.instance.${inst}.enable_root_access" "1" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.cpus" "4" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.ram" "3072" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.device_profile_code" "rogt" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.enable_high_fps" "1" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.max_fps" "240" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.enable_vsync" "0" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.graphics_engine" "pga" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.graphics_renderer" "gl" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.astc_decoding_mode" "disabled" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.fb_width" "1600" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.fb_height" "900" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.dpi" "240" "${BST_CONF}"
    done
fi

# 9. Create Desktop Launcher
mkdir -p "${DESKTOP_DIR}"
cat > "${DESKTOP_DIR}/BlueStacks 5.desktop" <<EOF
[Desktop Entry]
Name=BlueStacks 5
Comment=BlueStacks 5 Android App Player (KVM + GPU Accelerated)
Exec=${ROOT_DIR}/scripts/launch.sh
Type=Application
StartupNotify=true
Path=${WINEPREFIX}/dosdevices/c:/Program Files/BlueStacks_nxt
Icon=A60E_HD-Player.0
StartupWMClass=hd-player.exe
Categories=Game;Emulator;
EOF

echo "================================================================"
echo "[+] Installation & auto-configuration complete!"
echo "    Run BlueStacks anytime with: ./bluestacks-kvm run"
echo "================================================================"
