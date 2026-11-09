/**
 * ui_settings.c -- 界面选项的默认值与落盘
 */
#include "ui_settings.h"

ui_settings_t g_set;

static char s_path[128] = "wac_ui.conf";

/* 音量缓存的落盘时刻；0 = 没有待写的东西 */
static uint32_t s_vol_dirty_at;

void ui_set_load(const char *path)
{
    if (path && *path) snprintf(s_path, sizeof(s_path), "%s", path);

    /* 默认值：显示秒勾上；电量 / 反转音量 / 24 小时制都不勾 */
    g_set.show_seconds = true;
    g_set.show_battery = false;
    g_set.vol_reverse  = false;
    g_set.hour24       = false;
    g_set.brightness   = 100;
    g_set.vol_cache    = 50;

    s_vol_dirty_at = 0;

    FILE *f = fopen(s_path, "r");
    if (!f) {
        WAC_LOG("settings: %s not found -> default (sec=1 bat=0 volrev=0 h24=0)", s_path);
    }
    else {
        char line[128];
        while (fgets(line, sizeof(line), f)) {
            char key[64];
            int  val;
            if (sscanf(line, " %63[^= \t\r\n] = %d", key, &val) != 2) continue;
            if (!strcmp(key, "show_seconds"))      g_set.show_seconds = (val != 0);
            else if (!strcmp(key, "show_battery")) g_set.show_battery = (val != 0);
            else if (!strcmp(key, "vol_reverse"))  g_set.vol_reverse = (val != 0);
            else if (!strcmp(key, "hour24"))       g_set.hour24 = (val != 0);
            else if (!strcmp(key, "brightness")) {
                if (val < 10) val = 10;
                if (val > 100) val = 100;
                g_set.brightness = val;
            }
            else if (!strcmp(key, "volume")) {
                if (val < 0) val = 0;
                if (val > 100) val = 100;
                g_set.vol_cache = val;
            }
        }
        fclose(f);
    }

    /* 开机第一屏的音量：Go 还没报 VOL 之前先拿上次的值顶上，
       否则播放页的音量条会从 0 开始显示（"启动时音量不回显"就是这个）。 */
    if (!g_state.volume_known) {
        g_state.volume       = (uint8_t)g_set.vol_cache;
        g_state.volume_known = true;
    }

    WAC_LOG("settings: sec=%d bat=%d volrev=%d h24=%d bright=%d vol=%d (%s)",
            (int)g_set.show_seconds, (int)g_set.show_battery, (int)g_set.vol_reverse,
            (int)g_set.hour24, g_set.brightness, g_set.vol_cache, s_path);
}

void ui_set_note_volume(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    if (g_set.vol_cache == v) return;

    g_set.vol_cache = v;
    s_vol_dirty_at  = wac_millis() + 1000; /* 稳 1 秒再写盘 */
}

void ui_set_flush_volume(void)
{
    if (!s_vol_dirty_at || wac_millis() < s_vol_dirty_at) return;
    s_vol_dirty_at = 0;
    ui_set_save();
}

/* 反转调节方向：音量、亮度这些"能用方向键调"的条都走这一个出口，
 * 免得每个页面各写一遍、漏掉一处（之前亮度条就漏了）。 */
int ui_adj_dir(int dir)
{
    if (dir == 0) return 0;
    return g_set.vol_reverse ? -dir : dir;
}

void ui_set_save(void)
{
    FILE *f = fopen(s_path, "w");
    if (!f) {
        WAC_LOG("settings: cannot write %s", s_path);
        return;
    }
    fprintf(f,
            "show_seconds=%d\nshow_battery=%d\nvol_reverse=%d\nhour24=%d\nbrightness=%d\n"
            "volume=%d\n",
            (int)g_set.show_seconds, (int)g_set.show_battery, (int)g_set.vol_reverse,
            (int)g_set.hour24, g_set.brightness, g_set.vol_cache);
    fclose(f);
    WAC_LOG("settings: saved sec=%d bat=%d volrev=%d h24=%d bright=%d vol=%d",
            (int)g_set.show_seconds, (int)g_set.show_battery, (int)g_set.vol_reverse,
            (int)g_set.hour24, g_set.brightness, g_set.vol_cache);
}
