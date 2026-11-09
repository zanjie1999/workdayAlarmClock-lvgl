/**
 * ui_settings.h -- 界面选项（设置页里那几项）
 *
 * 只存"显示什么"，不存业务配置（闹钟/音量仍然由 Go 管）。
 * 落盘到 wac_ui.conf，格式是 key=0/1 的纯文本，手改也不会崩。
 */
#ifndef WAC_UI_SETTINGS_H
#define WAC_UI_SETTINGS_H

#include "wac.h"

typedef struct {
    bool show_seconds; /* 时钟显示秒；关掉后秒消失、字号会自动变大 */
    bool show_battery; /* 顶部状态行显示电量百分比 */
    bool vol_reverse;  /* 反转音量键方向（长按 PREV/NEXT 再加/减对调） */
    bool hour24;       /* 时间 24 小时制；关掉=12 小时制（默认关） */
    int  brightness;   /* 亮度 10..100（硬件背光，没有 sysfs 时不显示这条） */
    int  vol_cache;    /* 上一次的音量：开机第一屏先拿它回显，等 Go 报真值再覆盖 */
} ui_settings_t;

extern ui_settings_t g_set;

/* 读取配置；文件不存在或读失败就用默认值（只有"显示秒"默认勾选） */
void ui_set_load(const char *path);

/* 写回配置（改选项后调用） */
void ui_set_save(void);

/* 音量缓存：记下当前音量并标脏，1 秒内不再变才真正落盘
 * （音量键连按时不至于每按一下就写一次 flash）。 */
void ui_set_note_volume(int v);

/* 由 250ms 的状态定时器调用，负责把脏了的音量存下去 */
void ui_set_flush_volume(void);

#endif /* WAC_UI_SETTINGS_H */
