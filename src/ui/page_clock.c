/**
 * page_clock.c -- 时钟页
 *
 * 三段式（按 tk 断点，见计划 §2.1/§2.2）：
 *   顶部：电量（可选） / 日期 / 星期 / 天气 —— 四项均分
 *   中间：HH:MM:SS 大字（启动时实测选档，label 宽度锁定 → 跳秒不抖），
 *         在"状态行下沿 → 迷你条上沿"之间按墨迹上下居中
 *   底部：迷你播放条（停止时隐藏，时间块自动回到整屏可用区的正中）
 *
 * 顶部的"显示秒 / 显示电量"来自设置页（ui_settings），改完调
 * page_clock_settings_changed() 立即重排：关掉秒之后可用宽度变大，
 * 时钟字会跟着变大，而不是留一块空白。
 */
#include "page.h"
#include "theme.h"
#include "ui_font.h"
#include "ui_settings.h"
#include "battery.h"
#include "cmd.h"
#include "focus.h"
#include "swipe.h"

static lv_obj_t *s_batt;
static lv_obj_t *s_date;
static lv_obj_t *s_week;
static lv_obj_t *s_weather_top;
static lv_obj_t *s_time;
static lv_obj_t *s_mini;
static lv_obj_t *s_mini_icon;
static lv_obj_t *s_mini_title;
static lv_obj_t *s_hint;        /* 底部提示行（ECHO / WEATHERAL 的文字） */
static lv_obj_t *s_volrow;      /* 音量模式的底条（只在调节态里显示） */
static lv_obj_t *s_volbar;

static char s_hint_last[128];   /* 上一次设进 label 的文字，避免重复 set 把跑马灯打回原点 */

/* 当前是不是在音量调节模式（focus 的调节态）。底部那条由它决定显示谁：
   音量条 / 提示行 / 迷你条，三者互斥，都占同一块位置。 */
static bool s_vol_on;

static focus_list_t s_focus;

static int32_t s_avail_w;      /* 时钟可用的最大宽度（不含左右留白） */
static int32_t s_ink_h;        /* 数字"墨迹"高度（不含行框上下留白） */
static int32_t s_ink_ofs;      /* 墨迹相对行框顶部的偏移 */
static int32_t s_status_h;     /* 顶部状态区的实际高度（一行或两行，建页时定） */
static int     s_last_sec = -1;

static const char *WEEK_CN[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};

/* 数字墨迹相对行框顶部的偏移。跟 LVGL 内部画字形的算法保持一致（lv_draw_label.c）：
       ink_top = label_top + (line_height - base_line) - box_h - ofs_y
   少减 ofs_y 会差 2~3px，居中就会偏下半个字。取 '0' 的度量即可（数字等高、无下伸部）。 */
static int32_t ink_offset(const lv_font_t *f)
{
    lv_font_glyph_dsc_t g;

    if (!f || !lv_font_get_glyph_dsc(f, &g, '0', 0)) return 0;

    int32_t off = (f->line_height - f->base_line) - (int32_t)g.box_h - (int32_t)g.ofs_y;
    if (off < 0) off = 0;
    return off;
}

/* 时间上下居中：按"墨迹"算，不按行框算。
   这个字体行框 53px、数字墨迹只有 32px，而且墨迹是从行框顶往下 12px 才开始
   （ascent 里留给数字上方的空档更多），拿行框去居中，看上去整体偏上。
   区间取「顶部状态行下沿 → 迷你条上沿」，迷你条不显示时就到屏幕底部。 */
static void apply_time_pos(void)
{
    if (!s_time) return;

    const bool    mini = (g_state.state != WAC_ST_STOP);
    const int32_t top  = s_status_h;
    /* 底部要是有东西（迷你条 / 提示行 / 音量条），时间块只居中到它上沿 */
    const bool    bottom_used = mini || wac_hint_visible() || s_vol_on;
    int32_t       bot  = bottom_used ? g_tk.mini_top : WAC_SCR_H;
    if (bot < top + s_ink_h) bot = top + s_ink_h;

    int32_t y = top + ((bot - top) - s_ink_h) / 2 - s_ink_ofs;
    if (y + s_ink_ofs < top) y = top - s_ink_ofs; /* 极窄的屏上别顶到状态行里 */

    lv_obj_align(s_time, LV_ALIGN_TOP_MID, 0, y);
}

