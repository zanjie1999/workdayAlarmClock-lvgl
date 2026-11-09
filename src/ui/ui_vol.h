/**
 * ui_vol.h -- 音量浮层（挂在 lv_layer_top，避免被页面盖住）
 */
#ifndef WAC_UI_VOL_H
#define WAC_UI_VOL_H

#include "wac.h"

void ui_vol_init(void);
void ui_vol_poke(int vol); /* 显示并重新计时 1.5s */
void ui_vol_hide(void);

#endif /* WAC_UI_VOL_H */
