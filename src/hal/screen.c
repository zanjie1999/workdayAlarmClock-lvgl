/**
 * screen.c -- 熄屏 / 亮屏
 */
#include "screen.h"
#include "backlight.h"

static bool s_off;

#ifndef WAC_SIM
static void fb_blank(bool blank)
{
    FILE *f = fopen("/sys/class/graphics/fb0/blank", "w");
    if (!f) return;
    fprintf(f, "%d\n", blank ? 1 : 0);
    fclose(f);
}
#else
static void fb_blank(bool blank) { LV_UNUSED(blank); }
#endif

void screen_init(void)
{
    backlight_init();
    s_off = false;
    backlight_set(100);
}

void screen_on(void)
{
    if (!s_off && backlight_available() && backlight_get() > 0) return;
    s_off = false;
    backlight_set(100);
    fb_blank(false);
    WAC_LOG("screen on");
}

void screen_off(void)
{
    if (s_off) return;
    s_off = true;
    backlight_set(0);
    if (!backlight_available()) fb_blank(true);
    WAC_LOG("screen off");
}

bool screen_is_off(void)
{
    return s_off;
}
