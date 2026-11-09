/**
 * backlight.c -- 背光（Linux 回退链）
 */
#include "backlight.h"

#include <stdlib.h>

static int  s_cur = 100;
static int  s_max = 255;
static bool s_ok;
static char s_path[256];

#ifdef WAC_SIM

/* 电脑上没有 /sys/class/backlight，设置页的亮度行默认不建。
   调布局时用 WAC_SIM_BACKLIGHT=1 假装有背光，好核对那一行的排版。 */
int  backlight_init(void) { s_ok = getenv("WAC_SIM_BACKLIGHT") != NULL; return s_ok ? 0 : -1; }
void backlight_set(int percent) { s_cur = percent; }
int  backlight_get(void) { return s_cur; }
bool backlight_available(void) { return s_ok; }

#else

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

static bool try_dir(const char *dir, const char *file)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, file);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return false;
    close(fd);
    snprintf(s_path, sizeof(s_path), "%s", path);
    return true;
}

static bool scan(const char *base_dir, const char *file)
{
    DIR *d = opendir(base_dir);
    if (!d) return false;
    bool found = false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char dir[256];
        snprintf(dir, sizeof(dir), "%s/%s", base_dir, e->d_name);
        if (try_dir(dir, file)) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

static int read_int(const char *path, int fallback)
{
    FILE *f = fopen(path, "r");
    if (!f) return fallback;
    int v = fallback;
    if (fscanf(f, "%d", &v) != 1) v = fallback;
    fclose(f);
    return v;
}

int backlight_init(void)
{
    s_ok = false;

    /* 1. 标准 backlight */
    if (scan("/sys/class/backlight", "brightness")) {
        char p[300];
        snprintf(p, sizeof(p), "%s/../max_brightness", s_path);
        int mx = read_int(p, 0);
        if (mx > 0) s_max = mx;
        s_ok = true;
    }
    /* 2. leds 下的 backlight */
    else if (scan("/sys/class/leds", "brightness")) {
        s_max = 255;
        s_ok  = true;
    }
    /* 3. GPIO（由 /etc/wac.conf 指定，这里读环境变量简化） */
    else {
        const char *gpio = getenv("WAC_BACKLIGHT_GPIO");
        if (gpio && gpio[0]) {
            snprintf(s_path, sizeof(s_path), "%s", gpio);
            s_ok = true;
        }
    }

    if (s_ok) WAC_LOG("backlight: %s (max=%d)", s_path, s_max);
    else WAC_LOG("backlight: no sysfs node, screen-off will rely on fb blank");

    return s_ok ? 0 : -1;
}

void backlight_set(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    s_cur = percent;
    if (!s_ok) return;

    int v = s_max * percent / 100;
    FILE *f = fopen(s_path, "w");
    if (!f) return;
    fprintf(f, "%d\n", v);
    fclose(f);
}

int  backlight_get(void) { return s_cur; }
bool backlight_available(void) { return s_ok; }

#endif /* WAC_SIM */