/* 按"是否显示秒"重算字号与宽度。字体是 FreeType 实时渲染，可以任意像素，
   所以关秒之后不是留白，而是换一个更大的字号。

   可用高度取「状态行下沿 → 迷你条上沿」的整段（而不是主题里那个偏保守的
   clock_h），宽度取到屏宽 - 2。实测这块屏上数字是被**宽度**卡住的：
   "00:00" 撑到 158px 宽时墨迹约 53px 高，比原来按 clock_h=42 算的大了 26%。 */
static void clock_relayout(void)
{
    if (!s_time) return;

    const char *sample = g_set.show_seconds ? "00:00:00" : "00:00";

    int32_t avail_h = g_tk.mini_top - s_status_h;
    if (avail_h < 24) avail_h = 24;

    int32_t     tw = 0, th = 0;
    const lv_font_t *f =
        ui_font_clock_fit_text(sample, (int32_t)strlen(sample), s_avail_w, avail_h, &tw, &th);

    lv_obj_set_style_text_font(s_time, f, 0);
    lv_obj_set_width(s_time, tw);

    s_ink_h   = (th > 0) ? th : f->line_height;
    s_ink_ofs = ink_offset(f);
    apply_time_pos();

    s_last_sec = -1; /* 强制下一帧刷新文本 */

    WAC_LOG("clock: sample=%s avail=%dx%d w=%d ink_h=%d ink_ofs=%d line_h=%d", sample,
            (int)s_avail_w, (int)avail_h, (int)tw, (int)s_ink_h, (int)s_ink_ofs,
            (int)f->line_height);
}

static void refresh_battery(void)
{
    if (!s_batt) return;

    static int s_last = -2;
    int        p     = battery_percent();
    bool       show  = (g_set.show_battery && p >= 0);

    if (!show) {
        lv_obj_add_flag(s_batt, LV_OBJ_FLAG_HIDDEN);
        if (s_last != -1) {
            s_last = -1;
            WAC_LOG("clock: battery hidden");
        }
        return;
    }

    char b[8];
    snprintf(b, sizeof(b), "%d%%", p);
    if (strcmp(lv_label_get_text(s_batt), b) != 0) lv_label_set_text(s_batt, b);
    lv_obj_remove_flag(s_batt, LV_OBJ_FLAG_HIDDEN);

    if (s_last != p) {
        s_last = p;
        WAC_LOG("clock: battery %d%%", p);
    }
}

static void refresh_mini(void)
{
    bool visible = (g_state.state != WAC_ST_STOP);
    if (visible) {
        lv_obj_remove_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_mini_icon,
                          g_state.state == WAC_ST_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_label_set_text(s_mini_title, g_state.title[0] ? g_state.title : WAC_DEF_TITLE);
    }
    else {
        lv_obj_add_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
    }
    apply_time_pos();
}

/* 底部提示行：停止播放时显示 ECHO（10 秒）/ WEATHERAL（一直显示）的文字。
 * 只在内容真的变了才 set_text —— 每次 set 都会把跑马灯动画打回原点。 */
