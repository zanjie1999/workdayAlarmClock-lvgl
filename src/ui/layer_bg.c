/**
 * layer_bg.c -- 壁纸 + 固定 50% 遮罩
 *
 * 壁纸是**预转好的裸 RGB565（little-endian）**，不是 png/jpg —— 板子上没有解码器，
 * 也没内存留给一张全屏位图的解码结果。转换用 scripts/make_wallpaper.py，一条命令。
 *
 * 查找顺序（第一个能读通的就用）：
 *   1. 环境变量 WAC_WALLPAPER 指定的文件（调试用，直接指定任意路径）
 *   2. <dir>/wallpapers/bg_<W>x<H>.raw   与屏幕同尺寸，零缩放
 *   3. <dir>/wallpapers/bg.raw           裸尺寸，同样必须与屏幕一致
 *   4. <dir>/wallpapers/ 里第一个 bg_*.raw，尺寸从文件名解析，自动 cover 裁切缩放
 * 其中 <dir> 先试当前工作目录，再试可执行文件所在目录 —— 开机自启时 cwd 常常是 /，
 * 只认 cwd 会找不到图。
 *
 * 找不到 / 尺寸读不出来 / 读失败 → **纯黑**（等于没有背景图，不是错误）。
 *
 * 注意：壁纸和遮罩挂在**活动屏幕**的最底下两层，不是 lv_layer_bottom()。
 * LVGL 的刷新逻辑只在"屏幕这块区域没有被任何不透明对象覆盖"时才去画 bottom layer
 * （lv_refr.c: refr_obj_and_children(layer, lv_display_get_layer_bottom(...))），
 * 而屏幕默认主题背景是不透明白色 —— 挂 bottom layer 会整层被跳过，壁纸和遮罩全不显示。
 */
#include "layer_bg.h"
#include "theme.h"

#include <dirent.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define BG_CWD _getcwd
#else
#include <unistd.h>
#define BG_CWD getcwd
#endif

static uint8_t        *s_buf;
static lv_image_dsc_t  s_dsc;
static lv_obj_t       *s_wall; /* 壁纸（无壁纸时为 NULL） */
static lv_obj_t       *s_mask; /* 50% 压暗遮罩：只在有壁纸时存在 */

/* --------------------------------------------------------------- 路径 */

/* 从文件名尾部的 "<w>x<h>" 解析出尺寸：bg_160x128.raw / 任意名字_320x240.raw */
static bool parse_size(const char *path, int *w, int *h)
{
    const char *base = strrchr(path, '/');
#ifdef _WIN32
    const char *bs = strrchr(path, '\\');
    if (!base || (bs && bs > base)) base = bs;
#endif
    base = base ? base + 1 : path;

    for (const char *p = strchr(base, '_'); p; p = strchr(p + 1, '_')) {
        int a = 0, b = 0;
        if (sscanf(p + 1, "%dx%d", &a, &b) == 2 && a > 0 && b > 0 && a <= 8192 && b <= 8192) {
            *w = a;
            *h = b;
            return true;
        }
    }
    return false;
}

static bool file_ok(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

/* 可执行文件所在目录：真机自启时 cwd 通常是 /，相对路径找不到图 */
static bool exe_dir(char *out, size_t cap)
{
#ifdef _WIN32
    /* 模拟器上 cwd 就是 build 目录，不去折腾 GetModuleFileNameA */
    (void)out;
    (void)cap;
    return false;
#else
    ssize_t n = readlink("/proc/self/exe", out, cap - 1);
    if (n <= 0) return false;
    out[n] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash) return false;
    *slash = '\0';
    return true;
#endif
}

/* 在 dir 里找 "名字能解析出尺寸" 的第一个 bg_*.raw（同屏幕尺寸的优先） */
static bool scan_dir(const char *dir, char *out, size_t cap)
{
    DIR *d = opendir(dir);
    if (!d) return false; /* 没有 wallpapers/ 目录是常态，不是错误 */

    char best[512] = {0};
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strncmp(e->d_name, "bg", 2) != 0) continue;
        if (!strstr(e->d_name, ".raw")) continue;

        char p[512];
        if (snprintf(p, sizeof(p), "%s/%s", dir, e->d_name) >= (int)sizeof(p)) continue;

        int w = 0, h = 0;
        if (!parse_size(p, &w, &h)) continue;
        if (w == (int)WAC_SCR_W && h == (int)WAC_SCR_H) { /* 精确命中，直接用 */
            snprintf(out, cap, "%s", p);
            closedir(d);
            return true;
        }
        if (!best[0]) snprintf(best, sizeof(best), "%s", p);
    }
    closedir(d);

    WAC_LOG_DEBUG("wallpaper: scanned %s -> %s", dir, best[0] ? best : "none");
    if (!best[0]) return false;
    snprintf(out, cap, "%s", best);
    return true;
}

