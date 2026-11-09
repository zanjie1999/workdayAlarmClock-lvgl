/**
 * cmd.h -- 向 Go 下发的命令
 */
#ifndef WAC_CMD_H
#define WAC_CMD_H

#include "wac.h"

/* 按键调音量的步长（触摸拖是连续的，这个只管方向键/调节态里的逐步调） */
#define CMD_VOL_STEP 5

void cmd_play_pause(void);
void cmd_next(void);
void cmd_prev(void);
void cmd_stop(void);
void cmd_vol(int delta);

/* 音量键的方向出口：dir>0 = 用户想"加"，dir<0 = "减"。
   设置页的"反转音量方向"在这里统一生效，所有键位调用点共用一份逻辑。 */
void cmd_vol_step(int dir);

void cmd_one_key(void);

/* 关机 / 重启：直接在本进程里跑系统命令（不走 Go，Go 没有这两条命令）。
   命令扔后台执行，立即返回；返回 0 表示命令已经起来了。
   默认按 busybox 的常见布局挨个试，可用环境变量
   WAC_POWEROFF_CMD / WAC_REBOOT_CMD 覆盖（换了板子不用重编）。 */
int cmd_power_off(void);
int cmd_reboot(void);

#endif /* WAC_CMD_H */
