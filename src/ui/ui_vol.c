/**
 * ui_vol.c -- 音量浮层
 */
#include "ui_vol.h"
#include "theme.h"
#include "ui_font.h"
#include "page.h"

static lv_obj_t *s_box;
static lv_obj_t *s_lbl;
static lv_obj_t *s_bar;
static uint32_t  s_hide_at;

void ui_vol_hide(void)
{
    if (s_box) lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    s_hide_at = 0;
}

void ui_vol_poke(int vol)
{
    if (!s_box) return;
    /* 播放页本来就有常驻音量条，再弹浮层只会正好压住它 */
    if (page_current() == WAC_PAGE_PLAYER) {
        ui_vol_hide();
        return;
    }
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    char buf[24];
    snprintf(buf, sizeof(buf), "音量 %d", vol);
    lv_label_set_text(s_lbl, buf);
    lv_bar_set_value(s_bar, vol, LV_ANIM_OFF);

    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_box);
    s_hide_at = wac_millis() + 1500;
}

static void vol_timer(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (s_hide_at && wac_millis() >= s_hide_at) ui_vol_hide();
}

void ui_vol_init(void)
{
    lv_obj_t *top = lv_layer_top();
    lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);

    int32_t w = WAC_SCR_W - tk_px(24);
    if (w > 200) w = 200;
    int32_t h = tk_px(44);

    s_box = lv_obj_create(top);
    lv_obj_remove_style_all(s_box);
    lv_obj_set_size(s_box, w, h);
    lv_obj_align(s_box, LV_ALIGN_CENTER, 0, tk_px(20));
    lv_obj_set_style_bg_color(s_box, COL_CARD, 0);
    lv_obj_set_style_bg_opa(s_box, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_box, tk_px(6), 0);
    lv_obj_set_style_pad_all(s_box, tk_px(6), 0);
    lv_obj_set_flex_flow(s_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_box, tk_px(3), 0);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_box, LV_OBJ_FLAG_CLICKABLE);

    s_lbl = lv_label_create(s_box);
    lv_obj_set_style_text_font(s_lbl, ui_font_cjk(g_tk.bp == WAC_BP_TINY ? 12 : 16), 0);
    lv_obj_set_style_text_color(s_lbl, COL_TEXT, 0);
    lv_label_set_text(s_lbl, "音量 50");

    s_bar = lv_bar_create(s_box);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, w - tk_px(12), tk_px(3));
    lv_obj_set_style_bg_color(s_bar, COL_DIMMER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(s_bar, 0, 100);
    lv_bar_set_value(s_bar, 50, LV_ANIM_OFF);

    lv_obj_add_flag(s_box, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(vol_timer, 200, NULL);
}
