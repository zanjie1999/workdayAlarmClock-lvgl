/**
 * ui_pause.c -- 暂停 / 恢复整个 LVGL 输出（见 ui_pause.h）
 */
#include "ui_pause.h"

static bool s_paused;

void ui_pause_set(bool paused)
{
    if (paused == s_paused) return;
    s_paused = paused;

    if (paused) {
        /* 定时器一停，lv_timer_handler 就不再有活干：不采样、不渲染、不写 fb0 */
        lv_timer_enable(false);
        WAC_LOG("ui: paused -- external writer owns the screen");
    }
    else {
        lv_timer_enable(true);

        /* 先把整屏标脏再立刻刷一帧。
           不 invalidate 的话只有"脏过"的控件会重画，屏幕上会残留 Go 的画面；
           不 refr_now 的话要等到下一个 timer（最长 1 秒）才出画面。 */
        lv_obj_invalidate(lv_screen_active());
        lv_refr_now(NULL);
        WAC_LOG("ui: resumed");
    }
}

bool ui_pause_active(void) { return s_paused; }
