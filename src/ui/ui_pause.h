/**
 * ui_pause.h -- 把屏幕整个让给外部画面
 *
 * Go 那边的 /fb 是 MJPEG 长连接，会把帧直接写进 /dev/fb0（当投屏用）。
 * 这期间 LVGL 再刷就是两边互相覆盖、闪成一片，所以整个 UI 停摆：
 *
 *   - 暂停：lv_timer_enable(false)，所有 timer（时钟刷新、动画、indev 轮询）全停，
 *           一帧都不再写 fb0，屏幕完全交给 Go；
 *   - 恢复：lv_timer_enable(true) + 整屏 invalidate + lv_refr_now，
 *           立刻把 Go 停留的最后一帧盖掉，不用等下一个 timer。
 *
 * 暂停期间 LVGL 状态不动（时间不采样），恢复后第一帧就是当前时间。
 *
 * 触发源：Go 在 lvgl 模式下发的 FBSTART / FBEND 事件（见 docs/protocol.md）。
 */
#ifndef WAC_UI_PAUSE_H
#define WAC_UI_PAUSE_H

#include "wac.h"

/* 幂等：重复设同一个值什么都不做 */
void ui_pause_set(bool paused);

bool ui_pause_active(void);

#endif /* WAC_UI_PAUSE_H */
