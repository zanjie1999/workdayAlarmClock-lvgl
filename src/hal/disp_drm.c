/**
 * disp_drm.c -- 真机 DRM/KMS 后端（sprd SL8541E / 作业帮辅导机 / 展锐 9832E 这类板子）
 *
 * 为什么不用 /dev/fb0：
 *   这块板子（lcd_icnl9707_huiyi_mipi_hdp，DSI 命令模式）的 fb 只是 DRM 的
 *   fbdev 模拟层；weston 一停 CRTC 就是关的（/sys/class/drm/card0-DSI-1/enabled
 *   = disabled），往 fb0 里 pwrite 什么都不会上屏。实测只有在 DRM 里
 *   SETCRTC + PAGE_FLIP 才会真正推帧。
 *
 * 做法照 Go 版 display/drm_linux.go（已经在这台机器上验证过）：
 *   1. dumb buffer 一律按 DRM mode 的尺寸分配（600x1280），不是逻辑尺寸；
 *   2. 像素格式用 XRGB8888（XR24）—— plane 虽然也报 RG16，但那条路没验证过；
 *   3. 双缓冲 + DRM_MODE_PAGE_FLIP_EVENT，每次上屏前等上一帧 flip 完成；
 *   4. 这块屏物理是 1280x480 横屏，DSI 扫描却是竖的 600x1280（转 90°），
 *      可见区是扫描缓冲**中间**的 480 列（左右各 60 列被边框盖住）。所以逻辑
 *      画面 1280x480 顺时针转 90° 铺到 x=60..539，两边各留 60 列黑边。
 *      （对齐 displayPost 的 rotation=90，即 Rotate90CW：dst(x,y)=src(y,H-1-x)）
 *
 * LVGL 这边：LV_COLOR_DEPTH 仍是 16（RGB565，跟其它板子共用一套 UI 代码），
 * 用 DIRECT 模式渲染到一整块 1280x480x2 的 shadow buffer；一次刷新结束
 * （LV_EVENT_REFR_READY）再把 shadow 转成 XRGB8888、旋转、拷进后缓冲、翻页。
 *
 * 环境变量（排查用，正常不用设）：
 *   WAC_DRM_DEV   DRM 设备，默认 /dev/dri/card0
 *   WAC_DRM_565   1 = 试试 RGB565 扫描输出（免转换；不生效就退回 XRGB8888）
 *   WAC_DRM_ROT   逻辑画面转多少度进扫描缓冲：90(默认，CW) / 270(CCW) / 0
 *   WAC_DRM_X     逻辑画面在扫描缓冲里的列偏移，默认居中（(600-480)/2=60）
 */
#include "wac.h"

#ifndef WAC_SIM

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define DRM_DEV_DEFAULT "/dev/dri/card0"

/* 一帧 flip 最多等这么久；驱动不发事件时不能让界面卡死 */
#define FLIP_TIMEOUT_MS 200

typedef struct {
    uint32_t handle;
    uint32_t fb_id;
    uint32_t pitch;
    uint64_t size;
    uint8_t *map;
} drm_buf_t;

static int      s_fd = -1;
static uint32_t s_crtc_id;
static uint32_t s_conn_id;
static struct drm_mode_modeinfo s_mode;
static drm_buf_t s_buf[2];
static int       s_front;          /* 当前正在扫描输出的 buffer */
static bool      s_flip_pending;
static bool      s_use_565;        /* 扫描输出用 RGB565（否则 XRGB8888） */
static int       s_rot = 90;       /* 逻辑画面 -> 扫描缓冲的旋转角（CW） */
static int       s_x_off;          /* 逻辑画面在扫描缓冲里的列偏移（像素） */

static lv_display_t *s_disp;
static bool          s_dirty;
static int           s_cal;        /* WAC_CAL=1：扫描空间标定图；=2：逻辑空间标定图 */
static bool          s_yield;      /* WAC_DRM_YIELD=1：平时放开 master，让 Go 的 /fb 流能接管 */
static bool          s_mirror;     /* Go 正在投屏：一帧都不许写，屏归它 */

/* LVGL 的整屏渲染缓冲：1280x480 RGB565 = 1.2MB，放 BSS，不占 LV_MEM 池 */
static uint8_t s_lv_buf[WAC_SCR_W * WAC_SCR_H * 2] LV_ATTRIBUTE_MEM_ALIGN;

