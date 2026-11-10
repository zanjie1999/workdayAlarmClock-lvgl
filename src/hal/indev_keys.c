/**
 * indev_keys.c -- 物理按键扫描器（多 fd poll + 去抖 + 长按/连发）
 *
 * 真机：poll 所有 /dev/input/event*，只认绑定表里的 code。
 * 模拟器：用 GetAsyncKeyState 轮询几个键，行为完全一致
 *         Enter/Space=CONFIRM  Up=PREV  Down=NEXT  Esc=BACK  小键盘 +/-=音量±
 */
#include "indev_keys.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define DEBOUNCE_MS 30
#define LONG_MS     600
#define REPEAT_MS   180  /* 进长按后，每隔这么久发一次 KEY_REPEAT（键盘连发那种手感） */
#define STUCK_MS    5000

typedef struct {
    int  code; /* evdev code / 虚拟键码；<0 未绑定 */
    bool down;
    bool long_sent;
    uint32_t down_at;
    uint32_t up_at;    /* 去抖用 */
    uint32_t repeat_at; /* 下一次该发 KEY_REPEAT 的时刻 */
} role_state_t;

static role_state_t s_role[ROLE_COUNT];
static key_cb_t     s_cb;
static key_raw_cb_t s_raw_cb;
static char         s_cfg_path[256] = "wac_keys.conf";

static char s_last_dev[64];
static int  s_last_code = -1;

static const char *ROLE_KEY[ROLE_COUNT] = {"",     "confirm", "prev", "next",
                                           "back", "volup",   "voldown"};
static const char *ROLE_CN[ROLE_COUNT]  = {"", "确定", "上一个", "下一个", "返回", "音量加",
                                           "音量减"};

const char *keys_role_name(key_role_t r)
{
    if (r <= ROLE_NONE || r >= ROLE_COUNT) return "";
    return ROLE_CN[r];
}

const char *keys_last_device(void) { return s_last_dev; }
int         keys_last_code(void) { return s_last_code; }

/* ------------------------------------------------------------- 绑定表 */
void keys_clear(void)
{
    for (int i = 1; i < ROLE_COUNT; i++) {
        s_role[i].code = -1;
        s_role[i].down = false;
        s_role[i].long_sent = false;
    }
}

void keys_bind(key_role_t role, int code)
{
    if (role <= ROLE_NONE || role >= ROLE_COUNT) return;
    /* 同一个 code 不能绑两次 */
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (i != role && s_role[i].code == code) s_role[i].code = -1;
    }
    s_role[role].code = code;
}

int keys_code_of(key_role_t role)
{
    if (role <= ROLE_NONE || role >= ROLE_COUNT) return -1;
    return s_role[role].code;
}

int keys_role_of_code(int code)
{
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (s_role[i].code == code && code >= 0) return i;
    }
    return ROLE_NONE;
}

int keys_bound_count(void)
{
    int n = 0;
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (s_role[i].code >= 0) n++;
    }
    return n;
}

void keys_set_callback(key_cb_t cb) { s_cb = cb; }
void keys_set_raw_callback(key_raw_cb_t cb) { s_raw_cb = cb; }

/* ------------------------------------------------------------- 配置 */
static const char *role_to_key(key_role_t r)
{
    if (r <= ROLE_NONE || r >= ROLE_COUNT) return "";
    return ROLE_KEY[r];
}

static key_role_t key_to_role(const char *k)
{
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (strcmp(k, ROLE_KEY[i]) == 0) return (key_role_t)i;
    }
    return ROLE_NONE;
}

int keys_load_config(const char *path)
{
    /* 先把路径记下来再打开：文件不存在时（首次启动）向导保存要用它，
       否则 s_cfg_path 还停在默认的 "wac_keys.conf"（相对 cwd），
       配置会落到应用根目录、下次开机又找不到 → 每次开机都重走一遍向导。 */
    snprintf(s_cfg_path, sizeof(s_cfg_path), "%s", path);

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    keys_clear();
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *k = line;
        char *v = eq + 1;
        while (*k == ' ' || *k == '\t') k++;
        char *ke = k + strlen(k);
        while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t')) *--ke = '\0';

        if (strcmp(k, "mode") == 0) continue;

        key_role_t r = key_to_role(k);
        if (r == ROLE_NONE) continue;

        /* 值形如 KEY_POWER:116，取冒号后的数字。
           空值（`prev=` / `back=`）必须当"未绑定"跳过：atoi("") 返回 0，
           而这里原来判的是 code >= 0，于是空槽全被绑到 code 0 上。
           后果很隐蔽——只填了 2 个键，加载后却报 4 bound，nav_mode() 跟着
           变成 4 键模式，长按确认走的是 cmd_one_key() 而不是"返回"。
           code 0 本身是 KEY_RESERVED，也不是有效按键。 */
        char *val = v;
        while (*val == ' ' || *val == '\t') val++;
        char *colon = strrchr(val, ':');
        char *num   = colon ? colon + 1 : val;
        while (*num == ' ' || *num == '\t') num++;
        if (*num == '\0' || *num == '\n' || *num == '\r') continue;

        int code = atoi(num);
        if (code > 0) s_role[r].code = code;
    }
    fclose(f);
    WAC_LOG("keys: loaded %s (%d bound)", path, keys_bound_count());
    return 0;
}

