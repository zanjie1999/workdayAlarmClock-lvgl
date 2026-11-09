/**
 * backlight.h -- 背光控制
 *
 * Linux 上没有统一的背光接口，回退链（计划 §5.4）：
 *   1. /sys/class/backlight/<name>/brightness   （标准）
 *   2. /sys/class/leds/<name>:backlight/brightness （小音箱常挂在 leds 上）
 *   3. /sys/class/gpio/gpioNN/value             （配置指定）
 *   4. 都没有 → 只做 fb blank
 * 模拟器上全部是空实现。
 */
#ifndef WAC_BACKLIGHT_H
#define WAC_BACKLIGHT_H

#include "wac.h"

/* 启动时探测一次，返回 0 表示找到了可写节点 */
int  backlight_init(void);

/* 0..100；同时按探测到的最大值做比例换算 */
void backlight_set(int percent);

int  backlight_get(void);

/* 是否真的能调亮度（找不到节点时为 false，此时熄屏只能靠 fb blank） */
bool backlight_available(void);

#endif /* WAC_BACKLIGHT_H */
