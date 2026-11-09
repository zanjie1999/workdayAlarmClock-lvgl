/**
 * layer_bg.h -- 背景层：壁纸 + 50% 黑色遮罩
 *
 * 壁纸可选，纯黑是默认。挂在活动屏幕上（壁纸第 0 层 / 遮罩第 1 层），
 * 切页不重建，只在 bg_reload() 时换一次。详见 layer_bg.c 顶部说明
 * 和 docs/wallpaper.md。
 */
#ifndef WAC_LAYER_BG_H
#define WAC_LAYER_BG_H

#include "wac.h"

void bg_init(void);

/* 重新查找并装载壁纸；新图没找到就退回纯黑（旧图连同遮罩一起拆掉） */
void bg_reload(void);

#endif /* WAC_LAYER_BG_H */
