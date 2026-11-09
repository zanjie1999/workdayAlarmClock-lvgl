/**
 * ui_alarm.c -- 响铃态：回时钟页 + 整屏"点一下就是停"
 */
#include "ui_alarm.h"
#include "page.h"
#include "cmd.h"
#include "screen.h"

static lv_obj_t *s_ov;

static void ov_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    ui_alarm_stop_by_user();
}

void ui_alarm_init(void)
{
    s_ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ov);
    lv_obj_set_size(s_ov, WAC_SCR_W, WAC_SCR_H);
    lv_obj_set_pos(s_ov, 0, 0);
    lv_obj_set_style_bg_opa(s_ov, LV_OPA_TRANSP, 0); /* 只吃事件，不挡画面 */
    lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ov, ov_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
}

void ui_alarm_begin(void)
{
    g_state.ringing    = true;
    g_state.start_unix = (int64_t)time(NULL);

    screen_on();

    if (s_ov) {
        lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ov);
    }
    /* 闹钟一响就回时钟页：用户要的是"看见时间 + 随手按停" */
    nav_home();

    WAC_LOG("alarm: begin (page=clock, touch armed)");
}

void ui_alarm_end(void)
{
    const bool was = g_state.ringing;

    g_state.ringing = false;
    if (s_ov) lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);

    /* 只在"确实在响"的时候才跳页：ALARMSTOP 可能在任何时候单独到达 */
    if (was) nav_home();

    WAC_LOG("alarm: end");
}

void ui_alarm_stop_by_user(void)
{
    if (!g_state.ringing) {
        if (s_ov) lv_obj_add_flag(s_ov, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    WAC_LOG("alarm: user stop");
    cmd_stop(); /* Go 侧 player.Stop()，响铃和播放都吃这一条 */
    ui_alarm_end();
}

bool ui_alarm_active(void)
{
    return g_state.ringing;
}
