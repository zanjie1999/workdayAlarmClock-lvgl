/**
 * keymap.h -- 键位语义（由"绑到了几个键"自动推导 1~6 键模式）
 */
#ifndef WAC_KEYMAP_H
#define WAC_KEYMAP_H

#include "wac.h"
#include "indev_keys.h"

void nav_init(void);
void nav_dispatch(key_role_t role, key_kind_t kind);

/* 当前模式：1..6（5 键=无返回，6 键=有独立返回） */
int nav_mode(void);

/* 按页面环切页：dir = +1 下一张（时钟→播放→设置），-1 上一张。
 * 按键走到焦点尽头时内部会调它，触摸左右滑动也直接调它。 */
void nav_page_dir(int dir);

#endif /* WAC_KEYMAP_H */
