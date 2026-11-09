/**
 * app_state.c -- 全局状态
 */
#include "wac.h"
#include "ui_settings.h"

wac_state_t g_state;

/* 播放位置推进。两条来源：
 *   1. Go（lvgl 模式）每秒发一条 POS <ms> —— 准，暂停/切歌都由它兜住；
 *      两次 POS 之间用"收到 POS 的时刻 + 经过的时间"插值，页面 250ms 刷新也顺滑。
 *   2. 没有 POS 的旧后端 / mock：退回 (now - start_unix) 推算。
 */
void wac_state_tick(void)
{
    if (g_state.state != WAC_ST_PLAYING) return;

    uint32_t ms;
    if (g_state.pos_valid) {
        ms = g_state.pos_base_ms + (wac_millis() - g_state.pos_at_ms);
    }
    else if (g_state.start_unix > 0) {
        int64_t d = ((int64_t)time(NULL) - g_state.start_unix) * 1000;
        ms        = d > 0 ? (uint32_t)d : 0;
    }
    else {
        return;
    }

    if (g_state.duration_ms > 0 && ms > (uint32_t)g_state.duration_ms) {
        ms = (uint32_t)g_state.duration_ms;
    }
    g_state.progress_ms = ms;
}

static void state_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    wac_state_tick();
    ui_set_flush_volume(); /* 音量缓存防抖落盘，见 ui_settings.c */
}

void wac_state_init(void)
{
    memset(&g_state, 0, sizeof(g_state));
    g_state.state        = WAC_ST_STOP;
    g_state.volume       = 50;
    g_state.volume_known = false;
    g_state.go_port      = 8080;
    snprintf(g_state.title, sizeof(g_state.title), "%s", WAC_DEF_TITLE);

    lv_timer_create(state_timer_cb, 250, NULL);
}

/* ------------------------------------------------------------ 底部提示行 */

void wac_hint_set(wac_hint_kind_t kind, const char *text, uint32_t ttl_ms)
{
    if (!text) text = "";

    g_state.hint_kind = (uint8_t)kind;
    snprintf(g_state.hint, sizeof(g_state.hint), "%s", text);
    g_state.hint_until = ttl_ms ? (wac_millis() + ttl_ms) : 0;

    WAC_LOG("hint: kind=%d ttl=%u text='%s'", (int)kind, (unsigned)ttl_ms, g_state.hint);
}

void wac_hint_clear(wac_hint_kind_t kind)
{
    if (kind != WAC_HINT_NONE && (wac_hint_kind_t)g_state.hint_kind != kind) return;

    if (g_state.hint_kind != WAC_HINT_NONE) WAC_LOG("hint: cleared");
    g_state.hint_kind  = WAC_HINT_NONE;
    g_state.hint[0]    = '\0';
    g_state.hint_until = 0;
}

bool wac_hint_visible(void)
{
    if (g_state.hint_kind == WAC_HINT_NONE || !g_state.hint[0]) return false;

    /* 播放/暂停时底部是迷你条，提示行没有位置 */
    if (g_state.state != WAC_ST_STOP) return false;

    if (g_state.hint_until && wac_millis() >= g_state.hint_until) {
        wac_hint_clear(WAC_HINT_NONE);
        return false;
    }
    return true;
}
