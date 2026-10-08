#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST_DIR="${ROOT_DIR}/dist"
# shellcheck source=scripts/common.sh
source "${ROOT_DIR}/scripts/common.sh"

INSTALLER_EXE=""
IMAGE=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        --installer)
            INSTALLER_EXE="${2:?--installer requires a path}"
            shift 2
            ;;
        --installer=*)
            INSTALLER_EXE="${1#*=}"
            shift
            ;;
        --image)
            IMAGE="${2:?--image requires a name (Nougat32, Nougat64, Pie64, Rvc64, Tiramisu64)}"
            shift 2
            ;;
        --image=*)
            IMAGE="${1#*=}"
            shift
            ;;
        --no-launch)
            shift
            ;;
        *)
            echo "[!] Unknown option: $1"
            echo "Usage: $0 [--installer PATH] [--image NAME] [--no-launch]"
            exit 1
            ;;
    esac
done
if [[ -n "${INSTALLER_EXE}" && ! -f "${INSTALLER_EXE}" ]]; then
    echo "[!] Installer not found: ${INSTALLER_EXE}"
    exit 1
fi

echo "================================================================"
echo "  BlueStacks 5 on Linux (Wine + KVM + GPU Acceleration) Setup"
echo "================================================================"

# 1. Check & install required packages (wine, clang, lld, 7z, ...) if missing
install_deps() {
    local missing=()
    command -v wine >/dev/null 2>&1 || missing+=("wine")
    command -v clang >/dev/null 2>&1 || missing+=("clang")
    command -v base64 >/dev/null 2>&1 || missing+=("coreutils")
    command -v 7z >/dev/null 2>&1 || missing+=("7z")
    command -v strings >/dev/null 2>&1 || missing+=("binutils")
    command -v python3 >/dev/null 2>&1 || missing+=("python3")
    command -v curl >/dev/null 2>&1 || missing+=("curl")

    if [[ ${#missing[@]} -gt 0 ]]; then
        echo "[*] Installing missing dependencies: ${missing[*]}..."
        if command -v apt-get >/dev/null 2>&1; then
            sudo apt-get update -y
            sudo apt-get install -y wine64 wine clang lld mingw-w64-x86-64-dev p7zip-full binutils python3 curl
        elif command -v dnf >/dev/null 2>&1; then
            sudo dnf install -y wine clang lld mingw64-headers p7zip p7zip-plugins binutils python3 curl
        elif command -v pacman >/dev/null 2>&1; then
            sudo pacman -Sy --noconfirm wine clang lld mingw-w64-gcc 7zip binutils python curl
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

# 3. Create the dedicated BlueStacks Wine prefix if it does not exist yet
ensure_prefix() {
    if [[ -f "${WINEPREFIX}/system.reg" ]]; then
        echo "[+] Using Wine prefix ${WINEPREFIX}"
        return 0
    fi
    echo "[*] Creating Wine prefix at ${WINEPREFIX}..."
    mkdir -p "${WINEPREFIX}"
    WINEDEBUG=-all wineboot -i
    wineserver -w
}

install_deps
check_kvm
ensure_prefix

DESKTOP_DIR="${HOME}/.local/share/applications/wine/Programs"

# 4. Build WHPX-to-KVM bridge DLLs & ffmpeg stub
chmod +x "${ROOT_DIR}/scripts/"*.sh "${ROOT_DIR}/bluestacks-kvm"
"${ROOT_DIR}/scripts/build.sh"

# 5. Install WinHvPlatform.dll, WinHvEmulation.dll, vid.dll (+ bstdns.dll shim) into system32
echo "[*] Installing WHPX-to-KVM bridge DLLs into ${SYS32_DIR}..."
mkdir -p "${SYS32_DIR}"
cp -f "${DIST_DIR}/WinHvPlatform.dll" "${SYS32_DIR}/WinHvPlatform.dll"
cp -f "${DIST_DIR}/WinHvEmulation.dll" "${SYS32_DIR}/WinHvEmulation.dll"
cp -f "${DIST_DIR}/vid.dll" "${SYS32_DIR}/vid.dll"
cp -f "${DIST_DIR}/winhv_kvm.so" "${SYS32_DIR}/winhv_kvm.so"
cp -f "${DIST_DIR}/bstdns.dll" "${SYS32_DIR}/bstdns.dll"

# 6. If BlueStacks is not installed yet, install it from the official full installer.
#
# BlueStacksInstaller*.exe from bluestacks.com is only a .NET micro-installer that
# downloads the full installer + Android image and runs them. Under Wine (Mono) the
# full installer rolls back, so we read the version, CDN path and image from the
# micro-installer, download the same files, patch them (scripts/bstpatch.py) and
# run the full installer silently ourselves.
BST_CACHE_DIR="${XDG_CACHE_HOME:-${HOME}/.cache}/bluestacks-linux-kvm"
STAGE_DIR="${WINEPREFIX}/drive_c/bluestacks-setup"
STAGE_DIR_WIN='C:\bluestacks-setup'

# BlueStacksInstaller_<ver>_native_<md5>_<base64 "index;abi">.exe -> image name
image_from_installer_name() {
    local idx
    idx="$(basename "$1" .exe | sed 's/.*_//' | base64 -d 2>/dev/null | cut -d';' -f1 || true)"
    case "${idx}" in
        0) echo "Nougat32" ;;
        1) echo "Nougat64" ;;
        3) echo "Pie64" ;;
        4) echo "Rvc64" ;;
        5) echo "Tiramisu64" ;;
        *) echo "Nougat32" ;;
    esac
}

