#!/usr/bin/env bash
set -euo pipefail

ADB_ADDR="${ADB_ADDR:-127.0.0.1:5555}"
WINEPREFIX="${WINEPREFIX:-${HOME}/.wine}"
HD_ADB="${WINEPREFIX}/drive_c/Program Files/BlueStacks_nxt/HD-Adb.exe"

run_adb() {
    if command -v adb >/dev/null 2>&1; then
        adb "$@"
    else
        WINEDEBUG=-all WINEPREFIX="${WINEPREFIX}" wine "${HD_ADB}" "$@" 2>/dev/null | tr -d '\r'
    fi
}

echo "[*] Connecting to BlueStacks ADB at ${ADB_ADDR}..."
run_adb connect "${ADB_ADDR}" >/dev/null || true

for _ in {1..45}; do
    if run_adb -s "${ADB_ADDR}" shell getprop sys.boot_completed 2>/dev/null | grep -q "1"; then
        break
    fi
    sleep 1
    run_adb connect "${ADB_ADDR}" >/dev/null 2>&1 || true
done

PAYLOAD_B64="$(cat << 'EOF' | base64 -w0
#!/system/bin/sh
echo "0 0 0 0" > /proc/sys/kernel/printk 2>/dev/null

if [ -f /sys/devices/system/clocksource/clocksource0/current_clocksource ]; then
    echo tsc > /sys/devices/system/clocksource/clocksource0/current_clocksource 2>/dev/null
fi

for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    [ -f "$gov" ] && echo performance > "$gov" 2>/dev/null
done

setprop ctl.stop pagefusion
setprop debug.egl.swapinterval 0
setprop debug.sf.swapinterval 0
setprop debug.sf.latch_unsignaled 1
setprop debug.sf.disable_backpressure 1
setprop debug.hwui.renderer opengl
setprop persist.sys.NV_FPSLIMIT 240
setprop bst.fps 240

for pfile in /data/bluestacks.prop /data/.bluestacks.prop; do
    if [ -f "$pfile" ]; then
        cp "$pfile" /data/local/tmp/bstprop.tmp
        sed -i "s/^ro.product.model=.*/ro.product.model=ASUS_Z01QD/" /data/local/tmp/bstprop.tmp
        sed -i "s/^ro.product.brand=.*/ro.product.brand=asus/" /data/local/tmp/bstprop.tmp
        sed -i "s/^ro.product.name=.*/ro.product.name=WW_I001D/" /data/local/tmp/bstprop.tmp
        sed -i "s/^ro.product.device=.*/ro.product.device=ASUS_I001_1/" /data/local/tmp/bstprop.tmp
        sed -i "s/^ro.product.manufacturer=.*/ro.product.manufacturer=asus/" /data/local/tmp/bstprop.tmp
        cat /data/local/tmp/bstprop.tmp > "$pfile"
        chmod 644 "$pfile"
        rm -f /data/local/tmp/bstprop.tmp
    fi
done

settings put global window_animation_scale 0.0
settings put global transition_animation_scale 0.0
settings put global animator_duration_scale 0.0

# Distribute PCI-MSI IRQ affinity across VCPUs (BstkVMM's LowestPriority delivery
# uses ASMBitLastSet(smp_affinity), which otherwise pins all IRQs to CPU3 when mask=0xf)
if [ -f /proc/irq/27/smp_affinity ]; then
    echo 2 > /proc/irq/27/smp_affinity 2>/dev/null
fi
for irq in 19 24 26; do
    if [ -f "/proc/irq/${irq}/smp_affinity" ]; then
        echo 1 > "/proc/irq/${irq}/smp_affinity" 2>/dev/null
    fi
done
EOF
)"

echo "[*] Pushing and executing root guest-tuning payload via bstk/su..."
run_adb -s "${ADB_ADDR}" shell "echo ${PAYLOAD_B64} | base64 -d > /data/local/tmp/stop && chmod 755 /data/local/tmp/stop && PATH=/data/local/tmp:/system/bin /system/xbin/bstk/su root stop && rm -f /data/local/tmp/stop"

echo "[+] Guest tuning applied (240Hz unlocked, TSC clocksource, IRQ affinity balanced, pagefusion stopped, printk silenced, ASUS ROG 2 active)."
