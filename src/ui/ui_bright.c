/**
 * ui_bright.c -- 亮度（只走硬件背光）
 *
 * 这块板查过了：没有 /sys/class/backlight，没有 leds，没有 pwm，
 * disp 下也只有 fb*，背光根本调不了 —— 那就**不显示**设置里的亮度条
 * （页面用 backlight_available() 判断）。
 *
 * 所以这里不做软件调光遮罩：调不了就是调不了，用一层黑罩子假装调暗
 * 既不省电也会让人以为背光在工作。别的板子有 sysfs 节点时这里才真正生效。
 */
#include "ui_bright.h"
#include "ui_settings.h"
#include "backlight.h"

static int s_pct = 100;

void ui_bright_init(void)
{
    if (!backlight_available()) {
        /* 没有可调的背光：忽略配置文件里的历史值，别让屏幕莫名其妙变暗 */
        s_pct            = 100;
        g_set.brightness = 100;
        return;
    }

    ui_bright_set(g_set.brightness);
}

void ui_bright_set(int pct)
{
    if (pct < WAC_BRIGHT_MIN) pct = WAC_BRIGHT_MIN;
    if (pct > 100) pct = 100;

    s_pct            = pct;
    g_set.brightness = pct;
    backlight_set(pct);
}

int ui_bright_get(void) { return s_pct; }
