/**
 * indev_touch.c -- 真机触摸（evdev 直读）
 *
 * 只做一件事：把触摸面板的原始坐标翻成 LVGL 逻辑坐标，喂给 POINTER indev。
 * 为什么不用 LVGL 自带的 evdev 驱动：它单设备单点、不支持 MT-B 协议，
 * 而且没法处理我们的"屏转 90°"映射（要在原始坐标上做，不是渲染后旋转）。
 *
 * 设备发现：WAC_TOUCH_DEV 指定，否则扫 /dev/input/event*，
 *          取同时具备 EV_ABS + ABS_X/ABS_Y（或 MT 轴）的第一个。
 *
 * 校准用的环境变量（面板坐标轴千奇百怪，先在真机上试出来再写死）：
 *   WAC_TOUCH_MAX_X / WAC_TOUCH_MAX_Y   原始量程，默认读 EVIOCGABS
 *   WAC_TOUCH_SWAP=1                   交换 X/Y
 *   WAC_TOUCH_INV_X=1 / WAC_TOUCH_INV_Y=1   反转单轴
 */
#include "touch.h"

#ifndef WAC_SIM

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int  s_fd = -1;
static char s_name[64];

static int  s_max_x = 4095;
static int  s_max_y = 4095;

static bool s_swap;
static bool s_inv_x;
static bool s_inv_y;

static bool s_down;
static int  s_raw_x, s_raw_y;   /* 当前原始点 */
static int  s_mt_x, s_mt_y;     /* MT 槽位最近一次的值 */
static int  s_log_x, s_log_y;   /* 逻辑点（LVGL 用） */

/* ------------------------------------------------------------ 坐标换算 */
static void remap(void)
{
    int rx = s_raw_x;
    int ry = s_raw_y;

    if (s_swap) {
        int t = rx;
        rx    = ry;
        ry    = t;
    }
    if (s_inv_x) rx = (s_swap ? s_max_y : s_max_x) - rx;
    if (s_inv_y) ry = (s_swap ? s_max_x : s_max_y) - ry;

    /* 原始 → 面板像素 */
    int px = (int)((int64_t)rx * WAC_PANEL_W / (s_max_x > 0 ? s_max_x : 1));
    int py = (int)((int64_t)ry * WAC_PANEL_H / (s_max_y > 0 ? s_max_y : 1));
    if (px < 0) px = 0;
    if (py < 0) py = 0;
    if (px > WAC_PANEL_W - 1) px = WAC_PANEL_W - 1;
    if (py > WAC_PANEL_H - 1) py = WAC_PANEL_H - 1;

    /* 面板像素 → 逻辑坐标：必须和 disp_fbdev 写帧时的旋转公式互为逆运算。
     * rot 90 : 逻辑图顺时针转 90° 铺到面板 → px = PW-1-ly, py = lx
     * rot 270: 逆时针 90°               → px = ly,      py = PH-1-lx
     * rot 180: 转 180°                  → px = PW-1-lx, py = PH-1-ly
     */
    int lx, ly;
#if WAC_ROT == 90
    lx = py;
    ly = WAC_PANEL_W - 1 - px;
#elif WAC_ROT == 270
    lx = WAC_PANEL_H - 1 - py;
    ly = px;
#elif WAC_ROT == 180
    lx = WAC_PANEL_W - 1 - px;
    ly = WAC_PANEL_H - 1 - py;
#else
    lx = px;
    ly = py;
#endif

    if (lx < 0) lx = 0;
    if (ly < 0) ly = 0;
    if (lx > WAC_SCR_W - 1) lx = WAC_SCR_W - 1;
    if (ly > WAC_SCR_H - 1) ly = WAC_SCR_H - 1;

    s_log_x = lx;
    s_log_y = ly;
}

