#!/bin/sh
# 开机启动：先清掉老的显示/按键 helper，再拉起 LVGL 外壳。
#
# 这份是版本化副本，设备上的位置是 /root/workdayAlarmClock/start.sh：
#   adb push scripts/start-device.sh /root/workdayAlarmClock/start.sh
#   adb shell "chmod 755 /root/workdayAlarmClock/start.sh"
# 开机自启靠 /etc/rc.local 里的 `/root/workdayAlarmClock/start.sh &` 一行。
#
# 注意：LVGL 外壳自己会 fork workdayAlarmClock-linux-arm（argv 是 lvgl），
# 所以这里不要再单独起 Go，否则两个进程会抢 /dev/fb0。

BASE=/root/workdayAlarmClock
DIR="$BASE/lvgl"
LOG="$DIR/wac.log"

# 上电时这些可能已经在跑，必须全停：timeDisplay.sh 直接往 /dev/fb0 画，
# pwrKey.sh / volKey.sh 用 dd 阻塞读 /dev/input/event*，都会和 LVGL 抢资源。
kill_all() {
    for pat in timeDisplay.sh pwrKey.sh volKey.sh workdayAlarmClock-linux-arm; do
        ps | grep "$pat" | grep -v grep | awk '{print $1}' | while read -r p; do
            kill "$p" 2>/dev/null
        done
    done
    sleep 1
    ps | grep -E "timeDisplay.sh|pwrKey.sh|volKey.sh|workdayAlarmClock-linux-arm" |
        grep -v grep | awk '{print $1}' | while read -r p; do
        kill -9 "$p" 2>/dev/null
    done
}

# 手动重跑本脚本时，先收掉上一个 wac
if [ -f "$DIR/wac.pid" ]; then
    kill "$(cat "$DIR/wac.pid")" 2>/dev/null
    sleep 1
    rm -f "$DIR/wac.pid"
fi

kill_all

# 日志别无限长：超过 256KB 重开
if [ -f "$LOG" ] && [ "$(wc -c <"$LOG")" -gt 262144 ]; then
    : >"$LOG"
fi

# Go 依赖 cwd 找 workdayAlarmClock.json / weather.mp3，所以必须在它自己的目录里跑
cd "$BASE" || exit 1

export WAC_GO_BIN="$BASE/workdayAlarmClock-linux-arm"
export WAC_CJK_TTF="$DIR/assets/fonts/wac_cjk.ttf"
export WAC_CLOCK_TTF="$DIR/assets/fonts/zyymeclock.ttf"
export WAC_UI_CONF="$DIR/wac_ui.conf"
export WAC_KEYS_CONF="$DIR/wac_keys.conf"

# 时区：这块板把 CST-8 写在 /etc/TZ 里，busybox 的 date 认，但 musl 的
# localtime_r 不读那个文件（只认 TZ 环境变量或 /etc/localtime），
# 不设就会按 UTC 走，时钟整整差 8 小时。POSIX 写法：CST-8 表示 UTC+8。
export TZ="CST-8"

# 脱离 stdin：开机时没有 tty，LVGL 也不读 stdin
"$DIR/wac" </dev/null >>"$LOG" 2>&1 &
echo $! >"$DIR/wac.pid"
