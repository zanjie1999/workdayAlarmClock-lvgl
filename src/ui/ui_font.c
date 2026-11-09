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

/* 中文候选：桌面开发时直接吃 Windows 自带字体，真机上优先随程序带的那个 */
static const char *const CJK_CANDIDATES[] = {
    "wac_cjk.ttf",
    "assets/fonts/wac_cjk.ttf",
    "../assets/fonts/wac_cjk.ttf",
    "/usr/share/wac/wac_cjk.ttf",
    "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansSC-Regular.otf",
    "C:/Windows/Fonts/simhei.ttf",
    "C:/Windows/Fonts/msyh.ttc",
};

static const char *const CLK_CANDIDATES[] = {
    "wac_clock.ttf",
    "assets/fonts/zyymeclock.ttf",
    "../assets/fonts/zyymeclock.ttf",
    "/usr/share/wac/wac_clock.ttf",
};

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
    (*n)++;
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

int32_t ui_font_clock_text_w(const lv_font_t *f)
{
    if (!f) return 0;
    return lv_text_get_width("00:00:00", 8, f, 0);
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
