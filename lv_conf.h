/**
 * lv_conf.h -- workdayAlarmClock LVGL 外壳配置（LVGL v9.3.0）
 *
 * 只覆盖需要的项，其余走 lv_conf_internal.h 的默认值。
 * 构建开关由 CMake 传入：
 *   WAC_SIM      Windows/GDI 模拟后端（LV_USE_WINDOWS=1）
 *   (未定义)      Linux 真机（自写 fbdev + evdev 触摸）
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   颜色与渲染
 *====================*/
#define LV_COLOR_DEPTH 16
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_SUPPORT_RGB565 1
#define LV_DRAW_SW_SUPPORT_RGB565_SWAPPED 0
#define LV_DRAW_SW_SUPPORT_ARGB8888 0
#define LV_DRAW_SW_SUPPORT_XRGB8888 0
#define LV_DRAW_SW_SUPPORT_RGB888 0
#define LV_DRAW_SW_SUPPORT_L8 0
#define LV_DRAW_SW_SUPPORT_AL88 0

/* FreeType 在绘制线程里栅格化字形，8KB 默认栈不够（LVGL 自己会 #warning），给 32KB */
#define LV_DRAW_THREAD_STACK_SIZE (32 * 1024)

/*====================
   内存
 *====================*/
/* 用 LVGL 内置 TLSF 池：嵌入式上比 libc 的 malloc 更可预测（不会碎片化增长）。
   FreeType 的字形位图缓存走这个池，512 条 + 状态栏/页面控件，128×160 上实测峰值约 260KB */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#ifdef WAC_SIM
    /* 模拟器要跑 800×1280 / 1024×600 并截图（一张 RGB565 快照 = W×H×2），池子给足 */
    #define LV_MEM_SIZE (4096 * 1024U)
#else
    #define LV_MEM_SIZE (768 * 1024U)
#endif
#define LV_USE_STDLIB_STRING LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_BUILTIN

/*====================
   HAL / 驱动
 *====================*/
#define LV_DEF_REFR_PERIOD 20 /* 无 GPU 上 50fps 上限，够用且省 CPU */

#ifdef WAC_SIM
    #define LV_USE_WINDOWS 1  /* 只依赖 GDI，MinGW 可编 */
    #define LV_USE_EVDEV 0
    /* v9.3 的 windows 驱动强制要求 LV_OS_WINDOWS（内部 lv_lock 用 Win32 临界区）。
       主循环也必须 lv_lock/lv_unlock 包住 lv_timer_handler。 */
    #define LV_USE_OS LV_OS_WINDOWS
#else
    #define LV_USE_WINDOWS 0
    #define LV_USE_EVDEV 1 /* 仅触摸走它；物理键由 indev_keys.c 自己 poll */
    #define LV_USE_OS LV_OS_NONE
#endif

#define LV_USE_LINUX_FBDEV 0 /* AKU 的 fb 是 128×320 双页，必须自写后端 */
#define LV_USE_LINUX_DRM 0
#define LV_USE_SDL 0
#define LV_USE_X11 0
#define LV_USE_WAYLAND 0

/* 开发期截屏（dev_shot.c）：只有 sim 会用到，真机上编译进去也不占多少空间 */
#define LV_USE_SNAPSHOT 1

/*====================
   日志与断言
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1
#define LV_LOG_USE_TIMESTAMP 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

/*====================
   控件与特性
 *====================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0
#define LV_USE_FONT_PLACEHOLDER 1
#define LV_USE_OBJ_ID 1
#define LV_USE_OBJ_NAME 1 /* 方便 --debug 时按名字找控件 */

/* 不用动画，但过渡需要 lv_anim（音量条淡出等）*/
#define LV_USE_ANIM 1

/*====================
   字体
 *====================*/
/* 中文与时钟都走 FreeType 直接读 TTF（任意字号、任意汉字），不再烘焙位图字体。
 * 内置 Montserrat 只在没有 TTF 时兜底 ASCII，以及给 LVGL 内部控件用。 */
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#ifdef WAC_FREETYPE
    #define LV_USE_FREETYPE 1
    #if LV_USE_FREETYPE
        /* 0 = 用 FreeType 自己的 malloc；1 = 走 LVGL 内存池。分开更省 LV_MEM_SIZE */
        #define LV_FREETYPE_USE_LVGL_PORT 0
        /* 字形位图缓存条数：16px 中文约 130B/字，96px 数字约 4.6KB/字。
           512 条在 160×128 上够用，内存峰值 ~200KB */
        #define LV_FREETYPE_CACHE_FT_GLYPH_CNT 512
    #endif
#else
    #define LV_USE_FREETYPE 0
#endif

#define LV_USE_BIDI 0
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*====================
   调试
 *====================*/
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#endif /* LV_CONF_H */
