/**
 * swipe.c -- 触摸滑动导航
 */
#include "swipe.h"
#include "keymap.h"
#include "focus.h"
#include "bind_wizard.h"

#define SWIPE_POLL_MS 20
#define CLICK_GUARD_MS 300 /* 滑动结束后这段时间内的点击一律忽略 */

static lv_indev_t *s_ind;
static lv_point_t  s_org;
static bool        s_down;
static bool        s_consumed; /* 本次按下已被滑动吃掉 */
static uint32_t    s_fire_tick;

bool swipe_blocks_click(void)
{
    if (s_consumed) return true;
    if (s_fire_tick && lv_tick_elaps(s_fire_tick) < CLICK_GUARD_MS) return true;
    return false;
}

static void note_swipe(void)
{
    s_consumed   = true;
    s_fire_tick  = lv_tick_get();
}

static int32_t swipe_threshold(void)
{
    /* 屏宽的 1/8，最小 10px：小屏灵敏，大屏不至于手抖就翻页 */
    int32_t th = WAC_MIN_DIM / 8;
    if (th < 10) th = 10;
    return th;
}

/* 按下点是否落在滑块（或其子对象）上：滑块的拖拽不能被翻页抢走 */
static bool pressing_slider(void)
{
    lv_obj_t *o = lv_indev_get_active_obj();
    while (o) {
        if (lv_obj_check_type(o, &lv_slider_class)) return true;
        o = lv_obj_get_parent(o);
    }
    return false;
}

static void poll_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!s_ind) return;

    bool pressed = (lv_indev_get_state(s_ind) == LV_INDEV_STATE_PRESSED);
    if (!pressed) {
        s_down     = false;
        s_consumed = false;
        return;
    }

    /* 响铃期间手指只管"停铃"，滑动翻页一律不做 */
    if (g_state.ringing) {
        s_down     = false;
        s_consumed = true;
        return;
    }

    lv_point_t p;
    lv_indev_get_point(s_ind, &p);

    if (!s_down) { /* 按下沿：记起点 */
        s_down      = true;
        s_consumed  = false;
        s_fire_tick = 0;
        s_org       = p;
        return;
    }
    if (s_consumed) return;
    if (lv_indev_get_scroll_obj(s_ind)) return; /* 正在滚列表，让给列表 */
    if (pressing_slider()) return;              /* 手指按在滑块上，拖拽归滑块 */
    if (wizard_active()) return;

    int32_t dx = p.x - s_org.x;
    int32_t dy = p.y - s_org.y;
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t ay = dy < 0 ? -dy : dy;
    int32_t th = swipe_threshold();

    if (ax >= th && ax > ay) {
        note_swipe();
        nav_page_dir(dx < 0 ? +1 : -1); /* 手指往左 → 下一页 */
    }
    else if (ay >= th && ay > ax) {
        note_swipe();
        focus_step(dy < 0 ? +1 : -1); /* 手指往上 → 下一个 */
    }
}

void swipe_init(lv_indev_t *indev)
{
    s_ind = indev;
    if (!s_ind) {
        WAC_LOG("swipe: no pointer indev, touch navigation off");
        return;
    }
    lv_timer_create(poll_cb, SWIPE_POLL_MS, NULL);
    WAC_LOG("swipe: on (threshold %dpx)", (int)swipe_threshold());
}
