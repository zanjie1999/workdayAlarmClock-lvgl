/**
 * ui_font.c -- FreeType 直读 TTF
 *
 * 所有文字（时钟数字 + 中文 + 符号）都来自两个 TTF：
 *   cjk  : 界面文字（默认 wac_cjk.ttf，缺省时找系统字体）
 *   clock: 时钟数字（zyymeclock.ttf，等宽；缺省时退回 cjk 字体）
 *
 * 字号是精确像素，不再有"档位"概念。FreeType 字体对象很贵（每个都要建
 * glyph 缓存节点），所以按像素值缓存，同尺寸只建一次。
 */
#include "ui_font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WAC_FREETYPE
/* lv_freetype.h 由 lvgl.h 在 LV_USE_FREETYPE=1 时自动包含（路径是 src/libs/freetype/） */
#endif

#define MAX_FACES 20

typedef struct {
    int32_t    px;
    lv_font_t *font;
    int32_t    lift; /* 该字号汉字的上下偏移量，建字体时算一次，之后只查表 */
} face_t;

static face_t s_cjk[MAX_FACES];
static int    s_cjk_n;

static face_t s_clk[MAX_FACES];
static int    s_clk_n;

static char s_cjk_path[260];
static char s_clk_path[260];
static bool s_ready;

/* 没有 TTF 时的兜底：Montserrat 只有 ASCII，中文会画成方框 */
#define FALLBACK_FONT (&lv_font_montserrat_14)

