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

# ---------------------------------------------------------------- 对时
# 这块板没有 RTC（hwclock 读 /dev/misc/rtc 直接报不存在），每次上电系统时间
# 都是 1970-01-01。原先的校时是"小度"干的（duer_linux 里有 NTPReadyEv /
# ntp_successful 字样），它被停掉之后整机就再没有东西校时了 —— 所以自己补上。
#
# 策略：等网卡拿到地址 → 没对上时每 15 秒重试（断网也能自愈）→ 对上以后
# 每 6 小时再校一次。失败只在成功时写日志，断网一晚上不会把日志刷爆。
NTP_SERVERS="203.107.6.88 120.25.115.20 ntp.aliyun.com cn.pool.ntp.org"
NTP_EVERY=21600
NTP_RETRY=15
NTP_RETRY_SLOW=300

ntp_once() {
    for s in $NTP_SERVERS; do
        if ntpdate -b -t 3 "$s" >/dev/null 2>&1; then
            echo "[time] ntp ok: $s -> $(date '+%Y-%m-%d %H:%M:%S')"
            return 0
        fi
    done
    return 1
}

time_sync_loop() {
    echo "[time] start, wait for network"

    # 本脚本是 S99 拉起来的，网卡可能还没拿到地址，先等（最多 3 分钟）
    n=0
    while [ "$n" -lt 36 ]; do
        ifconfig wlan0 2>/dev/null | grep -q "inet addr" && break
        n=$((n + 1))
        sleep 5
    done

    synced=0
    fail=0
    while :; do
        # 已经对上过：先睡 6 小时再校准；还没对上就立刻重试
        [ "$synced" = 1 ] && sleep "$NTP_EVERY"

        if ntp_once; then
            synced=1
            fail=0
        else
            synced=0
            fail=$((fail + 1))
            # 头 40 次（约 10 分钟）勤快点，之后降到 5 分钟一次，别空转
            if [ "$fail" -lt 40 ]; then
                sleep "$NTP_RETRY"
            else
                sleep "$NTP_RETRY_SLOW"
            fi
        fi
    done
}

# 本脚本被自己以 __time_sync 调起来时，只跑对时循环
case "$1" in
    __time_sync)
        time_sync_loop
        exit 0
        ;;
esac

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

# 对时循环：后台跑，不挡界面（网卡可能要几十秒才拿到地址）。
# 用 pid 文件防重复：手工重跑 start.sh 时别再起第二个。
TIME_LOG="$DIR/time.log"
if [ -f "$TIME_LOG" ] && [ "$(wc -c <"$TIME_LOG")" -gt 32768 ]; then
    : >"$TIME_LOG"
fi
if command -v ntpdate >/dev/null 2>&1; then
    if [ -f "$DIR/time.pid" ] && kill -0 "$(cat "$DIR/time.pid")" 2>/dev/null; then
        :
    else
        if command -v setsid >/dev/null 2>&1; then
            setsid sh "$0" __time_sync </dev/null >>"$TIME_LOG" 2>&1 &
        else
            nohup sh "$0" __time_sync </dev/null >>"$TIME_LOG" 2>&1 &
        fi
        echo $! >"$DIR/time.pid"
    fi
fi

# 必须彻底脱离终端会话：
#   开机时是 init 拉起来的，本来就没有 tty；但手工 `adb shell sh start.sh` 时，
#   wac 和 adb 的那个 pty 在同一个进程组，adb 断开 -> pty 关闭 -> SIGHUP 全组，
#   wac 会静默死掉（日志里一个字都没有，排查过一次，别再踩）。
#   setsid 让它单开一个会话，从此和终端无关；没有 setsid 就退回 nohup。
if command -v setsid >/dev/null 2>&1; then
    setsid "$DIR/wac" </dev/null >>"$LOG" 2>&1 &
else
    nohup "$DIR/wac" </dev/null >>"$LOG" 2>&1 &
fi
echo $! >"$DIR/wac.pid"