static void refresh_hint(void)
{
    if (!s_hint) return;

    /* 音量调节模式下这条归音量条用 */
    const bool vis = wac_hint_visible() && !s_vol_on;

    if (!vis) {
        if (!lv_obj_has_flag(s_hint, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
            s_hint_last[0] = '\0';
            apply_time_pos();
        }
        return;
    }

    if (strcmp(s_hint_last, g_state.hint) != 0) {
        snprintf(s_hint_last, sizeof(s_hint_last), "%s", g_state.hint);
        lv_label_set_text(s_hint, s_hint_last);
    }

    if (lv_obj_has_flag(s_hint, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_remove_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        apply_time_pos();
    }
}

/* ------------------------------------------------------- 音量调节模式
 *
 * 时钟页按确认键进：底部出现音量条，方向键 ±5，再按确认（或 BACK）消失 ——
 * 跟播放页音量条上的调节态是同一套机制（focus 的 EDITED 状态），
 * 只是这页没有可聚焦的音量控件，所以"确认键"直接进，不看焦点落在哪。
 *
 * 为什么不让确认键继续"点迷你条 → 进播放页"：迷你条只在播放时才有，
 * 平时那一下确认键等于什么也没有；改成调音量正好补上最常用的那个操作。
 * 想进播放页仍然可以触摸点整屏。
 */
static void vol_mode_apply(void)
{
    s_vol_on = focus_in_edit();

    if (s_volrow) {
        if (s_vol_on) {
            if (s_volbar) {
                lv_slider_set_value(s_volbar, g_state.volume_known ? (int32_t)g_state.volume : 0,
                                    LV_ANIM_OFF);
            }
            lv_obj_remove_flag(s_volrow, LV_OBJ_FLAG_HIDDEN);
        }
        else {
            lv_obj_add_flag(s_volrow, LV_OBJ_FLAG_HIDDEN);
        }
    }

    refresh_hint(); /* 提示行跟音量条抢同一条，二选一 */
    apply_time_pos();
}

/* 调节态里的方向键：dir<0（PREV/上/左）= 加，dir>0 = 减，跟播放页一致。
   dir==0 表示刚进/刚出调节态，只刷一次显示。 */
static void vol_edit_step(int dir)
{
    if (dir != 0) {
        cmd_vol_step(dir > 0 ? -1 : +1);
        /* 乐观更新：Go 那侧会丢连发命令，UI 不能等回执 */
        if (s_volbar) lv_slider_set_value(s_volbar, (int32_t)g_state.volume, LV_ANIM_OFF);
    }
    vol_mode_apply();
}

static void clock_activate(lv_obj_t *obj)
{
    LV_UNUSED(obj);
    focus_edit_support(vol_edit_step);
    if (!focus_enter_edit()) vol_mode_apply();
}

bool page_clock_vol_mode(void)
{
    return s_vol_on;
}

static void page_clock_refresh(void)
{
    time_t    now = time(NULL);
    struct tm tm_now;
    wac_localtime(now, &tm_now);

    if (tm_now.tm_sec != s_last_sec) {
        s_last_sec = tm_now.tm_sec;

        /* 12 小时制：0 点显示 12，并去掉前导 0（"6:05" 而不是 "06:05"）。
           屏幕太窄，字数少一个反而更清爽；label 宽度是按 "00:00:00" 预留的，
           文字居中，所以位数变化不会让整块左右晃。 */
        int hh = tm_now.tm_hour;
        if (!g_set.hour24) {
            hh %= 12;
            if (hh == 0) hh = 12;
        }

        char buf[16];
        if (g_set.hour24) {
            if (g_set.show_seconds) {
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hh, tm_now.tm_min, tm_now.tm_sec);
            }
            else {
                snprintf(buf, sizeof(buf), "%02d:%02d", hh, tm_now.tm_min);
            }
        }
        else {
            if (g_set.show_seconds) {
                snprintf(buf, sizeof(buf), "%d:%02d:%02d", hh, tm_now.tm_min, tm_now.tm_sec);
            }
            else {
                snprintf(buf, sizeof(buf), "%d:%02d", hh, tm_now.tm_min);
            }
        }
        lv_label_set_text(s_time, buf);
    }

    /* 日期/星期只在跨天时变，但每秒重设成本极低（字符串相同 LVGL 会跳过重绘） */
    char d[24], w[16];
    if (g_tk.bp == WAC_BP_TINY) {
        snprintf(d, sizeof(d), "%02d/%02d", tm_now.tm_mon + 1, tm_now.tm_mday);
    }
    else {
        snprintf(d, sizeof(d), "%d月%02d日", tm_now.tm_mon + 1, tm_now.tm_mday);
    }
    snprintf(w, sizeof(w), "%s", WEEK_CN[tm_now.tm_wday]);
    lv_label_set_text(s_date, d);
    lv_label_set_text(s_week, w);

    lv_label_set_text(s_weather_top, g_state.weather_ok ? g_state.weather : "--");

    refresh_battery();
    refresh_mini();
    refresh_hint();
}

void page_clock_settings_changed(void)
{
    clock_relayout();
    refresh_battery();
    page_clock_refresh();
}

static void mini_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    if (swipe_blocks_click()) return;
    page_push(WAC_PAGE_PLAYER);
}

