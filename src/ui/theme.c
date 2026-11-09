/**
 * theme.c -- 令牌计算
 *
 * 三套已验证的骨架（见计划 §2.1/§2.2）：
 *   160×128 : 顶部 18 / 时间 42 / 天气 38 三行 + 底部迷你播放条
 *   320×240 : 16px 日期行 / 64px 时间 / 次行 / 迷你条
 *   大屏    : 单列居中，播放页两栏
 */
#include "theme.h"

wac_token_t g_tk;

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void theme_init(void)
{
    wac_token_t *t = &g_tk;
    memset(t, 0, sizeof(*t));

    t->w       = WAC_SCR_W;
    t->h       = WAC_SCR_H;
    t->min_dim = WAC_MIN_DIM;

    /* k = min_dim / 240，限制在 [53, 400]（即 0.53x .. 4x） */
    t->k = clamp_i32(t->min_dim * 100 / 240, 53, 400);

    t->pad       = tk_px(4);
    t->gap       = tk_px(8);
    t->pad_lg    = tk_px(16);
    t->touch_min = tk_px(44);
    if (t->touch_min < 24) t->touch_min = 24; /* 极小屏仍留一点可点面积 */

    if (t->min_dim <= 150) {
        t->bp        = WAC_BP_TINY;
        t->show_hint = false;
        t->status_h  = 18;
        t->clock_top = 18;
        t->clock_h   = 42;
        t->sub_top   = 60;
        t->mini_top  = 96;
    }
    else if (t->min_dim < 280) {
        t->bp        = WAC_BP_SMALL;
        t->show_hint = false;
        t->status_h  = 30;
        t->clock_top = 30;
        t->clock_h   = 120;
        t->sub_top   = 150;
        t->mini_top  = 192;
    }
    else {
        /* MEDIUM 及以上：纵向尺寸从屏高算，不能用 min_dim。
           1024×600 这种"宽而矮"的屏 k=2.5，用 k 缩放会让
           clock_top + clock_h 直接超出屏高，大字压到迷你条上。
           改为：迷你条高度按比例定 → 从下往上留天气行 → 剩下的给大字。 */
        t->bp        = (t->min_dim < 480) ? WAC_BP_MEDIUM
                     : (t->min_dim < 720) ? WAC_BP_LARGE
                                          : WAC_BP_XL;
        t->show_hint = true;

        const int32_t mini_h    = clamp_i32(t->h * 12 / 100, 36, 96);
        const int32_t weather_h = clamp_i32(t->h * 8 / 100, 22, 48);

        t->status_h  = clamp_i32(t->h * 7 / 100, 24, 56);
        t->clock_top = t->status_h;
        t->mini_top  = t->h - mini_h;

        t->clock_h = t->mini_top - weather_h - t->clock_top;
        if (t->clock_h < 40) t->clock_h = 40;
        t->sub_top = t->clock_top + t->clock_h;
    }

    WAC_LOG("theme: %dx%d min=%d k=%d bp=%d pad=%d gap=%d clock_top=%d clock_h=%d mini_top=%d",
            (int)t->w, (int)t->h, (int)t->min_dim, (int)t->k, (int)t->bp,
            (int)t->pad, (int)t->gap, (int)t->clock_top, (int)t->clock_h, (int)t->mini_top);
}

void theme_focus_style(lv_obj_t *obj)
{
    if (!obj) return;

    /* 焦点框画在对象「内部」（border），不再用 outline。
     * outline 画在盒子外面，会被父容器的裁剪区、屏幕四边吃掉一部分 ——
     * 128×160 这种贴边布局上尤其明显，四条边只剩一两条。
     *
     * 基础态先占一条 2px 的透明边框：border_width 参与布局计算，
     * 这样切焦点时内容区不会突然缩 2px，图标/文字不会跳。 */
    lv_obj_set_style_border_width(obj, THEME_FOCUS_BW, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);

    /* 可见的描边绑在 FOCUSED + THEME_ST_FOCUS_VIS 上（见 theme.h）：
     * 触摸操作时 focus.c 会把 VIS 位摘掉，框立即消失，而 border_width
     * 仍由基础态占着 2px，轨道/文字位置一动不动。 */
    lv_obj_set_style_border_width(obj, THEME_FOCUS_BW, LV_STATE_FOCUSED | THEME_ST_FOCUS_VIS);
    lv_obj_set_style_border_color(obj, COL_FOCUS, LV_STATE_FOCUSED | THEME_ST_FOCUS_VIS);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_FOCUSED | THEME_ST_FOCUS_VIS);

    /* 按下反馈：手指落下去立刻有一层薄蓝，触摸屏上不至于"点了没反应" */
    lv_obj_set_style_bg_color(obj, COL_FOCUS, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_20, LV_STATE_PRESSED);
}
