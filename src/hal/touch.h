/**
 * touch.h -- 触摸 / 指针输入
 *
 * 两种后端各实现一份 wac_touch_init()：
 *   sim  : LVGL 自带 Windows 驱动的鼠标指针 indev（disp_sim.c）
 *   linux: 自写 evdev 触摸读取（indev_touch.c）
 * 没有触摸设备时返回 NULL，调用方要能容忍（只靠按键也能用）。
 */
#ifndef WAC_TOUCH_H
#define WAC_TOUCH_H

#include "wac.h"

lv_indev_t *wac_touch_init(lv_display_t *disp);

#endif /* WAC_TOUCH_H */
