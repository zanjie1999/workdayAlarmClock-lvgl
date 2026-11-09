/**
 * ui_alarm.h -- 响铃态
 *
 * Go 发 ALARM 到 ALARMSTOP 这段时间：
 *   - 界面强制在时钟页（不管刚才在哪一页）
 *   - 任意键、任意触摸都等于"停止"，不等长按
 *
 * 触摸靠一层盖在 top layer 上的透明遮罩接住：LVGL 的命中测试只认最上层，
 * 遮罩一点到就把点击吃掉，底下的时钟页/迷你条都收不到，不会"停完铃又跳去播放页"。
 */
#ifndef WAC_UI_ALARM_H
#define WAC_UI_ALARM_H

#include "wac.h"

/* 建遮罩（main 里在建完页面之前调） */
void ui_alarm_init(void);

/* Go 发来 ALARM：置响铃位、亮屏、盖遮罩、回时钟页 */
void ui_alarm_begin(void);

/* 用户按了任意键 / 点了屏幕：向 Go 发 stop，然后收尾 */
void ui_alarm_stop_by_user(void);

/* Go 说 ALARMSTOP：只收尾，不再回发命令 */
void ui_alarm_end(void);

bool ui_alarm_active(void);

#endif /* WAC_UI_ALARM_H */