BSTPATCH=(python3 -I "${ROOT_DIR}/scripts/bstpatch.py")

# Wine fixes for BstkSVC.exe / BstkProxyStub.dll in DIR (idempotent).
patch_bluestacks_binaries() {
    local dir="$1"
    # Wine's DnsQueryConfig ignores DNS_CONFIG_FLAG_ALLOC (see src/bstdns.c).
    "${BSTPATCH[@]}" rename-import "${dir}/BstkSVC.exe" DNSAPI.dll bstdns.dll
    # Wine's rpcrt4 fills MIDL's proxy/stub vtables in place, lacks NdrStubCall3 (NDR64)
    # and does not export ObjectStublessClientN / NdrProxyForwardingFunctionN.
    "${BSTPATCH[@]}" writable-section "${dir}/BstkProxyStub.dll" .rdata
    "${BSTPATCH[@]}" rename-import-func "${dir}/BstkProxyStub.dll" RPCRT4.dll NdrStubCall3 NdrStubCall2
    "${BSTPATCH[@]}" wine-proxy-vtables "${dir}/BstkProxyStub.dll"
}

fetch() {
    local url="$1" dest="$2"
    [[ -f "${dest}" ]] && return 0
    echo "[*] Downloading $(basename "${dest}")..."
    curl -fL --retry 3 --progress-bar -C - -o "${dest}.part" "${url}"
    mv -f "${dest}.part" "${dest}"
}

