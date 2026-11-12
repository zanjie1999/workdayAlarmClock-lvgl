/**
 * page_settings.c -- 设置页
 *
 * 一个可上下滚动的列表，条目分三类：
 *   亮度条：有可调背光时才有，并且排在第一行（最常调的那一项）
 *   勾选项：显示秒 / 显示电量 / 反转音量方向 / 时间24小时制
 *   动作项：获取IP（向 Go 发 ip）/ 检查更新（向 Go 发 update）/
 *           重新绑定按键（删掉绑定表并重走向导）/ 关机 / 重启
 *           —— 前两个发完直接回时钟页，关机和重启则先盖一层提示再执行
 *
 * 勾选框用「方框 + LV_SYMBOL_OK」画，不依赖中文字体里有没有 ☑ / ✓。
 *
 * 交互：上下移动焦点，确认键（或触摸点一下）切换勾选 / 执行动作。
 * 行高固定，列表放不下就滚动；按键换项时 focus.c 会 lv_obj_scroll_to_view
 * 把当前项滚进可视区，所以没有触摸屏也能翻到底。
 *
 * 亮度是唯一一条"能原地调值"的项，跟播放页的音量条一个套路：
 * 确认键不执行动作，而是进调节态（方框描边变白），方向键 ±5，
 * 再按确认退出回到外层。走的是 focus.c 的通用机制，页面只管调值。
 */
#include "page.h"
#include "theme.h"
#include "ui_font.h"
#include "ui_settings.h"
#include "ui_bright.h"
#include "backlight.h"
#include "cmd.h"
#include "focus.h"
#include "swipe.h"
#include "indev_keys.h"
#include "bind_wizard.h"

#define N_OPT 10

/* 方向键一次调多少（触摸拖是连续的，这个只管按键） */
#define BR_STEP 5

/* id 只当"下标"用（s_row/s_box/s_chk/OPT_TEXT 全按 id 索引），
   跟屏幕上的先后顺序无关 —— 顺序看下面的 OPT_ORDER。 */
enum {
    OPT_SECONDS = 0,
    OPT_BATTERY = 1,
    OPT_VOLREV  = 2,
    OPT_HOUR24  = 3,
    OPT_IP      = 4,
    OPT_UPDATE  = 5,
    OPT_BRIGHT  = 6,
    OPT_POWEROFF = 7,
    OPT_REBOOT   = 8,
    OPT_REBIND   = 9
};

static const char *OPT_TEXT[N_OPT] = {"显示秒",       "显示电量", "反转调节方向",
                                      "时间24小时制", "获取IP",   "检查更新",
                                      "亮度",         "关机",     "重启",
                                      "重新绑定按键"};

/* 视觉顺序：跟 id 解耦，加条目 / 挪位置只动这一张表。
   亮度排第一 —— 它是最常调的一项；没有可调背光的板子整行跳过。 */
static const int OPT_ORDER[] = {
    OPT_BRIGHT,   OPT_SECONDS, OPT_BATTERY, OPT_VOLREV,   OPT_HOUR24,
    OPT_IP,       OPT_UPDATE,  OPT_REBIND,  OPT_POWEROFF, OPT_REBOOT,
};

/* 实际建了哪几行（没有可调背光的板子跳过亮度那行），
   行序 -> 选项下标。以前靠"少建最后一行"偷懒，加了关机这条就不成立了。 */
static int s_order[N_OPT];
static int s_n_opt;

static lv_obj_t *s_row[N_OPT];
static lv_obj_t *s_box[N_OPT];
static lv_obj_t *s_chk[N_OPT];

/* 亮度那一行自己的一套控件 */
static lv_obj_t *s_brow;
static lv_obj_t *s_bslider;
static lv_obj_t *s_bval;

/* 关机时盖在最上层的那张"正在关机"，命令没生效时还要撤掉 */
static lv_obj_t *s_pw_ov;

static focus_list_t s_focus;

static bool *opt_flag(int i)
{
    switch (i) {
        case OPT_SECONDS: return &g_set.show_seconds;
        case OPT_BATTERY: return &g_set.show_battery;
        case OPT_VOLREV:  return &g_set.vol_reverse;
        case OPT_HOUR24:  return &g_set.hour24;
        default:          return NULL; /* 动作项和亮度行没有勾选框 */
    }
}

static bool is_action(int i)
{
    return (i == OPT_IP) || (i == OPT_UPDATE) || (i == OPT_REBIND) || (i == OPT_POWEROFF) ||
           (i == OPT_REBOOT);
}

