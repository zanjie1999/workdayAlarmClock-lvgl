/**
 * disp_fbdev.c -- 真机 /dev/fb0 后端
 *
 * 两种面板方向都支持，靠 CMake 的 WAC_ROT 选：
 *   rot 0   ：横装（rns 音箱，480x320 / 32bpp / line 1920），逻辑行=物理行，直写
 *   rot 90  ：竖装（aku / sun5i，面板 128x160，两页共 128x320），转置后用
 *             （照现有 timeDisplay.sh 的约定）
 *
 *     物理列 col = 逻辑 y
 *     物理行 row = (面板高 - 1) - 逻辑 x
 *
 * 三个坑，都是实测踩出来的：
 *   1. 物理行必须按"面板高"(s_ph = yres)，不能用 yres_virtual。aku 上是 320，
 *      用 320 会把整屏画进第二页，屏幕扫的是第一页 -> 全黑。
 *   2. 这台板子的 fb mmap 出来的内存跟 /dev/fb0 的 read/write 不是同一块
 *      （mmap 写进去，dd if=/dev/fb0 读不回来），所以一律用 pwrite 落盘。
 *      原来那个 timeDisplay.sh 也是 `> /dev/fb0` 直接写，这是唯一验证过能上屏的路径。
 *   3. 转置只能按 WAC_ROT 分支走。以前这里是写死 90° 转置的，rot=0 的面板
 *      也会被转 90°，屏幕上是竖排的字。
 *
 * 像素格式用 LV_COLOR_FORMAT_RGB565（对应 lv_conf 的颜色深度），写帧缓冲时
 * 再展开成 24bpp 的 B/G/R 或 32bpp 的 XRGB8888。
 *
 * 不做 page flip：一页足够。
 */
#include "wac.h"

#ifndef WAC_SIM

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define FB_DEV "/dev/fb0"

/* 一次 flush 的高度（像素）。越小内存越省，但调用次数多 */
#define FB_CHUNK_H 40

static int    s_fd = -1;
static size_t s_mem_len;  /* 一页的字节数，只用于清屏 */
static int    s_line;     /* 每行字节数（line_length） */
static int    s_bpp;      /* 位深：24 / 32 */
static int    s_ph;       /* 面板物理行数（= WAC_PANEL_H），逻辑 x 的最大值 */

static lv_display_t *s_disp;

static uint8_t s_drawbuf[WAC_SCR_W * FB_CHUNK_H * 4];

/* 一行物理像素的临时缓冲：横装面板一行可以有 WAC_SCR_W 个像素，竖装是 WAC_SCR_H 个 */
static uint8_t s_row[(WAC_SCR_W > WAC_SCR_H ? WAC_SCR_W : WAC_SCR_H) * 4];

/* ------------------------------------------------------------ 时间 */
static uint32_t tick_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

uint32_t wac_millis(void) { return tick_ms(); }

void wac_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ------------------------------------------------------------ 刷屏 */
/* RGB565 -> 帧缓冲像素。
   24bpp：全志 fb 是 little-endian 0x00RRGGBB，内存里就是 B,G,R 三个字节；
   32bpp：0xFFRRGGBB 写进去就是 XRGB8888 小端。 */
static uint8_t *put_px(uint8_t *dst, int px, uint16_t c)
{
    /* RGB565 -> 8bit，逐通道把高位补到低位，避免发暗 */
    uint32_t r = (c >> 11) & 0x1Fu;
    uint32_t g = (c >> 5) & 0x3Fu;
    uint32_t b = c & 0x1Fu;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);

    if (px == 3) {
        dst[0] = (uint8_t)b;
        dst[1] = (uint8_t)g;
        dst[2] = (uint8_t)r;
        return dst + 3;
    }
    *(uint32_t *)(void *)dst = 0xFF000000u | (r << 16) | (g << 8) | b;
    return dst + 4;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    /* 必须跟 LV_COLOR_DEPTH 16 / LV_DRAW_SW_SUPPORT_RGB565 对齐：
       lv_conf 里 XRGB8888 是关掉的，若这里声明 XRGB8888，软件渲染器会
       静默地"什么也不画"，flush 拿到的全是 0（排查过一次，别再踩）。 */
    const uint16_t *src = (const uint16_t *)(const void *)px_map;
    const int       px  = s_bpp / 8;
    const int32_t   w   = area->x2 - area->x1 + 1;

#if WAC_ROT == 90 || WAC_ROT == 270
    /* 竖装面板（aku）：逻辑 x 走物理行，逻辑 y 走物理列。
       一次 flush = 若干物理行，每行是"逻辑 y 方向"上连续的一段。 */
    for (int32_t x = area->x1; x <= area->x2; x++) {
#if WAC_ROT == 90
        const int     prow = (s_ph - 1) - (int)x;
        const int32_t col0 = area->y1;
#else
        const int     pcols = s_line / px; /* 物理面板一行有多少像素 */
        const int     prow = (int)x;
        const int32_t col0 = (pcols - 1) - area->y2;
#endif
        if (prow < 0 || prow >= s_ph) continue;

        const int32_t n_y = area->y2 - area->y1 + 1;
        uint8_t *dst = s_row;
        for (int32_t i = 0; i < n_y; i++) {
#if WAC_ROT == 90
            const uint16_t c = src[(size_t)i * (size_t)w + (size_t)(x - area->x1)];
#else
            const uint16_t c = src[(size_t)(n_y - 1 - i) * (size_t)w + (size_t)(x - area->x1)];
#endif
            dst = put_px(dst, px, c);
        }

        const size_t n = (size_t)n_y * (size_t)px;
        if (pwrite(s_fd, s_row, n, (off_t)prow * s_line + (off_t)col0 * px) != (ssize_t)n)
            WAC_LOG("fb: pwrite row %d failed (%d)", prow, errno);
    }
