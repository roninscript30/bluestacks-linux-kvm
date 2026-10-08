# shellcheck shell=bash disable=SC2034
# Shared paths for all scripts (source this file, do not execute it).
#
# BlueStacks lives in its own dedicated Wine prefix. Override the location with
# BLUESTACKS_PREFIX; an ambient WINEPREFIX is ignored on purpose so we never
# touch an unrelated prefix.

BLUESTACKS_PREFIX="${BLUESTACKS_PREFIX:-${HOME}/.bluestacks}"
export WINEPREFIX="${BLUESTACKS_PREFIX}"
export WINEARCH=win64

SYS32_DIR="${WINEPREFIX}/drive_c/windows/system32"
BST_PROG_DIR="${WINEPREFIX}/drive_c/Program Files/BlueStacks_nxt"
BST_DATA_DIR="${WINEPREFIX}/drive_c/ProgramData/BlueStacks_nxt"
BST_CONF="${BST_DATA_DIR}/bluestacks.conf"
HD_PLAYER="${BST_PROG_DIR}/HD-Player.exe"
BST_LOG_DIR="${BLUESTACKS_PREFIX}/logs"
