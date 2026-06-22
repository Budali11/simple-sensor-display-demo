#!/bin/sh
#
# load_and_run.sh — load the AP3216C + ICM20608 IIO drivers, then start the
# LVGL sensor-display app. Meant to be run ON THE TARGET BOARD, from the
# directory that holds ap3216c.ko / icm20608.ko / rgb_lcd_app (the NFS rootfs).
#
# Usage:  ./load_and_run.sh
#         ./load_and_run.sh --no-app   # only (re)load the drivers
#
set -e

# Resolve the directory this script lives in, so it works regardless of CWD.
DIR=$(cd "$(dirname "$0")" && pwd)
APP="$DIR/rgb_lcd_app"
MODULES="ap3216c icm20608"

run_app=1
[ "$1" = "--no-app" ] && run_app=0

# Remove any sensor modules already bound to these devices first — this
# includes the original i2cd / spid modules, which match the same device-tree
# nodes (compatible = dunnan,ap3216c / tdk,icm20608) and would block probing.
for m in ap3216c icm20608 i2cd spid; do
    if lsmod 2>/dev/null | grep -q "^$m "; then
        echo "rmmod $m"
        rmmod "$m" 2>/dev/null || echo "  (warning: rmmod $m failed)"
    fi
done

# Insert our modules.
for m in $MODULES; do
    if [ ! -f "$DIR/$m.ko" ]; then
        echo "error: $DIR/$m.ko not found — build & deploy the project first" >&2
        exit 1
    fi
    echo "insmod $m.ko"
    insmod "$DIR/$m.ko"
done

# Report which IIO devices showed up.
echo "--- registered IIO devices ---"
for d in /sys/bus/iio/devices/iio:device*; do
    [ -e "$d/name" ] && echo "  $d -> $(cat "$d/name")"
done

if [ "$run_app" -eq 0 ]; then
    echo "drivers loaded; skipping app (--no-app)."
    exit 0
fi

if [ ! -x "$APP" ]; then
    echo "error: $APP not found or not executable" >&2
    exit 1
fi

echo "starting rgb_lcd_app (Ctrl-C to quit) ..."
exec "$APP"