/* 动作行右侧的图标：获取IP=WiFi（网络地址）、检查更新=上传（上报版本）、
   重新绑定按键=键盘、重启=循环刷新、关机=电源。
   内置符号字体里都有（montserrat 12/14/20 的字形表里，U+F11C 那位就是键盘），
   别用三元链兜底 —— 以前兜底是电源图标，新加的动作项会被认成"关机"。 */
static const char *action_icon(int i)
{
    switch (i) {
        case OPT_IP:     return LV_SYMBOL_WIFI;
        case OPT_UPDATE: return LV_SYMBOL_UPLOAD;
        case OPT_REBIND: return LV_SYMBOL_KEYBOARD;
        case OPT_REBOOT: return LV_SYMBOL_REFRESH;
        default:         return LV_SYMBOL_POWER;
    }
}

/* 关机 / 重启：先把提示盖上屏再执行命令。
   命令回来就说明没成功（成功的话进程已经被带走了），
   3 秒后把遮罩撤掉回时钟页，绝不把界面留在一块黑屏上。 */
static void poweroff_fail_cb(lv_timer_t *t)
{
    lv_timer_delete(t);
    if (s_pw_ov) {
        lv_obj_delete(s_pw_ov);
        s_pw_ov = NULL;
    }
    WAC_LOG("power: command returned, still alive -> restore UI");
    page_set(WAC_PAGE_CLOCK);
}

static void do_power(int idx)
{
    if (s_pw_ov) return; /* 已经在关了，别叠第二层 */

    const bool reboot = (idx == OPT_REBOOT);

    lv_obj_t *ov = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, WAC_SCR_W, WAC_SCR_H);
    lv_obj_set_pos(ov, 0, 0);
    lv_obj_set_style_bg_color(ov, COL_BG, 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(ov);
    lv_obj_set_style_text_font(lbl, ui_font_cjk(g_tk.bp == WAC_BP_TINY ? 13 : 18), 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_label_set_text(lbl, reboot ? "正在重启" : "正在关机");
    lv_obj_center(lbl);

    s_pw_ov = ov;
    /* 命令一执行电源随时会断，先强制刷一帧出来 */
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);

    if (reboot) cmd_reboot();
    else cmd_power_off();

    lv_timer_create(poweroff_fail_cb, 3000, NULL);
}

/* 动作项：向 Go 发一条命令，然后直接跳回时钟页（用户要的操作完就回去看结果） */
static void run_action(int i)
{
    if (i == OPT_POWEROFF || i == OPT_REBOOT) {
        do_power(i);
        return;
    }

    /* 重新绑定按键：删掉绑定表再走一遍向导。
       顺序不能反 —— 向导收尾（wizard_close）在"已经按过第一下"时会顺手
       keys_save_config()，先起向导后删文件等于把刚删的又写回去。
       这里也刻意不清内存里的绑定（keys_clear）：向导第 1 步按到键才清，
       在用户按下第一下之前旧按键仍然可用，半途退出也不会把设备搞成没键可用。
       不跳页：向导收尾后用户还站在设置页。 */
    if (i == OPT_REBIND) {
        keys_remove_config();
        WAC_LOG("settings: rebind -> bind wizard");
        wizard_force_start();
        return;
    }

    const char *cmd = (i == OPT_IP) ? "ip" : "update";

    WAC_LOG("settings: send '%s' -> back to clock", cmd);
    wac_cmd("%s", cmd);
    page_set(WAC_PAGE_CLOCK);
}