const char *keys_config_path(void) { return s_cfg_path; }

int keys_save_config(const char *path)
{
    if (!path) return -1;

    /* path 很可能就是 s_cfg_path 本身 —— 绑定向导收尾时传的是 keys_config_path()。
       之前这里直接 snprintf(s_cfg_path, sizeof, "%s", path)，目标和源是同一块内存，
       属于重叠拷贝（未定义行为），实测把路径写坏，fopen 失败后静默返回 -1：
       于是 wac_keys.conf 从来没落过盘，每次开机都得重走一遍绑定向导。
       先拷到局部缓冲，彻底避开自引用。 */
    char target[sizeof(s_cfg_path)];
    snprintf(target, sizeof(target), "%s", path);
    memcpy(s_cfg_path, target, sizeof(s_cfg_path));

    FILE *f = fopen(target, "w");
    if (!f) {
        WAC_LOG("keys: 写入失败 %s (%s)", target, strerror(errno));
        return -1;
    }
    for (int i = 1; i < ROLE_COUNT; i++) {
        if (s_role[i].code >= 0) fprintf(f, "%s=%d\n", ROLE_KEY[i], s_role[i].code);
        else fprintf(f, "%s=\n", ROLE_KEY[i]);
    }
    fprintf(f, "mode=%d\n", keys_bound_count());
    fclose(f);
    WAC_LOG("keys: saved %s (%d bound)", target, keys_bound_count());
    return 0;
}

/* 删掉绑定表文件。只在设置页的"重新绑定按键"里用。
   两个刻意的选择：
     1) 删文件而不是写一张空表 —— 空表还留着文件，keys_load_config() 会返回 0，
        开机的"没有配置就走向导"判定失效，同时 0 个绑定 → 只剩触摸能用；
     2) 这里不清内存里的绑定 —— 向导第 1 步按到键才 keys_clear()，
        在此之前用户的旧按键仍然可用。真在这儿清掉的话，向导半途退出
        （带触摸的板子 60s 无按键会自动退）会留下一台没有可用按键的设备。 */
int keys_remove_config(void)
{
    /* s_cfg_path 是 keys_load_config() 收到的那条路径（开机就设好了），
       所以删的一定是"当初读的那一份"，不是 cwd 下的同名文件。 */
    if (remove(s_cfg_path) == 0) {
        WAC_LOG("keys: removed %s", s_cfg_path);
        return 0;
    }
    if (errno == ENOENT) {
        WAC_LOG("keys: %s 不存在，视作已删除", s_cfg_path);
        return 0;
    }
    WAC_LOG("keys: 删除 %s 失败 (%s)", s_cfg_path, strerror(errno));
    return -1;
}

/* ------------------------------------------------------------- 事件派发 */

static void emit(key_role_t role, key_kind_t kind)
{
    if (s_cb) s_cb(role, kind);
}

static void on_code(int code, bool pressed, const char *dev)
{
    if (dev) snprintf(s_last_dev, sizeof(s_last_dev), "%s", dev);
    s_last_code = code;
    if (s_raw_cb) s_raw_cb(code, pressed);

    int role = keys_role_of_code(code);
    if (role == ROLE_NONE) {
        /* 未绑定：raw 回调已经拿到，这里直接返回 */
        return;
    }

    role_state_t *r = &s_role[role];
    uint32_t now    = wac_millis();

    if (pressed) {
        if (r->down) return;
        /* 去抖：距上次抬起太近，丢弃 */
        if (r->up_at && (now - r->up_at) < DEBOUNCE_MS) return;
        r->down      = true;
        r->long_sent = false;
        r->down_at   = now;
    }
    else {
        if (!r->down) return;
        uint32_t held = now - r->down_at;
        r->down       = false;
        r->up_at      = now;
        if (held < DEBOUNCE_MS) return; /* 抖动 */
        if (!r->long_sent) emit((key_role_t)role, KEY_SHORT);
    }
}

/* ------------------------------------------------------------- 模拟器 */
#ifdef WAC_SIM

#include <windows.h>

typedef struct {
    int  vk;
    int  code; /* 虚构的 code，和绑定表一致 */
} sim_key_t;

/* 用常见键：回车=确认，↑=上一个，↓=下一个，Esc=返回，小键盘 +/-=音量± */
static const sim_key_t SIM_KEYS[] = {
    {VK_RETURN,   28},  /* KEY_ENTER */
    {VK_UP,       103}, /* KEY_UP */
    {VK_DOWN,     108}, /* KEY_DOWN */
    {VK_ESCAPE,   158}, /* KEY_BACK */
    {VK_SPACE,    57},  /* KEY_SPACE */
    {VK_ADD,      115}, /* KEY_VOLUMEUP */
    {VK_SUBTRACT, 114}, /* KEY_VOLUMEDOWN */
};

