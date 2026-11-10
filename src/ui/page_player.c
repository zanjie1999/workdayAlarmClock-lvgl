/**
 * page_player.c -- 播放页
 *
 * 标题（ECHO 跑马灯）+ 进度条 + 已播/总时长 + 上一首/播放暂停/下一首/停止 + 常驻音量条。
 *
 * 布局规则（不按断点硬编码，按宽度算）：
 *   4 个按钮 + 音量条放得进一行 → 同排（音量条 flex_grow 吃掉剩余宽度）
 *   放不进 → 按钮一行、音量条一行
 * 160×128 上是两行；1024×600 上同排。
 *
 * 音量条可拖（触摸），松手才发命令：Go 的 stdin 循环遇到缓冲里还有命令会整批丢掉，
 * 拖动过程中连发几十条只会让最后一条也丢。
 *
 * 按键：焦点走到音量条后按确认进入"调节态"（描边由蓝变白、标题临时显示音量读数），
 * 方向键 ±5，再按确认（或 BACK）退出。调节态在 focus.c 里是通用机制，
 * 页面只在 activate 回调里声明"这一项支持调节"。
 */
#include "page.h"
#include "theme.h"
#include "ui_font.h"
#include "ui_settings.h"
#include "cmd.h"
#include "focus.h"
#include "swipe.h"

/* 播放键底板：60% 透明（颜色只剩 40% 深度）→ LV_OPA_40。
 * 透出壁纸但白图标仍压得住；要改回不透明就换 LV_OPA_COVER，改这一处即可。 */
#define PLAYER_BTN_BG_OPA LV_OPA_40

static lv_obj_t *s_title;
static lv_obj_t *s_bar;
static lv_obj_t *s_cursor; /* 时长未知时来回滑的小块 */
static lv_obj_t *s_pos;
static lv_obj_t *s_dur;

static lv_obj_t *s_btn_prev;
static lv_obj_t *s_btn_play;
static lv_obj_t *s_btn_next;
static lv_obj_t *s_btn_stop;
static lv_obj_t *s_vol;
static lv_obj_t *s_vol_box; /* 音量行的外框：焦点框和"调节态"都挂在它身上 */

/* 音量外框高度 = 轨道 + 滑块头上下的溢出(各 2) + 焦点描边(各 2)。
   少了这段，白圆头会压住蓝色描边的上下两条。 */
#define VOL_BOX_EXTRA (4 + THEME_FOCUS_BW * 2)

static const lv_font_t *s_f_btn;
static focus_list_t     s_focus;

static void page_player_refresh(void);
static void vol_edit_step(int dir);

enum { ACT_PREV = 1, ACT_PLAY, ACT_NEXT, ACT_STOP };

static void do_action(int act)
{
    switch (act) {
        case ACT_PREV: cmd_prev(); break;
        case ACT_PLAY: cmd_play_pause(); break;
        case ACT_NEXT: cmd_next(); break;
        case ACT_STOP: cmd_stop(); break;
        default: break;
    }
    page_player_refresh();
}

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    /* 左右滑动切页时手指底下可能压着按钮，松手的 CLICKED 要丢掉 */
    if (swipe_blocks_click()) return;

    int act = (int)(intptr_t)lv_event_get_user_data(e);
    focus_set_index(act - 1); /* 触摸点选：同时把焦点移过来 */
    do_action(act);
}

static void activate_cb(lv_obj_t *obj)
{
    /* 音量条不走"按一下执行一个动作"，而是进调节态：
       方向键改成调音量，再按一次确认退出（退出逻辑在 focus_activate 里）。 */
    if (obj == s_vol_box) {
        focus_edit_support(vol_edit_step);
        focus_enter_edit();
        return;
    }
    do_action((int)(intptr_t)lv_obj_get_user_data(obj));
}

static void on_show(void)
{
    focus_use(&s_focus);
    /* 进播放页先把焦点落在"播放/暂停"上：这是这页最常用的一个键，
       老是停在"上一首"上，要按两下才播得起来。 */
    focus_set_obj(s_btn_play);
    focus_set_activate(activate_cb);
    page_player_refresh();
}

