#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/src"
DIST_DIR="${ROOT_DIR}/dist"

CC="${CC:-clang}"
HOST_CC="${HOST_CC:-cc}"
TARGET="${TARGET:-x86_64-w64-mingw32}"
CFLAGS=(-O2 -nostdlib -fno-builtin -mno-stack-arg-probe -I"${SRC_DIR}")

# The WHPX bridge DLLs call into winhv_kvm.so through Wine unix calls, which need
# the installed Wine's PE import libraries (libwinecrt0.a provides __wine_load_unix_lib).
find_wine_pe_libdir() {
    local wine_bin dir
    wine_bin="$(command -v wine || true)"
    for dir in "${WINE_PE_LIBDIR:-}" \
               "${wine_bin:+$(dirname "$(readlink -f "${wine_bin}")")/../lib/wine/x86_64-windows}" \
               /usr/lib/wine/x86_64-windows /usr/lib64/wine/x86_64-windows \
               /usr/lib/x86_64-linux-gnu/wine/x86_64-windows /opt/wine*/lib/wine/x86_64-windows; do
        if [[ -n "${dir}" && -f "${dir}/libwinecrt0.a" && -f "${dir}/libntdll.a" ]]; then
            readlink -f "${dir}"
            return 0
        fi
    done
    return 1
}
if ! WINE_PE_LIBDIR="$(find_wine_pe_libdir)"; then
    echo "[!] Wine's PE import libraries (libwinecrt0.a, libntdll.a) were not found."
    echo "    Install your distribution's Wine development files (Arch: wine; Fedora: wine-devel;"
    echo "    Debian/Ubuntu: wine64-tools), or set WINE_PE_LIBDIR to the .../wine/x86_64-windows directory."
    exit 1
fi
WINE_LIBS=("${WINE_PE_LIBDIR}/libwinecrt0.a" "${WINE_PE_LIBDIR}/libntdll.a" "${WINE_PE_LIBDIR}/libkernel32.a")

mkdir -p "${DIST_DIR}"

echo "[*] Building winhv_kvm.so (Linux side of the WHPX bridge)..."
"${HOST_CC}" -O2 -fPIC -shared -fvisibility=hidden -Wall -I"${SRC_DIR}" \
    "${SRC_DIR}/winhv_kvm.c" -o "${DIST_DIR}/winhv_kvm.so"

echo "[*] Building WinHvPlatform.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,_DllMainCRTStartup \
    "${SRC_DIR}/WinHvPlatform.c" -o "${DIST_DIR}/WinHvPlatform.dll" "${WINE_LIBS[@]}"

echo "[*] Building WinHvEmulation.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,_DllMainCRTStartup \
    "${SRC_DIR}/WinHvEmulation.c" -o "${DIST_DIR}/WinHvEmulation.dll" "${WINE_LIBS[@]}"

echo "[*] Building vid.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,DllMain \
    "${SRC_DIR}/vid.c" -o "${DIST_DIR}/vid.dll" -lntdll

echo "[*] Building bstdns.dll (DnsQueryConfig shim for BstkSVC.exe)..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,_DllMainCRTStartup \
    "${SRC_DIR}/bstdns.c" -o "${DIST_DIR}/bstdns.dll" -lkernel32

echo "[*] Building bstkdrv_stub.sys (no-op BlueStacksDrv_nxt service driver)..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,--subsystem,native -Wl,-e,DriverEntry \
    "${SRC_DIR}/bstkdrv_stub.c" -o "${DIST_DIR}/bstkdrv_stub.sys"

echo "[*] Building ffmpeg.exe stub..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -Wl,-e,mainCRTStartup -Wl,--subsystem,console \
    "${SRC_DIR}/ffmpeg_stub.c" -o "${DIST_DIR}/ffmpeg.exe"

echo "[+] Build complete -> ${DIST_DIR}"
ls -lh "${DIST_DIR}"
