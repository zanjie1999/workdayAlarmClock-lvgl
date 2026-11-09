/**
 * cmd.c -- 命令下发
 *
 * 走 Go 的 stdin（一行一条）。注意 Go 端的防连按逻辑：
 * stdin 循环里若 reader 还有缓冲（Buffered() > 0）会直接 Discard 后续命令，
 * 所以 UI 侧必须做乐观更新，不要指望每条命令都生效，也不要重发排队。
 */
#include "wac.h"
#include "cmd.h"
#include "ui_settings.h"

#include <stdarg.h>
#include <stdlib.h>

/* 由 go_proc.c（真机）或 mock_go.c（模拟器）实现 */
void wac_go_write(const char *line);

void wac_cmd(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    wac_go_write(buf);
}

/* 便捷封装 */
void cmd_play_pause(void);
void cmd_next(void);
void cmd_prev(void);
void cmd_stop(void);
void cmd_vol(int delta);
void cmd_one_key(void);

void cmd_play_pause(void)
{
    if (g_state.state == WAC_ST_PLAYING) {
        wac_cmd("pause");
        g_state.state = WAC_ST_PAUSED; /* 乐观更新 */
    }
    else if (g_state.state == WAC_ST_PAUSED) {
        wac_cmd("resume");
        g_state.state = WAC_ST_PLAYING;
    }
    else {
        wac_cmd("play");
        g_state.state = WAC_ST_PLAYING;
    }
}

void cmd_next(void) { wac_cmd("next"); }
void cmd_prev(void) { wac_cmd("prev"); }
void cmd_stop(void)
{
    wac_cmd("stop");
    g_state.state = WAC_ST_STOP;
}

void cmd_vol(int delta)
{
    int v = (int)g_state.volume + delta;
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    g_state.volume       = (uint8_t)v;
    g_state.volume_known = true;
    ui_set_note_volume(v); /* 记进 wac_ui.conf，下次开机第一屏就有数 */
    wac_cmd("vol %d", v);
}

/* 反转开关只翻"方向"，不改步长；dir==0 直接忽略 */
void cmd_vol_step(int dir)
{
    if (dir == 0) return;
    int s = (dir > 0) ? +CMD_VOL_STEP : -CMD_VOL_STEP;
    if (g_set.vol_reverse) s = -s;
    cmd_vol(s);
}

void cmd_one_key(void) { wac_cmd("1key"); }

/*
 * 关机 / 重启
 *
 * 不走 Go：Go 那边没有 poweroff / reboot 这两条 stdin 命令，而且关机本来就不该
 * 看播放器的脸色。直接在本进程 fork 一个 sh 去执行，命令失败也不会把外壳搞崩。
 *
 * 命令末尾的 `&` 是必须的：成功时整个进程（含我们）会被带走，
 * 但如果这台机器上命令不存在或者没权限，我们要能立刻拿回控制权，
 * 好让界面把"正在关机"撤掉，不至于卡死在一块黑屏上。
 *
 * 默认命令按 busybox 常见布局挨个试；换了板子可以用环境变量覆盖
 * （比如 WAC_REBOOT_CMD="echo 1 > /sys/.../reboot"），不用重新编译。
 */
static int run_bg(const char *env_name, const char *def_cmd)
{
    const char *env = getenv(env_name);
    char        buf[256];

    if (env && *env) {
        snprintf(buf, sizeof(buf), "( sync; %s ) >/dev/null 2>&1 &", env);
    }
    else {
        snprintf(buf, sizeof(buf), "( sync; %s ) >/dev/null 2>&1 &", def_cmd);
    }

    WAC_LOG("%s: %s", env_name, buf);
    return system(buf);
}

int cmd_power_off(void)
{
    return run_bg("WAC_POWEROFF_CMD",
                  "poweroff || /sbin/poweroff || busybox poweroff || halt -p");
}

int cmd_reboot(void)
{
    return run_bg("WAC_REBOOT_CMD",
                  "reboot || /sbin/reboot || busybox reboot || reboot -f");
}
