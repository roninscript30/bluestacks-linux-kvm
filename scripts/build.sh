#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="${ROOT_DIR}/src"
DIST_DIR="${ROOT_DIR}/dist"

CC="${CC:-clang}"
TARGET="${TARGET:-x86_64-w64-mingw32}"
CFLAGS=(-O2 -nostdlib -fno-builtin -mno-stack-arg-probe -I"${SRC_DIR}")

mkdir -p "${DIST_DIR}"

echo "[*] Building WinHvPlatform.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,_DllMainCRTStartup \
    "${SRC_DIR}/WinHvPlatform.c" -o "${DIST_DIR}/WinHvPlatform.dll"

echo "[*] Building WinHvEmulation.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,_DllMainCRTStartup \
    "${SRC_DIR}/WinHvEmulation.c" -o "${DIST_DIR}/WinHvEmulation.dll"

echo "[*] Building vid.dll..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -shared -Wl,-e,DllMain \
    "${SRC_DIR}/vid.c" -o "${DIST_DIR}/vid.dll"

echo "[*] Building ffmpeg.exe stub..."
"${CC}" --target="${TARGET}" "${CFLAGS[@]}" -Wl,-e,mainCRTStartup -Wl,--subsystem,console \
    "${SRC_DIR}/ffmpeg_stub.c" -o "${DIST_DIR}/ffmpeg.exe"

echo "[+] Build complete -> ${DIST_DIR}"
ls -lh "${DIST_DIR}"