static bool find_wallpaper(char *out, size_t cap)
{
    const char *env = getenv("WAC_WALLPAPER");
    if (env && env[0]) {
        snprintf(out, cap, "%s", env);
        return true; /* 明确指定的路径就算读不通也不去猜别的 */
    }

    char dirs[2][256];
    int  nd = 0;
    if (BG_CWD(dirs[nd], sizeof(dirs[nd]))) nd++;
    if (nd < 2 && exe_dir(dirs[nd], sizeof(dirs[nd]))) nd++;

    for (int i = 0; i < nd; i++) {
        char base[320];

        snprintf(base, sizeof(base), "%s/wallpapers/bg_%dx%d.raw", dirs[i], (int)WAC_SCR_W,
                 (int)WAC_SCR_H);
        if (file_ok(base)) {
            snprintf(out, cap, "%s", base);
            return true;
        }

        snprintf(base, sizeof(base), "%s/wallpapers/bg.raw", dirs[i]);
        if (file_ok(base)) {
            snprintf(out, cap, "%s", base);
            return true;
        }

        snprintf(base, sizeof(base), "%s/wallpapers", dirs[i]);
        if (scan_dir(base, out, cap)) return true;
    }
    return false;
}

/* --------------------------------------------------------------- 缩放 */

/* cover：取源图中间一块（比例与屏幕一致）最近邻铺满，不变形也不留黑边 */
static void cover_blit(const uint8_t *src, int sw, int sh, uint8_t *dst, int dw, int dh)
{
    int cw, ch;
    if ((int64_t)sw * dh >= (int64_t)sh * dw) {
        ch = sh;
        cw = (int)((int64_t)sh * dw / dh);
    }
    else {
        cw = sw;
        ch = (int)((int64_t)sw * dh / dw);
    }
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    const int cx = (sw - cw) / 2;
    const int cy = (sh - ch) / 2;

    for (int y = 0; y < dh; y++) {
        int            sy  = cy + (int)((int64_t)y * ch / dh);
        const uint8_t *row = src + ((size_t)sy * (size_t)sw + (size_t)cx) * 2;
        uint16_t      *orow = (uint16_t *)(void *)(dst + (size_t)y * (size_t)dw * 2);
        for (int x = 0; x < dw; x++) {
            int sx = (int)((int64_t)x * cw / dw);
            orow[x] = ((const uint16_t *)(const void *)row)[sx];
        }
    }
}

/* --------------------------------------------------------------- 装载 */

static const size_t NEED = (size_t)WAC_SCR_W * (size_t)WAC_SCR_H * 2;

/* 读一个 raw；尺寸与屏幕不同就 cover 缩放到屏幕。成功返回 malloc 出来的屏幕尺寸缓冲。 */
static uint8_t *read_raw(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        WAC_LOG("wallpaper: cannot open %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) {
        WAC_LOG("wallpaper: %s is empty", path);
        fclose(f);
        return NULL;
    }

    /* 情况一：裸尺寸正好是屏幕 —— 直接读，零缩放 */
    if ((size_t)sz == NEED) {
        uint8_t *buf = (uint8_t *)malloc(NEED);
        if (!buf) {
            fclose(f);
            WAC_LOG("wallpaper: out of memory (%u bytes)", (unsigned)NEED);
            return NULL;
        }
        size_t rd = fread(buf, 1, NEED, f);
        fclose(f);
        if (rd != NEED) {
            free(buf);
            WAC_LOG("wallpaper: short read on %s", path);
            return NULL;
        }
        return buf;
    }

    /* 情况二：别的尺寸 —— 从文件名拿源宽高，cover 缩放 */
    int sw = 0, sh = 0;
    if (!parse_size(path, &sw, &sh)) {
        WAC_LOG("wallpaper: %s is %ld bytes, need %u (%dx%d rgb565); "
                "name has no WxH so it cannot be scaled",
                path, sz, (unsigned)NEED, (int)WAC_SCR_W, (int)WAC_SCR_H);
        fclose(f);
        return NULL;
    }
    const size_t src_need = (size_t)sw * (size_t)sh * 2;
    if ((size_t)sz != src_need) {
        WAC_LOG("wallpaper: %s claims %dx%d (=%u bytes) but is %ld bytes", path, sw, sh,
                (unsigned)src_need, sz);
        fclose(f);
        return NULL;
    }

    uint8_t *src = (uint8_t *)malloc(src_need);
    uint8_t *dst = (uint8_t *)malloc(NEED);
    if (!src || !dst) {
        free(src);
        free(dst);
        fclose(f);
        WAC_LOG("wallpaper: out of memory (%u + %u bytes)", (unsigned)src_need, (unsigned)NEED);
        return NULL;
    }
    size_t rd = fread(src, 1, src_need, f);
    fclose(f);
    if (rd != src_need) {
        free(src);
        free(dst);
        WAC_LOG("wallpaper: short read on %s", path);
        return NULL;
    }

    cover_blit(src, sw, sh, dst, (int)WAC_SCR_W, (int)WAC_SCR_H);
    free(src);

    WAC_LOG("wallpaper: %dx%d -> %dx%d (cover)", sw, sh, (int)WAC_SCR_W, (int)WAC_SCR_H);
    return dst;
}

