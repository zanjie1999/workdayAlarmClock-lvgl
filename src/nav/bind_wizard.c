/**
 * bind_wizard.c -- 按键绑定向导
 *
 * 四步：确定 → 下一个 → 上一个 → 返回，每步都可以按「确定」跳过。
 * 顺序按「确定→下→上→返回」：只绑到前 2 个时，正好是「确定 + 下」2 键模式。
 * 界面上实时显示收到的 (设备名 / 键码)，接线不对时能马上看出来。
 */
#include "bind_wizard.h"
#include "indev_keys.h"
#include "theme.h"
#include "ui_font.h"

#define CONFIG_PATH "wac_keys.conf"

typedef struct {
    key_role_t  role;
    const char *title;
    const char *hint;
} step_t;

static const step_t STEPS[] = {
    {ROLE_CONFIRM, "按一下「确定」键", NULL},
    {ROLE_NEXT,    "按「下一个」键",   "没有这个键就按确定"},
    {ROLE_PREV,    "按「上一个」键",   "没有这个键就按确定"},
    {ROLE_BACK,    "按「返回」键",     "没有这个键就按确定"},
};
#define N_STEPS ((int)(sizeof(STEPS) / sizeof(STEPS[0])))

static lv_obj_t *s_root;
static lv_obj_t *s_step_lbl;
static lv_obj_t *s_title;
static lv_obj_t *s_hint;
static lv_obj_t *s_dots;
static lv_obj_t *s_dbg;

static int      s_step;
static int      s_confirm_code = -1;
static bool     s_active;
static uint32_t s_done_at;
static uint32_t s_start_at;

bool wizard_active(void) { return s_active; }

static void refresh_ui(void)
{
    char buf[64];

    if (s_step < N_STEPS) {
        snprintf(buf, sizeof(buf), "按键绑定 %d/%d", s_step + 1, N_STEPS);
        lv_label_set_text(s_step_lbl, buf);
        lv_label_set_text(s_title, STEPS[s_step].title);
        lv_label_set_text(s_hint, STEPS[s_step].hint ? STEPS[s_step].hint : "任意键都可以");
    }

    char dots[16] = {0};
    for (int i = 0; i < N_STEPS; i++) {
        strcat(dots, i < s_step ? "●" : "○");
    }
    lv_label_set_text(s_dots, dots);

    snprintf(buf, sizeof(buf), "%s / %d", keys_last_device(), keys_last_code());
    lv_label_set_text(s_dbg, buf);
}

static void wizard_close(void)
{
    if (!s_active) return;

    if (s_confirm_code >= 0) {
        keys_save_config(CONFIG_PATH);
        WAC_LOG("wizard done: %d keys", keys_bound_count());
    }
    else {
        WAC_LOG("wizard cancelled at step %d", s_step);
    }

    keys_set_raw_callback(NULL);
    if (s_root) lv_obj_delete(s_root);
    s_root   = NULL;
    s_active = false;
}

static void finish(void)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "已识别：%d 键模式", keys_bound_count());
    lv_label_set_text(s_step_lbl, "完成");
    lv_label_set_text(s_title, buf);
    lv_label_set_text(s_hint, "");
    lv_label_set_text(s_dots, "");
    s_step    = N_STEPS;
    s_done_at = wac_millis() + 2000;
}

static void on_raw(int code, bool pressed)
{
    if (!pressed) return;
    if (s_step >= N_STEPS) return;

    if (s_step == 0) {
        s_confirm_code = code;
        keys_clear();
        keys_bind(ROLE_CONFIRM, code);
        s_step++;
        if (s_step >= N_STEPS) finish();
        else refresh_ui();
        return;
    }

    /* 其余步骤：按到「确定」那个键 = 跳过（严格照需求） */
    if (code == s_confirm_code) {
        s_step++;
    }
    else {
        keys_bind(STEPS[s_step].role, code);
        s_step++;
    }

    if (s_step >= N_STEPS) finish();
    else refresh_ui();
}

static void wizard_timer(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!s_active) return;

    if (s_done_at && wac_millis() >= s_done_at) {
        wizard_close();
        return;
    }

    /* 第 1 步 60s 没动静：有触摸设备就先让用户用触摸，别卡死 */
    if (s_step == 0 && (wac_millis() - s_start_at) > 60000) {
        if (lv_indev_get_next(NULL) != NULL) {
            WAC_LOG("wizard: no key in 60s, fall back to touch-only");
            wizard_close();
        }
    }
}

void wizard_start(void)
{
    if (s_active) return;

    s_step         = 0;
    s_confirm_code = -1;
    s_done_at      = 0;
    s_start_at     = wac_millis();
    s_active       = true;

    lv_obj_t *top = lv_layer_top();

    s_root = lv_obj_create(top);
    lv_obj_remove_style_all(s_root);
    lv_obj_set_size(s_root, WAC_SCR_W, WAC_SCR_H);
    lv_obj_set_pos(s_root, 0, 0);
    lv_obj_set_style_bg_color(s_root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_root, LV_OBJ_FLAG_SCROLLABLE);

    const bool tiny = (g_tk.bp == WAC_BP_TINY);
    int32_t pad = tiny ? 6 : g_tk.pad_lg;

    s_step_lbl = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_step_lbl, ui_font_cjk(tiny ? 12 : 14), 0);
    lv_obj_set_style_text_color(s_step_lbl, COL_DIM, 0);
    lv_obj_align(s_step_lbl, LV_ALIGN_TOP_LEFT, pad, pad);

    s_dbg = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dbg, ui_font_cjk(12), 0);
    lv_obj_set_style_text_color(s_dbg, COL_DIMMER, 0);
    lv_obj_align(s_dbg, LV_ALIGN_TOP_RIGHT, -pad, pad);

    s_title = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_title, ui_font_cjk(16), 0);
    lv_obj_set_style_text_color(s_title, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_title, WAC_SCR_W - pad * 2);
    lv_obj_align(s_title, LV_ALIGN_CENTER, 0, 0);

    s_hint = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_hint, ui_font_cjk(tiny ? 12 : 14), 0);
    lv_obj_set_style_text_color(s_hint, COL_DIM, 0);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_hint, WAC_SCR_W - pad * 2);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, tiny ? 22 : 32);

    s_dots = lv_label_create(s_root);
    lv_obj_set_style_text_font(s_dots, ui_font_cjk(12), 0);
    lv_obj_set_style_text_color(s_dots, COL_DIM, 0);
    lv_obj_align(s_dots, LV_ALIGN_BOTTOM_MID, 0, -pad);

    keys_set_raw_callback(on_raw);
    lv_timer_create(wizard_timer, 100, NULL);

    refresh_ui();
    WAC_LOG("wizard started");
}

void wizard_force_start(void)
{
    wizard_close();
    wizard_start();
}
