/**
 * ui_bright.h -- 亮度（软件调光 + 硬件背光）
 *
 * 目标板没有 /sys/class/backlight 也没有 pwm/leds 节点（查过，只有 disp/fb*），
 * 背光调不了。所以界面上这条"亮度"落在**软件调光**上：盖一层黑色遮罩改不透明度，
 * 屏幕整体变暗。硬件节点存在时（其他板子）顺带一起调，两边不冲突。
 */
#ifndef WAC_UI_BRIGHT_H
#define WAC_UI_BRIGHT_H

#include "wac.h"

/* 最低亮度：再暗就什么都看不见了，也留个"还活着"的反馈 */
#define WAC_BRIGHT_MIN 10

void ui_bright_init(void);

/* pct = 10..100。会同步 g_set.brightness 并调硬件背光（如果有） */
void ui_bright_set(int pct);

#endif /* WAC_UI_BRIGHT_H */
