// drm_probe.c -- 探针：把这块 sprd 板子的 DRM 能力打印出来，并可以画一张测试图
//
// 只依赖内核 UAPI 头（<drm/drm.h> 等），不链接 libdrm，静态 musl 直接跑。
//   drm_probe dump      只打印
//   drm_probe pattern   打印 + 画测试图 + SETCRTC（8 秒后退出，退出时恢复原 CRTC）
//
// 编译（zig）：
//   zig cc -target arm-linux-musleabihf -mcpu=cortex_a53 -static -O2 -o drm_probe drm_probe.c
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>

static int g_fd = -1;

static int xioctl(unsigned long req, void *arg)
{
    int r = ioctl(g_fd, req, arg);
    if (r < 0) fprintf(stderr, "ioctl 0x%lx failed: %s (%d)\n", req, strerror(errno), errno);
    return r;
}

static const char *fourcc_str(uint32_t f, char out[5])
{
    out[0] = (char)(f & 0xff);
    out[1] = (char)((f >> 8) & 0xff);
    out[2] = (char)((f >> 16) & 0xff);
    out[3] = (char)((f >> 24) & 0xff);
    out[4] = 0;
    return out;
}

static void dump_resources(void)
{
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return;

    uint32_t *fbs = calloc(res.count_fbs + 1, 4);
    uint32_t *crtcs = calloc(res.count_crtcs + 1, 4);
    uint32_t *conns = calloc(res.count_connectors + 1, 4);
    uint32_t *encs = calloc(res.count_encoders + 1, 4);
    res.fb_id_ptr = (uint64_t)(uintptr_t)fbs;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)encs;
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) {
        free(fbs); free(crtcs); free(conns); free(encs);
        return;
    }
    printf("== resources: fbs=%u crtcs=%u connectors=%u encoders=%u\n", res.count_fbs,
           res.count_crtcs, res.count_connectors, res.count_encoders);
    printf("   min %ux%u max %ux%u\n", res.min_width, res.min_height, res.max_width, res.max_height);

    for (uint32_t i = 0; i < res.count_crtcs; i++) {
        struct drm_mode_crtc c;
        memset(&c, 0, sizeof(c));
        c.crtc_id = crtcs[i];
        if (xioctl(DRM_IOCTL_MODE_GETCRTC, &c) < 0) continue;
        printf("-- crtc %u: fb=%u pos=%d,%d mode_valid=%u mode=%s %ux%u clock=%u\n", c.crtc_id,
               c.fb_id, c.x, c.y, c.mode_valid, c.mode.name, c.mode.hdisplay, c.mode.vdisplay,
               c.mode.clock);
    }

    for (uint32_t i = 0; i < res.count_connectors; i++) {
        struct drm_mode_get_connector conn;
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = conns[i];
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        struct drm_mode_modeinfo *modes = calloc(conn.count_modes + 1, sizeof(*modes));
        uint32_t *enc = calloc(conn.count_encoders + 1, 4);
        uint32_t *props = calloc(conn.count_props + 1, 4);
        uint32_t *propv = calloc(conn.count_props + 1, 4);
        conn.modes_ptr = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)enc;
        conn.props_ptr = (uint64_t)(uintptr_t)props;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)propv;
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            free(modes); free(enc); free(props); free(propv);
            continue;
        }
        printf("-- connector %u: type=%u type_id=%u status=%u(mm %ux%u) encoder=%u modes=%u\n",
               conn.connector_id, conn.connector_type, conn.connector_type_id, conn.connection,
               conn.mm_width, conn.mm_height, conn.encoder_id, conn.count_modes);
        for (uint32_t m = 0; m < conn.count_modes; m++)
            printf("     mode[%u] %s %ux%u clock=%u type=0x%x\n", m, modes[m].name,
                   modes[m].hdisplay, modes[m].vdisplay, modes[m].clock, modes[m].type);
        free(modes);
        free(enc);
    }

    free(fbs); free(crtcs); free(conns); free(encs);
}