/* RGB565 -> XRGB8888 查表，一次初始化，之后每像素一次查表 */
static uint32_t s_lut[65536];

/* ------------------------------------------------------------ 小工具 */
static int drm_ioctl(unsigned long req, void *arg)
{
    int r = ioctl(s_fd, req, arg);
    if (r < 0) WAC_LOG("drm: ioctl 0x%lx failed: %s (%d)", req, strerror(errno), errno);
    return r;
}

/* 本后端独占 disp_fbdev.c 里那份时间实现（两个文件不会同时编进来） */
uint32_t wac_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

void wac_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static void build_lut(void)
{
    for (uint32_t c = 0; c < 65536; c++) {
        uint32_t r = (c >> 11) & 0x1Fu;
        uint32_t g = (c >> 5) & 0x3Fu;
        uint32_t b = c & 0x1Fu;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        s_lut[c] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* ------------------------------------------------------------ DRM 资源 */
typedef struct {
    uint32_t count_crtcs, count_conns, count_encs, count_fbs;
    uint32_t *crtcs, *conns, *encs, *fbs;
} drm_res_t;

static void res_free(drm_res_t *r)
{
    free(r->crtcs);
    free(r->conns);
    free(r->encs);
    free(r->fbs);
    memset(r, 0, sizeof(*r));
}

/* 注意：内核会把 count_* 里的每个数组都写一遍，指针给 NULL 就 EFAULT */
static bool get_resources(drm_res_t *out)
{
    memset(out, 0, sizeof(*out));
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    if (drm_ioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return false;

    out->count_crtcs = res.count_crtcs;
    out->count_conns = res.count_connectors;
    out->count_encs = res.count_encoders;
    out->count_fbs = res.count_fbs;
    out->crtcs = calloc(res.count_crtcs + 1, sizeof(uint32_t));
    out->conns = calloc(res.count_connectors + 1, sizeof(uint32_t));
    out->encs = calloc(res.count_encoders + 1, sizeof(uint32_t));
    out->fbs = calloc(res.count_fbs + 1, sizeof(uint32_t));

    res.crtc_id_ptr = (uint64_t)(uintptr_t)out->crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)out->conns;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)out->encs;
    res.fb_id_ptr = (uint64_t)(uintptr_t)out->fbs;
    if (drm_ioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        res_free(out);
        return false;
    }
    return true;
}

static bool get_connector(uint32_t id, struct drm_mode_get_connector *conn,
                          struct drm_mode_modeinfo **modes, uint32_t **encoders)
{
    memset(conn, 0, sizeof(*conn));
    conn->connector_id = id;
    if (drm_ioctl(DRM_IOCTL_MODE_GETCONNECTOR, conn) < 0) return false;

    *modes = calloc(conn->count_modes + 1, sizeof(**modes));
    *encoders = calloc(conn->count_encoders + 1, sizeof(uint32_t));
    uint32_t *props = calloc(conn->count_props + 1, sizeof(uint32_t));
    uint32_t *values = calloc(conn->count_props + 1, sizeof(uint32_t));

    conn->modes_ptr = (uint64_t)(uintptr_t)*modes;
    conn->encoders_ptr = (uint64_t)(uintptr_t)*encoders;
    conn->props_ptr = (uint64_t)(uintptr_t)props;
    conn->prop_values_ptr = (uint64_t)(uintptr_t)values;

    bool ok = drm_ioctl(DRM_IOCTL_MODE_GETCONNECTOR, conn) >= 0;
    free(props);
    free(values);
    if (!ok) {
        free(*modes);
        free(*encoders);
        *modes = NULL;
        *encoders = NULL;
    }
    return ok;
}

/* 选一个带 mode 的 connector + 一个能用的 CRTC（这块板 connector->encoder_id 是 0，
   要从 encoders 列表里挨个看 possible_crtcs） */
static bool pick_display(drm_res_t *res)
{
    for (uint32_t i = 0; i < res->count_conns && !s_conn_id; i++) {
        struct drm_mode_get_connector conn;
        struct drm_mode_modeinfo *modes = NULL;
        uint32_t *encoders = NULL;
        if (!get_connector(res->conns[i], &conn, &modes, &encoders)) continue;
        if (conn.count_modes == 0) {
            free(modes);
            free(encoders);
            continue;
        }

        struct drm_mode_modeinfo mode = modes[0];
        for (uint32_t m = 0; m < conn.count_modes; m++) {
            if (modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                mode = modes[m];
                break;
            }
        }

        /* 先看 encoder 当前绑定的 CRTC，再看 possible_crtcs */
        uint32_t found_crtc = 0;
        uint32_t enc_ids[9];
        uint32_t enc_n = 0;
        for (uint32_t e = 0; e < conn.count_encoders && enc_n < 8; e++) enc_ids[enc_n++] = encoders[e];
        if (conn.encoder_id) enc_ids[enc_n++] = conn.encoder_id;
        for (uint32_t e = 0; e < enc_n && !found_crtc; e++) {
            struct drm_mode_get_encoder enc;
            memset(&enc, 0, sizeof(enc));
            enc.encoder_id = enc_ids[e];
            if (drm_ioctl(DRM_IOCTL_MODE_GETENCODER, &enc) < 0) continue;
            if (enc.crtc_id) found_crtc = enc.crtc_id;
            if (!found_crtc && enc.possible_crtcs) {
                for (uint32_t b = 0; b < res->count_crtcs; b++) {
                    if (enc.possible_crtcs & (1u << b)) {
                        found_crtc = res->crtcs[b];
                        break;
                    }
                }
            }
        }
        /* 兜底：资源里第一个 CRTC */
        if (!found_crtc && res->count_crtcs) found_crtc = res->crtcs[0];

        WAC_LOG("drm: connector %u type=%u status=%u modes=%u -> %s %ux%u crtc=%u", conn.connector_id,
                conn.connector_type, conn.connection, conn.count_modes, mode.name, mode.hdisplay,
                mode.vdisplay, found_crtc);

        if (found_crtc) {
            s_conn_id = conn.connector_id;
            s_crtc_id = found_crtc;
            s_mode = mode;
        }
        free(modes);
        free(encoders);
    }
    return s_conn_id != 0 && s_crtc_id != 0;
}

/* 检查 plane 支不支持某个 fourcc；顺便打印可用格式 */
static bool plane_supports(uint32_t fourcc)
{
    struct drm_mode_get_plane_res pres;
    memset(&pres, 0, sizeof(pres));
    if (drm_ioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) return false;
    uint32_t *ids = calloc(pres.count_planes + 1, sizeof(uint32_t));
    pres.plane_id_ptr = (uint64_t)(uintptr_t)ids;
    if (drm_ioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) {
        free(ids);
        return false;
    }

    bool found = false;
    for (uint32_t i = 0; i < pres.count_planes && !found; i++) {
        struct drm_mode_get_plane p;
        memset(&p, 0, sizeof(p));
        p.plane_id = ids[i];
        if (drm_ioctl(DRM_IOCTL_MODE_GETPLANE, &p) < 0) continue;
        uint32_t *fmts = calloc(p.count_format_types + 1, sizeof(uint32_t));
        p.format_type_ptr = (uint64_t)(uintptr_t)fmts;
        if (drm_ioctl(DRM_IOCTL_MODE_GETPLANE, &p) < 0) {
            free(fmts);
            continue;
        }
        for (uint32_t f = 0; f < p.count_format_types; f++) {
            if (fmts[f] == fourcc) found = true;
        }
        free(fmts);
    }
    free(ids);
    return found;
}

/* ------------------------------------------------------------ dumb buffer */
static bool create_fb(drm_buf_t *b, uint32_t w, uint32_t h, uint32_t bpp, uint32_t fourcc)
{
    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width = w;
    create.height = h;
    create.bpp = bpp;
    if (drm_ioctl(DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) return false;

    struct drm_mode_map_dumb map;
    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    if (drm_ioctl(DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) return false;

    void *p = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, s_fd, map.offset);
    if (p == MAP_FAILED) {
        WAC_LOG("drm: mmap dumb failed: %s", strerror(errno));
        return false;
    }

    struct drm_mode_fb_cmd2 fb;
    memset(&fb, 0, sizeof(fb));
    fb.width = w;
    fb.height = h;
    fb.pixel_format = fourcc;
    fb.handles[0] = create.handle;
    fb.pitches[0] = create.pitch;
    if (drm_ioctl(DRM_IOCTL_MODE_ADDFB2, &fb) < 0) {
        munmap(p, create.size);
        return false;
    }

    b->handle = create.handle;
    b->pitch = create.pitch;
    b->size = create.size;
    b->map = p;
    b->fb_id = fb.fb_id;
    WAC_LOG("drm: fb %ux%u %s pitch=%u size=%llu fb_id=%u", w, h,
            bpp == 16 ? "RGB565" : "XRGB8888", create.pitch,
            (unsigned long long)create.size, fb.fb_id);
    return true;
}

static void destroy_fb(drm_buf_t *b)
{
    if (b->fb_id) drm_ioctl(DRM_IOCTL_MODE_RMFB, &b->fb_id);
    if (b->map) munmap(b->map, b->size);
    if (b->handle) {
        struct drm_mode_destroy_dumb d;
        memset(&d, 0, sizeof(d));
        d.handle = b->handle;
        drm_ioctl(DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }
    memset(b, 0, sizeof(*b));
}

/* ------------------------------------------------------------ 上屏 */
/* 等上一帧的 flip 完成（事件在 DRM fd 上）；超时就不等了，不能让界面卡住 */
static void wait_flip(void)
{
    if (!s_flip_pending) return;
    s_flip_pending = false;

    struct pollfd pfd = {.fd = s_fd, .events = POLLIN};
    int r = poll(&pfd, 1, FLIP_TIMEOUT_MS);
    if (r <= 0) {
        if (r == 0) WAC_LOG("drm: page flip event timeout");
        return;
    }
    uint8_t ev[64];
    ssize_t n = read(s_fd, ev, sizeof(ev));
    if (n < 0) WAC_LOG("drm: read flip event failed: %s", strerror(errno));
}

/* 标定图：直接画在扫描缓冲上（600x1280，只画可见的 0..479 列）。
   四个角颜色不同 + 顶边品红条，用来问清楚"扫描空间的哪个角在物理屏的哪个角"。
   左上红、右上绿、左下蓝、右下黄、顶边品红、中间白框。 */
static void draw_cal_scan(drm_buf_t *b)
{
    const int vis_w = (int)WAC_SCR_H; /* 480 */

    for (uint32_t y = 0; y < b->size / b->pitch; y++) {
        uint32_t *row = (uint32_t *)(void *)(b->map + (size_t)y * b->pitch);
        uint32_t bg = 0xFF202020u;
        for (int x = 0; x < vis_w; x++) row[x] = bg;
        if (y < 16) {
            for (int x = 0; x < vis_w; x++) row[x] = 0xFFFF00FFu; /* 品红顶条 */
        }
        if (y < 6 || y >= (uint32_t)(s_mode.vdisplay - 6)) {
            for (int x = 0; x < vis_w; x++) row[x] = 0xFFFFFFFFu; /* 上下白边 */
        }
        else {
            row[0] = row[1] = row[2] = row[3] = row[4] = row[5] = 0xFFFFFFFFu;
            row[vis_w - 1] = row[vis_w - 2] = row[vis_w - 3] = 0xFFFFFFFFu;
            row[vis_w - 4] = row[vis_w - 5] = row[vis_w - 6] = 0xFFFFFFFFu;
        }
    }

    /* 四个 120x80 的色块 */
    const uint32_t cols[4] = {0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0xFFFFFF00u}; /* 红 绿 蓝 黄 */
    const int xs[4] = {10, vis_w - 130, 10, vis_w - 130};
    const int ys[4] = {20, 20, (int)s_mode.vdisplay - 100, (int)s_mode.vdisplay - 100};
    for (int k = 0; k < 4; k++) {
        for (int y = ys[k]; y < ys[k] + 80; y++) {
            uint32_t *row = (uint32_t *)(void *)(b->map + (size_t)y * b->pitch);
            for (int x = xs[k]; x < xs[k] + 120; x++) row[x] = cols[k];
        }
    }

    /* 正中 100x100 白块 */
    for (int y = (int)s_mode.vdisplay / 2 - 50; y < (int)s_mode.vdisplay / 2 + 50; y++) {
        uint32_t *row = (uint32_t *)(void *)(b->map + (size_t)y * b->pitch);
        for (int x = vis_w / 2 - 50; x < vis_w / 2 + 50; x++) row[x] = 0xFFFFFFFFu;
    }
}

/* 逻辑空间标定图（1280x480）：红左上、绿右上、蓝左下、黄右下、顶边品红、中间白块。
   画进 LVGL 的 shadow buffer，再走一遍正常的旋转/拷贝，用来验证整条链路。 */
static void draw_cal_logical(void)
{
    uint16_t *p = (uint16_t *)(void *)s_lv_buf;
    const int W = (int)WAC_SCR_W;
    const int H = (int)WAC_SCR_H;

    for (int i = 0; i < W * H; i++) p[i] = 0x2104; /* 深灰 */

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < W; x++) p[(size_t)y * W + x] = 0xF81F; /* 品红顶条 */
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < 6; x++) {
            p[(size_t)y * W + x] = 0xFFFF;
            p[(size_t)y * W + (W - 1 - x)] = 0xFFFF;
        }
    }
    for (int x = 0; x < W; x++) {
        for (int y = 0; y < 6; y++) {
            p[(size_t)y * W + x] = 0xFFFF;
            p[(size_t)(H - 1 - y) * W + x] = 0xFFFF;
        }
    }

    const uint16_t cols[4] = {0xF800, 0x07E0, 0x001F, 0xFFE0}; /* 红 绿 蓝 黄 */
    const int xs[4] = {10, W - 130, 10, W - 130};
    const int ys[4] = {20, 20, H - 100, H - 100};
    for (int k = 0; k < 4; k++) {
        for (int y = ys[k]; y < ys[k] + 80; y++) {
            for (int x = xs[k]; x < xs[k] + 120; x++) p[(size_t)y * W + x] = cols[k];
        }
    }
    for (int y = H / 2 - 50; y < H / 2 + 50; y++) {
        for (int x = W / 2 - 50; x < W / 2 + 50; x++) p[(size_t)y * W + x] = 0xFFFF;
    }
}

