/**
 * disp_sim.c -- Windows/GDI 模拟后端
 *
 * LVGL v9 自带 windows 驱动（LV_USE_WINDOWS），只依赖 GDI/User32，MinGW 可直接编。
 * 每个 display 一个独立窗口 + 独立线程；窗口消息回调内部自己 lv_lock()。
 * 因 LV_USE_OS = LV_OS_NONE（lv_lock 是空宏），**只开一个窗口**，多尺寸靠多进程验证。
 */
#include "wac.h"
#include "touch.h"

#ifdef WAC_SIM

#include <windows.h>

static lv_display_t *s_disp;

lv_display_t *wac_disp_init(void)
{
    /* 小屏放大显示，便于在桌面上看 160×128 的排版细节 */
    int32_t zoom = 100;
    if (WAC_MIN_DIM <= 140)      zoom = 300;
    else if (WAC_MIN_DIM <= 200) zoom = 250;
    else if (WAC_MIN_DIM <= 260) zoom = 200;
    else if (WAC_MIN_DIM <= 400) zoom = 150;

    wchar_t title[64];
    swprintf(title, 64, L"wac %dx%d  (logic %dx%d)", (int)WAC_SCR_W, (int)WAC_SCR_H, (int)WAC_SCR_W, (int)WAC_SCR_H);

    s_disp = lv_windows_create_display(title, WAC_SCR_W, WAC_SCR_H, zoom, false, true);
    if (!s_disp) {
        WAC_LOG("lv_windows_create_display failed");
        return NULL;
    }
    WAC_LOG("sim display %dx%d zoom=%d%%", (int)WAC_SCR_W, (int)WAC_SCR_H, zoom);
    return s_disp;
}

/* Windows 驱动的鼠标指针 indev 不会自动创建，必须显式 acquire 一次。
 * 不 acquire 就是"窗口能点、LVGL 收不到"——之前触摸完全没反应就是这个原因。 */
lv_indev_t *wac_touch_init(lv_display_t *disp)
{
    lv_indev_t *ind = lv_windows_acquire_pointer_indev(disp);
    WAC_LOG("sim pointer indev: %s", ind ? "ok" : "FAILED");
    return ind;
}

void wac_disp_deinit(void)
{
    /* 模拟器直接退出进程即可 */
}

void wac_sleep_ms(uint32_t ms)
{
    Sleep(ms);
}

uint32_t wac_millis(void)
{
    return (uint32_t)GetTickCount64();
}

/* 模拟器没有 master 之争，投屏交接是空操作 */
void wac_disp_release(void) {}
void wac_disp_reclaim(void) {}

#endif /* WAC_SIM */
