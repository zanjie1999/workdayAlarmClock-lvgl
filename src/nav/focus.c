/**
 * focus.c -- 焦点列表与视觉状态
 */
#include "focus.h"
#include "page.h"
#include "theme.h"

static focus_list_t *s_list;
static activate_fn   s_act;

/* 用户最后一次输入的性质：按键 → true，触摸 → false。
 * 实际画不画框还要看当前页（时钟页恒隐），真正生效的值见 vis_now()。 */
static bool s_vis_want = true;

/* 调节态：只对"当前焦点项"有效，换项 / 换页自动清掉 */
static focus_edit_fn s_edit;
static bool          s_editing;

static bool vis_now(void)
{
    /* 时钟页整块迷你条是唯一的焦点项，框起来很难看，这页永远不画 */
    if (page_current() == WAC_PAGE_CLOCK) return false;
    return s_vis_want;
}

static void vis_apply(void)
{
    if (!s_list) return;

    bool v = vis_now();
    for (int i = 0; i < s_list->n; i++) {
        lv_obj_t *o = s_list->items[i];
        if (!o) continue;
        if (v) lv_obj_add_state(o, THEME_ST_FOCUS_VIS);
        else   lv_obj_remove_state(o, THEME_ST_FOCUS_VIS);
    }
}

void focus_set_visible(bool v)
{
    s_vis_want = v;
    vis_apply(); /* 值没变也走一遍：换页后当前项是新的那批对象 */
}

bool focus_visible(void) { return vis_now(); }

static void edit_reset(void)
{
    if (s_editing && s_list && s_list->n > 0) {
        lv_obj_t *o = s_list->items[s_list->cur];
        if (o) lv_obj_remove_state(o, LV_STATE_EDITED);
    }
    s_editing = false;
    s_edit    = NULL;
}

void focus_list_init(focus_list_t *l)
{
    memset(l, 0, sizeof(*l));
    l->cur = 0;
}

void focus_list_add(focus_list_t *l, lv_obj_t *o)
{
    if (!o || l->n >= FOCUS_MAX_ITEMS) return;
    l->items[l->n++] = o;
}

static bool item_visible(lv_obj_t *o)
{
    return o && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN);
}

void focus_use(focus_list_t *l)
{
    edit_reset();

    if (s_list && s_list != l) {
        for (int i = 0; i < s_list->n; i++) {
            if (s_list->items[i]) lv_obj_remove_state(s_list->items[i], LV_STATE_FOCUSED);
        }
    }
    s_list = l;
    s_act  = NULL;
    vis_apply(); /* 新页的焦点项按当前可见性亮/灭 */

    if (s_list) {
        /* 找第一个可见项落焦 */
        for (int i = 0; i < s_list->n; i++) {
            if (item_visible(s_list->items[i])) {
                focus_set_index(i);
                return;
            }
        }
    }
}

focus_list_t *focus_active_list(void) { return s_list; }
int           focus_index(void) { return s_list ? s_list->cur : -1; }

lv_obj_t *focus_obj(void)
{
    if (!s_list || s_list->n == 0) return NULL;
    return s_list->items[s_list->cur];
}

void focus_set_index(int idx)
{
    if (!s_list || s_list->n == 0) return;
    if (idx < 0 || idx >= s_list->n) return;

    /* 换项就退出调节态：新的一项未必支持调节，也别让它继承旧的 handler */
    if (idx != s_list->cur) edit_reset();

    for (int i = 0; i < s_list->n; i++) {
        if (s_list->items[i]) lv_obj_remove_state(s_list->items[i], LV_STATE_FOCUSED);
    }
    s_list->cur = idx;
    lv_obj_t *o = s_list->items[idx];
    if (o) {
        lv_obj_add_state(o, LV_STATE_FOCUSED);
        lv_obj_scroll_to_view(o, LV_ANIM_OFF);
    }
    vis_apply(); /* VIS 位不给的话，触摸模式下这里只登记焦点、不亮框 */
}

/* 按对象落焦：触摸点到音量条这类非按钮控件时用 */
void focus_set_obj(lv_obj_t *o)
{
    if (!s_list || !o) return;
    for (int i = 0; i < s_list->n; i++) {
        if (s_list->items[i] == o) {
            focus_set_index(i);
            return;
        }
    }
}

void focus_move(int dir)
{
    if (!s_list || s_list->n == 0) return;

    int idx = s_list->cur;
    for (int step = 0; step < s_list->n; step++) {
        idx = (idx + dir + s_list->n) % s_list->n;
        if (item_visible(s_list->items[idx])) {
            focus_set_index(idx);
            return;
        }
    }
}

bool focus_step(int dir)
{
    if (!s_list || s_list->n == 0) return false;

    int vis = 0;
    for (int i = 0; i < s_list->n; i++) {
        if (item_visible(s_list->items[i])) vis++;
    }
    if (vis <= 1) return false; /* 只有一个可选项：这就是"到头的页" */

    for (int i = s_list->cur + dir; i >= 0 && i < s_list->n; i += dir) {
        if (item_visible(s_list->items[i])) {
            focus_set_index(i);
            return true;
        }
    }
    return false;
}

void focus_set_activate(activate_fn cb) { s_act = cb; }

/* ------------------------------------------------------------ 调节态 */
void focus_edit_support(focus_edit_fn cb) { s_edit = cb; }

bool focus_in_edit(void) { return s_editing; }

bool focus_enter_edit(void)
{
    if (!s_edit || s_editing) return false;
    s_editing = true;

    lv_obj_t *o = focus_obj();
    if (o) lv_obj_add_state(o, LV_STATE_EDITED);

    s_edit(0); /* 让页面刷一次"我在调节"的显示 */
    WAC_LOG_DEBUG("focus: enter edit (idx=%d)", focus_index());
    return true;
}

bool focus_exit_edit(void)
{
    if (!s_editing) return false;

    lv_obj_t *o = focus_obj();
    if (o) lv_obj_remove_state(o, LV_STATE_EDITED);

    focus_edit_fn cb = s_edit; /* 先留一份：页面可能顺手把 support 撤掉 */
    s_editing        = false;
    s_edit           = NULL;
    if (cb) cb(0);

    WAC_LOG_DEBUG("focus: exit edit (idx=%d)", focus_index());
    return true;
}

void focus_edit_step(int dir)
{
    if (!s_editing || !s_edit) return;
    s_edit(dir);
}

void focus_activate(void)
{
    /* 调节态里确认键 = 退出，不再触发页面动作 */
    if (focus_exit_edit()) return;

    lv_obj_t *o = focus_obj();
    if (!o) return;
    if (s_act) {
        s_act(o);
        return;
    }
    /* 默认：给对象发一个点击事件 */
    lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
}
