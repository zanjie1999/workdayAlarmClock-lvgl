/**
 * event_parse.c -- Go 的 stdout 单行事件
 */
#include "event_parse.h"

#include <stdlib.h>
#include "page.h"
#include "screen.h"
#include "ui_pause.h"
#include "ui_alarm.h"
#include "ui_settings.h"

static bool starts_with(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* 去掉行尾 \r\n */
static void chomp(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r')) s[--n] = '\0';
}

static void handle_play_event(void)
{
    g_state.state       = WAC_ST_PLAYING; /* 开播即转播放态，Go 只会发 PLAY 不发 RESUME */
    g_state.start_unix  = (int64_t)time(NULL);
    g_state.progress_ms = 0;
    g_state.pos_valid   = false; /* 等 Go 的下一条 POS */
}

void event_handle_line(const char *line)
{
    if (!line || !line[0]) return;

    char buf[512];
    snprintf(buf, sizeof(buf), "%s", line);
    chomp(buf);

    if (starts_with(buf, "ECHO ")) {
        snprintf(g_state.title, sizeof(g_state.title), "%s", buf + 5);
        WAC_LOG("echo: %s", g_state.title);
        /* 时钟页底部也显示一份，10 秒后自己消失（停止播放时才可见） */
        wac_hint_set(WAC_HINT_ECHO, buf + 5, 10000);
    }
    else if (strcmp(buf, "STOP") == 0) {
        g_state.state       = WAC_ST_STOP;
        g_state.progress_ms = 0;
        g_state.pos_valid   = false;
    }
    else if (strcmp(buf, "PAUSE") == 0) {
        g_state.state = WAC_ST_PAUSED;
    }
    else if (strcmp(buf, "RESUME") == 0) {
        g_state.state = WAC_ST_PLAYING;
    }
    else if (starts_with(buf, "VOL ")) {
        int v = atoi(buf + 4);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_state.volume       = (uint8_t)v;
        g_state.volume_known = true;
        ui_set_note_volume(v);
    }
    else if (strcmp(buf, "VOLP") == 0) {
        if (g_state.volume < 100) g_state.volume++;
        g_state.volume_known = true;
        ui_set_note_volume(g_state.volume);
    }
    else if (strcmp(buf, "VOLM") == 0) {
        if (g_state.volume > 0) g_state.volume--;
        g_state.volume_known = true;
        ui_set_note_volume(g_state.volume);
    }
    else if (strcmp(buf, "ALARM") == 0 || starts_with(buf, "ALARM ")) {
        /* 闹铃开始：回时钟页 + 整屏接管（任意键/任意触摸都算停）。
           带不带文字都一样 —— 底部常驻文字现在走 WEATHERAL。 */
        ui_alarm_begin();
    }
    else if (strcmp(buf, "ALARMSTOP") == 0) {
        ui_alarm_end();
    }
    else if (strcmp(buf, "ALARMON") == 0) {
        /* 闹钟开启（非响铃），仅状态 */
    }
    else if (strcmp(buf, "ALARMOFF") == 0) {
        ui_alarm_end();
    }
    else if (starts_with(buf, "WEATHERAL")) {
        /* 天气预警：WEATHERAL <文字> 常驻时钟页底部，空 WEATHERAL 清掉。
           Go 每次刷新天气都会发一条，没有预警时发的是空串，正好当作"清除"。
           注意不能写进 g_state.weather —— 那是顶部天气行的内容，
           预警文字长得多，会把整行挤爆。 */
        const char *t = buf + 9;
        while (*t == ' ') t++;
        if (*t) wac_hint_set(WAC_HINT_ALERT, t, 0);
        else wac_hint_clear(WAC_HINT_ALERT);
    }
    else if (starts_with(buf, "WEATHER ")) {
        snprintf(g_state.weather, sizeof(g_state.weather), "%s", buf + 8);
        g_state.weather_ok = true;
    }
    else if (strcmp(buf, "WAKELOCK") == 0) {
        g_state.wakelock = true;
    }
    else if (strcmp(buf, "HASUPDATE") == 0) {
        g_state.has_update = true;
    }
    else if (strcmp(buf, "RESTART") == 0) {
        WAC_LOG("go asks for restart");
        /* 子进程会在 go_proc 里被重启（退出后 1s 重拉） */
    }
    else if (strcmp(buf, "SCREENON") == 0) {
        screen_on();
    }
    else if (strcmp(buf, "SCREENOFF") == 0) {
        screen_off();
    }
    /* Go 的 /fb 投屏长连接：整屏交给它，LVGL 一帧都别再写 */
    else if (strcmp(buf, "FBSTART") == 0) {
        WAC_LOG("go: fb stream start -> lvgl output paused");
        ui_pause_set(true);
    }
    else if (strcmp(buf, "FBEND") == 0) {
        WAC_LOG("go: fb stream end -> lvgl output resumed");
        ui_pause_set(false);
    }
    else if (starts_with(buf, "DUR ")) {
        g_state.duration_ms = atoi(buf + 4);
    }
    else if (starts_with(buf, "POS ")) {
        /* lvgl 模式专属：Go 每秒报一次当前位置（毫秒） */
        int v = atoi(buf + 4);
        if (v < 0) v = 0;
        g_state.pos_base_ms = (uint32_t)v;
        g_state.pos_at_ms   = wac_millis();
        g_state.progress_ms = (uint32_t)v;
        g_state.pos_valid   = true;
    }
    else if (starts_with(buf, "PLAY ")) {
        snprintf(g_state.now_url, sizeof(g_state.now_url), "%s", buf + 5);
        handle_play_event();
    }
    else if (starts_with(buf, "SONGID ")) {
        snprintf(g_state.now_id, sizeof(g_state.now_id), "%s", buf + 7);
    }
    else {
        WAC_LOG_DEBUG("go: %s", buf);
    }
}