install_bluestacks() {
    local cdn_path version base cache meta image_md5 full_exe image_exe stamp log rc=0

    cdn_path="$(7z e -so "${INSTALLER_EXE}" BlueStacksInstaller.exe 2>/dev/null | strings -e l \
        | grep -oE 'windows/nxt/[0-9.]+/[0-9a-f]{32}' | head -n 1 || true)"
    if [[ -z "${cdn_path}" ]]; then
        echo "[!] ${INSTALLER_EXE} does not look like a BlueStacks 5 installer from bluestacks.com."
        exit 1
    fi
    version="$(cut -d/ -f3 <<<"${cdn_path}")"
    base="https://cdn3.bluestacks.com/downloads/${cdn_path}"
    IMAGE="${IMAGE:-$(image_from_installer_name "${INSTALLER_EXE}")}"
    echo "[*] Installing BlueStacks ${version} with Android image ${IMAGE}"

    # Downloads are kept outside the prefix so a prefix reset does not re-download ~900 MB.
    cache="${BST_CACHE_DIR}/${version}"
    if ! mkdir -p "${cache}" 2>/dev/null; then
        cache="${BLUESTACKS_PREFIX}/cache/${version}"
        mkdir -p "${cache}"
    fi
    echo "    Download cache: ${cache}"
    meta="${cache}/metadata.txt"
    full_exe="${cache}/BlueStacks-Installer_${version}_amd64_native.exe"
    image_exe="${cache}/${IMAGE}.exe"
    fetch "${base}/Android/metadata.txt" "${meta}"
    image_md5="$(grep -oE "\"${IMAGE}\": *\"[0-9a-f]{32}\"" "${meta}" | grep -oE '[0-9a-f]{32}' || true)"
    if [[ -z "${image_md5}" ]]; then
        echo "[!] Unknown Android image '${IMAGE}'. Available: $(grep -oE '"[A-Za-z0-9]+":' "${meta}" | tr -d '":' | xargs)"
        exit 1
    fi
    fetch "${base}/x64/BlueStacks-Installer_${version}_amd64_native.exe" "${full_exe}"
    fetch "${base}/Android/${IMAGE}/${IMAGE}.exe" "${image_exe}"
    if [[ "$(md5sum "${image_exe}" | cut -d' ' -f1)" != "${image_md5}" ]]; then
        rm -f "${image_exe}"
        echo "[!] ${IMAGE}.exe failed its MD5 check and was deleted; re-run ./install.sh to download it again."
        exit 1
    fi

    echo "[*] Staging and patching the installer for Wine in ${STAGE_DIR}..."
    rm -rf "${STAGE_DIR}"
    mkdir -p "${STAGE_DIR}/_patch"
    7z x -y "${full_exe}" -o"${STAGE_DIR}" >/dev/null
    7z e -y "${STAGE_DIR}/PF.zip" BstkTypeLib.dll BstkSVC.exe BstkProxyStub.dll -o"${STAGE_DIR}/_patch" >/dev/null
    # Mono resolves field types eagerly, so the installer needs this interop
    # assembly next to it before PF.zip is even deployed.
    cp -f "${STAGE_DIR}/_patch/BstkTypeLib.dll" "${STAGE_DIR}/"
    # ServiceController.ServiceHandle is unimplemented in Wine Mono; service DACLs are moot under Wine.
    "${BSTPATCH[@]}" stub-method "${STAGE_DIR}/HD-Common.dll" BlueStacks.Common.ServiceManager SetServicePermissions
    # The installer itself talks to BstkSVC.exe over COM, so patch inside PF.zip too.
    patch_bluestacks_binaries "${STAGE_DIR}/_patch"
    (cd "${STAGE_DIR}/_patch" && 7z a -tzip "${STAGE_DIR}/PF.zip" BstkSVC.exe BstkProxyStub.dll >/dev/null)
    rm -rf "${STAGE_DIR}/_patch"
    # The installer looks for <image>_<version>.exe next to -parentpath (and deletes it when done).
    cp -f "${image_exe}" "${STAGE_DIR}/${IMAGE}_${version}.exe"

    mkdir -p "${BST_LOG_DIR}"
    log="${BST_LOG_DIR}/installer-$(date +%Y%m%d-%H%M%S).log"
    stamp="${BST_LOG_DIR}/.installer-start"
    touch "${stamp}"
    echo "[*] Running the BlueStacks installer silently (about a minute)..."
    echo "    Wine log: ${log}"
    (cd "${STAGE_DIR}" && WINEDEBUG="${WINEDEBUG:-err+all}" wine "${STAGE_DIR_WIN}\\BlueStacksInstaller.exe" -s \
        '-pddir=C:\ProgramData\BlueStacks_nxt' "-defaultImageName=${IMAGE}" "-imageToLaunch=${IMAGE}" \
        "-parentpath=${STAGE_DIR_WIN}\\BlueStacksInstaller.exe") >"${log}" 2>&1 || rc=$?

    if [[ ${rc} -ne 0 || ! -f "${HD_PLAYER}" ]]; then
        echo "[!] BlueStacks installation failed (installer exit code ${rc})."
        echo "    Wine log: ${log}"
        echo "    BlueStacks logs written during this run:"
        find "${WINEPREFIX}/drive_c" -iname '*.log' -ipath '*bluestacks*' -newer "${stamp}" 2>/dev/null \
            | sed 's/^/      /' || true
        echo "    The staged installer was left in ${STAGE_DIR} for debugging."
        exit 1
    fi
    rm -rf "${STAGE_DIR}"
    echo "[+] BlueStacks 5 installed in ${BST_PROG_DIR}"
}

