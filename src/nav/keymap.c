/**
 * keymap.c -- 键位语义
 *
 * 模式由"绑到了几个角色"自动推导，规则完全对照需求：
 *   1 键：短按=焦点前移（走完整环）   长按=确定
 *   2 键：方向短=下一个 方向长=上一个   确认短=确定 确认长=返回
 *   3 键：上下短=焦点 上下长=连发       确认短=确定 确认长=返回
 *   4 键：同 3 键 + BACK 短=返回上一层（时钟页=播放/暂停），BACK 长=停止播放
 *   5 键：确认/上一个/下一个/音量加/音量减（没有返回键）
 *         —— 确认长按当返回用；时钟页确认短按=播放/暂停；音量键短按调音量、
 *            长按切下一曲/上一曲
 *   6 键：5 键 + BACK（返回键独立，确认长按改回"一键"）
 *
 * 音量键是"绝对语义"，跟当前在哪个页面、焦点在哪都无关（响铃/绑定向导除外）：
 *   短按 = 音量 ±5（时钟页顺带把底部音量条亮一下再自动收起）
 *   长按 = 下一曲 / 上一曲，只切一首，按住不连发
 *
 * "连发"= 按住不放就按 REPEAT_MS 反复触发短按那个动作（键盘连发的手感），
 * 和长按本身绑定在一起，不再像以前那样另做一套"长按调音量 + 弹浮窗"。
 * 确定/返回这类一次性动作不跟着连发，避免把开关来回拨。
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
    if (n > 6) n = 6;
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
   时钟页是根页面，没有"上一层"可回 —— 那里"返回"= 播放/暂停。 */
static void nav_back_or_exit_edit(void)
{
    if (focus_exit_edit()) return;
    if (page_current() == WAC_PAGE_CLOCK) {
        WAC_LOG("back on clock page -> play/pause");
        cmd_play_pause();
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

    /* 音量键：5/6 键模式专属，语义不随页面/焦点变化。
       短按调音量（时钟页顺便把底部音量条亮一下，1.5 秒后自己收）；
       长按切歌，只切一首 —— KEY_REPEAT 直接丢掉，否则按住会一口气跳十几首。 */
    if (role == ROLE_VOL_UP || role == ROLE_VOL_DOWN) {
        const bool up = (role == ROLE_VOL_UP);

        if (kind == KEY_SHORT) {
            /* 走 cmd_vol 而不是 cmd_vol_step：键上印的就是"加/减"，
               不该被设置里的"反转调节方向"翻过来（那是给方向键用的） */
            cmd_vol(up ? +CMD_VOL_STEP : -CMD_VOL_STEP);
            if (page_current() == WAC_PAGE_CLOCK) page_clock_vol_flash();
        }
        else if (kind == KEY_LONG) {
            if (up) cmd_next();
            else cmd_prev();
        }
        return;
    }

    /* 返回键：只有绑到它才会走到这里（5 键模式明确没有返回）。
       短=返回上一层（时钟页=播放/暂停），长=停止播放。 */
    if (role == ROLE_BACK) {
        if (kind == KEY_SHORT) {
            nav_back_or_exit_edit();
        }
        else if (kind == KEY_LONG) {
            WAC_LOG("back long -> stop");
            cmd_stop();
        }
        return;
    }

    switch (nav_mode()) {
        case 1:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) nav_dir(+1);
                else if (kind == KEY_LONG) focus_activate();
                /* KEY_REPEAT：1 键模式下只有"焦点前移 / 确定"两种含义，
                   连发会把确定重复触发（比如把开关来回拨），所以忽略 */
            }
            break;

        case 2:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) focus_activate();
                else if (kind == KEY_LONG) nav_back_or_exit_edit();
            }
            else {
                /* 唯一的方向键：短=下一个，长=上一个；继续持有就按"上一个"连发 */
                nav_dir(kind == KEY_SHORT ? +1 : -1);
            }
            break;

        case 3:
        case 4:
        case 5:
        case 6:
        default:
            if (role == ROLE_CONFIRM) {
                if (kind == KEY_SHORT) {
                    /* 5/6 键：时钟页的确认键给播放/暂停，不再展开音量条
                       （音量有独立键，短按就调，条会自己亮一下）。
                       万一这会儿还在调节态，那一下先当"退出"。 */
                    if (nav_mode() >= 5 && page_current() == WAC_PAGE_CLOCK) {
                        if (!focus_exit_edit()) cmd_play_pause();
                    }
                    else {
                        focus_activate();
                    }
                }
                else if (kind == KEY_LONG) {
                    /* 有独立返回键（4/6 键）时，确认长按让位给"一键"；
                       没有返回键（3/5 键）时长按就当返回 */
                    if (keys_code_of(ROLE_BACK) >= 0) cmd_one_key();
                    else nav_back_or_exit_edit();
                }
            }
            else if (role == ROLE_PREV) {
                /* 长按不再是"调音量 + 弹浮窗"：满 600ms 先走一步，
                   之后每 REPEAT_MS 再来一步，就是短按那个动作在连发 */
                nav_dir(-1);
            }
            else if (role == ROLE_NEXT) {
                nav_dir(+1);
            }
            break;
    }
}

void nav_init(void)
{
    keys_set_callback(nav_dispatch);
    /* 模式要等 wac_keys.conf 读完才准（main 里在这之后补一条 resolved 日志） */
    WAC_LOG("nav: dispatch ready");
}
