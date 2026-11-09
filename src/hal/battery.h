/**
 * battery.h -- 电量百分比
 *
 * Linux 下读 /sys/class/power_supply 里 type=Battery 的 capacity；
 * 读不到（台式机、OpenWrt 没电池节点）返回 -1，UI 就把电量整个隐藏。
 * 结果缓存 30s，避免每秒开文件。
 */
#ifndef WAC_BATTERY_H
#define WAC_BATTERY_H

#include "wac.h"

/* 0..100；-1 = 未知/没有电池 */
int battery_percent(void);

#endif /* WAC_BATTERY_H */
