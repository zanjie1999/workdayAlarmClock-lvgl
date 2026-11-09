/**
 * page.c -- 页面栈与懒加载
 */
#include "page.h"

typedef struct {
    page_build_fn build;
    page_show_fn  on_show;
    lv_obj_t     *root;
} page_slot_t;

static page_slot_t s_slots[WAC_PAGE_COUNT];
static wac_page_id_t s_cur = WAC_PAGE_CLOCK;
static wac_page_id_t s_stack[8];
static int           s_depth;

void page_register(wac_page_id_t id, page_build_fn build, page_show_fn on_show)
{
    if (id < 0 || id >= WAC_PAGE_COUNT) return;
    s_slots[id].build   = build;
    s_slots[id].on_show = on_show;
}

lv_obj_t *page_root(wac_page_id_t id)
{
    if (id < 0 || id >= WAC_PAGE_COUNT) return NULL;
    return s_slots[id].root;
}

wac_page_id_t page_current(void)
{
    return s_cur;
}

static lv_obj_t *ensure_built(wac_page_id_t id)
{
    page_slot_t *p = &s_slots[id];
    if (p->root) return p->root;
    if (!p->build) return NULL;

    lv_obj_t *root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, WAC_SCR_W, WAC_SCR_H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    p->root = root;
    p->build(root);
    return root;
}

void page_show(wac_page_id_t id)
{
    if (id < 0 || id >= WAC_PAGE_COUNT) return;
    lv_obj_t *root = ensure_built(id);
    if (!root) {
        WAC_LOG("page %d has no builder", (int)id);
        return;
    }

    if (s_cur != id) {
        lv_obj_t *old = s_slots[s_cur].root;
        if (old) lv_obj_add_flag(old, LV_OBJ_FLAG_HIDDEN);
    }
    s_cur = id;
    lv_obj_remove_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    if (s_slots[id].on_show) s_slots[id].on_show();
}

void page_push(wac_page_id_t id)
{
    if (s_depth < (int)(sizeof(s_stack) / sizeof(s_stack[0]))) {
        s_stack[s_depth++] = id;
    }
    else {
        s_stack[s_depth - 1] = id;
    }
    page_show(id);
}

void page_set(wac_page_id_t id)
{
    page_set_dir(id, 0);
}

/* 页面滑入：只动 translate_x（纯整数平移，不缩放 → 不触发离屏图层），
 * 出页立刻隐藏，背景层留在原地，视觉上就是新页从侧边推上来。 */
static void anim_translate_x(void *obj, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)obj, v, 0);
}

static void slide_in(lv_obj_t *root, int dir)
{
    if (!root) return;

    lv_anim_delete(root, anim_translate_x);

    if (dir == 0) {
        lv_obj_set_style_translate_x(root, 0, 0);
        return;
    }

    int32_t from = dir * (WAC_SCR_W / 5);
    lv_obj_set_style_translate_x(root, from, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_exec_cb(&a, anim_translate_x);
    lv_anim_set_values(&a, from, 0);
    lv_anim_set_duration(&a, 180);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

void page_set_dir(wac_page_id_t id, int dir)
{
    if (id < 0 || id >= WAC_PAGE_COUNT) return;

    wac_page_id_t prev = s_cur;

    s_depth    = 1;
    s_stack[0] = id;
    page_show(id);

    if (id != prev) slide_in(s_slots[id].root, dir);
}

void page_init(void)
{
    page_clock_register();
    page_player_register();
    page_settings_register();

    s_depth     = 0;
    s_stack[0]  = WAC_PAGE_CLOCK;
    s_depth     = 1;
    s_cur       = WAC_PAGE_CLOCK;
    page_show(WAC_PAGE_CLOCK);
}

void nav_back(void)
{
    if (s_depth > 1) {
        s_depth--;
        page_show(s_stack[s_depth - 1]);
    }
    else {
        page_show(WAC_PAGE_CLOCK);
    }
}

void nav_home(void)
{
    s_depth    = 1;
    s_stack[0] = WAC_PAGE_CLOCK;
    page_show(WAC_PAGE_CLOCK);
}