/* 触摸：时钟页整屏可点，点一下就进播放页（迷你条自己也是可点的） */
static void clock_tapped(lv_event_t *e)
{
    LV_UNUSED(e);
    if (swipe_blocks_click()) return;
    page_push(WAC_PAGE_PLAYER);
}

/* 底部音量条的触摸：拖动过程中只改本地值，松手才发命令
   （Go 的 stdin 循环遇到缓冲里还有命令会整批丢掉） */
static void vol_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_VALUE_CHANGED) {
        g_state.volume       = (uint8_t)lv_slider_get_value(s_volbar);
        g_state.volume_known = true;
        ui_set_note_volume(g_state.volume);
    }
    else if (code == LV_EVENT_RELEASED) {
        if (swipe_blocks_click()) return;
        wac_cmd("vol %d", (int)lv_slider_get_value(s_volbar));
    }
}

static void on_show(void)
{
    focus_use(&s_focus);
    /* 确认键：不是"点迷你条"，而是进音量调节模式 */
    focus_set_activate(clock_activate);

    /* 每次进页都把调节态收掉：底部还回迷你条 / 提示行 */
    s_vol_on = false;
    if (s_volrow) lv_obj_add_flag(s_volrow, LV_OBJ_FLAG_HIDDEN);

    page_clock_refresh();
}

static void timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    page_clock_refresh();
}