static bool bg_load(void)
{
    char path[512];
    if (!find_wallpaper(path, sizeof(path))) return false;

    uint8_t *buf = read_raw(path);
    if (!buf) return false;

    s_buf = buf;

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w      = (uint32_t)WAC_SCR_W;
    s_dsc.header.h      = (uint32_t)WAC_SCR_H;
    s_dsc.header.stride = (uint32_t)WAC_SCR_W * 2;
    s_dsc.data_size     = NEED;
    s_dsc.data          = buf;

    s_wall = lv_image_create(lv_screen_active());
    lv_image_set_src(s_wall, &s_dsc);
    lv_obj_set_pos(s_wall, 0, 0);
    lv_obj_remove_flag(s_wall, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(s_wall, LV_OBJ_FLAG_CLICKABLE);

    WAC_LOG("wallpaper loaded: %s", path);
    return true;
}

/* --------------------------------------------------------------- 图层 */

/* 壁纸始终在第 0 层，遮罩第 1 层，页面（后创建 / move_foreground）在它们上面 */
static void bg_restack(void)
{
    if (s_wall) lv_obj_move_to_index(s_wall, 0);
    if (s_mask) lv_obj_move_to_index(s_mask, s_wall ? 1 : 0);
}

/* 遮罩只用来压暗壁纸。纯黑底上盖 50% 黑没有任何视觉效果，
   却要每帧多合成一层，所以没有壁纸时干脆不建。 */
/* 遮罩浓度默认 WAC_MASK_OPA；WAC_MASK_OPA 环境变量可临时改（调试/对比用） */
static lv_opa_t mask_opa(void)
{
    const char *e = getenv("WAC_MASK_OPA");
    if (e && *e) {
        int v = atoi(e);
        if (v >= 0 && v <= 255) return (lv_opa_t)v;
    }
    return WAC_MASK_OPA;
}

static void bg_mask_sync(void)
{
    if (s_wall && !s_mask) {
        s_mask = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_mask);
        lv_obj_set_size(s_mask, WAC_SCR_W, WAC_SCR_H);
        lv_obj_set_pos(s_mask, 0, 0);
        lv_obj_set_style_bg_color(s_mask, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(s_mask, mask_opa(), 0);
        lv_obj_remove_flag(s_mask, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(s_mask, LV_OBJ_FLAG_CLICKABLE);
    }
    else if (!s_wall && s_mask) {
        lv_obj_delete(s_mask);
        s_mask = NULL;
    }
    bg_restack();
}

static void bg_free_all(void)
{
    if (s_wall) {
        lv_obj_delete(s_wall);
        s_wall = NULL;
    }
    if (s_mask) {
        lv_obj_delete(s_mask);
        s_mask = NULL;
    }
    free(s_buf);
    s_buf = NULL;
}

void bg_reload(void)
{
    bg_free_all();
    if (!bg_load()) WAC_LOG("wallpaper: none -> pure black");
    bg_mask_sync();
}

void bg_init(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* 屏幕自己就是底板：没有壁纸时这里就是"纯黑"。
       默认主题给 screen 的样式是白色底，所以颜色和渐变都要显式压成黑，
       否则没有壁纸时会显示成 LVGL 的默认白（叠加遮罩后变灰）。 */
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_grad_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    if (!bg_load()) {
        WAC_LOG("wallpaper: none -> pure black");
    }
    bg_mask_sync();
}
