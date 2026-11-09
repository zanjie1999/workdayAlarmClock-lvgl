/**
 * mock_go.c -- 模拟器下的假 Go 后端
 *
 * 目标：整个 UI 能在 Windows 上做完，不用等板子、不用真跑 Go。
 * 它响应和真实 Go 一样的一套命令，并把状态变化回灌进 g_state。
 * 启动即进入播放态（这样时钟页底部迷你条直接可见），时长未知 → 进度条走不确定态。
 */
#include "wac.h"
#include "go_proc.h"

#ifdef WAC_SIM

static const char *TITLES[] = {
    "晴天 - 周杰伦",
    "夜空中最亮的星 - 逃跑计划",
    "Happy - Pharrell Williams",
};
static int s_song;

/* 模拟"新的一曲"：位置归零，总时长假一个 3:30（真机上是 Go 解析 mp3 头得到的） */
static void mock_pos_reset(void)
{
    g_state.start_unix  = (int64_t)time(NULL);
    g_state.progress_ms = 0;
    g_state.pos_base_ms = 0;
    g_state.pos_at_ms   = wac_millis();
    g_state.pos_valid   = true;
    g_state.duration_ms = 210000;
}

static void set_status_line(void)
{
    WAC_LOG("mock status: %s / %s / vol=%d",
            g_state.state == WAC_ST_PLAYING ? "playing"
                                            : (g_state.state == WAC_ST_PAUSED ? "paused" : "stopped"),
            g_state.title, g_state.volume);
}

void wac_go_write(const char *line)
{
    WAC_LOG("cmd> %s", line);

    if (strcmp(line, "play") == 0) {
        g_state.state = WAC_ST_PLAYING;
        mock_pos_reset();
    }
    else if (strcmp(line, "pause") == 0) {
        g_state.state = WAC_ST_PAUSED;
    }
    else if (strcmp(line, "resume") == 0) {
        g_state.state = WAC_ST_PLAYING;
    }
    else if (strcmp(line, "stop") == 0) {
        g_state.state = WAC_ST_STOP;
        g_state.progress_ms = 0;
        g_state.pos_valid   = false;
        g_state.duration_ms = 0;
    }
    else if (strcmp(line, "next") == 0 || strcmp(line, "prev") == 0) {
        s_song = (s_song + 1) % 3;
        snprintf(g_state.title, sizeof(g_state.title), "%s", TITLES[s_song]);
        g_state.state = WAC_ST_PLAYING;
        mock_pos_reset();
        WAC_LOG("mock: ECHO %s", g_state.title);
    }
    else if (strncmp(line, "vol ", 4) == 0) {
        int v = atoi(line + 4);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        g_state.volume = (uint8_t)v;
        g_state.volume_known = true;
    }
    else if (strcmp(line, "1key") == 0) {
        WAC_LOG("mock: 1key (一键听歌)");
    }
    set_status_line();
}

/* 模拟 lvgl 后端：每秒把位置推进一秒，等价于 Go 发来的 POS */
static void mock_timer(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (g_state.state != WAC_ST_PLAYING) return;

    uint32_t p = g_state.progress_ms + 1000;
    if (g_state.duration_ms > 0 && p > (uint32_t)g_state.duration_ms) {
        p = (uint32_t)g_state.duration_ms;
    }
    g_state.pos_base_ms = p;
    g_state.pos_at_ms   = wac_millis();
    g_state.progress_ms = p;
}

/* 模拟器没有真实管道：进度由 mock_timer 驱动，这里什么都不用做。
   真机版见 go_proc.c（那边要读 stdout，投屏时也必须继续跑）。 */
void wac_go_poll(void) {}

void mock_go_init(void)
{
    snprintf(g_state.weather, sizeof(g_state.weather), "%s", "晴 22℃");
    g_state.weather_ok   = true;
    g_state.volume       = 42;
    g_state.volume_known = true;
    g_state.go_alive     = true;
    g_state.go_pid       = 1234;

    g_state.state = WAC_ST_PLAYING;
    snprintf(g_state.title, sizeof(g_state.title), "%s", TITLES[0]);
    mock_pos_reset();

    lv_timer_create(mock_timer, 1000, NULL);
    WAC_LOG("mock backend ready: 启动即播放，Enter/Up/Down/Esc = 确认/上/下/返回");
}

#endif /* WAC_SIM */
