#!/bin/sh
cd /proc/device-tree/lcds/lcd_icnl9707_huiyi_mipi_hdp/display-timings/timing0 || exit 1
for f in hactive vactive hfront-porch hback-porch hsync-len vfront-porch vback-porch vsync-len clock-frequency name; do
    printf '%-16s ' "$f"
    hexdump -C "$f" 2>/dev/null | head -n 2
done
