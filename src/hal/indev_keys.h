/**
 * indev_keys.h -- 物理按键扫描器
 *
 * 为什么不用 LVGL 的 evdev 驱动（已核实 v9.3 源码）：
 *   - lv_evdev 的按键映射表只认 13 个键，没有 KEY_POWER / KEY_VOLUMEUP；
 *   - lv_evdev_t 只存一个 key/state，不支持多设备；而 AKU 的 3 个键分在
 *     event0(KEY_POWER) 和 event1(VOLUMEUP/VOLUMEDOWN) 两个设备上；
 *   - LV_INDEV_TYPE_ENCODER 配 evdev 在驱动里是 default: break（空实现）。
 * 所以自己 poll 多个 fd，只输出 (角色, 短按/长按/连发)，完全不进 LVGL 输入子系统。
 *
 * 时序：30ms 去抖、600ms 进长按、之后每 180ms 发一次 KEY_REPEAT（像键盘连发），
 *       按下超 5s 未抬起自动复位（防卡键）。
 */
#ifndef WAC_INDEV_KEYS_H
#define WAC_INDEV_KEYS_H

#include "wac.h"

typedef enum {
    ROLE_NONE = 0,
    ROLE_CONFIRM,
    ROLE_PREV,
    ROLE_NEXT,
    ROLE_BACK,
    ROLE_COUNT
} key_role_t;

typedef enum {
    KEY_SHORT = 0,
    KEY_LONG,   /* 按下满 600ms 时发一次 */
    KEY_REPEAT, /* 长按继续持有：每 REPEAT_MS 发一次，抬起即停 */
} key_kind_t;

typedef void (*key_cb_t)(key_role_t role, key_kind_t kind);
/* 原始键事件：任何 EV_KEY（含未绑定的），绑定向导用 */
typedef void (*key_raw_cb_t)(int code, bool pressed);

/* 打开按键设备 / 初始化模拟按键 */
void keys_init(void);

/* 载入/保存绑定（wac_keys.conf） */
int  keys_load_config(const char *path);
int  keys_save_config(const char *path);

/* 绑定某个角色到 evdev code（sim 下是虚拟键码）；code<0 表示解除 */
void keys_bind(key_role_t role, int code);
int  keys_code_of(key_role_t role);
int  keys_role_of_code(int code);

/* 已绑定多少个角色（决定 1/2/3/4 键模式） */
int  keys_bound_count(void);

/* 清空所有绑定（重绑向导开头调用） */
void keys_clear(void);

void keys_set_callback(key_cb_t cb);
void keys_set_raw_callback(key_raw_cb_t cb);

/* 创建轮询 timer（20ms） */
void keys_start(void);

/* 最近一次收到的 (设备名, code)，绑定向导用来显示 */
const char *keys_last_device(void);
int         keys_last_code(void);

#endif /* WAC_INDEV_KEYS_H */