static void sim_poll(void)
{
    static bool prev[sizeof(SIM_KEYS) / sizeof(SIM_KEYS[0])];
    for (unsigned i = 0; i < sizeof(SIM_KEYS) / sizeof(SIM_KEYS[0]); i++) {
        bool down = (GetAsyncKeyState(SIM_KEYS[i].vk) & 0x8000) != 0;
        if (down != prev[i]) {
            prev[i] = down;
            on_code(SIM_KEYS[i].code, down, "sim-kbd");
        }
    }
}

void keys_init(void)
{
    keys_clear();
    /* 默认 4 键（回车/↑/↓/Esc）。WAC_SIM_KEYS=5 或 6 时把小键盘 + / - 也绑成音量键，
       用来在本机复现 5/6 键模式：
         5 键 = 确定 / 上 / 下 / 音量加 / 音量减（没有返回）
         6 键 = 再加上 Esc 返回 */
    keys_bind(ROLE_CONFIRM, 28);
    keys_bind(ROLE_PREV, 103);
    keys_bind(ROLE_NEXT, 108);

    int want = 4;
    const char *env = getenv("WAC_SIM_KEYS");
    if (env && *env) want = atoi(env);

    if (want == 5) {
        /* 5 键：两个音量键 + 没有返回 */
        keys_bind(ROLE_VOL_UP, 115);
        keys_bind(ROLE_VOL_DOWN, 114);
    }
    else if (want >= 6) {
        keys_bind(ROLE_VOL_UP, 115);
        keys_bind(ROLE_VOL_DOWN, 114);
        keys_bind(ROLE_BACK, 158);
    }
    else {
        keys_bind(ROLE_BACK, 158); /* 默认 4 键：确定/上/下/返回 */
    }

    WAC_LOG("keys: sim %d keys (Enter/Up/Down%s%s)", keys_bound_count(),
            want >= 5 ? "/Num+,-" : "", want >= 6 ? "/Esc" : "");
}

/* ------------------------------------------------------------- 真机 */
#else

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#define MAX_DEV 12

static int  s_fd[MAX_DEV];
static int  s_nfd;
static char s_devname[MAX_DEV][32];

void keys_init(void)
{
    keys_clear();
    s_nfd = 0;

    DIR *d = opendir("/dev/input");
    if (!d) {
        WAC_LOG("keys: no /dev/input");
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_nfd < MAX_DEV) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[128];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;

        /* 只留支持 EV_KEY 的设备 */
        unsigned long evbit = 0;
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit) < 0 ||
            !(evbit & (1UL << EV_KEY))) {
            close(fd);
            continue;
        }
        char name[64] = {0};
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        s_fd[s_nfd] = fd;
        snprintf(s_devname[s_nfd], sizeof(s_devname[s_nfd]), "%s", e->d_name);
        WAC_LOG("keys: %s (%s)", path, name);
        s_nfd++;
    }
    closedir(d);
    WAC_LOG("keys: %d device(s)", s_nfd);
}

static void evdev_poll(void)
{
    if (s_nfd <= 0) return;

    struct pollfd pfd[MAX_DEV];
    for (int i = 0; i < s_nfd; i++) {
        pfd[i].fd      = s_fd[i];
        pfd[i].events  = POLLIN;
        pfd[i].revents = 0;
    }
    int rc = poll(pfd, (nfds_t)s_nfd, 0);
    if (rc <= 0) return;

    for (int i = 0; i < s_nfd; i++) {
        if (!(pfd[i].revents & POLLIN)) continue;
        struct input_event ev;
        while (read(s_fd[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type != EV_KEY) continue;
            if (ev.value == 2) continue; /* 内核自动重复，丢掉 */
            on_code(ev.code, ev.value == 1, s_devname[i]);
        }
    }
}

#endif /* WAC_SIM */

/* ------------------------------------------------------------- 长按 / 连发 */
static void check_long(void)
{
    uint32_t now = wac_millis();
    for (int i = 1; i < ROLE_COUNT; i++) {
        role_state_t *r = &s_role[i];
        if (!r->down) continue;
        uint32_t held = now - r->down_at;

        if (!r->long_sent) {
            if (held >= LONG_MS) {
                r->long_sent = true;
                r->repeat_at = now + REPEAT_MS;
                emit((key_role_t)i, KEY_LONG);
            }
        }
        else if ((int32_t)(now - r->repeat_at) >= 0) {
            /* 以"现在"为基准重新排期，不用 repeat_at + REPEAT_MS：
               主循环偶尔卡一下时不会攒出一串补发的重复事件 */
            r->repeat_at = now + REPEAT_MS;
            emit((key_role_t)i, KEY_REPEAT);
        }

        if (held > STUCK_MS) {
            /* 卡键：可能是抬起事件丢了，强制复位不发事件。
               代价是按住超过 5s 就不再连发，当安全阀用。 */
            WAC_LOG("keys: role %d stuck, reset", i);
            r->down      = false;
            r->long_sent = false;
            r->up_at     = now;
        }
    }
}

static void keys_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
#ifdef WAC_SIM
    sim_poll();
#else
    evdev_poll();
#endif
    check_long();
}

void keys_start(void)
{
    lv_timer_create(keys_timer_cb, 20, NULL);
}
