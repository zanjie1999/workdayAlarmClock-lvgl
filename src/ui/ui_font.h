/**
 * ui_font.h -- 字体（FreeType 直读 TTF）
 *
 * 不再烘焙位图字体：中文和时钟数字都在运行时由 FreeType 从 TTF 渲染，
 * 任意汉字、任意字号，换字体只要换一个文件。
 *
 *   - 时钟大字：按可用宽度算出精确像素值，能占满就占满
 *   - UI 小字：给定像素就按给定渲染（12/14/16/20/24 都是常见档，但不限死）
 *
 * 找不到 TTF 时回落到 LVGL 内置 Montserrat（只有 ASCII），并打一条错误日志。
 */
#ifndef WAC_UI_FONT_H
#define WAC_UI_FONT_H

#include "wac.h"

/* 启动时调用一次（lv_init 之后）。
 * 两个参数都可以传 NULL，表示按 WAC_CJK_TTF / WAC_CLOCK_TTF 环境变量 →
 * 内置路径列表 的顺序去找。找不到也不算致命错误。 */
void ui_font_init(const char *cjk_ttf, const char *clock_ttf);

/* 按像素取中文字体；同尺寸会复用同一个 lv_font_t（FreeType 字体对象很贵，别每题都建） */
const lv_font_t *ui_font_cjk(int32_t px);

/* 在一段中英混排文本上选"刚好放得下"的最大中文字号。
 * avail_w = 可用宽度，avail_h = 可用行高（0 表示不限）；out_w 返回实测宽度。
 * 给顶部状态行用：多留几个字的位置，字号就能比写死 12px 大一截。 */
const lv_font_t *ui_font_cjk_fit_text(const char *sample, int32_t len, int32_t avail_w,
                                      int32_t avail_h, int32_t *out_w);

/* 选能放下 "00:00:00" 的最大时钟字号。
 * avail_w = 可用宽度，avail_h = 可用高度（数字墨迹高度，不是行高）；
 * out_w 返回实测宽度。out_h 可传 NULL。 */
const lv_font_t *ui_font_clock_fit(int32_t avail_w, int32_t avail_h,
                                   int32_t *out_w, int32_t *out_h);

/* 同上，但样本可指定：关掉秒显示时用 "00:00" 能拿到更大的字号 */
const lv_font_t *ui_font_clock_fit_text(const char *sample, int32_t len,
                                        int32_t avail_w, int32_t avail_h,
                                        int32_t *out_w, int32_t *out_h);

/* 当前时钟字体实测宽度（用于锁定 label 宽度，避免跳秒抖动） */
int32_t ui_font_clock_text_w(const lv_font_t *f);

/* 是否真的用上了 TTF（false = 回落 Montserrat，中文会显示成方块/空白） */
bool ui_font_has_ttf(void);

/* 实际加载的字体路径，供日志/排错 */
const char *ui_font_cjk_path(void);

#endif /* WAC_UI_FONT_H */
