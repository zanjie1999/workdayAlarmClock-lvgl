/**
 * dev_shot.c -- 开发用截屏
 *
 * 用 lv_snapshot 把整个 active screen 渲染到一块 RGB565 缓冲，再手写一个
 * 24bit BMP。这样做不依赖驱动、不依赖窗口是否在最前面，适合在自动化脚本里
 * 抓图核对排版（焦点框是否完整、字有没有超出边界）。
 */
#include "dev_shot.h"

#ifdef WAC_SIM

#include "wac.h"
#include <stdlib.h>

/* 放大倍数：WAC_SHOT_SCALE=4 时输出 4 倍最近邻图，方便肉眼核对 1px 细节 */
static int shot_scale(void)
{
    const char *s = getenv("WAC_SHOT_SCALE");
    int         v = s ? atoi(s) : 1;
    if (v < 1) v = 1;
    if (v > 8) v = 8;
    return v;
}

static void put_u16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void put_u32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f);
    fputc((v >> 24) & 0xFF, f);
}

void dev_shot(const char *path)
{
    if (!path || !*path) return;

    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!snap) {
        WAC_LOG("shot: snapshot failed");
        return;
    }

    const int32_t w     = (int32_t)snap->header.w;
    const int32_t h     = (int32_t)snap->header.h;
    const uint32_t strd = snap->header.stride;
    const uint8_t *data = snap->data;

    const int32_t scale     = shot_scale();
    const int32_t ow        = w * scale;
    const int32_t oh        = h * scale;
    const int32_t row_bytes = (ow * 3 + 3) / 4 * 4;
    const uint32_t img_size = (uint32_t)row_bytes * (uint32_t)oh;
    const uint32_t file_size = 54 + img_size;

    FILE *f = fopen(path, "wb");
    if (!f) {
        WAC_LOG("shot: cannot open %s", path);
        lv_draw_buf_destroy(snap);
        return;
    }

    fputc('B', f);
    fputc('M', f);
    put_u32(f, file_size);
    put_u16(f, 0);
    put_u16(f, 0);
    put_u32(f, 54);

    put_u32(f, 40);
    put_u32(f, (uint32_t)ow);
    put_u32(f, (uint32_t)oh);
    put_u16(f, 1);
    put_u16(f, 24);
    put_u32(f, 0); /* BI_RGB */
    put_u32(f, img_size);
    put_u32(f, 2835);
    put_u32(f, 2835);
    put_u32(f, 0);
    put_u32(f, 0);

    /* BMP 自下而上存；最近邻放大：一列像素横向重复 scale 次，一行重复 scale 遍 */
    for (int32_t y = h - 1; y >= 0; y--) {
        const uint16_t *row = (const uint16_t *)(data + (uint32_t)y * strd);
        for (int32_t rep = 0; rep < scale; rep++) {
            for (int32_t x = 0; x < w; x++) {
                uint16_t c = row[x];
                uint8_t  r = (uint8_t)(((c >> 11) & 0x1F) << 3);
                uint8_t  g = (uint8_t)(((c >> 5) & 0x3F) << 2);
                uint8_t  b = (uint8_t)((c & 0x1F) << 3);
                for (int32_t k = 0; k < scale; k++) {
                    fputc(b, f);
                    fputc(g, f);
                    fputc(r, f);
                }
            }
            for (int32_t i = ow * 3; i < row_bytes; i++) fputc(0, f);
        }
    }

    fclose(f);
    lv_draw_buf_destroy(snap);
    WAC_LOG("shot: %s (%dx%d x%d)", path, (int)w, (int)h, (int)scale);
}

#else

void dev_shot(const char *path) { (void)path; }

#endif /* WAC_SIM */
