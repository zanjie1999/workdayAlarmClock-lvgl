/**
 * keymap.c -- 键位语义
 *
 * 模式由"绑到了几个角色"自动推导，规则完全对照需求：
 *   1 键：短按=焦点前移（走完整环）   长按=确定
 *   2 键：方向短=下一个 方向长=上一个   确认短=确定 确认长=返回
 *   3 键：上下短=焦点 上下长=音量±      确认短=确定 确认长=返回
 *   4 键：同 3 键 + BACK 短=返回上一层，BACK 长=回主页
 *
 * 翻页：焦点走到本页头/尾再继续按，就切到相邻页（时钟 → 播放 → 设置 → 时钟，
 * 反过来同理）。这样即使只有上下两个键也永远走得完所有页面。
 *
 * 响铃态例外：任何短按都直接停止闹钟（不等长按）。
 */
#include "keymap.h"
#include "focus.h"
#include "page.h"
#include "cmd.h"
#include "ui_vol.h"
#include "ui_alarm.h"
#include "bind_wizard.h"

/* 循环翻页的顺序：时钟 → 播放 → 设置 → 时钟 */
static const wac_page_id_t PAGE_RING[] = {
    WAC_PAGE_CLOCK, WAC_PAGE_PLAYER, WAC_PAGE_SETTINGS,
};

int nav_mode(void)
{
    int n = keys_bound_count();
    if (n < 1) n = 1;
    if (n > 4) n = 4;
    return n;
}

/* 切到环上的下一页（dir=+1 下一张 / -1 上一张）。触摸滑动也走这里。 */
void nav_page_dir(int dir)
{
    const int n = (int)(sizeof(PAGE_RING) / sizeof(PAGE_RING[0]));

    int cur = -1;
    for (int i = 0; i < n; i++) {
        if (PAGE_RING[i] == page_current()) cur = i;
    }
    if (cur < 0) cur = 0;

    int next = (cur + dir + n) % n;
    page_set_dir(PAGE_RING[next], dir > 0 ? +1 : -1);
}

/* 方向键的唯一出口：调节态里交给控件自己调值，否则先走焦点，走到头就翻页 */
static void nav_dir(int dir)
{
    if (focus_in_edit()) {
        focus_edit_step(dir);
        return;
    }
    if (!focus_step(dir)) nav_page_dir(dir);
}

/* 返回上一层：在调节态里先退出调节（BACK 键在音量条上应该只是"退出来"）。
   时钟页是根页面，没有"上一层"可回 —— 那里返回等于给 Go 发一条 1key。 */
static void nav_back_or_exit_edit(void)
{
    if (focus_exit_edit()) return;
    if (page_current() == WAC_PAGE_CLOCK) {
        WAC_LOG("back on clock page -> 1key");
        cmd_one_key();
        return;
    }
    nav_back();
}

/* ------------------------------------------------------------------ */
void nav_dispatch(key_role_t role, key_kind_t kind)
{
    if (role == ROLE_NONE) return;
    if (wizard_active()) return; /* 绑定向导期间键盘完全归它 */

    /* 按了实体键：说明这会儿不是用手指，焦点框该亮出来了
       （触摸时会被 focus_set_visible(false) 收起来，见 main.c 的 indev 回调） */
    focus_set_visible(true);

    WAC_LOG_DEBUG("key: role=%d kind=%d mode=%d page=%d", (int)role, (int)kind, nav_mode(),
                  (int)page_current());

    /* 响铃：任意键（短按、长按都算）直接停止，不等长按 */
    if (g_state.ringing) {
        ui_alarm_stop_by_user();
        return;
    }

    /* 4 键的 BACK：短=返回上一层，长=回主页
       只有真的凑齐 4 键时才这么解释；2/3 键向导里跳步后可能误绑到 BACK，
       那时它应该退回"方向键"的角色，否则短按会变成返回、焦点动不了。 */
    if (role == ROLE_BACK && nav_mode() >= 4) {
        if (kind == KEY_SHORT) nav_back_or_exit_edit();
        else nav_home();
        return;
    }

    switch (nav_mode()) {
        case 1:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) nav_dir(+1);
                else focus_activate();
            }
            break;

        case 2:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) focus_activate();
                else nav_back_or_exit_edit();
            }
            else {
                /* 唯一的方向键：短=下一个，长=上一个 */
                nav_dir(kind == KEY_SHORT ? +1 : -1);
            }
            break;

        case 3:
        case 4:
        default:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) {
                    focus_activate();
                }
                else {
                    /* 3 键：确认长按=返回；4 键：确认长按=一键（1key） */
                    if (nav_mode() == 3) nav_back_or_exit_edit();
                    else cmd_one_key();
                }
            }
            else if (role == ROLE_PREV) {
                if (kind == KEY_SHORT) {
                    nav_dir(-1);
                }
                else {
                    cmd_vol_step(+1); /* 方向可被设置页"反转音量方向"翻掉 */
                    ui_vol_poke(g_state.volume);
                }
            }
            else if (role == ROLE_NEXT) {
                if (kind == KEY_SHORT) {
                    nav_dir(+1);
                }
                else {
                    cmd_vol_step(-1);
                    ui_vol_poke(g_state.volume);
                }
            }
            break;
    }
}

void nav_init(void)
{
    keys_set_callback(nav_dispatch);
    WAC_LOG("nav: %d-key mode", nav_mode());
}