/* ------------------------------------------------------------ 读事件 */
static void drain(void)
{
    if (s_fd < 0) return;

    struct input_event ev;
    while (read(s_fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        if (ev.type == EV_ABS) {
            switch (ev.code) {
                case ABS_X: s_raw_x = ev.value; break;
                case ABS_Y: s_raw_y = ev.value; break;
                case ABS_MT_POSITION_X: s_mt_x = ev.value; break;
                case ABS_MT_POSITION_Y: s_mt_y = ev.value; break;
                case ABS_MT_TRACKING_ID:
                    /* MT-B：-1 表示手指抬起，>=0 表示新触点 */
                    s_down = (ev.value >= 0);
                    break;
                default: break;
            }
            /* 单点面板没有 MT 轴，MT 值保持最后的原始点即可 */
            if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_MT_POSITION_Y) {
                s_raw_x = s_mt_x;
                s_raw_y = s_mt_y;
            }
        }
        else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
            s_down = (ev.value != 0);
        }
        else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
            if (s_mt_x >= 0) { /* 有 MT 轴就优先用 MT 坐标 */
                s_raw_x = s_mt_x;
                s_raw_y = s_mt_y;
            }
            remap();
        }
    }
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    drain();
    data->point.x = (int32_t)s_log_x;
    data->point.y = (int32_t)s_log_y;
    data->state   = s_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* ------------------------------------------------------------ 打开设备 */
static bool abs_has(int fd, int code)
{
    unsigned long bits[(ABS_CNT + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(bits)), bits) < 0) return false;
    return (bits[code / (8 * sizeof(unsigned long))] >> (code % (8 * sizeof(unsigned long)))) & 1UL;
}

static void read_range(int fd, int code, int *max)
{
    struct input_absinfo ai;
    if (ioctl(fd, EVIOCGABS(code), &ai) == 0 && ai.maximum > 0) *max = ai.maximum;
}

static bool try_open(const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) return false;

    bool has_xy  = abs_has(fd, ABS_X) && abs_has(fd, ABS_Y);
    bool has_mt  = abs_has(fd, ABS_MT_POSITION_X) && abs_has(fd, ABS_MT_POSITION_Y);
    if (!has_xy && !has_mt) {
        close(fd);
        return false;
    }

    s_fd = fd;
    ioctl(fd, EVIOCGNAME(sizeof(s_name)), s_name);

    s_max_x = 0;
    s_max_y = 0;
    read_range(fd, has_xy ? ABS_X : ABS_MT_POSITION_X, &s_max_x);
    read_range(fd, has_xy ? ABS_Y : ABS_MT_POSITION_Y, &s_max_y);
    if (s_max_x <= 0) s_max_x = 4095;
    if (s_max_y <= 0) s_max_y = 4095;

    /* 单点面板才有 ABS_X/Y 的稳定值；纯 MT 面板得从这里取初始点 */
    s_raw_x = s_max_x / 2;
    s_raw_y = s_max_y / 2;
    s_mt_x  = -1;
    s_mt_y  = -1;
    s_log_x = WAC_SCR_W / 2;
    s_log_y = WAC_SCR_H / 2;

    WAC_LOG("touch: %s (%s) max=%dx%d %s", path, s_name[0] ? s_name : "?", s_max_x, s_max_y,
            has_mt ? "MT" : "single");
    return true;
}

lv_indev_t *wac_touch_init(lv_display_t *disp)
{
    const char *env = getenv("WAC_TOUCH_DEV");
    if (env && *env) {
        if (!try_open(env)) {
            WAC_LOG("touch: open %s failed", env);
            return NULL;
        }
    }
    else {
        DIR *d = opendir("/dev/input");
        if (!d) {
            WAC_LOG("touch: no /dev/input");
            return NULL;
        }
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (strncmp(e->d_name, "event", 5) != 0) continue;
            char path[128];
            snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
            if (try_open(path)) break;
        }
        closedir(d);
    }

    if (s_fd < 0) {
        WAC_LOG("touch: no touch device, key-only mode");
        return NULL;
    }

    if (getenv("WAC_TOUCH_MAX_X")) s_max_x = atoi(getenv("WAC_TOUCH_MAX_X"));
    if (getenv("WAC_TOUCH_MAX_Y")) s_max_y = atoi(getenv("WAC_TOUCH_MAX_Y"));
    s_swap  = getenv("WAC_TOUCH_SWAP") && atoi(getenv("WAC_TOUCH_SWAP")) != 0;
    s_inv_x = getenv("WAC_TOUCH_INV_X") && atoi(getenv("WAC_TOUCH_INV_X")) != 0;
    s_inv_y = getenv("WAC_TOUCH_INV_Y") && atoi(getenv("WAC_TOUCH_INV_Y")) != 0;

    lv_indev_t *ind = lv_indev_create();
    if (!ind) return NULL;
    lv_indev_set_type(ind, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ind, read_cb);
    lv_indev_set_display(ind, disp);
    return ind;
}

#endif /* !WAC_SIM */