if [[ ! -f "${HD_PLAYER}" ]]; then
    if [[ -z "${INSTALLER_EXE}" ]]; then
        INSTALLER_EXE="$(find "${HOME}/Downloads" -maxdepth 1 -iname 'BlueStacksInstaller*.exe' -printf '%T@ %p\n' 2>/dev/null \
            | sort -rn | head -n 1 | cut -d' ' -f2- || true)"
    fi
    if [[ -z "${INSTALLER_EXE}" ]]; then
        echo "[!] BlueStacks 5 is not installed in ${WINEPREFIX} yet, and no installer was found in ~/Downloads."
        echo "    WHPX-to-KVM DLLs are installed in ${SYS32_DIR}."
        echo "    Download BlueStacks 5, then re-run: ./install.sh --installer /path/to/BlueStacksInstaller.exe"
        exit 1
    fi
    install_bluestacks
fi

# Stop any running BlueStacks / VirtualBox COM daemon: the binaries below are rewritten in
# place (Wine maps them), and .bstk/.conf edits must not be overwritten from memory
pkill -9 -f "HD-Player.exe|BstkSVC.exe|HD-MultiInstanceManager.exe|HD-Adb.exe|HD-GLCheck.exe" 2>/dev/null || true
wineserver -k 2>/dev/null || true

# Re-apply the Wine fixes to the installed binaries (no-op if already patched; also
# upgrades installs made by older versions of this script, or replaced by a BlueStacks update)
echo "[*] Applying Wine fixes to installed BlueStacks binaries..."
patch_bluestacks_binaries "${BST_PROG_DIR}"
# Route the VM through WHPX (the KVM bridge) instead of the BlueStacks kernel driver,
# which cannot run under Wine: select the Hyper-V path, let SUPLib run driverless,
# and skip VirtualBox's "are we inside a Hyper-V partition" CPUID probe.
"${BSTPATCH[@]}" force-hyperv "${BST_PROG_DIR}/HD-Player.exe"
"${BSTPATCH[@]}" driverless-fallback "${BST_PROG_DIR}/BstkRT.dll"
"${BSTPATCH[@]}" nem-skip-cpuid-probe "${BST_PROG_DIR}/BstkVMM.dll"

# HD-Player.exe crashes when the BlueStacksDrv_nxt service fails to start (the real
# driver needs VMX root mode) and refuses to boot when it is disabled, so point the
# service at a no-op driver that starts cleanly (see src/bstkdrv_stub.c).
mkdir -p "${SYS32_DIR}/drivers"
cp -f "${DIST_DIR}/bstkdrv_stub.sys" "${SYS32_DIR}/drivers/bstkdrv_stub.sys"
WINEDEBUG=-all wine reg add 'HKLM\System\CurrentControlSet\Services\BlueStacksDrv_nxt' \
    /v ImagePath /t REG_EXPAND_SZ /d 'C:\windows\system32\drivers\bstkdrv_stub.sys' /f >/dev/null
WINEDEBUG=-all wine reg add 'HKLM\System\CurrentControlSet\Services\BlueStacksDrv_nxt' \
    /v Start /t REG_DWORD /d 3 /f >/dev/null