static void fmt_ms(char *buf, size_t cap, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(buf, cap, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void page_player_refresh(void)
{
    if (!s_title) return;

    /* 调节态：标题临时让位给音量读数 —— 小屏上条只有 140px 宽，
       5% 才 7px，光看条看不出现在是 60 还是 65 */
    if (focus_in_edit()) {
        char vb[20];
        snprintf(vb, sizeof(vb), "音量 %d", (int)(g_state.volume_known ? g_state.volume : 0));
        lv_label_set_text(s_title, vb);
    }
    else {
        lv_label_set_text(s_title, g_state.title[0] ? g_state.title : WAC_DEF_TITLE);
    }

    const char *icon;
    switch (g_state.state) {
        case WAC_ST_PLAYING: icon = LV_SYMBOL_PAUSE; break;
        case WAC_ST_PAUSED:  icon = LV_SYMBOL_PLAY;  break;
        default:             icon = LV_SYMBOL_PLAY;  break;
    }
    lv_obj_t *lbl = lv_obj_get_child(s_btn_play, 0);
    if (lbl) lv_label_set_text(lbl, icon);

    /* 音量：正在拖的时候不要回写，否则手指会被拽回去 */
    if (s_vol && !lv_obj_has_state(s_vol, LV_STATE_PRESSED)) {
        int32_t v = g_state.volume_known ? (int32_t)g_state.volume : 0;
        if (lv_slider_get_value(s_vol) != v) lv_slider_set_value(s_vol, v, LV_ANIM_OFF);
    }

    if (g_state.state == WAC_ST_STOP) {
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        if (s_cursor) lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        if (s_pos) lv_label_set_text(s_pos, "");
        if (s_dur) lv_label_set_text(s_dur, "");
        return;
    }

    if (g_state.duration_ms > 0) {
        if (s_cursor) lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
        lv_bar_set_range(s_bar, 0, g_state.duration_ms);
        lv_bar_set_value(s_bar, (int32_t)g_state.progress_ms, LV_ANIM_OFF);
    }
    else {
        /* 总时长未知：条填 0，换一块来回滑的小方块表示"在走但不知道还剩多少" */
        lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
        if (s_cursor) lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);
    }

    if (s_pos) {
        char a[12], b[12];
        fmt_ms(a, sizeof(a), g_state.progress_ms);
        lv_label_set_text(s_pos, a);
        if (g_state.duration_ms > 0) {
            fmt_ms(b, sizeof(b), (uint32_t)g_state.duration_ms);
            lv_label_set_text(s_dur, b);
        }
        else {
            lv_label_set_text(s_dur, "--:--");
        }
    }
}

static void timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    page_player_refresh();
}

/* ------------------------------------------------------------ 控件 */
static lv_obj_t *make_button(lv_obj_t *parent, int32_t w, int32_t h, const char *txt)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, COL_CARD, 0);
    lv_obj_set_style_bg_opa(btn, PLAYER_BTN_BG_OPA, 0);
    lv_obj_set_style_radius(btn, h / 4, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, s_f_btn, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_label_set_text(lbl, txt);
    lv_obj_center(lbl);

    theme_focus_style(btn);
    return btn;
}

static void anim_x_cb(void *obj, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)obj, v);
}

/* 时长未知：一小块在进度条轨道里来回滑（纯整数平移，不缩放） */
static void make_cursor(lv_obj_t *bar, int32_t bar_w, int32_t bar_h)
{
    s_cursor = lv_obj_create(bar);
    lv_obj_remove_style_all(s_cursor);
    lv_obj_remove_flag(s_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_cursor, bar_w / 5, bar_h);
    lv_obj_set_style_bg_color(s_cursor, COL_FOCUS, 0);
    lv_obj_set_style_bg_opa(s_cursor, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_cursor, 0, 0);
    lv_obj_add_flag(s_cursor, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_cursor);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_values(&a, 0, bar_w - bar_w / 5);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

/* 调节态里的方向键：dir<0（PREV/上/左）= 加，dir>0（NEXT/下/右）= 减。
   设置页勾了"反转音量方向"时，cmd_vol_step 会把这一层方向翻过来。
   dir==0 表示刚进/刚出，只刷显示。 */
static void vol_edit_step(int dir)
{
    if (dir != 0) {
        cmd_vol_step(dir > 0 ? -1 : +1);
        /* 乐观更新：Go 那侧会丢连发命令，UI 不能等回执 */
        if (s_vol) lv_slider_set_value(s_vol, (int32_t)g_state.volume, LV_ANIM_OFF);
    }
    page_player_refresh();
}

static void vol_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        g_state.volume       = (uint8_t)lv_slider_get_value(s_vol);
        g_state.volume_known = true;
        ui_set_note_volume(g_state.volume);
    }
    else if (code == LV_EVENT_PRESSED) {
        /* 手指碰滑条就把键盘焦点也挪过来，两套操作看到的是同一个"当前项" */
        focus_set_obj(s_vol_box);
    }
    else if (code == LV_EVENT_RELEASED) {
        if (swipe_blocks_click()) return;
        /* 松手才下发：Go 的 stdin 缓冲有内容时会丢弃后续命令 */
        wac_cmd("vol %d", (int)lv_slider_get_value(s_vol));
    }
}