static uint32_t g_plane_id;

static int probe_planes(uint32_t crtc_index)
{
    struct drm_mode_get_plane_res pres;
    memset(&pres, 0, sizeof(pres));
    if (xioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) return 0;
    uint32_t *ids = calloc(pres.count_planes + 1, 4);
    pres.plane_id_ptr = (uint64_t)(uintptr_t)ids;
    if (xioctl(DRM_IOCTL_MODE_GETPLANERESOURCES, &pres) < 0) {
        free(ids);
        return 0;
    }
    int have565 = 0;
    printf("== planes: %u\n", pres.count_planes);
    for (uint32_t i = 0; i < pres.count_planes; i++) {
        struct drm_mode_get_plane p;
        memset(&p, 0, sizeof(p));
        p.plane_id = ids[i];
        if (xioctl(DRM_IOCTL_MODE_GETPLANE, &p) < 0) continue;
        uint32_t *fmts = calloc(p.count_format_types + 1, 4);
        p.format_type_ptr = (uint64_t)(uintptr_t)fmts;
        if (xioctl(DRM_IOCTL_MODE_GETPLANE, &p) < 0) {
            free(fmts);
            continue;
        }
        printf("-- plane %u: crtc=%u fb=%u possible_crtcs=0x%x gamma=%u formats=%u\n", p.plane_id,
               p.crtc_id, p.fb_id, p.possible_crtcs, p.gamma_size, p.count_format_types);
        printf("   formats:");
        for (uint32_t f = 0; f < p.count_format_types; f++) {
            char s[5];
            printf(" %s", fourcc_str(fmts[f], s));
            if (fmts[f] == DRM_FORMAT_RGB565 || fmts[f] == DRM_FORMAT_BGR565) have565 = 1;
        }
        printf("\n");
        if (!g_plane_id && (p.possible_crtcs & (1u << crtc_index))) {
            g_plane_id = p.plane_id;
            printf("   (selected for crtc index %u)\n", crtc_index);
        }
        free(fmts);
    }
    free(ids);
    return have565;
}

static uint32_t find_crtc_for_connector(uint32_t connector_id, uint32_t *crtc_index)
{
    struct drm_mode_get_connector conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = connector_id;
    if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) return 0;
    uint32_t *enc = calloc(conn.count_encoders + 1, 4);
    uint32_t *props = calloc(conn.count_props + 1, 4);
    uint32_t *propv = calloc(conn.count_props + 1, 4);
    struct drm_mode_modeinfo *modes = calloc(conn.count_modes + 1, sizeof(*modes));
    conn.encoders_ptr = (uint64_t)(uintptr_t)enc;
    conn.props_ptr = (uint64_t)(uintptr_t)props;
    conn.prop_values_ptr = (uint64_t)(uintptr_t)propv;
    conn.modes_ptr = (uint64_t)(uintptr_t)modes;
    if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
        free(enc); free(props); free(propv); free(modes);
        return 0;
    }

    // 这块板的 connector->encoder_id 是 0（没绑定），要从 encoders 列表里挨个试
    uint32_t enc_count = conn.count_encoders;
    uint32_t ids[8];
    uint32_t n = enc_count < 8 ? enc_count : 8;
    for (uint32_t i = 0; i < n; i++) ids[i] = enc[i];
    free(enc); free(props); free(propv); free(modes);

    for (uint32_t i = 0; i < n; i++) {
        struct drm_mode_get_encoder e;
        memset(&e, 0, sizeof(e));
        e.encoder_id = ids[i];
        if (xioctl(DRM_IOCTL_MODE_GETENCODER, &e) < 0) continue;
        printf("   encoder %u: crtc=%u possible_crtcs=0x%x possible_clones=0x%x\n", ids[i],
               e.crtc_id, e.possible_crtcs, e.possible_clones);
        for (uint32_t b = 0; b < 32; b++) {
            if (!(e.possible_crtcs & (1u << b))) continue;
            struct drm_mode_card_res res;
            memset(&res, 0, sizeof(res));
            if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return 0;
            uint32_t *crtcs = calloc(res.count_crtcs + 1, 4);
            uint32_t *conns2 = calloc(res.count_connectors + 1, 4);
            uint32_t *fbs2 = calloc(res.count_fbs + 1, 4);
            uint32_t *encs2 = calloc(res.count_encoders + 1, 4);
            res.crtc_id_ptr = (uint64_t)(uintptr_t)crtcs;
            res.connector_id_ptr = (uint64_t)(uintptr_t)conns2;
            res.fb_id_ptr = (uint64_t)(uintptr_t)fbs2;
            res.encoder_id_ptr = (uint64_t)(uintptr_t)encs2;
            uint32_t id = 0;
            if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) == 0 && b < res.count_crtcs) {
                id = crtcs[b];
                *crtc_index = b;
            }
            free(crtcs); free(conns2); free(fbs2); free(encs2);
            if (id) return id;
        }
    }
    return 0;
}

