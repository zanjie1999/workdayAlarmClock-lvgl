/**
 * battery.c -- 电量读取（sysfs）
 *
 * 和 Go 端 /status 里的做法一致：直接读一个文件
 *   /sys/class/power_supply/battery/capacity
 * 内容就是 0..100 的纯数字。读不到返回 -1，UI 就把电量整个隐藏。
 * 个别设备节点名不是 battery（比如 BAT0），用 WAC_BATTERY 环境变量覆盖。
 */
#include "battery.h"

#include <stdlib.h>

#define CACHE_MS     30000u
#define DEFAULT_PATH "/sys/class/power_supply/battery/capacity"

static int      s_val = -2; /* -2 = 还没读过 */
static uint32_t s_at;

static int read_now(void)
{
#ifdef _WIN32
    /* 模拟器没有电池节点：给个固定值，方便看效果 */
    return 76;
#else
    const char *path = getenv("WAC_BATTERY");
    if (!path || !*path) path = DEFAULT_PATH;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int  cap = -1;
    bool ok  = (fscanf(f, "%d", &cap) == 1);
    fclose(f);
    if (!ok || cap < 0) return -1;
    return cap > 100 ? 100 : cap;
#endif
}

int battery_percent(void)
{
    uint32_t now = wac_millis();
    if (s_val != -2 && (now - s_at) < CACHE_MS) return s_val;

    s_at  = now;
    s_val = read_now();
    if (s_val < 0) WAC_LOG("battery: no %s", DEFAULT_PATH);
    else WAC_LOG("battery: %d%%", s_val);
    return s_val;
}
