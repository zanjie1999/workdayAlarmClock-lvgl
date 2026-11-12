#!/bin/sh
sh -c 'sleep 20; ps|grep -E "z03|Z03|control_engine"|awk "{system(\"kill -9 \"\$1)}"' &
/etc/init.d/weston stop
cd /home/root/workdayAlarmClock
killall workdayAlarmClock-linux-arm
killall wac

export FB_WIDTH=480
export FB_HEIGHT=1280
export WAC_GO_BIN=./workdayAlarmClock-linux-arm
export WAC_CJK_TTF=/home/root/workdayAlarmClock/lvgl/assets/fonts/wac_cjk.ttf
export WAC_CLOCK_TTF=/home/root/workdayAlarmClock/lvgl/assets/fonts/zyymeclock.ttf
export WAC_KEYS_CONF=/home/root/workdayAlarmClock/lvgl/wac_keys.conf

setsid ./lvgl/wac </dev/null >/tmp/wac.log 2>&1 &
