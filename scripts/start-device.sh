#!/bin/sh
# 开机启动：先清掉老的显示/按键 helper 和 Go，再拉起 LVGL 外壳。
#
# 这份是版本化副本，设备上的位置是 <应用目录>/start.sh：
#   aku（ARM）  : /root/workdayAlarmClock/start.sh
#   rns（MIPS） : /usr/data/workdayAlarmClock/start.sh
# 两边内容一模一样 —— 应用目录由脚本自己所在位置推出来，不用改路径。
#
# 开机自启靠 /etc/init.d/S99workdayAlarmClock 调这里的 $1=start。
#
# 注意：LVGL 外壳自己会 fork Go（argv 是 lvgl），所以这里不要再单独起 Go，
# 否则两个进程会抢 /dev/fb0 和音频。

BASE=$(cd "$(dirname "$0")" && pwd) || exit 1
DIR="$BASE/lvgl"
LOG="$DIR/wac.log"

# 上电时这些可能已经在跑，必须全停：timeDisplay.sh 直接往 /dev/fb0 画，
# pwrKey.sh / volKey.sh / playKey.sh 用 dd 阻塞读 /dev/input/event*，
# 老 Go 会抢屏幕和音频，都会和 LVGL 打架。
PAT="timeDisplay.sh|pwrKey.sh|volKey.sh|playKey.sh|workdayAlarmClock-linux"

kill_pat() {
    sig="$1"
    # 排除 awk / grep 自己那一行：它们的命令行里就带着 PAT，不排会把自己杀掉
    ps | awk -v pat="$PAT" 'NR > 1 && $0 ~ pat && $0 !~ /awk|grep/ {print $1}' |
        while read -r p; do
            [ "$p" = "$$" ] && continue
            if [ -n "$sig" ]; then
                kill "-$sig" "$p" 2>/dev/null
            else
                kill "$p" 2>/dev/null
            fi
        done
}

# 手动重跑本脚本时，先收掉上一个 wac（它下面挂着 Go，一起走）
if [ -f "$DIR/wac.pid" ]; then
    kill "$(cat "$DIR/wac.pid")" 2>/dev/null
    sleep 1
    rm -f "$DIR/wac.pid"
fi

kill_pat ""
sleep 1
kill_pat -9

# 日志别无限长：超过 256KB 重开
if [ -f "$LOG" ] && [ "$(wc -c <"$LOG")" -gt 262144 ]; then
    : >"$LOG"
fi

# Go 依赖 cwd 找 workdayAlarmClock.json / weather.mp3，所以必须在它自己的目录里跑
cd "$BASE" || exit 1

# Go 可执行文件这块板叫什么名字不一样（arm / mipsle），按前缀自己找
for f in "$BASE"/workdayAlarmClock-linux-*; do
    case "$f" in
        *.old) continue ;;
    esac
    [ -x "$f" ] && WAC_GO_BIN="$f"
done
export WAC_GO_BIN

export WAC_CJK_TTF="$DIR/assets/fonts/wac_cjk.ttf"
export WAC_CLOCK_TTF="$DIR/assets/fonts/zyymeclock.ttf"
export WAC_UI_CONF="$DIR/wac_ui.conf"
export WAC_KEYS_CONF="$DIR/wac_keys.conf"

# 时区：板子把 CST-8 写在 /etc/TZ 里，busybox 的 date 认，但 musl 的
# localtime_r 不读那个文件（只认 TZ 环境变量或 /etc/localtime），
# 不设就会按 UTC 走，时钟整整差 8 小时。POSIX 写法：CST-8 表示 UTC+8。
export TZ="CST-8"

# 脱离 stdin：开机时没有 tty，LVGL 也不读 stdin
"$DIR/wac" </dev/null >>"$LOG" 2>&1 &
echo $! >"$DIR/wac.pid"