# 7. Replace ffmpeg.exe with no-op stub (eliminates periodic DXVK camera-polling stutters)
if [[ -d "${BST_PROG_DIR}" ]]; then
    echo "[*] Installing no-op ffmpeg.exe stub (eliminates DXVK camera-polling stutters)..."
    if [[ -f "${BST_PROG_DIR}/ffmpeg.exe" && ! -f "${BST_PROG_DIR}/ffmpeg.exe.bak" ]]; then
        cp -f "${BST_PROG_DIR}/ffmpeg.exe" "${BST_PROG_DIR}/ffmpeg.exe.bak"
    fi
    cp -f "${DIST_DIR}/ffmpeg.exe" "${BST_PROG_DIR}/ffmpeg.exe"
fi

# 8. Auto-detect all instances in bluestacks.conf & apply 240 FPS + ROG 2 + AGA/GL config

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
    set_conf_key "bst.enable_image_detection" "0" "${BST_CONF}"
    set_conf_key "bst.enable_ai_highlights" "0" "${BST_CONF}"
    set_conf_key "bst.enable_auto_upload_recording" "0" "${BST_CONF}"
    set_conf_key "bst.enable_discord_integration" "0" "${BST_CONF}"
    set_conf_key "bst.enable_programmatic_ads" "0" "${BST_CONF}"
    set_conf_key "bst.feature.programmatic_ads" "0" "${BST_CONF}"
    set_conf_key "bst.feature.show_moments" "0" "${BST_CONF}"
    set_conf_key "bst.feature.auto_upload_nowgg_moments" "0" "${BST_CONF}"
    set_conf_key "bst.feature.auto_upload_nowgg_recording" "0" "${BST_CONF}"
    set_conf_key "bst.mem_opt_mode" "0" "${BST_CONF}"

    # Find all configured instances (e.g., Nougat32, Nougat64, Pie64, Rvc64)
    mapfile -t INSTANCES < <(grep -oE '^bst\.instance\.[^.]+\.' "${BST_CONF}" | cut -d. -f3 | sort -u)
    if [[ ${#INSTANCES[@]} -eq 0 ]]; then
        INSTANCES=("Nougat32")
    fi

    for inst in "${INSTANCES[@]}"; do
        echo "[*] Applying 240 FPS + ASUS ROG 2 + AGA/GL configuration to instance: ${inst}..."
        set_conf_key "bst.instance.${inst}.enable_root_access" "1" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.cpus" "4" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.ram" "4096" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.device_profile_code" "rogt" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.enable_high_fps" "1" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.max_fps" "240" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.enable_vsync" "0" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.enable_fps_display" "1" "${BST_CONF}"
        # PGA fails graphics init (err 1011) for Android 11 on BlueStacks 5.22; AGA works.
        set_conf_key "bst.instance.${inst}.graphics_engine" "aga" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.graphics_renderer" "gl" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.vulkan_supported" "0" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.camera_backend" "" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.camera_device" "" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.astc_decoding_mode" "software" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.fb_width" "1600" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.fb_height" "900" "${BST_CONF}"
        set_conf_key "bst.instance.${inst}.dpi" "240" "${BST_CONF}"
    done
fi

# Strip restrictive HostCPUID/00000001/ecx=0x201 mask and ensure RealTSCOffset in VirtualBox .bstk configs
ENGINE_DIR="${BST_DATA_DIR}/Engine"
if [[ -d "${ENGINE_DIR}" ]]; then
    while IFS= read -r -d '' bstk_file; do
        sed -i '/HostCPUID\/00000001\/ecx/d' "${bstk_file}" 2>/dev/null || true
        if ! grep -q 'VBoxInternal/TM/TSCMode' "${bstk_file}" 2>/dev/null; then
            sed -i 's|<ExtraData>|<ExtraData>\n      <ExtraDataItem name="VBoxInternal/TM/TSCMode" value="RealTSCOffset"/>|' "${bstk_file}" 2>/dev/null || true
        fi
    done < <(find "${ENGINE_DIR}" -maxdepth 2 \( -name "*.bstk" -o -name "*.bstk-prev" -o -name "Android.bstk.in" \) -print0 2>/dev/null)
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