static lv_obj_t *make_volume(lv_obj_t *parent, int32_t h, int32_t w, const lv_font_t *f_icon)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    /* 外框比轨道高一圈：焦点描边 + 滑块头的溢出都在这一圈里，轨道粗细不变 */
    lv_obj_set_height(box, h + VOL_BOX_EXTRA);
    if (w > 0) {
        lv_obj_set_width(box, w);
    }
    else {
        lv_obj_set_flex_grow(box, 1);
    }
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(box, 3, 0);
    lv_obj_set_style_pad_left(box, 3, 0); /* 喇叭图标别顶着描边的圆角 */
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(box, h / 2 + THEME_FOCUS_BW, 0);
    theme_focus_style(box); /* 基础态就占住 2px 透明边框，切焦点时轨道不跳 */
    /* 调节态：描边从蓝换白，"只是选中"和"正在调"一眼能分开。
       状态位必须写到最全：LVGL 取"状态位匹配最多"的那条样式，
       这里 FOCUSED+EDITED+VIS 三位一定压过 theme_focus_style 里的两位蓝框，
       少写一位就会退回去显示蓝色。 */
    lv_obj_set_style_border_color(box, COL_TEXT,
                                  LV_STATE_FOCUSED | LV_STATE_EDITED | THEME_ST_FOCUS_VIS);
    s_vol_box = box;

    lv_obj_t *icon = lv_label_create(box);
    lv_obj_set_style_text_font(icon, f_icon, 0);
    lv_obj_set_style_text_color(icon, COL_DIM, 0);
    lv_label_set_text(icon, LV_SYMBOL_VOLUME_MID);

    s_vol = lv_slider_create(box);
    lv_obj_set_height(s_vol, h);
    lv_obj_set_flex_grow(s_vol, 1);
    lv_slider_set_range(s_vol, 0, 100);
    lv_slider_set_value(s_vol, g_state.volume_known ? g_state.volume : 0, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(s_vol, COL_DIMMER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_vol, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_vol, h / 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_vol, COL_FOCUS, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_vol, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_vol, h / 2, LV_PART_INDICATOR);

    /* 滑块头按高度缩：小屏上默认头比整条还大 */
    int32_t knob = h + 4;
    lv_obj_set_style_width(s_vol, knob, LV_PART_KNOB);
    lv_obj_set_style_height(s_vol, knob, LV_PART_KNOB);
    lv_obj_set_style_radius(s_vol, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(s_vol, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_vol, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_vol, 0, LV_PART_KNOB);

    /* 触摸热区上下各放大一点，条本身只有 14px 高 */
    lv_obj_set_ext_click_area(s_vol, 10);

    lv_obj_add_event_cb(s_vol, vol_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_vol, vol_event_cb, LV_EVENT_RELEASED, NULL);

    return box;
}

static void build(lv_obj_t *root)
{
    const int32_t W    = WAC_SCR_W;
    const int32_t H    = WAC_SCR_H;
    const int32_t avail = W - g_tk.pad * 2;
    const bool    tiny  = (g_tk.bp == WAC_BP_TINY);
    const bool    small = (g_tk.bp == WAC_BP_SMALL);

    /* ---------- 标题 ---------- */
    const int32_t    fs_title = tk_pick(16, 20, 24, 28);
    const lv_font_t *f_title  = ui_font_cjk(fs_title);

    s_title = lv_label_create(root);
    lv_obj_set_style_text_font(s_title, f_title, 0);
    lv_obj_set_style_text_color(s_title, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_title, avail);
    lv_label_set_long_mode(s_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_title, WAC_DEF_TITLE);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, g_tk.pad);

    /* ---------- 进度条 ----------
       以前是"标题顶 + 固定 34px"，跟字号脱钩：标题 22px 时行高就 30，
       留给进度条的只有 4px，看着贴在一起。改成按标题行高 + 间距算。 */
    const int32_t bar_h   = tk_pick(3, 4, 6, 6);
    const int32_t bar_gap = tk_pick(5, 7, 12, 14);
    const int32_t bar_y   = g_tk.pad + lv_font_get_line_height(f_title) + bar_gap;
    s_bar = lv_bar_create(root);
    lv_obj_remove_style_all(s_bar);
    lv_obj_set_size(s_bar, avail, bar_h);
    lv_obj_align(s_bar, LV_ALIGN_TOP_MID, 0, bar_y);
    lv_obj_set_style_bg_color(s_bar, COL_DIMMER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 0, LV_PART_INDICATOR);
    lv_obj_remove_flag(s_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    make_cursor(s_bar, avail, bar_h);

    /* ---------- 时间文字：小屏也显示（11px），不然"有进度"看不出来 ----------
       用主文字色，不用 COL_DIM：这两行压在插画壁纸上，暗色底上几乎看不到。 */
    s_pos = lv_label_create(root);
    lv_obj_set_style_text_font(s_pos, ui_font_cjk(tk_pick(11, 14, 14, 18)), 0);
    lv_obj_set_style_text_color(s_pos, COL_TEXT, 0);
    lv_label_set_text(s_pos, "");
    lv_obj_align(s_pos, LV_ALIGN_TOP_LEFT, g_tk.pad + 1, bar_y + bar_h + 3);

    s_dur = lv_label_create(root);
    lv_obj_set_style_text_font(s_dur, ui_font_cjk(tk_pick(11, 14, 14, 18)), 0);
    lv_obj_set_style_text_color(s_dur, COL_TEXT, 0);
    lv_label_set_text(s_dur, "");
    lv_obj_align(s_dur, LV_ALIGN_TOP_RIGHT, -g_tk.pad - 1, bar_y + bar_h + 3);

    /* ---------- 尺寸 ---------- */
    int32_t bw_side, bw_mid, bh, gap, vol_h, vol_min_w;
    if (g_tk.bp == WAC_BP_TINY) {
        bw_side = 30; bw_mid = 46; bh = 36; gap = 4;
        vol_h = 14;   vol_min_w = 44;
    }
    else if (g_tk.bp == WAC_BP_SMALL) {
        bw_side = 64; bw_mid = 96; bh = 56; gap = 6;
        vol_h = 20;   vol_min_w = 90;
    }
    else if (g_tk.bp == WAC_BP_MEDIUM) {
        /* 480×320 这类屏：以前的"大屏档"是 96/160/80，一行 484px 放不进 470，
           LVGL 的 flex 会把它挤成不等宽、两边顶到屏幕边缘。这里按 470 反推：
           3*88 + 140 + 3*12 = 440，两边各留 15px。 */
        bw_side = 88; bw_mid = 140; bh = 96; gap = 12;
        vol_h = 22;   vol_min_w = 150;
    }
    else {
        bw_side = 96; bw_mid = 160; bh = 80; gap = 12;
        vol_h = 28;   vol_min_w = 180;
    }

    /* 图标用 LVGL 内置符号字体：编译进程序，不依赖中文字体里有没有 ▶ */
    s_f_btn = tiny ? &lv_font_montserrat_20
                   : (small ? &lv_font_montserrat_28 : &lv_font_montserrat_48);

    int32_t btns_w = bw_side * 3 + bw_mid + gap * 3;
    /* 兜底：万一还是放不下，按 mid:side ≈ 1.6 反解着收，
       绝不能让 flex 去挤（挤出来就是"按钮左右顶出屏幕"） */
    if (btns_w > avail) {
        bw_side = (avail - gap * 3) * 10 / 56;
        if (bw_side < 8) bw_side = 8;
        bw_mid  = bw_side * 16 / 10;
        btns_w  = bw_side * 3 + bw_mid + gap * 3;
    }

    /* 放得进一行就同排，放不进就换行 —— 不猜分辨率 */
    const bool same_row = (btns_w + gap + vol_min_w) <= avail;

    /* 时间行往下到屏底的这段：按钮在它上面居中，音量条钉在最底下。
       以前是"整块居中"和"音量条 = 按钮下沿 + gap"两种写法，前者底部空一大条，
       后者在高屏上按钮和音量条都挤在上半截。 */
    const int32_t vol_box_h = vol_h + VOL_BOX_EXTRA;
    const int32_t time_h    = lv_font_get_line_height(lv_obj_get_style_text_font(s_pos, 0));
    const int32_t area_top  = bar_y + bar_h + 1 + time_h + 2;
    const int32_t area_bot  = H - g_tk.pad;

    const int32_t vol_y = same_row ? 0 : (area_bot - vol_box_h);
    int32_t       btn_y;
    if (same_row) {
        btn_y = area_top + ((area_bot - area_top) - bh) / 2;
    }
    else {
        int32_t band_bot = vol_y - gap;
        if (band_bot < area_top + bh) band_bot = area_top + bh;
        btn_y = area_top + ((band_bot - area_top) - bh) / 2;
    }
    if (btn_y < area_top) btn_y = area_top;

    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, avail, bh);
    lv_obj_set_pos(row, g_tk.pad, btn_y);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_prev = make_button(row, bw_side, bh, LV_SYMBOL_PREV);
    s_btn_play = make_button(row, bw_mid, bh, LV_SYMBOL_PLAY);
    s_btn_next = make_button(row, bw_side, bh, LV_SYMBOL_NEXT);
    s_btn_stop = make_button(row, bw_side, bh, LV_SYMBOL_STOP);

    const lv_font_t *f_vicon = tiny ? &lv_font_montserrat_12
                                    : (small ? &lv_font_montserrat_16 : &lv_font_montserrat_20);

    if (same_row) {
        /* 同排：把剩余宽度全给音量条，边界明确（不靠 flex_grow 摊）
           —— 大屏上 flex_grow 会把滑块拉到几百像素宽，看着像根杆子 */
        make_volume(row, vol_h, avail - btns_w - gap, f_vicon);
    }
    else {
        lv_obj_t *volrow = lv_obj_create(root);
        lv_obj_remove_style_all(volrow);
        lv_obj_set_size(volrow, avail, vol_box_h);
        lv_obj_set_pos(volrow, g_tk.pad, vol_y);
        lv_obj_set_flex_flow(volrow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(volrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(volrow, LV_OBJ_FLAG_SCROLLABLE);
        make_volume(volrow, vol_h, 0, f_vicon); /* 0 → flex_grow 吃掉整行 */
    }

    lv_obj_set_user_data(s_btn_prev, (void *)(intptr_t)ACT_PREV);
    lv_obj_set_user_data(s_btn_play, (void *)(intptr_t)ACT_PLAY);
    lv_obj_set_user_data(s_btn_next, (void *)(intptr_t)ACT_NEXT);
    lv_obj_set_user_data(s_btn_stop, (void *)(intptr_t)ACT_STOP);
    lv_obj_add_event_cb(s_btn_prev, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_PREV);
    lv_obj_add_event_cb(s_btn_play, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_PLAY);
    lv_obj_add_event_cb(s_btn_next, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_NEXT);
    lv_obj_add_event_cb(s_btn_stop, btn_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ACT_STOP);

    focus_list_init(&s_focus);
    focus_list_add(&s_focus, s_btn_prev);
    focus_list_add(&s_focus, s_btn_play);
    focus_list_add(&s_focus, s_btn_next);
    focus_list_add(&s_focus, s_btn_stop);
    /* 第四个按钮之后再往下就落到音量条：确认进调节态，再按确认回外层 */
    focus_list_add(&s_focus, s_vol_box);

    page_player_refresh();
    lv_timer_create(timer_cb, 250, NULL);
}

void page_player_register(void)
{
    page_register(WAC_PAGE_PLAYER, build, on_show);
}
