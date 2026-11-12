/**
 * wac.h -- workdayAlarmClock LVGL 外壳的公共头
 *
 * 分层：hal（屏幕/背光/触摸/按键/壁纸）、proto（Go 子进程 + HTTP + 事件解析）、
 *       state（应用状态 + 命令下发）、nav（键位/焦点/向导）、ui（页面与控件）
 */
#ifndef WAC_H
#define WAC_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lvgl.h"

/* ===== 屏幕尺寸（CMake 按 -DPANEL 注入）===== */
#ifndef WAC_LCD_W
#error "WAC_LCD_W must be defined by CMake"
#endif
#ifndef WAC_LCD_H
#error "WAC_LCD_H must be defined by CMake"
#endif

#define WAC_SCR_W   ((int32_t)WAC_LCD_W) /* 逻辑宽（已含旋转） */
#define WAC_SCR_H   ((int32_t)WAC_LCD_H) /* 逻辑高 */
#define WAC_MIN_DIM (WAC_SCR_W < WAC_SCR_H ? WAC_SCR_W : WAC_SCR_H)

/* ===== 日志 ===== */
#define WAC_LOG(...)                                  \
    do {                                              \
        fprintf(stderr, "[wac] " __VA_ARGS__);        \
        fputc('\n', stderr);                          \
        fflush(stderr);                               \
    } while (0)

#ifdef WAC_SIM
    #define WAC_LOG_DEBUG(...) WAC_LOG(__VA_ARGS__)
#else
    #define WAC_LOG_DEBUG(...) \
        do {                   \
        } while (0)
#endif

/* ===== 时间（MinGW 没有 localtime_r，用 localtime_s 对应）===== */
static inline void wac_localtime(time_t t, struct tm *out)
{
#ifdef _WIN32
    localtime_s(out, &t);
#else
    localtime_r(&t, out);
#endif
}

/* ===== 平台 ===== */
void     wac_sleep_ms(uint32_t ms);
uint32_t wac_millis(void);

/* ===== 显示（sim / fbdev 各实现一份）===== */
lv_display_t *wac_disp_init(void);

/* 退出时恢复控制台（fbdev 用：Ctrl-C 后不留花屏） */
void wac_disp_deinit(void);

/* 投屏交接（Go 的 /fb 流要自己写屏时）：
   release 把 DRM master 让出去并停掉本进程的推帧，reclaim 收回来重画。
   非 DRM 后端是空实现。 */
void wac_disp_release(void);
void wac_disp_reclaim(void);


/* ===== 状态层 ===== */
/* 没有任何 ECHO 时的默认标题（时钟迷你条 / 播放页共用一份） */
#define WAC_DEF_TITLE "工作咩闹钟"

typedef enum {
    WAC_ST_STOP = 0,
    WAC_ST_PLAYING,
    WAC_ST_PAUSED,
} wac_play_state_t;

typedef struct {
    /* 播放 */
    wac_play_state_t state;
    char             title[128];   /* 来自 ECHO */
    char             now_url[256];
    char             now_id[64];
    int64_t          start_unix;   /* /status.startUnix */
    int32_t          duration_ms;  /* 未知 = 0 */
    uint32_t       progress_ms;    /* 当前播放位置 */
    /* 位置来源：Go（lvgl 模式）每秒发 POS，两次之间本地插值；
       没有 POS 的老后端退回用 start_unix 推算 */
    uint32_t         pos_base_ms;  /* 最后一个 POS 的值 */
    uint32_t         pos_at_ms;    /* 收到它时的 wac_millis() */
    bool             pos_valid;
    uint8_t          volume;       /* 0..100 */
    bool             volume_known;

    /* 天气 */
    char weather[64];              /* WEATHER 原文 */
    bool weather_ok;

    /* 闹钟 */
    bool ringing;                  /* ALARM ... ALARMSTOP */
    bool wakelock;

    /* 时钟页底部的提示行（停止播放时才显示）
     *   ECHO <文字>      -> 显示 10 秒
     *   WEATHERAL <文字> -> 天气预警，一直显示，直到收到一个不带内容的 WEATHERAL
     * 时间基准是 wac_millis()；hint_until == 0 表示不过期。 */
    char     hint[128];
    uint8_t  hint_kind;            /* wac_hint_kind_t */
    uint32_t hint_until;           /* 毫秒时间戳，0 = 永久 */

    /* Go 子进程 */
    int32_t go_pid;
    int32_t go_port;
    bool    go_alive;

    /* 杂项 */
    bool has_update;
    bool screen_off;
} wac_state_t;

extern wac_state_t g_state;

void wac_state_init(void);

/* 定时推进 progress_ms：有 POS 就插值，没有就按 start_unix 推算。
 * wac_state_init() 里会自动挂一个 250ms 定时器，页面只管读。 */
void wac_state_tick(void);

/* ===== 时钟页底部提示行 =====
 * 文字来自 Go 的 ECHO / WEATHERAL（见 docs/protocol.md）。 */
typedef enum {
    WAC_HINT_NONE = 0,
    WAC_HINT_ECHO,  /* 曲名/提示，ttl 到了自动消失 */
    WAC_HINT_ALERT, /* 天气预警文字，一直显示到显式清除 */
} wac_hint_kind_t;

/* 设一条提示。ttl_ms <= 0 表示不过期。 */
void wac_hint_set(wac_hint_kind_t kind, const char *text, uint32_t ttl_ms);

/* 清掉提示（只清指定种类时传 kind，全清传 WAC_HINT_NONE） */
void wac_hint_clear(wac_hint_kind_t kind);

/* 现在该不该在时钟页底部显示提示 —— 播放/暂停时不显示（底下是迷你条）。
 * 顺手处理过期，调用方不用自己算时间。 */
bool wac_hint_visible(void);

/* ===== 命令下发（写 Go 的 stdin；sim 下打到日志）===== */
void wac_cmd(const char *fmt, ...);

/* ===== 页面/UI ===== */
typedef enum {
    WAC_PAGE_CLOCK = 0,
    WAC_PAGE_PLAYER,
    WAC_PAGE_SETTINGS,
    WAC_PAGE_ALARM,
    WAC_PAGE_COUNT
} wac_page_id_t;

#endif /* WAC_H */