/* 调试：把后缓冲的可见区导出成 PPM（P6），抓回来看实际送屏的像素 */
static void dump_scan(drm_buf_t *b)
{
    const char *path = getenv("WAC_DRM_DUMP");
    if (!path || !*path) return;

    /* 先写临时文件再 rename，避免外面抓到写了一半的 PPM */
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        WAC_LOG("drm: dump open %s failed", tmp);
        return;
    }
    const int w = (int)WAC_SCR_H;
    const int h = (int)s_mode.vdisplay;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++) {
        const uint32_t *row = (const uint32_t *)(const void *)(b->map + (size_t)y * (size_t)b->pitch);
        for (int x = 0; x < w; x++) {
            uint32_t c = row[x + s_x_off];
            uint8_t  rgb[3] = {(uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF),
                               (uint8_t)(c & 0xFF)};
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    rename(tmp, path);
    WAC_LOG("drm: dumped %s (%dx%d)", path, w, h);
}

/* master 现在在谁手里：yield 模式下每次推帧现拿现放 */
static bool s_have_master = true;

/* 拿回 master，并确认 CRTC 还指着我们自己的 buffer（被别人动过就重新 SETCRTC） */
static bool take_master(void)
{
    if (!s_have_master) {
        if (drm_ioctl(DRM_IOCTL_SET_MASTER, NULL) < 0) return false;
        s_have_master = true;
    }

    struct drm_mode_crtc get;
    memset(&get, 0, sizeof(get));
    get.crtc_id = s_crtc_id;
    if (drm_ioctl(DRM_IOCTL_MODE_GETCRTC, &get) == 0 && get.mode_valid &&
        get.fb_id != s_buf[s_front].fb_id) {
        struct drm_mode_crtc set;
        memset(&set, 0, sizeof(set));
        set.set_connectors_ptr = (uint64_t)(uintptr_t)&s_conn_id;
        set.count_connectors = 1;
        set.crtc_id = s_crtc_id;
        set.fb_id = s_buf[s_front].fb_id;
        set.mode_valid = 1;
        set.mode = s_mode;
        if (drm_ioctl(DRM_IOCTL_MODE_SETCRTC, &set) < 0) return false;
    }
    return true;
}

static void drop_master(void)
{
    if (!s_have_master) return;
    drm_ioctl(DRM_IOCTL_DROP_MASTER, NULL);
    s_have_master = false;
}

/* yield 模式：推完一帧就放开，平时不占着 */
static void let_go_master(void)
{
    if (!s_yield) return;
    drop_master();
}

/* 调试：把 LVGL 的 shadow buffer 原样倒出来（WAC_LV_DUMP=/tmp/lv.ppm）。
   用来区分"花纹是渲染出来的"还是"旋转拷贝写坏的"。 */
static void dump_lv_once(void)
{
    static int done;
    if (done) return;
    const char *p = getenv("WAC_LV_DUMP");
    if (!p || !*p) return;
    done = 1;

    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", (int)WAC_SCR_W, (int)WAC_SCR_H);
    const uint16_t *s = (const uint16_t *)(const void *)s_lv_buf;
    for (int y = 0; y < WAC_SCR_H; y++) {
        for (int x = 0; x < WAC_SCR_W; x++) {
            uint16_t v = s[(size_t)y * WAC_SCR_W + x];
            uint8_t  rgb[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31),
                                (uint8_t)(((v >> 5) & 63) * 255 / 63),
                                (uint8_t)((v & 31) * 255 / 31) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    WAC_LOG("drm: lv buffer dumped %s", p);
}

/* shadow(RGB565) -> 后缓冲(XRGB8888)，按 s_rot 旋转，然后 page flip */
static void present(void)
{
    if (s_mirror) return; /* 投屏中：屏幕归 Go，别抢 */
    dump_lv_once();
    if (s_yield && !take_master()) {
        /* 别人（Go 的 /fb 流）正占着屏，这一帧先不推，画面留给他 */
        static int n;
        if ((n++ % 30) == 0) WAC_LOG("drm: display busy, skip frame");
        return;
    }
    wait_flip();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    drm_buf_t *b = &s_buf[s_front ^ 1];
    const uint16_t *src = (const uint16_t *)(const void *)s_lv_buf;

    if (s_cal == 2) draw_cal_logical();

    if (s_cal == 1) {
        draw_cal_scan(b);
    }
    else if (s_rot == 0) {
        for (int y = 0; y < (int)WAC_SCR_H; y++) {
            const uint16_t *s = src + (size_t)y * WAC_SCR_W;
            if (s_use_565) {
                memcpy(b->map + (size_t)y * (size_t)b->pitch + (size_t)s_x_off * 2, s,
                       (size_t)WAC_SCR_W * 2);
            }
            else {
                uint32_t *d = (uint32_t *)(void *)(b->map + (size_t)y * (size_t)b->pitch) + s_x_off;
                for (int x = 0; x < (int)WAC_SCR_W; x++) d[x] = s_lut[s[x]];
            }
        }
    }
    else {
        /* 转 90/270°：扫描行 = 逻辑列（x），扫描列 = 逻辑行（y）。
           注意这里读的是逻辑图的"列"，步长是整行宽 W，不是连续内存。 */
        const int W = (int)WAC_SCR_W;
        const int H = (int)WAC_SCR_H;
        if (s_use_565) {
            for (int py = 0; py < W; py++) {
                const uint16_t *s = src + (size_t)(s_rot == 90 ? py : (W - 1 - py));
                uint16_t       *d =
                    (uint16_t *)(void *)(b->map + (size_t)py * (size_t)b->pitch) + s_x_off;
                if (s_rot == 90) {
                    for (int px = 0; px < H; px++) d[px] = s[(size_t)(H - 1 - px) * W];
                }
                else {
                    for (int px = 0; px < H; px++) d[px] = s[(size_t)px * W];
                }
            }
        }
        else {
            /* 一次处理 8 个扫描行：这 8 行在逻辑图里是同一条列上连续的 8 个
               像素（16 字节），读一次缓存行能喂 8 行，比逐行跳着读快好几倍。 */
            for (int py = 0; py < W; py += 8) {
                int       n = (W - py < 8) ? (W - py) : 8;
                uint32_t *d[8];
                for (int i = 0; i < n; i++) {
                    d[i] = (uint32_t *)(void *)(b->map + (size_t)(py + i) * (size_t)b->pitch) +
                           s_x_off;
                }
                if (s_rot == 90) {
                    for (int px = 0; px < H; px++) {
                        const uint16_t *s = src + (size_t)(H - 1 - px) * W + py;
                        for (int i = 0; i < n; i++) d[i][px] = s_lut[s[i]];
                    }
                }
                else {
                    for (int px = 0; px < H; px++) {
                        const uint16_t *s = src + (size_t)px * W + (W - 1 - py);
                        for (int i = 0; i < n; i++) d[i][px] = s_lut[s[-i]];
                    }
                }
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    {
        long ms = (long)((t1.tv_sec - t0.tv_sec) * 1000 +
                         (t1.tv_nsec - t0.tv_nsec) / 1000000);
        static int n;
        if (ms >= 20 && (n++ % 20) == 0) WAC_LOG("drm: blit %ld ms", ms);
    }

    dump_scan(b);

    struct drm_mode_crtc_page_flip flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id = s_crtc_id;
    flip.fb_id = b->fb_id;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (drm_ioctl(DRM_IOCTL_MODE_PAGE_FLIP, &flip) < 0) {
        /* 有些驱动不支持 flip：退回 SETCRTC（会闪，但至少能显示） */
        struct drm_mode_crtc set;
        memset(&set, 0, sizeof(set));
        set.set_connectors_ptr = (uint64_t)(uintptr_t)&s_conn_id;
        set.count_connectors = 1;
        set.crtc_id = s_crtc_id;
        set.fb_id = b->fb_id;
        set.mode_valid = 1;
        set.mode = s_mode;
        if (drm_ioctl(DRM_IOCTL_MODE_SETCRTC, &set) < 0) return;
        s_front ^= 1;
        let_go_master();
        return;
    }
    s_flip_pending = true;
    s_front ^= 1;

    if (s_yield) {
        /* 等这一帧真的上屏再放开，不然 flip 可能被拦下 */
        wait_flip();
        let_go_master();
    }
}

/* Go 的 /fb 流要写屏：把 master 让出去，本进程停止推帧 */
void wac_disp_release(void)
{
    if (s_mirror) return;
    s_mirror = true;
    /* 立刻放：Go 收到 FBSTART 后几毫秒就去 SET_MASTER，慢一步它就退回
       /dev/fb0 了。手头那个未完成的 flip 让它自己去完成/作废都行。 */
    drop_master();
    WAC_LOG("drm: released (go /fb stream owns the screen)");
}

/* 投屏结束：收回 master，必要时把 CRTC 重新指回我们的 buffer */
void wac_disp_reclaim(void)
{
    if (!s_mirror) return;
    s_mirror = false;
    if (take_master()) WAC_LOG("drm: reclaimed");
    else WAC_LOG("drm: reclaim busy, will retry on next frame");
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    LV_UNUSED(area);
    LV_UNUSED(px_map);
    s_dirty = true;
    lv_display_flush_ready(disp);
}

static void on_refr_ready(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_dirty) return;
    s_dirty = false;
    present();
}

/* ------------------------------------------------------------ 初始化 */
lv_display_t *wac_disp_init(void)
{
    lv_tick_set_cb(wac_millis);
    build_lut();

    const char *dev = getenv("WAC_DRM_DEV");
    if (!dev || !*dev) dev = DRM_DEV_DEFAULT;
    const char *cal = getenv("WAC_CAL");
    s_cal = (cal && *cal) ? atoi(cal) : 0;
    if (s_cal < 0 || s_cal > 2) s_cal = 0;
    const char *yield_env = getenv("WAC_DRM_YIELD");
    s_yield = (yield_env && *yield_env && atoi(yield_env) != 0);
    s_fd = open(dev, O_RDWR | O_CLOEXEC);
    if (s_fd < 0) {
        WAC_LOG("drm: open %s failed: %s", dev, strerror(errno));
        return NULL;
    }
    if (drm_ioctl(DRM_IOCTL_SET_MASTER, NULL) < 0) {
        WAC_LOG("drm: set master failed (another compositor holding it?)");
        return NULL;
    }

    struct drm_set_client_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;
    cap.value = 1;
    drm_ioctl(DRM_IOCTL_SET_CLIENT_CAP, &cap);

    drm_res_t res;
    if (!get_resources(&res)) return NULL;
    WAC_LOG("drm: %s crtcs=%u connectors=%u encoders=%u fbs=%u", dev, res.count_crtcs,
            res.count_conns, res.count_encs, res.count_fbs);

    if (!pick_display(&res)) {
        WAC_LOG("drm: no usable connector/crtc");
        res_free(&res);
        return NULL;
    }
    res_free(&res);

    /* 逻辑画面尺寸不能超过 mode（转 90/270 时长宽互换） */
    s_rot = 90;
    const char *rot_env = getenv("WAC_DRM_ROT");
    if (rot_env && *rot_env) s_rot = atoi(rot_env);
    if (s_rot != 0 && s_rot != 90 && s_rot != 270) s_rot = 90;

    uint32_t need_w = (s_rot == 90 || s_rot == 270) ? WAC_SCR_H : WAC_SCR_W;
    uint32_t need_h = (s_rot == 90 || s_rot == 270) ? WAC_SCR_W : WAC_SCR_H;
    if (need_w > s_mode.hdisplay || need_h > s_mode.vdisplay) {
        WAC_LOG("drm: logical %dx%d rot %d needs %ux%u, mode is %ux%u", (int)WAC_SCR_W,
                (int)WAC_SCR_H, s_rot, need_w, need_h, s_mode.hdisplay, s_mode.vdisplay);
        return NULL;
    }

    /* 格式：默认跟 Go 版一致用 XRGB8888，WAC_DRM_565=1 时才试 RGB565 */
    const char *want565 = getenv("WAC_DRM_565");
    s_use_565 = (want565 && atoi(want565) != 0) && plane_supports(DRM_FORMAT_RGB565);
    if (!s_use_565 && !plane_supports(DRM_FORMAT_XRGB8888)) {
        WAC_LOG("drm: plane has neither XRGB8888 nor RGB565");
        return NULL;
    }
    WAC_LOG("drm: scanout format = %s", s_use_565 ? "RGB565" : "XRGB8888");

    /* 默认居中：这块板 600 宽的扫描缓冲里只有中间 480 列可见（上下各 60 列被面板
       边框盖住）。也可以用 WAC_DRM_X 覆盖。 */
    s_x_off = (int)((s_mode.hdisplay - need_w) / 2);
    const char *xo = getenv("WAC_DRM_X");
    if (xo && *xo) s_x_off = atoi(xo);
    if (s_x_off < 0 || (uint32_t)s_x_off + need_w > s_mode.hdisplay) s_x_off = 0;

    uint32_t bpp = s_use_565 ? 16 : 32;
    uint32_t fourcc = s_use_565 ? DRM_FORMAT_RGB565 : DRM_FORMAT_XRGB8888;
    for (int i = 0; i < 2; i++) {
        if (!create_fb(&s_buf[i], s_mode.hdisplay, s_mode.vdisplay, bpp, fourcc)) {
            WAC_LOG("drm: create framebuffer %d failed", i);
            return NULL;
        }
        memset(s_buf[i].map, 0, s_buf[i].size);
    }

    struct drm_mode_crtc set;
    memset(&set, 0, sizeof(set));
    set.set_connectors_ptr = (uint64_t)(uintptr_t)&s_conn_id;
    set.count_connectors = 1;
    set.crtc_id = s_crtc_id;
    set.fb_id = s_buf[0].fb_id;
    set.mode_valid = 1;
    set.mode = s_mode;
    if (drm_ioctl(DRM_IOCTL_MODE_SETCRTC, &set) < 0) {
        WAC_LOG("drm: SETCRTC failed");
        return NULL;
    }
    s_front = 0;
    s_flip_pending = false;

    /* 标定模式：先把测试图推上去，后面每次刷新也重画（盖掉 UI） */
    if (s_cal) present();

    /* LVGL：RGB565 + DIRECT（整屏 buffer），刷新结束再 blit + flip */
    s_disp = lv_display_create(WAC_SCR_W, WAC_SCR_H);
    if (!s_disp) {
        WAC_LOG("drm: lv_display_create failed");
        return NULL;
    }
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_lv_buf, NULL, sizeof(s_lv_buf), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_add_event_cb(s_disp, on_refr_ready, LV_EVENT_REFR_READY, NULL);
    lv_display_set_default(s_disp);

    WAC_LOG("drm: ready mode %ux%u logical %dx%d rot %d at x=%d format=%s", s_mode.hdisplay,
            s_mode.vdisplay, (int)WAC_SCR_W, (int)WAC_SCR_H, s_rot, s_x_off,
            s_use_565 ? "RGB565" : "XRGB8888");
    return s_disp;
}

void wac_disp_deinit(void)
{
    if (s_fd < 0) return;

    /* 退到黑屏再收资源，别把最后一帧留在面板上 */
    if (s_buf[s_front].map) memset(s_buf[s_front].map, 0, s_buf[s_front].size);
    for (int i = 0; i < 2; i++) destroy_fb(&s_buf[i]);
    drm_ioctl(DRM_IOCTL_DROP_MASTER, NULL);
    close(s_fd);
    s_fd = -1;
}

#endif /* !WAC_SIM */
