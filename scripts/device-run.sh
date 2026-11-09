#!/bin/sh
# 在设备上启动 LVGL 外壳。
#
# 这块板原来跑的是三个 shell 脚本（timeDisplay.sh 直接往 /dev/fb0 画时间、
# pwrKey.sh / volKey.sh 用 dd 阻塞读 /dev/input/event*）+ 一个 Go 进程。
# LVGL 要独占屏幕和按键，所以先把它们全停掉；Go 由 LVGL 自己 fork 拉起
# （它会带 lvgl 参数，于是 Go 才肯把 POS/DUR/VOL 打到 stdout）。
#
# 用法：
#   ./run.sh          前台跑（Ctrl-C 退出，日志直接看）
#   ./run.sh bg       后台跑，日志进 wac.log

BASE=/root/workdayAlarmClock
DIR="$BASE/lvgl"
LOG="$DIR/wac.log"

kill_all() {
    for pat in timeDisplay.sh pwrKey.sh volKey.sh workdayAlarmClock-linux-arm; do
        ps | grep "$pat" | grep -v grep | awk '{print $1}' | while read -r p; do
            kill "$p" 2>/dev/null
        done
    done
    sleep 1
    # 还赖着就强杀
    ps | grep -E "timeDisplay.sh|pwrKey.sh|volKey.sh|workdayAlarmClock-linux-arm" |
        grep -v grep | awk '{print $1}' | while read -r p; do
        kill -9 "$p" 2>/dev/null
    done
    sleep 1
}

case "$1" in
    stop)
        kill_all
        echo "stopped"
        exit 0
        ;;
esac

kill_all

# Go 依赖 cwd 找 workdayAlarmClock.json / weather.mp3，所以 LVGL 也在它的目录里跑；
# 我们自己的配置和字体用环境变量指到 lvgl/ 下面。
cd "$BASE" || exit 1
export WAC_GO_BIN="$BASE/workdayAlarmClock-linux-arm"
export WAC_CJK_TTF="$DIR/assets/fonts/wac_cjk.ttf"
export WAC_CLOCK_TTF="$DIR/assets/fonts/zyymeclock.ttf"
export WAC_UI_CONF="$DIR/wac_ui.conf"
export WAC_KEYS_CONF="$DIR/wac_keys.conf"

if [ "$1" = "bg" ]; then
    "$DIR/wac" >>"$LOG" 2>&1 &
    echo $! >"$DIR/wac.pid"
    echo "started pid $(cat "$DIR/wac.pid"), log: $LOG"
else
    exec "$DIR/wac"
fi
