/**
 * event_parse.h -- Go 子进程 stdout 事件解析
 *
 * Go 往 stdout 打单行文本（协议见 docs/protocol.md），前缀必须带空格：
 *   ECHO <文本>            曲名 / 提示
 *   STOP PAUSE RESUME      播放状态
 *   PLAY <url>             开始播放（位置归零）
 *   SONGID <id>            曲目 id
 *   DUR <毫秒>            本曲总时长，0 = 未知（网络流没解析出来）
 *   POS <毫秒>             当前位置：lvgl 模式每秒一条
 *   VOL <0-100> VOLP VOLM  音量
 *   ALARM ALARMSTOP ALARMON ALARMOFF
 *   WEATHER <文本> WEATHERAL <文本>
 *   WAKELOCK HASUPDATE RESTART SCREENON SCREENOFF
 * 解析只改状态，不碰 UI（UI 由各自的 timer 拉取状态刷新）。
 */
#ifndef WAC_EVENT_PARSE_H
#define WAC_EVENT_PARSE_H

#include "wac.h"

/* 处理一行（不含换行）。未识别的行只在 debug 下打日志。 */
void event_handle_line(const char *line);

#endif /* WAC_EVENT_PARSE_H */
