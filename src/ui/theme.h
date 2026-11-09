/**
 * theme.h -- 设计令牌：颜色 / 间距 / 字号 / 断点
 *
 * 自适应策略：不用百分比，全部由 min(宽,高) 推一个缩放系数 k，
 * 再按断点选布局骨架。换分辨率只重跑 theme_init() 并重建页面。
 */
#ifndef WAC_THEME_H
#define WAC_THEME_H

#include "wac.h"

typedef enum {
    WAC_BP_TINY = 0, /* ≤150：160×128 */
    WAC_BP_SMALL,    /* <280：320×240 */
    WAC_BP_MEDIUM,   /* <480 */
    WAC_BP_LARGE,    /* <720 */
    WAC_BP_XL,       /* ≥720：大屏 */
} wac_bp_t;

typedef struct {
    wac_bp_t bp;
    int32_t  w, h;
    int32_t  min_dim;

    int32_t k;        /* 缩放系数，定点 ×100（min_dim/240） */
    int32_t pad;      /* 4k  */
    int32_t gap;      /* 8k  */
    int32_t pad_lg;   /* 16k */
    int32_t touch_min;/* 最小热区 */
    bool    show_hint;/* 底部键位提示条 */

    /* 布局骨架 */
    int32_t status_h; /* 顶部日期/天气行高 */
    int32_t clock_top;/* 时间块 y */
    int32_t clock_h;  /* 时间块可用高（状态行下沿 → 迷你条上沿） */
    int32_t mini_top; /* 迷你播放条 y */
} wac_token_t;

extern wac_token_t g_tk;

void theme_init(void);

/* 以 240 为基准的等比缩放：tk_px(16) 在 160×128 上约为 10px */
static inline int32_t tk_px(int32_t base)
{
    return (base * g_tk.k + 50) / 100;
}

/* 按断点取尺寸/字号：tiny / small / medium / large+（XL 与 LARGE 共用一档）。
 * 以前各页面写的是"tiny ? a : (small ? b : c)"，导致 480×320 这种 MEDIUM 屏
 * 直接吃到大屏那一档（字号 24、按钮 96×80 → 一行放不下、数字被挤成竖排）。
 * 统一走这里，加一档只改一处。 */
static inline int32_t tk_pick(int32_t tiny, int32_t small, int32_t medium, int32_t large)
{
    switch (g_tk.bp) {
        case WAC_BP_TINY:   return tiny;
        case WAC_BP_SMALL:  return small;
        case WAC_BP_MEDIUM: return medium;
        default:            return large;
    }
}

/* ===== 颜色 ===== */
#define COL_BG     lv_color_hex(0x000000)
#define COL_TEXT   lv_color_hex(0xFFFFFF)
#define COL_DIM    lv_color_hex(0x9AA0A6)
#define COL_DIMMER lv_color_hex(0x5F6368)
#define COL_CARD   lv_color_hex(0x1C1C1E)
/* 焦点描边：#1282da（用户指定），在黑白底上都能看清 */
#define COL_FOCUS  lv_color_hex(0x1282DA)

/* 背景遮罩：固定 50%，写死不可调（见计划 §2.5） */
#define WAC_MASK_OPA LV_OPA_50

/* 统一的焦点视觉：2px 描边，画在对象内部（border）。
 * 不能用 outline —— 它画在盒子外面，会被父容器/屏幕边缘裁掉；
 * 也不能用 transform_scale（v9 一旦缩放就走离屏 layer，无 GPU 上是灾难）。 */
#define THEME_FOCUS_BW 2

/* 焦点框"可见"的开关位。
 *
 * 描边只在 LV_STATE_FOCUSED **且** 带这个自定义状态时才画 —— LVGL 的样式
 * 匹配要求 selector 上的状态位全部成立，所以少了它描边自动落回基础态的
 * 透明边框（宽度不变，布局不跳）。
 *
 * 用它的目的：触摸屏上手指是主角，选中框一直亮着反而脏；改成"手指一碰就
 * 收起来、按实体键再亮出来"，时钟页则无论如何都不亮。见 focus_set_visible()。
 */
#define THEME_ST_FOCUS_VIS LV_STATE_USER_1

void theme_focus_style(lv_obj_t *obj);

#endif /* WAC_THEME_H */