#else
    /* 横装面板（rns，rot 0 / 180）：逻辑行就是物理行，整行连续直写。
       （以前这里写死成 90° 转置，rot=0 的面板也会被转 90°，屏幕上是竖排字） */
    for (int32_t y = area->y1; y <= area->y2; y++) {
        const int prow = (WAC_ROT == 180) ? (s_ph - 1 - (int)y) : (int)y;
        if (prow < 0 || prow >= s_ph) continue;

        const uint16_t *s = src + (size_t)(y - area->y1) * (size_t)w;
        uint8_t *dst = s_row;
        for (int32_t i = 0; i < w; i++) {
            const int32_t j = (WAC_ROT == 180) ? (w - 1 - i) : i;
            dst = put_px(dst, px, s[j]);
        }

        const size_t n = (size_t)w * (size_t)px;
        const off_t  off = (off_t)prow * s_line + (off_t)area->x1 * px;
        if (pwrite(s_fd, s_row, n, off) != (ssize_t)n)
            WAC_LOG("fb: pwrite row %d failed (%d)", prow, errno);
    }
#endif

    lv_display_flush_ready(disp);
}

/* ------------------------------------------------------------ 初始化 */
static bool fb_open(void)
{
    s_fd = open(FB_DEV, O_RDWR);
    if (s_fd < 0) {
        WAC_LOG("fb: cannot open %s (%d)", FB_DEV, errno);
        return false;
    }

    struct fb_var_screeninfo vi;
    struct fb_fix_screeninfo fi;
    if (ioctl(s_fd, FBIOGET_VSCREENINFO, &vi) < 0 || ioctl(s_fd, FBIOGET_FSCREENINFO, &fi) < 0) {
        WAC_LOG("fb: ioctl failed (%d)", errno);
        return false;
    }

    s_bpp  = (int)vi.bits_per_pixel;
    s_line = (int)fi.line_length;
    /* 必须用 yres，不是 yres_virtual：yres_virtual 含第二页，用它算行号会画到看不见的页 */
    s_ph   = (int)vi.yres;

    WAC_LOG("fb: %ux%u (virtual %ux%u) %dbpp line=%d  want logical %dx%d (panel %dx%d rot %d)",
            vi.xres, vi.yres, vi.xres_virtual, vi.yres_virtual, s_bpp, s_line, (int)WAC_SCR_W,
            (int)WAC_SCR_H, (int)WAC_PANEL_W, (int)WAC_PANEL_H, (int)WAC_ROT);

    if (s_bpp != 24 && s_bpp != 32) {
        WAC_LOG("fb: unsupported %dbpp", s_bpp);
        return false;
    }
    if ((int)vi.xres < (int)WAC_PANEL_W || s_line <= 0 || s_ph < (int)WAC_PANEL_H) {
        WAC_LOG("fb: geometry mismatch (xres %u line %d yres %d)", vi.xres, s_line, s_ph);
        return false;
    }

    s_mem_len = (size_t)s_line * (size_t)s_ph;

    /* mmap 在这块板上写不到屏幕（见文件头注释 #2），这里只用 pwrite */
    return true;
}

lv_display_t *wac_disp_init(void)
{
    /* LVGL 的 tick：没有 OS 层时得自己喂，否则动画/定时器全停 */
    lv_tick_set_cb(tick_ms);

    if (!fb_open()) return NULL;

    /* 开机先把这一页刷黑：LVGL 只重画"失效区域"，fb 里原有的内容
       （上一个程序退出时留下的画面）会一直留在背景上，看着像残影。 */
    {
        static const uint8_t zero[4096];
        for (int r = 0; r < s_ph; r++) {
            const size_t n = (size_t)(s_line < (int)sizeof(zero) ? s_line : (int)sizeof(zero));
            if (pwrite(s_fd, zero, n, (off_t)r * s_line) < 0) break;
        }
    }

    s_disp = lv_display_create(WAC_SCR_W, WAC_SCR_H);
    if (!s_disp) {
        WAC_LOG("fb: lv_display_create failed");
        return NULL;
    }

    /* 帧缓冲是 24bpp，但 LVGL 渲染用 RGB565：
       这是 lv_conf.h 里 LV_COLOR_DEPTH=16 + LV_DRAW_SW_SUPPORT_RGB565 决定的，
       改这里必须同步改 lv_conf，否则渲染器会静默不画。 */
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_drawbuf, NULL, sizeof(s_drawbuf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(s_disp);

    WAC_LOG("fb: display ready %dx%d (page %d bytes, direct pwrite)", (int)WAC_SCR_W,
            (int)WAC_SCR_H, (int)s_mem_len);
    return s_disp;
}

void wac_disp_deinit(void)
{
    if (s_fd < 0) return;

    /* 退出时清一页，免得不留花屏 */
    static const uint8_t zero[384];
    for (int r = 0; r < s_ph; r++) {
        if (pwrite(s_fd, zero, (size_t)(s_line < 384 ? s_line : 384), (off_t)r * s_line) < 0) break;
    }

    close(s_fd);
    s_fd = -1;
}

#endif /* WAC_SIM */