static void build(lv_obj_t *root)
{
    const int32_t W = WAC_SCR_W;

    /* 顶部/底部这些"次要文字"跟着分辨率走：480×320 上以前只有中间的大数字
       被放大了，日期行和迷你条还停在 16px，跟设置页/播放页一比就显小。
       480×320（MEDIUM）用 24，跟设置页列表同一个字号。 */
    const int32_t fs_top = tk_pick(16, 20, 24, 28);

/* ---------- 顶部状态行 ---------- */
    /* 字号跟底部迷你条对齐。160 宽的屏上这四项 16px 一行放不下，
       所以按"最坏情况（电量显示着 + 最长天气）"量一遍：
         放得下 -> 一行：[电量][日期][星期][天气]
         放不下 -> 两行：[日期 ......][星期]
                          [电量 ......][天气]
       行数决定 status_h，时钟的居中区间跟着算。 */
    const lv_font_t *f_top = ui_font_cjk(fs_top);
    const int32_t    lh    = lv_font_get_line_height(f_top);
    const int32_t    inner = W - (g_tk.pad + 2) * 2;

    {
        char ds[24];
        if (g_tk.bp == WAC_BP_TINY) {
            snprintf(ds, sizeof(ds), "%d/%02d", 11, 9);
        }
        else {
            snprintf(ds, sizeof(ds), "%d月%02d日", 11, 9);
        }

        const char *batt_s    = "100%";
        const char *week_s    = "周一";
        const char *weather_s = "多云 22°C";
        const int32_t gap     = g_tk.pad;

        int32_t w_weather = lv_text_get_width(weather_s, (uint32_t)strlen(weather_s), f_top, 0);
        if (w_weather > W * 2 / 5) w_weather = W * 2 / 5; /* 天气超了会自己省略 */

        int32_t need = lv_text_get_width(batt_s, (uint32_t)strlen(batt_s), f_top, 0)
                     + lv_text_get_width(ds, (uint32_t)strlen(ds), f_top, 0)
                     + lv_text_get_width(week_s, (uint32_t)strlen(week_s), f_top, 0)
                     + w_weather
                     + gap * 3;

        const bool two = (need > inner);

        /* 两行时总数不超过屏高的 1/3，否则挤得时钟没地方了（超大字号屏除外） */
        s_status_h = two ? lh * 2 : lh;

        WAC_LOG("clock: top font=%d line_h=%d need=%d inner=%d -> %s (status_h=%d)", (int)fs_top,
                (int)lh, (int)need, (int)inner, two ? "two rows" : "one row", (int)s_status_h);

        lv_obj_t *top = lv_obj_create(root);
        lv_obj_remove_style_all(top);
        lv_obj_set_size(top, W, s_status_h);
        lv_obj_set_pos(top, 0, 0);
        lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_SPACE_EVENLY);
        lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

        /* 一行的容器：内部靠 SPACE_BETWEEN 左右分开。
           中间塞一个 grow 的占位，这样某一项隐藏时另一端不会被拉过来。 */
        lv_obj_t *r1 = lv_obj_create(top);
        lv_obj_remove_style_all(r1);
        lv_obj_set_size(r1, W, lh);
        lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r1, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_left(r1, g_tk.pad + 2, 0);
        lv_obj_set_style_pad_right(r1, g_tk.pad + 2, 0);
        lv_obj_set_style_pad_column(r1, gap, 0);
        lv_obj_remove_flag(r1, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *r2 = NULL;
        if (two) {
            r2 = lv_obj_create(top);
            lv_obj_remove_style_all(r2);
            lv_obj_set_size(r2, W, lh);
            lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(r2, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_left(r2, g_tk.pad + 2, 0);
            lv_obj_set_style_pad_right(r2, g_tk.pad + 2, 0);
            lv_obj_set_style_pad_column(r2, gap, 0);
            lv_obj_remove_flag(r2, LV_OBJ_FLAG_SCROLLABLE);
        }

        /* 状态行原来是灰的（COL_DIM），在 160×128 的小屏上远看糊成一片，
           统一改成正文白（COL_TEXT），只靠字号和位置区分层级。 */

        if (two) {
            s_date = lv_label_create(r1);
            lv_obj_set_style_text_font(s_date, f_top, 0);
            lv_obj_set_style_text_color(s_date, COL_TEXT, 0);
            lv_label_set_text(s_date, "--");

            lv_obj_t *sp1 = lv_obj_create(r1); /* 撑开：日期在左、星期在右 */
            lv_obj_remove_style_all(sp1);
            lv_obj_set_height(sp1, 1);
            lv_obj_set_flex_grow(sp1, 1);
            lv_obj_remove_flag(sp1, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(sp1, LV_OBJ_FLAG_CLICKABLE);

            s_week = lv_label_create(r1);
            lv_obj_set_style_text_font(s_week, f_top, 0);
            lv_obj_set_style_text_color(s_week, COL_TEXT, 0);
            lv_label_set_text(s_week, "");

            s_batt = lv_label_create(r2);
            lv_obj_set_style_text_font(s_batt, f_top, 0);
            lv_obj_set_style_text_color(s_batt, COL_TEXT, 0);
            lv_label_set_text(s_batt, "");
            lv_obj_add_flag(s_batt, LV_OBJ_FLAG_HIDDEN);

            lv_obj_t *sp2 = lv_obj_create(r2); /* 撑开：电量在左、天气在右 */
            lv_obj_remove_style_all(sp2);
            lv_obj_set_height(sp2, 1);
            lv_obj_set_flex_grow(sp2, 1);
            lv_obj_remove_flag(sp2, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(sp2, LV_OBJ_FLAG_CLICKABLE);

            s_weather_top = lv_label_create(r2);
            lv_obj_set_style_text_font(s_weather_top, f_top, 0);
            lv_obj_set_style_text_color(s_weather_top, COL_TEXT, 0);
            lv_obj_set_style_max_width(s_weather_top, W * 2 / 5, 0);
            lv_label_set_long_mode(s_weather_top, LV_LABEL_LONG_DOT);
            lv_label_set_text(s_weather_top, "--");
        }
        else {
            s_batt = lv_label_create(r1);
            lv_obj_set_style_text_font(s_batt, f_top, 0);
            lv_obj_set_style_text_color(s_batt, COL_TEXT, 0);
            lv_label_set_text(s_batt, "");
            lv_obj_add_flag(s_batt, LV_OBJ_FLAG_HIDDEN);

            s_date = lv_label_create(r1);
            lv_obj_set_style_text_font(s_date, f_top, 0);
            lv_obj_set_style_text_color(s_date, COL_TEXT, 0);
            lv_label_set_text(s_date, "--");

            s_week = lv_label_create(r1);
            lv_obj_set_style_text_font(s_week, f_top, 0);
            lv_obj_set_style_text_color(s_week, COL_TEXT, 0);
            lv_label_set_text(s_week, "");

            s_weather_top = lv_label_create(r1);
            lv_obj_set_style_text_font(s_weather_top, f_top, 0);
            lv_obj_set_style_text_color(s_weather_top, COL_TEXT, 0);
            lv_obj_set_style_max_width(s_weather_top, W * 2 / 5, 0);
            lv_label_set_long_mode(s_weather_top, LV_LABEL_LONG_DOT);
            lv_label_set_text(s_weather_top, "--");
        }
    }

    /* ---------- 时间 ---------- */
    /* 宽度几乎给满：这块屏上数字是被宽度卡住的，每多给 1px 宽，字就高 0.33px */
    s_avail_w = W - 2;
    s_time    = lv_label_create(root);
    lv_obj_set_style_text_color(s_time, COL_TEXT, 0);
    lv_obj_set_style_text_align(s_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_time, "00:00:00");
    clock_relayout();

    /* 天气只在顶部状态行右侧显示一处。原来这里还有一行居中的天气描述，
       但顶部那格早就能放下天气了，两处同源同文案，等于重复画一遍。 */

    /* ---------- 迷你播放条 ---------- */
    s_mini = lv_obj_create(root);
    lv_obj_remove_style_all(s_mini);
    /* 四周各留 2px：焦点框画在盒子外面，贴边会被屏幕裁掉只剩一条线 */
    lv_obj_set_size(s_mini, W - 4, WAC_SCR_H - g_tk.mini_top - 2);
    lv_obj_set_pos(s_mini, 2, g_tk.mini_top);
    lv_obj_set_flex_flow(s_mini, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_mini, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(s_mini, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_right(s_mini, g_tk.pad + 2, 0);
    lv_obj_set_style_pad_column(s_mini, g_tk.pad, 0);
    lv_obj_remove_flag(s_mini, LV_OBJ_FLAG_SCROLLABLE);

    s_mini_icon = lv_label_create(s_mini);
    /* 同上：图标走内置符号字体，不依赖中文字体的符号覆盖 */
    lv_obj_set_style_text_font(s_mini_icon,
                               g_tk.bp == WAC_BP_TINY  ? &lv_font_montserrat_14
                               : g_tk.bp == WAC_BP_SMALL ? &lv_font_montserrat_20
                                                          : &lv_font_montserrat_28,
                               0);
    lv_obj_set_style_text_color(s_mini_icon, COL_TEXT, 0);
    lv_label_set_text(s_mini_icon, LV_SYMBOL_PLAY);

    s_mini_title = lv_label_create(s_mini);
    lv_obj_set_style_text_font(s_mini_title, ui_font_cjk(fs_top), 0);
    lv_obj_set_style_text_color(s_mini_title, COL_TEXT, 0);
    lv_obj_set_flex_grow(s_mini_title, 1);
    lv_label_set_long_mode(s_mini_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(s_mini_title, WAC_DEF_TITLE);

    lv_obj_add_flag(s_mini, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_mini, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_mini, mini_clicked, LV_EVENT_CLICKED, NULL);

    /* 迷你播放条是时钟页唯一的焦点项：给个圆角 + 统一焦点框（#1282da） */
    lv_obj_set_style_radius(s_mini, 4, 0);
    theme_focus_style(s_mini);

    focus_list_init(&s_focus);
    focus_list_add(&s_focus, s_mini);

    /* ---------- 底部提示行（ECHO / WEATHERAL 的文字） ----------
       占的就是底部那一条（迷你条的高度），没播放时才出现，两者不会同时在。
       字号按这条的高度定，宽度用跑马灯兜住长文本。 */
    {
        const int32_t strip = WAC_SCR_H - g_tk.mini_top;
        int32_t       fs    = strip - 10;
        if (fs < 12) fs = 12;
        if (fs > 22) fs = 22;

        s_hint = lv_label_create(root);
        lv_obj_set_style_text_font(s_hint, ui_font_cjk(fs), 0);
        lv_obj_set_style_text_color(s_hint, COL_TEXT, 0);
        lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(s_hint, W - 6);
        lv_label_set_long_mode(s_hint, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_label_set_text(s_hint, "");
        lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    }

    /* ---------- 音量条（确认键进出的调节模式） ----------
       画在底部那条上（迷你条 / 提示行同一块位置），三条互斥。
       高度按这条的高度取一半左右：小屏 32px 的条里放 13px 的轨道，
       滑块头再溢出 2px，焦点/边框都还有地方。 */
    {
        const int32_t strip = WAC_SCR_H - g_tk.mini_top;
        int32_t       vol_h = strip / 2 - 3;
        if (vol_h < 6) vol_h = 6;
        if (vol_h > 26) vol_h = 26;

        s_volrow = lv_obj_create(root);
        lv_obj_remove_style_all(s_volrow);
        lv_obj_set_size(s_volrow, W - 6, vol_h + 8);
        lv_obj_align(s_volrow, LV_ALIGN_BOTTOM_MID, 0, -2);
        lv_obj_set_flex_flow(s_volrow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(s_volrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(s_volrow, 3, 0);
        lv_obj_set_style_pad_left(s_volrow, 3, 0);
        lv_obj_remove_flag(s_volrow, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *vic = lv_label_create(s_volrow);
        lv_obj_set_style_text_font(vic,
                                   g_tk.bp == WAC_BP_TINY ? &lv_font_montserrat_14
                                                          : &lv_font_montserrat_20,
                                   0);
        lv_obj_set_style_text_color(vic, COL_DIM, 0);
        lv_label_set_text(vic, LV_SYMBOL_VOLUME_MID);
        lv_obj_remove_flag(vic, LV_OBJ_FLAG_CLICKABLE);

        s_volbar = lv_slider_create(s_volrow);
        lv_obj_set_height(s_volbar, vol_h);
        lv_obj_set_flex_grow(s_volbar, 1);
        lv_slider_set_range(s_volbar, 0, 100);
        lv_slider_set_value(s_volbar, g_state.volume_known ? g_state.volume : 0, LV_ANIM_OFF);

        lv_obj_set_style_bg_color(s_volbar, COL_DIMMER, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(s_volbar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(s_volbar, vol_h / 2, LV_PART_MAIN);
        lv_obj_set_style_bg_color(s_volbar, COL_FOCUS, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(s_volbar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(s_volbar, vol_h / 2, LV_PART_INDICATOR);

        const int32_t knob = vol_h + 4;
        lv_obj_set_style_width(s_volbar, knob, LV_PART_KNOB);
        lv_obj_set_style_height(s_volbar, knob, LV_PART_KNOB);
        lv_obj_set_style_radius(s_volbar, LV_RADIUS_CIRCLE, LV_PART_KNOB);
        lv_obj_set_style_bg_color(s_volbar, COL_TEXT, LV_PART_KNOB);
        lv_obj_set_style_bg_opa(s_volbar, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_pad_all(s_volbar, 0, LV_PART_KNOB);

        /* 触摸也能拖：热区上下放大一点，条本身只有十几个像素高 */
        lv_obj_set_ext_click_area(s_volbar, 8);
        lv_obj_add_event_cb(s_volbar, vol_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_add_event_cb(s_volbar, vol_event_cb, LV_EVENT_RELEASED, NULL);

        lv_obj_add_flag(s_volrow, LV_OBJ_FLAG_HIDDEN);
    }

    /* 整屏可点：没播放时迷你条是隐藏的，屏幕上就没有任何可点对象了 */
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, clock_tapped, LV_EVENT_CLICKED, NULL);

    page_clock_refresh();

    lv_timer_create(timer_cb, 200, NULL);
}

void page_clock_register(void)
{
    page_register(WAC_PAGE_CLOCK, build, on_show);
}
