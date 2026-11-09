/**
 * bind_wizard.h -- 首次启动的按键绑定向导
 *
 * 严格照需求：「如果没这个按键就按确定」—— 跳过动作就绑在 CONFIRM 上，
 * 不需要额外约定键。绑到重复的键也当跳过处理，不报错。
 */
#ifndef WAC_BIND_WIZARD_H
#define WAC_BIND_WIZARD_H

#include "wac.h"

/* 全程占屏，结束时自动保存 wac_keys.conf 并消失 */
void wizard_start(void);
bool wizard_active(void);

/* 强制重绑（--bind 或开机按住键） */
void wizard_force_start(void);

#endif /* WAC_BIND_WIZARD_H */