static bool file_readable(const char *p)
{
    if (!p || !*p) return false;
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* 依次试：环境变量 → 候选列表（当前目录 / 程序旁 assets / 系统字体目录） */
static bool pick_font(char *out, size_t cap, const char *env_name,
                      const char *const *names, int n)
{
    const char *e = getenv(env_name);
    if (file_readable(e)) {
        snprintf(out, cap, "%s", e);
        return true;
    }
    for (int i = 0; i < n; i++) {
        if (file_readable(names[i])) {
            snprintf(out, cap, "%s", names[i]);
            return true;
        }
    }
    return false;
}

/* 中文候选：桌面开发时直接吃 Windows 自带字体，真机上优先随程序带的那个。
   顺序原则：雅黑类无衬线 > 其他无衬线中文字体 > 楷体类兜底。
   rns 音箱的数据分区只有 17MB，塞不下 15MB 的雅黑，所以真机上退到系统
   自带的 Reeji-CloudKaiTi-GB.ttf（楷体，GB2312 字集，字形没那么好看但字都全）。 */
static const char *const CJK_CANDIDATES[] = {
    "wac_cjk.ttf",
    "assets/fonts/wac_cjk.ttf",
    "../assets/fonts/wac_cjk.ttf",
    "/usr/share/wac/wac_cjk.ttf",
    /* 微软雅黑：有就用，字形最好看 */
    "/usr/share/fonts/msyh.ttf",
    "/usr/share/fonts/truetype/msyh.ttf",
    "/usr/share/fonts/truetype/microsoft/msyh.ttf",
    "msyh.ttc",
    "C:/Windows/Fonts/msyh.ttc",
    /* 其他无衬线中文字体 */
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
    "C:/Windows/Fonts/simhei.ttf",
    /* 兜底：rns 音箱系统里就这一个中文字体 */
    "/usr/share/fonts/Reeji-CloudKaiTi-GB.ttf",
};

static const char *const CLK_CANDIDATES[] = {
    "wac_clock.ttf",
    "assets/fonts/zyymeclock.ttf",
    "../assets/fonts/zyymeclock.ttf",
    "/usr/share/wac/wac_clock.ttf",
};

/* 墨迹中心相对行框中心的下沉量（正=看着偏下）。
   LVGL 画字形（lv_draw_label.c）：y1 = label_top + (line_height - base_line) - box_h - ofs_y；
   行框居中时 label_top = 中心 - line_height/2，于是
       d = ink_top + box_h/2 - line_height/2
         = (line_height/2 - base_line) - (box_h/2 + ofs_y)。
   同一字体同一字号这个值固定，所以每个 face 只在建字体时算一次，之后全项目查表。 */
static int32_t ink_lift_calc(const lv_font_t *f, uint32_t cp)
{
    lv_font_glyph_dsc_t g;
    if (!f || !lv_font_get_glyph_dsc(f, &g, cp, 0)) return 0;
    if (g.is_placeholder) return 0; /* 字体里没这个字：度量不可信，宁可不提 */

    const int32_t ink_top = (f->line_height - f->base_line) - (int32_t)g.box_h - (int32_t)g.ofs_y;
    return ink_top + (int32_t)g.box_h / 2 - f->line_height / 2;
}

/* 两套字体各用一个代表字符：汉字用「中」；时钟字体里没有汉字，用「0」。
   列表指针本身就区分了族，不必再传参数。 */
static uint32_t lift_cp_of(const face_t *list)
{
    return (list == s_clk) ? (uint32_t)'0' : 0x4E2D;
}

static lv_font_t *face_get(face_t *list, int *n, const char *path, int32_t px)
{
    if (px < 6) px = 6;
    for (int i = 0; i < *n; i++) {
        if (list[i].px == px) return list[i].font;
    }
    if (*n >= MAX_FACES) return list[*n - 1].font; /* 缓存满了就复用最后一档 */

#ifdef WAC_FREETYPE
    lv_font_t *f = lv_freetype_font_create(path, LV_FREETYPE_FONT_RENDER_MODE_BITMAP,
                                          (uint32_t)px, LV_FREETYPE_FONT_STYLE_NORMAL);
    if (!f) {
        WAC_LOG("font: create failed %s @%dpx", path, (int)px);
        return NULL;
    }
#else
    LV_UNUSED(path);
    lv_font_t *f = (lv_font_t *)FALLBACK_FONT;
    LV_UNUSED(px);
#endif

    list[*n].px   = px;
    list[*n].font = f;
    list[*n].lift = ink_lift_calc(f, lift_cp_of(list)); /* 只在这里算一次 */
    (*n)++;

    WAC_LOG("font: %s @%dpx lift=%d", (list == s_clk) ? "clock" : "cjk", (int)px,
            (int)list[*n - 1].lift);
    return f;
}

void ui_font_init(const char *cjk_ttf, const char *clock_ttf)
{
    if (s_ready) return;
    s_ready = true;

    if (cjk_ttf && file_readable(cjk_ttf)) {
        snprintf(s_cjk_path, sizeof(s_cjk_path), "%s", cjk_ttf);
    }
    else {
        pick_font(s_cjk_path, sizeof(s_cjk_path), "WAC_CJK_TTF",
                  CJK_CANDIDATES, (int)(sizeof(CJK_CANDIDATES) / sizeof(CJK_CANDIDATES[0])));
    }

    if (clock_ttf && file_readable(clock_ttf)) {
        snprintf(s_clk_path, sizeof(s_clk_path), "%s", clock_ttf);
    }
    else {
        pick_font(s_clk_path, sizeof(s_clk_path), "WAC_CLOCK_TTF",
                  CLK_CANDIDATES, (int)(sizeof(CLK_CANDIDATES) / sizeof(CLK_CANDIDATES[0])));
    }

    if (!s_cjk_path[0]) {
        WAC_LOG("font: no CJK ttf found -> fallback Montserrat (Chinese will be boxes)");
    }
    else {
        WAC_LOG("font: cjk = %s", s_cjk_path);
    }
    if (!s_clk_path[0]) {
        /* 时钟字体缺失不算错：数字用中文体也能看，只是不是那套等宽数字 */
        snprintf(s_clk_path, sizeof(s_clk_path), "%s", s_cjk_path);
        if (s_clk_path[0]) WAC_LOG("font: no clock ttf -> reuse cjk");
    }
    else {
        WAC_LOG("font: clock = %s", s_clk_path);
    }
}

bool ui_font_has_ttf(void) { return s_cjk_path[0] != '\0'; }
const char *ui_font_cjk_path(void) { return s_cjk_path; }

const lv_font_t *ui_font_cjk(int32_t px)
{
    if (!s_cjk_path[0]) return FALLBACK_FONT;
    lv_font_t *f = face_get(s_cjk, &s_cjk_n, s_cjk_path, px);
    return f ? f : FALLBACK_FONT;
}

/* 查表：字体指针 → 建字体时算好的偏移量 */
static bool face_lift_find(const face_t *list, int n, const lv_font_t *f, int32_t *out)
{
    for (int i = 0; i < n; i++) {
        if (list[i].font == f) {
            *out = list[i].lift;
            return true;
        }
    }
    return false;
}

int32_t ui_font_ink_lift(const lv_font_t *f)
{
    int32_t d = 0;
    if (!f) return 0;
    if (face_lift_find(s_cjk, s_cjk_n, f, &d)) return d;
    if (face_lift_find(s_clk, s_clk_n, f, &d)) return d;
    return 0; /* 内置符号字体 / Montserrat 兜底：没有汉字度量，不动它 */
}

void ui_font_vcenter(lv_obj_t *obj)
{
    if (!obj) return;

    const lv_font_t *f = lv_obj_get_style_text_font(obj, 0);
    const int32_t    d = ui_font_ink_lift(f);

    /* 图标（符号字体）偏移量恒为 0，本来就在中间：别去碰它。 */
    if (d == 0 && lv_obj_get_style_translate_y(obj, 0) == 0) return;

    /* 但只要有过偏移就必须写：字号或字体改过之后旧值要清掉，不能靠 if (d) 跳过 */
    lv_obj_set_style_translate_y(obj, -d, 0);
}

int32_t ui_font_clock_text_w(const lv_font_t *f)
{
    if (!f) return 0;
    return lv_text_get_width("00:00:00", 8, f, 0);
}

/* 汉字墨迹高度（拿"中"量，不用数字：数字是半角，跟汉字不是一个 em 框） */
static int32_t cjk_ink_h(const lv_font_t *f)
{
    lv_font_glyph_dsc_t g;
    if (!f || !lv_font_get_glyph_dsc(f, &g, 0x4E2D /* 中 */, 0)) return 0;
    return g.box_h;
}

const lv_font_t *ui_font_cjk_fit_ink(int32_t box_h, int32_t pct, int32_t *out_px)
{
    if (out_px) *out_px = 0;
    if (box_h <= 0 || !s_cjk_path[0]) return ui_font_cjk(12);
    if (pct <= 0 || pct > 100) pct = 60;

    /* 墨迹高度跟像素值是线性的（同一字体），所以量一次 32px 就能按比例反算：
           px = box_h * pct% * 32 / ink32
       字号是从字体自身度量来的 —— 换雅黑/楷体、换分辨率都自动跟着走，
       不需要"120 用 16、320 用 24"这种表。 */
    lv_font_t *probe = face_get(s_cjk, &s_cjk_n, s_cjk_path, 32);
    if (!probe) return ui_font_cjk(12);

    int32_t ink32 = cjk_ink_h(probe);
    if (ink32 <= 0) ink32 = 24;

    int32_t px = (int32_t)((int64_t)box_h * pct * 32 / ((int64_t)ink32 * 100));
    if (px < 8) px = 8;

    /* 反算值通常刚好，但 hinting 会让墨迹偶尔多 1px：往下退两步兜住 */
    for (int i = 0; i < 3 && px > 8; i++, px--) {
        lv_font_t *f = face_get(s_cjk, &s_cjk_n, s_cjk_path, px);
        if (!f) break;
        if ((int64_t)cjk_ink_h(f) * 100 <= (int64_t)box_h * pct) {
            if (out_px) *out_px = px;
            WAC_LOG("font: fit_ink box=%d pct=%d ink32=%d -> %dpx (ink=%d)", (int)box_h, (int)pct,
                    (int)ink32, (int)px, (int)cjk_ink_h(f));
            return f;
        }
    }
    return face_get(s_cjk, &s_cjk_n, s_cjk_path, px > 8 ? px : 8);
}

/* 内置符号字体（LV_SYMBOL_*）是用位图烘焙好的，没有任意字号：只能从 lv_conf
 * 里开出来的那几档里就近取。取 ≤ px 的最大一档，比全取最小一档更不容易糊。 */
const lv_font_t *ui_font_symbol(int32_t px)
{
    static const struct {
        int32_t          px;
        const lv_font_t *f;
    } LADDER[] = {
        {12, &lv_font_montserrat_12}, {14, &lv_font_montserrat_14},
        {16, &lv_font_montserrat_16}, {20, &lv_font_montserrat_20},
        {28, &lv_font_montserrat_28}, {32, &lv_font_montserrat_32},
        {48, &lv_font_montserrat_48},
    };

    const int n = (int)(sizeof(LADDER) / sizeof(LADDER[0]));
    int       i = 0;
    while (i + 1 < n && LADDER[i + 1].px <= px) i++;
    return LADDER[i].f;
}

/* 数字字形墨迹高度（不含行距）。FreeType 的 line_height 通常比 px 大 20~30%，
   直接用 line_height 会把大字压小一大截。 */
static int32_t glyph_h(const lv_font_t *f)
{
    lv_font_glyph_dsc_t dsc;
    if (!f) return 0;
    if (!lv_font_get_glyph_dsc(f, &dsc, '0', 0)) return 0;
    return dsc.box_h;
}

const lv_font_t *ui_font_clock_fit(int32_t avail_w, int32_t avail_h,
                                   int32_t *out_w, int32_t *out_h)
{
    return ui_font_clock_fit_text("00:00:00", 8, avail_w, avail_h, out_w, out_h);
}

const lv_font_t *ui_font_clock_fit_text(const char *sample, int32_t len,
                                        int32_t avail_w, int32_t avail_h,
                                        int32_t *out_w, int32_t *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;

    if (!s_clk_path[0] || avail_w <= 0 || !sample || len <= 0) return FALLBACK_FONT;

    /* 用 32px 探一次：等宽数字的宽度/墨迹高度都与字号成正比，
       两个方向各按比例算一次，取小的那个，再往下微调。 */
    lv_font_t *probe = face_get(s_clk, &s_clk_n, s_clk_path, 32);
    if (!probe) return FALLBACK_FONT;

    int32_t w32 = lv_text_get_width(sample, (uint32_t)len, probe, 0);
    int32_t h32 = glyph_h(probe);
    if (w32 <= 0) return probe;

    int32_t px_w = (int32_t)((int64_t)32 * avail_w / w32);
    int32_t px_h = (h32 > 0 && avail_h > 0) ? (int32_t)((int64_t)32 * avail_h / h32) : px_w;

    int32_t px = px_w < px_h ? px_w : px_h;

    /* 上下限：太小看不清，太大直接把 LVGL 内存吃光 */
    int32_t px_max = (int32_t)(WAC_SCR_H * 0.80f);
    if (px_max < 12) px_max = 12;
    if (px > px_max) px = px_max;
    if (px < 10) px = 10;

    lv_font_t *f  = face_get(s_clk, &s_clk_n, s_clk_path, px);
    if (!f) return probe;

    int32_t w = lv_text_get_width(sample, (uint32_t)len, f, 0);
    int32_t h = glyph_h(f);

    /* 逐像素回退，直到宽和高都不越界（通常 0~2 次） */
    while ((w > avail_w || (avail_h > 0 && h > avail_h)) && px > 10) {
        px--;
        f = face_get(s_clk, &s_clk_n, s_clk_path, px);
        if (!f) break;
        w = lv_text_get_width(sample, (uint32_t)len, f, 0);
        h = glyph_h(f);
    }

    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
    return f;
}

const lv_font_t *ui_font_cjk_fit_text(const char *sample, int32_t len,
                                      int32_t avail_w, int32_t avail_h, int32_t *out_w)
{
    if (out_w) *out_w = 0;
    if (!sample || len <= 0 || avail_w <= 0) return ui_font_cjk(12);
    if (!s_cjk_path[0]) return FALLBACK_FONT;

    /* 同时钟字体：拿 32px 探一次，宽度与行高都跟字号成正比，按比例算再回退 */
    lv_font_t *probe = face_get(s_cjk, &s_cjk_n, s_cjk_path, 32);
    if (!probe) return ui_font_cjk(12);

    const int32_t w32  = lv_text_get_width(sample, (uint32_t)len, probe, 0);
    const int32_t lh32 = lv_font_get_line_height(probe);
    if (w32 <= 0) return probe;

    int32_t px = (int32_t)((int64_t)32 * avail_w / w32);
    if (avail_h > 0 && lh32 > 0) {
        const int32_t px_h = (int32_t)((int64_t)32 * avail_h / lh32);
        if (px_h < px) px = px_h;
    }
    if (px > 48) px = 48;
    if (px < 8) px = 8;

    /* 比例估算一般已经放得下，回退不超过 3 次，避免白白建一堆字体对象 */
    lv_font_t *f = NULL;
    for (int tries = 0; px >= 8 && tries < 4; tries++, px--) {
        f = face_get(s_cjk, &s_cjk_n, s_cjk_path, px);
        if (!f) break;
        const int32_t w = lv_text_get_width(sample, (uint32_t)len, f, 0);
        if (w <= avail_w && (avail_h <= 0 || lv_font_get_line_height(f) <= avail_h)) {
            if (out_w) *out_w = w;
            return f;
        }
    }
    return f ? f : ui_font_cjk(10);
}
