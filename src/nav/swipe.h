/**
 * swipe.h -- 触摸滑动导航
 *
 * LVGL 自带的 LV_EVENT_GESTURE 有一个写死的门槛：gesture_limit = 50px。
 * 128 宽的屏上要拖掉 40% 屏宽才算"滑动"，手感很差，而且这个值没有公开的 setter。
 * 所以这里自己按指针的按下点/当前点算位移，阈值按屏宽比例给：
 *   左右滑 → 切页（时钟 → 播放 → 设置 → 时钟）
 *   上下滑 → 页内焦点上下移动
 * 手指按在可滚动列表上拖动时，滚动归列表（scroll_obj 非空就直接让路）。
 */
#ifndef WAC_SWIPE_H
#define WAC_SWIPE_H

#include "wac.h"

void swipe_init(lv_indev_t *indev);

/* 本次按下已经被滑动消费掉 → 点击回调必须直接 return，
 * 否则"滑一下"会顺带把手指底下的按钮点掉（LVGL 松手时 scroll_obj 为空就发 CLICKED）。 */
bool swipe_blocks_click(void);

#endif /* WAC_SWIPE_H */
