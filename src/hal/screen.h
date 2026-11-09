/**
 * screen.h -- 熄屏 / 亮屏
 *
 * 熄屏 = 背光 0 + （没有背光节点时）fb blank。
 * 计划里的语义：
 *   ALARM  → 无条件亮屏 + 背光拉满
 *   WAKELOCK → 抑制自动熄屏（引用计数在 app_state 里）
 */
#ifndef WAC_SCREEN_H
#define WAC_SCREEN_H

#include "wac.h"

void screen_init(void);
void screen_on(void);
void screen_off(void);
bool screen_is_off(void);

#endif /* WAC_SCREEN_H */