struct fb_ctx {
    uint32_t handle, pitch, size, fb_id;
    uint8_t *map;
};

static int create_fb(struct fb_ctx *fb, uint32_t w, uint32_t h, uint32_t bpp, uint32_t fourcc)
{
    struct drm_mode_create_dumb create;
    memset(&create, 0, sizeof(create));
    create.width = w;
    create.height = h;
    create.bpp = bpp;
    if (xioctl(DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) return -1;
    fb->handle = create.handle;
    fb->pitch = create.pitch;
    fb->size = create.size;

    struct drm_mode_map_dumb map;
    memset(&map, 0, sizeof(map));
    map.handle = create.handle;
    if (xioctl(DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) return -1;
    fb->map = mmap(NULL, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, map.offset);
    if (fb->map == MAP_FAILED) {
        perror("mmap");
        fb->map = NULL;
        return -1;
    }

    struct drm_mode_fb_cmd2 cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.width = w;
    cmd.height = h;
    cmd.pixel_format = fourcc;
    cmd.handles[0] = create.handle;
    cmd.pitches[0] = create.pitch;
    if (xioctl(DRM_IOCTL_MODE_ADDFB2, &cmd) < 0) return -1;
    fb->fb_id = cmd.fb_id;
    printf("   fb: %ux%u bpp=%u pitch=%u size=%u handle=%u fb_id=%u\n", w, h, bpp, fb->pitch,
           fb->size, fb->handle, fb->fb_id);
    return 0;
}

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static void put565(struct fb_ctx *fb, int x, int y, uint16_t c)
{
    ((uint16_t *)(fb->map + (size_t)y * fb->pitch))[x] = c;
}

static void fill565(struct fb_ctx *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) put565(fb, x, y, c);
}

static void draw_pattern(struct fb_ctx *fb, int w, int h)
{
    // 背景：深灰
    fill565(fb, 0, 0, w, h, rgb565(24, 24, 24));

    // 顶部 10 条 60px 竖条：红 橙 黄 绿 青 蓝 紫 洋红 白 黑
    static const uint16_t bars[10] = {
        [0] = 0xF800, [1] = 0xFC00, [2] = 0xFFE0, [3] = 0x07E0, [4] = 0x07FF,
        [5] = 0x001F, [6] = 0x8010, [7] = 0xF81F, [8] = 0xFFFF, [9] = 0x0000,
    };
    for (int i = 0; i < 10; i++) fill565(fb, i * 60, 0, (i + 1) * 60, 120, bars[i]);

    // 逻辑区 480 宽的白色边框（外面 120px 保持深灰）
    fill565(fb, 0, 120, 480, 124, 0xFFFF);
    fill565(fb, 0, h - 4, 480, h, 0xFFFF);
    fill565(fb, 0, 120, 4, h, 0xFFFF);
    fill565(fb, 476, 120, 480, h, 0xFFFF);

    // 每 160px 一条横线（只在 480 内），方便数格子
    for (int y = 320; y < h; y += 160) fill565(fb, 4, y, 476, y + 2, rgb565(0, 128, 255));

    // 中心：白色方块（表示"屏幕中心"）
    fill565(fb, 200, 400, 280, 480, 0xFFFF);
    // 中心上方：绿色小方块；下方：红色小方块（判断有没有上下翻转）
    fill565(fb, 200, 300, 280, 340, 0x07E0);
    fill565(fb, 200, 540, 280, 580, 0xF800);
}

int main(int argc, char **argv)
{
    int pattern = (argc > 1 && strcmp(argv[1], "pattern") == 0);
    g_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (g_fd < 0) {
        perror("open /dev/dri/card0");
        return 1;
    }
    if (xioctl(DRM_IOCTL_SET_MASTER, NULL) < 0) fprintf(stderr, "warning: set master failed\n");

    // 不开这个 cap，legacy 驱动（sprd 4.14）的 GETPLANERESOURCES 直接返回 0 个 plane
    struct drm_set_client_cap cap;
    memset(&cap, 0, sizeof(cap));
    cap.capability = DRM_CLIENT_CAP_UNIVERSAL_PLANES;
    cap.value = 1;
    if (xioctl(DRM_IOCTL_SET_CLIENT_CAP, &cap) < 0)
        fprintf(stderr, "warning: universal planes cap failed\n");

    dump_resources();

    // 选第一个 connected + 有 mode 的 connector
    struct drm_mode_card_res res;
    memset(&res, 0, sizeof(res));
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return 1;
    // 四个数组都必须给：内核会把 count_* 里的每一项都写回来，NULL 就是 EFAULT
    uint32_t *conns = calloc(res.count_connectors + 1, 4);
    uint32_t *crtc_ids = calloc(res.count_crtcs + 1, 4);
    uint32_t *fb_ids = calloc(res.count_fbs + 1, 4);
    uint32_t *enc_ids = calloc(res.count_encoders + 1, 4);
    res.connector_id_ptr = (uint64_t)(uintptr_t)conns;
    res.crtc_id_ptr = (uint64_t)(uintptr_t)crtc_ids;
    res.fb_id_ptr = (uint64_t)(uintptr_t)fb_ids;
    res.encoder_id_ptr = (uint64_t)(uintptr_t)enc_ids;
    if (xioctl(DRM_IOCTL_MODE_GETRESOURCES, &res) < 0) return 1;

    uint32_t conn_id = 0;
    struct drm_mode_modeinfo mode;
    memset(&mode, 0, sizeof(mode));
    for (uint32_t i = 0; i < res.count_connectors && !conn_id; i++) {
        struct drm_mode_get_connector conn;
        memset(&conn, 0, sizeof(conn));
        conn.connector_id = conns[i];
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) continue;
        if (conn.count_modes == 0) continue;
        struct drm_mode_modeinfo *modes = calloc(conn.count_modes + 1, sizeof(*modes));
        uint32_t *enc = calloc(conn.count_encoders + 1, 4);
        uint32_t *props = calloc(conn.count_props + 1, 4);
        uint32_t *propv = calloc(conn.count_props + 1, 4);
        conn.modes_ptr = (uint64_t)(uintptr_t)modes;
        conn.encoders_ptr = (uint64_t)(uintptr_t)enc;
        conn.props_ptr = (uint64_t)(uintptr_t)props;
        conn.prop_values_ptr = (uint64_t)(uintptr_t)propv;
        if (xioctl(DRM_IOCTL_MODE_GETCONNECTOR, &conn) < 0) {
            free(modes); free(enc); free(props); free(propv);
            continue;
        }
        conn_id = conns[i];
        mode = modes[0];
        printf("== selected connector %u mode %s %ux%u\n", conn_id, mode.name, mode.hdisplay,
               mode.vdisplay);
        free(modes);
    }
    free(conns);
    if (!conn_id) {
        fprintf(stderr, "no connector with modes\n");
        return 1;
    }

    uint32_t crtc_index = 0;
    uint32_t crtc_id = find_crtc_for_connector(conn_id, &crtc_index);
    printf("== selected crtc %u (index %u)\n", crtc_id, crtc_index);
    if (!crtc_id) return 1;

    int have565 = probe_planes(crtc_index);
    printf("== RGB565 scanout support: %s\n", have565 ? "yes" : "no");

    if (pattern) {
        struct fb_ctx fb;
        memset(&fb, 0, sizeof(fb));
        uint32_t fourcc = have565 ? DRM_FORMAT_RGB565 : DRM_FORMAT_XRGB8888;
        uint32_t bpp = have565 ? 16 : 32;
        printf("== creating test fb %ux%u %s\n", mode.hdisplay, mode.vdisplay,
               have565 ? "RGB565" : "XRGB8888");
        if (create_fb(&fb, mode.hdisplay, mode.vdisplay, bpp, fourcc) < 0) return 1;

        // 先保存原 CRTC 状态
        struct drm_mode_crtc old;
        memset(&old, 0, sizeof(old));
        old.crtc_id = crtc_id;
        xioctl(DRM_IOCTL_MODE_GETCRTC, &old);

        if (have565) {
            draw_pattern(&fb, (int)mode.hdisplay, (int)mode.vdisplay);
        } else {
            // XRGB8888 下也画一版（简单四象限 + 顶条）
            uint32_t *p = (uint32_t *)(void *)fb.map;
            for (uint32_t y = 0; y < mode.vdisplay; y++) {
                uint32_t *row = (uint32_t *)(void *)(fb.map + (size_t)y * fb.pitch);
                for (uint32_t x = 0; x < mode.hdisplay; x++) {
                    uint32_t c = 0xff101010u;
                    if (y < 120) {
                        static const uint32_t bars[10] = {
                            0xff0000, 0xff8000, 0xffff00, 0x00ff00, 0x00ffff,
                            0x0000ff, 0x800080, 0xff00ff, 0xffffff, 0x000000};
                        c = bars[(x / 60) % 10];
                    } else if (x < 4 || x >= 476 || y < 124 || y >= mode.vdisplay - 4) {
                        c = 0xffffff;
                    }
                    row[x] = c;
                }
            }
            (void)p;
        }

        struct drm_mode_crtc set;
        memset(&set, 0, sizeof(set));
        uint32_t conns_arr[1] = {conn_id};
        set.set_connectors_ptr = (uint64_t)(uintptr_t)conns_arr;
        set.count_connectors = 1;
        set.crtc_id = crtc_id;
        set.fb_id = fb.fb_id;
        set.mode_valid = 1;
        set.mode = mode;
        if (xioctl(DRM_IOCTL_MODE_SETCRTC, &set) < 0) {
            fprintf(stderr, "SETCRTC failed\n");
            return 1;
        }
        printf("== SETCRTC ok, pattern on screen for 600s (pid %d)\n", (int)getpid());
        fflush(stdout);
        sleep(600);

        // 恢复
        if (old.mode_valid && old.fb_id) {
            struct drm_mode_crtc restore = old;
            uint32_t c2[1] = {conn_id};
            restore.set_connectors_ptr = (uint64_t)(uintptr_t)c2;
            restore.count_connectors = 1;
            if (xioctl(DRM_IOCTL_MODE_SETCRTC, &restore) == 0)
                printf("== restored previous crtc\n");
        }
        xioctl(DRM_IOCTL_MODE_RMFB, &fb.fb_id);
        munmap(fb.map, fb.size);
        struct drm_mode_destroy_dumb d = {.handle = fb.handle};
        xioctl(DRM_IOCTL_MODE_DESTROY_DUMB, &d);
    }

    xioctl(DRM_IOCTL_DROP_MASTER, NULL);
    close(g_fd);
    return 0;
}