static void settings_refresh(void)
{
    for (int k = 0; k < s_n_opt; k++) {
        const int i = s_order[k];
        bool     *f = opt_flag(i);
        if (!f) continue; /* 动作行 / 亮度行没有方框 */
        if (*f) {
            lv_obj_set_style_bg_color(s_box[i], COL_FOCUS, 0);
            lv_obj_set_style_bg_opa(s_box[i], LV_OPA_COVER, 0);
            lv_obj_remove_flag(s_chk[i], LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_set_style_bg_opa(s_box[i], LV_OPA_TRANSP, 0);
            lv_obj_add_flag(s_chk[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void toggle(int i)
{
    bool *f = opt_flag(i);
    *f      = !*f;
    ui_set_save();
    settings_refresh();
    page_clock_settings_changed();
}

static void row_event_cb(lv_event_t *e)
{
    if (swipe_blocks_click()) return;

    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        /* 按对象反查焦点下标。焦点列表是按"行序"排的，而这里的 i 是选项 id：
           没有可调背光时 s_order 跳过了亮度行，两者差一位 ——
           拿 id 当 focus_set_index 的下标，点"关机"焦点框会落到"重启"上。 */
        focus_set_obj(lv_event_get_current_target(e));
        if (is_action(i)) run_action(i);
        else if (i != OPT_BRIGHT) toggle(i);
    }
}

/* ---------------------------------------------------------- 亮度（调节态） */
static void bright_refresh(void)
{
    if (!s_bslider) return;

    if (lv_slider_get_value(s_bslider) != g_set.brightness) {
        lv_slider_set_value(s_bslider, g_set.brightness, LV_ANIM_OFF);
    }
    char b[8];
    snprintf(b, sizeof(b), "%d", g_set.brightness);
    lv_label_set_text(s_bval, b);
}

/* dir > 0 = 下一个键 = 变暗；跟音量条同一套方向约定（下减上加）。
   勾了"反转调节方向"时由 ui_adj_dir 统一翻过来。 */
static void bright_edit_step(int dir)
{
    const int d = ui_adj_dir(dir);
    if (d) {
        int v = g_set.brightness + (d > 0 ? -BR_STEP : +BR_STEP);
        if (v < WAC_BRIGHT_MIN) v = WAC_BRIGHT_MIN;
        if (v > 100) v = 100;
        ui_bright_set(v);
        ui_set_save();
    }
    bright_refresh(); /* dir==0：刚进/刚出调节态，刷一次显示 */
}

static void bright_slider_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);

    if (c == LV_EVENT_VALUE_CHANGED) {
        ui_bright_set((int)lv_slider_get_value(s_bslider));
        bright_refresh();
    }
    else if (c == LV_EVENT_RELEASED) {
        ui_set_save(); /* 松手才落盘，拖动过程中不写文件 */
    }
}

static void activate_cb(lv_obj_t *obj)
{
    if (obj == s_brow) {
        focus_edit_support(bright_edit_step);
        focus_enter_edit();
        return;
    }
    for (int k = 0; k < s_n_opt; k++) {
        const int i = s_order[k];
        if (s_row[i] == obj) {
            if (is_action(i)) run_action(i);
            else if (i != OPT_BRIGHT) toggle(i);
            return;
        }
    }
}

static void on_show(void)
{
    settings_refresh();
    bright_refresh();
    focus_use(&s_focus);
    focus_set_activate(activate_cb);
}

/* 行内文字按"墨迹"居中：偏移量由 ui_font 按字号算好缓存，这里直接调公共入口。
   flex 的居中是对"行框"算的，而字体行框上方留白各家不一样：雅黑只差 1px，
   设备上那个楷体差 6~7px，于是汉字看着"沉"在行底。 */

static lv_obj_t *make_row(lv_obj_t *parent, int idx, int32_t box, int32_t fs, const lv_font_t *f_chk,
                          int32_t row_h)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    /* 行高固定：列表放不下就滚动（以前小屏用 flex_grow 吃掉剩余高度，
       条目一多就全被压扁，也就没法滚了） */
    lv_obj_set_height(row, row_h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_column(row, g_tk.pad + 3, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    theme_focus_style(row);

    /* 勾选框：空心方框，勾上时填充主题蓝并显示对勾 */
    lv_obj_t *bx = lv_obj_create(row);
    lv_obj_remove_style_all(bx);
    lv_obj_set_size(bx, box, box);
    lv_obj_set_style_radius(bx, 2, 0);
    lv_obj_set_style_border_width(bx, 1, 0);
    lv_obj_set_style_border_color(bx, COL_DIM, 0);
    lv_obj_set_style_border_opa(bx, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(bx, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(bx, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(bx, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *chk = lv_label_create(bx);
    lv_obj_set_style_text_font(chk, f_chk, 0);
    lv_obj_set_style_text_color(chk, lv_color_white(), 0);
    lv_label_set_text(chk, LV_SYMBOL_OK);
    lv_obj_center(chk);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, ui_font_cjk(fs), 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_label_set_text(lbl, OPT_TEXT[idx]);
    ui_font_vcenter(lbl);

    lv_obj_add_event_cb(row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    s_row[idx] = row;
    s_box[idx] = bx;
    s_chk[idx] = chk;
    return row;
}

/* 动作行：跟勾选行同一套行高/焦点框，只是左边留一个和方框等宽的空档
   （这样两类条目的文字左边缘对齐），右边挂一个图标表示"按下就执行"。
   获取IP = 上传图标，检查更新 = 刷新图标，都用内置符号字体，不依赖中文字体。 */
static lv_obj_t *make_action_row(lv_obj_t *parent, int idx, int32_t box, int32_t fs, int32_t row_h,
                                 const lv_font_t *f_icon, const char *icon)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, row_h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_right(row, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_column(row, g_tk.pad + 3, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    theme_focus_style(row);

    lv_obj_t *gap0 = lv_obj_create(row); /* 和勾选框等宽的空档 */
    lv_obj_remove_style_all(gap0);
    lv_obj_set_size(gap0, box, 1);
    lv_obj_remove_flag(gap0, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(gap0, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, ui_font_cjk(fs), 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_label_set_text(lbl, OPT_TEXT[idx]);
    ui_font_vcenter(lbl);

    lv_obj_t *sp = lv_obj_create(row); /* 撑开，把图标推到右端 */
    lv_obj_remove_style_all(sp);
    lv_obj_set_height(sp, 1);
    lv_obj_set_flex_grow(sp, 1);
    lv_obj_remove_flag(sp, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(sp, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *ic = lv_label_create(row);
    lv_obj_set_style_text_font(ic, f_icon, 0);
    lv_obj_set_style_text_color(ic, COL_DIM, 0);
    lv_label_set_text(ic, icon);

    lv_obj_add_event_cb(row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    s_row[idx] = row;
    return row;
}

/* 亮度行：标签 + 滑块 + 百分比，没有勾选框。
   轨道和滑块头的画法跟播放页的音量条保持一致，两处看起来才是一套东西。 */
static lv_obj_t *make_bright_row(lv_obj_t *parent, int32_t fs, int32_t sl_h, int32_t row_h,
                                 const lv_font_t *f_val)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, row_h);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(row, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_right(row, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_column(row, 3, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    theme_focus_style(row);
    /* 调节态描边转白：状态位必须写全三位，否则会被两位的蓝框压回去 */
    lv_obj_set_style_border_color(row, COL_TEXT,
                                  LV_STATE_FOCUSED | LV_STATE_EDITED | THEME_ST_FOCUS_VIS);

    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, ui_font_cjk(fs), 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_label_set_text(lbl, OPT_TEXT[OPT_BRIGHT]);
    ui_font_vcenter(lbl);

    lv_obj_t *sl = lv_slider_create(row);
    lv_obj_set_height(sl, sl_h);
    lv_obj_set_flex_grow(sl, 1);
    lv_slider_set_range(sl, WAC_BRIGHT_MIN, 100);
    lv_slider_set_value(sl, g_set.brightness, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(sl, COL_DIMMER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, COL_FOCUS, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    const int32_t knob = sl_h + 4;
    lv_obj_set_style_width(sl, knob, LV_PART_KNOB);
    lv_obj_set_style_height(sl, knob, LV_PART_KNOB);
    lv_obj_set_style_radius(sl, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sl, COL_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 0, LV_PART_KNOB);

    lv_obj_add_event_cb(sl, bright_slider_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *val = lv_label_create(row);
    lv_obj_set_style_text_font(val, f_val, 0);
    lv_obj_set_style_text_color(val, COL_DIM, 0);
    /* 宽度按"100"实测算，别写死：以前是 22px，24px 字号下连一个数字都放不下，
       数字被挤成一列竖排（"1/0/0" 疯狂换行）。再加 CLIP 兜底，永远不折行。 */
    lv_obj_set_width(val, lv_text_get_width("100", 3, f_val, 0) + 4);
    lv_label_set_long_mode(val, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(val, "100");
    ui_font_vcenter(val); /* 数字走 Montserrat，偏移量为 0，会自动跳过，不改动 */

    s_brow    = row;
    s_bslider = sl;
    s_bval    = val;
    s_row[OPT_BRIGHT] = row;

    lv_obj_add_event_cb(row, row_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)OPT_BRIGHT);
    return row;
}

static void build(lv_obj_t *root)
{
    const int32_t W = WAC_SCR_W;
    const bool    tiny  = (g_tk.bp == WAC_BP_TINY);
    const bool    small = (g_tk.bp == WAC_BP_SMALL);

    /* 没有可调背光就不显示亮度条：留一条调不动的滑块只会让人以为坏了。
       行序完全由 OPT_ORDER 决定（亮度在最前），所以不能再靠"少建首/末行"来省。 */
    const bool has_bl = backlight_available();
    s_n_opt           = 0;
    for (int k = 0; k < (int)(sizeof(OPT_ORDER) / sizeof(OPT_ORDER[0])); k++) {
        const int i = OPT_ORDER[k];
        if (i == OPT_BRIGHT && !has_bl) continue;
        s_order[s_n_opt++] = i;
    }

    /* 用一列 flex 排：标题 + 可滚动的选项列表，全部靠布局算，不猜坐标 */
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, W, WAC_SCR_H);
    lv_obj_set_pos(col, 0, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    /* 左右各留 pad+4：焦点框要往外画 3px，贴边会被屏幕裁掉 */
    lv_obj_set_style_pad_left(col, g_tk.pad + 4, 0);
    lv_obj_set_style_pad_right(col, g_tk.pad + 4, 0);
    lv_obj_set_style_pad_top(col, g_tk.pad, 0);
    lv_obj_set_style_pad_bottom(col, g_tk.pad, 0);
    lv_obj_set_style_pad_row(col, tiny ? 2 : 4, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    /* 标题 */
    lv_obj_t *title = lv_label_create(col);
    lv_obj_set_style_text_font(title, ui_font_cjk(tiny ? 13 : (small ? 18 : 24)), 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_label_set_text(title, "设置");

    /* 选项列表：内容比它高就滚动，放得下才上下匀一份 pad 摆中间 */
    lv_obj_t *list = lv_obj_create(col);
    lv_obj_remove_style_all(list);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(list, tiny ? 2 : g_tk.gap, 0);
    /* 原生滚动：手指直接拖；没有触摸屏时靠 focus.c 在换项时 scroll_to_view */
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    int32_t box = tk_pick(14, 16, 20, 20);
    int32_t fs  = tk_pick(13, 18, 24, 24);
    const lv_font_t *f_chk = tiny ? &lv_font_montserrat_12
                                  : (small ? &lv_font_montserrat_14 : &lv_font_montserrat_20);

    /* 行高固定（小屏也要固定）：条目多了就滚动，而不是把每行压扁。
       小屏 22px 仍大于 14px 的方框 + 13px 的字，行本身就是触摸区。
       MEDIUM（480×320）用 52：字号 24 不缩，但比大屏的 64 矮一截，
       一屏能多看一行，不至于翻半天。 */
    const int32_t row_h = tk_pick(22, 44, 52, 64);

    {
        /* 内容比列表矮就上下匀一份 pad，把各行摆到视觉中间；
           内容更高时不加 pad，从第一行开始排，靠滚动看剩下的 */
        const int32_t title_lh = lv_font_get_line_height(lv_obj_get_style_text_font(title, 0));
        const int32_t row_gap  = tiny ? 2 : g_tk.gap;
        const int32_t list_h   = WAC_SCR_H - g_tk.pad * 2 - title_lh - (tiny ? 2 : 4);
        const int32_t content  = s_n_opt * row_h + (s_n_opt - 1) * row_gap;
        int32_t       pad_v    = (list_h - content) / 2;
        if (pad_v > 0) {
            lv_obj_set_style_pad_top(list, pad_v, 0);
            lv_obj_set_style_pad_bottom(list, pad_v, 0);
        }
        WAC_LOG("settings: %d rows, row_h=%d content=%d list_h=%d -> %s", s_n_opt, (int)row_h,
                (int)content, (int)list_h, content > list_h ? "scroll" : "fit");
    }

    focus_list_init(&s_focus);
    for (int k = 0; k < s_n_opt; k++) {
        const int i = s_order[k];
        lv_obj_t *row;
        if (i == OPT_BRIGHT) {
            row = make_bright_row(list, fs, tk_pick(6, 8, 12, 24), row_h,
                                  tiny ? &lv_font_montserrat_12
                                       : (small ? &lv_font_montserrat_14 : &lv_font_montserrat_20));
        }
        else if (is_action(i)) {
            row = make_action_row(list, i, box, fs, row_h, f_chk, action_icon(i));
        }
        else {
            row = make_row(list, i, box, fs, f_chk, row_h);
        }
        focus_list_add(&s_focus, row);
    }

    settings_refresh();
    bright_refresh();
}

void page_settings_register(void)
{
    page_register(WAC_PAGE_SETTINGS, build, on_show);
}
