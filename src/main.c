/**
 * main.c -- 启动入口
 *
 * 顺序很关键：lv_init → display（要先有 display 才有 lv_screen_active）
 *            → theme（算 token）→ bg（挂 bottom layer）→ page（建 UI）
 *            → 输入 → 协议/子进程
 */
#include "page.h"
#include "theme.h"
#include "layer_bg.h"
#include "ui_bright.h"
#include "ui_alarm.h"
#include "ui_font.h"
#include "screen.h"
#include "indev_keys.h"
#include "keymap.h"
#include "bind_wizard.h"
#include "ui_settings.h"
#include "focus.h"
#include "touch.h"
#include "swipe.h"
#include "dev_shot.h"
#include "go_proc.h"
#include "cmd.h"
#include "event_parse.h"
#include "ui_pause.h"

#include <stdlib.h>

#ifdef WAC_SIM
void mock_go_init(void);

/* 模拟器自检：WAC_AUTOTEST=1 时按脚本跑一遍导航，核对翻页 / 勾选的日志。
 * 真机不编这段，平时也不触发。 */
static void st_tick(int frames)
{
    for (int i = 0; i < frames; i++) {
        lv_lock();
        lv_timer_handler();
        lv_unlock();
        wac_sleep_ms(20);
    }
}

static void st_press(const char *name, key_role_t r, key_kind_t k)
{
    nav_dispatch(r, k);
    st_tick(4);
    WAC_LOG("AUTOTEST %-15s -> page=%d focus=%d vol=%d edit=%d", name, (int)page_current(),
            focus_index(), (int)g_state.volume, focus_in_edit() ? 1 : 0);
}

/* 当前焦点项是否带着"画框"的状态位 */
static int vis_state(void)
{
    lv_obj_t *o = focus_obj();
    return (o && lv_obj_has_state(o, THEME_ST_FOCUS_VIS)) ? 1 : 0;
}

static void st_vis(const char *name)
{
    WAC_LOG("AUTOTEST VIS %-15s -> page=%d vis=%d state=%d", name, (int)page_current(),
            focus_visible() ? 1 : 0, vis_state());
}

static void selftest(lv_indev_t *touch)
{
    st_tick(10);

    /* 时钟 → 播放。进播放页焦点已经落在"播放/暂停"（索引 1），
       所以往后按 3 下就到音量条，不像以前要按 4 下。 */
    st_press("NEXT x1", ROLE_NEXT, KEY_SHORT);    /* page=1 focus=1 */
    st_press("NEXT x2", ROLE_NEXT, KEY_SHORT);    /* focus=2 */
    st_press("NEXT x3", ROLE_NEXT, KEY_SHORT);    /* focus=3 */
    st_press("NEXT x4", ROLE_NEXT, KEY_SHORT);    /* focus=4 = 音量条，不翻页 */

    /* 确认进调节态 → 方向键调音量 → 再确认退出 */
    st_press("VOL ENTER", ROLE_CONFIRM, KEY_SHORT); /* edit=1 */
    st_press("VOL BACK", ROLE_BACK, KEY_SHORT);     /* BACK 只退出调节，不翻页 */
    st_press("VOL ENTER2", ROLE_CONFIRM, KEY_SHORT);/* edit=1 再进 */
    st_press("VOL UP", ROLE_PREV, KEY_SHORT);       /* vol +5 */
    st_press("VOL UP2", ROLE_PREV, KEY_SHORT);      /* vol +5 */
    st_press("VOL DOWN", ROLE_NEXT, KEY_SHORT);     /* vol -5 */
    st_press("VOL LEAVE", ROLE_CONFIRM, KEY_SHORT); /* edit=0，焦点仍在音量条 */

    /* 焦点在末项再往下 → 翻到设置页 */
    st_press("TO SETTINGS", ROLE_NEXT, KEY_SHORT);  /* page=2 focus=0 */
    st_press("SET 1", ROLE_CONFIRM, KEY_SHORT);     /* 取消"显示秒" */
    st_press("SET NEXT", ROLE_NEXT, KEY_SHORT);     /* focus=1 */
    st_press("SET 2", ROLE_CONFIRM, KEY_SHORT);     /* 勾上"显示电量" */

    /* 反转音量方向 / 24 小时制：先确认默认都没勾，再逐项切一次 */
    WAC_LOG("AUTOTEST DEFAULTS        -> volrev=%d h24=%d", (int)g_set.vol_reverse,
            (int)g_set.hour24);
    st_press("SET NEXT2", ROLE_NEXT, KEY_SHORT);    /* focus=2 反转音量方向 */
    st_press("SET 3", ROLE_CONFIRM, KEY_SHORT);     /* 勾上 */
    st_press("SET NEXT3", ROLE_NEXT, KEY_SHORT);    /* focus=3 时间24小时制 */
    st_press("SET 4", ROLE_CONFIRM, KEY_SHORT);     /* 勾上 */
    WAC_LOG("AUTOTEST TOGGLED         -> volrev=%d h24=%d", (int)g_set.vol_reverse,
            (int)g_set.hour24);

    /* 音量方向：同一个 dir=+1，勾了反转就应该"减"，不勾就应该"加" */
    {
        int v0 = (int)g_state.volume;
        cmd_vol_step(+1);
        st_tick(1);
        WAC_LOG("AUTOTEST VOL rev=%d  step+1 -> %d (from %d)", (int)g_set.vol_reverse,
                (int)g_state.volume, v0);
        g_set.vol_reverse = false;
        v0                = (int)g_state.volume;
        cmd_vol_step(+1);
        st_tick(1);
        WAC_LOG("AUTOTEST VOL rev=%d  step+1 -> %d (from %d)", (int)g_set.vol_reverse,
                (int)g_state.volume, v0);
    }

    /* 动作项：确认后应向 Go 发命令并跳回时钟页（日志里看 cmd> ip / cmd> update） */
    st_press("SET NEXT4", ROLE_NEXT, KEY_SHORT);    /* focus=4 获取IP */
    st_press("SET IP", ROLE_CONFIRM, KEY_SHORT);    /* 发 ip -> page=0 */

    st_press("BACK TO SET", ROLE_PREV, KEY_SHORT);  /* page=2 设置页 focus=0 */
    for (int i = 0; i < 5; i++) st_press("SET DOWN", ROLE_NEXT, KEY_SHORT); /* focus=5 检查更新 */
    st_press("SET UPD", ROLE_CONFIRM, KEY_SHORT);   /* 发 update -> page=0 */

    /* 反向：时钟 -> 设置 -> 播放 */
    st_press("PREV", ROLE_PREV, KEY_SHORT);         /* page=2 */
    st_press("PREV", ROLE_PREV, KEY_SHORT);         /* page=1 */

    /* 焦点框可见性：按键亮 → 触摸收（走真实 indev 事件）→ 时钟页恒隐 */
    st_press("VIS KEY", ROLE_NEXT, KEY_SHORT);      /* 按键：want=1 state=1 */
    st_vis("after key");
    if (touch) lv_indev_send_event(touch, LV_EVENT_PRESSED, NULL);
    st_tick(2);
    st_vis("after touch");                          /* want=0 state=0 */
    nav_home();
    st_tick(4);
    focus_set_visible(true);
    st_vis("clock page");                           /* page=0，want 被压回 0 */

    /* 底部提示行：停止播放时 ECHO 显示 10 秒、WEATHERAL 一直显示到空 WEATHERAL */
    event_handle_line("STOP");
    event_handle_line("ECHO 正在下载歌单");
    st_tick(2);
    WAC_LOG("AUTOTEST HINT echo        -> vis=%d text='%s' kind=%d", (int)wac_hint_visible(),
            g_state.hint, (int)g_state.hint_kind);
    event_handle_line("WEATHERAL 【测试区雷雨大风黄色预警】");
    st_tick(2);
    WAC_LOG("AUTOTEST HINT alert       -> vis=%d text='%s' kind=%d", (int)wac_hint_visible(),
            g_state.hint, (int)g_state.hint_kind);
    event_handle_line("WEATHERAL ");
    st_tick(2);
    WAC_LOG("AUTOTEST HINT empty alert -> vis=%d kind=%d", (int)wac_hint_visible(),
            (int)g_state.hint_kind);
    /* 播放中不该显示（底部是迷你条） */
    event_handle_line("ECHO 播放中");
    wac_go_write("play"); /* sim：mock 后端把它切成播放态 */
    st_tick(2);
    WAC_LOG("AUTOTEST HINT playing     -> vis=%d (expect 0)", (int)wac_hint_visible());
    event_handle_line("STOP");
    nav_home();
    st_tick(4);

    /* ALARM：不管刚才在哪一页，立刻回时钟页；响铃期间任意键都算"停" */
    page_set(WAC_PAGE_SETTINGS);
    st_tick(4);
    event_handle_line("ALARM");
    st_tick(2);
    WAC_LOG("AUTOTEST ALARM begin      -> page=%d ringing=%d", (int)page_current(),
            (int)ui_alarm_active());
    st_press("ALARM KEY", ROLE_NEXT, KEY_LONG); /* 长按也停，不等长按判定 */
    WAC_LOG("AUTOTEST ALARM stop       -> ringing=%d page=%d", (int)g_state.ringing,
            (int)page_current());

    /* 时钟页确认键：进音量调节模式（底部出条）→ 方向键调 → 再确认退出 */
    nav_home();
    st_tick(4);
    st_press("CLOCK OK", ROLE_CONFIRM, KEY_SHORT);
    WAC_LOG("AUTOTEST CLOCK VOL on     -> edit=%d vol_on=%d page=%d", (int)focus_in_edit(),
            (int)page_clock_vol_mode(), (int)page_current());
    st_press("CLOCK VOL UP", ROLE_PREV, KEY_SHORT); /* 不翻页，只调音量 */
    st_press("CLOCK OK2", ROLE_CONFIRM, KEY_SHORT);
    WAC_LOG("AUTOTEST CLOCK VOL off    -> edit=%d vol_on=%d page=%d", (int)focus_in_edit(),
            (int)page_clock_vol_mode(), (int)page_current());

    /* 播放页进页焦点应该落在"播放/暂停"（索引 1），不是上一首 */
    page_set(WAC_PAGE_PLAYER);
    st_tick(4);
    WAC_LOG("AUTOTEST PLAYER FOCUS     -> page=%d focus=%d (expect 1)", (int)page_current(),
            focus_index());

    /* 投屏让位：FBSTART 停掉 LVGL 输出，FBEND 恢复。
       放在最后 —— 暂停后所有 timer 都停了，再做交互测试没有意义。 */
    event_handle_line("FBSTART");
    WAC_LOG("AUTOTEST FBSTREAM          -> paused=%d", (int)ui_pause_active());
    event_handle_line("FBEND");
    WAC_LOG("AUTOTEST FBSTREAM          -> paused=%d", (int)ui_pause_active());

    /* 5/6 键模式（WAC_SIM_KEYS=5|6 启动才有）：
       时钟页确认键 = 播放/暂停（不展开音量条），音量键短按调音量并闪现条、
       长按切歌。默认 4 键不跑这段。 */
    if (nav_mode() >= 5) {
        nav_home();
        st_tick(4);

        const int st0 = (int)g_state.state;
        st_press("5K CLOCK OK", ROLE_CONFIRM, KEY_SHORT);
        WAC_LOG("AUTOTEST 5K CLOCK OK    -> page=%d edit=%d vol_on=%d state %d->%d",
                (int)page_current(), focus_in_edit() ? 1 : 0, page_clock_vol_mode() ? 1 : 0, st0,
                (int)g_state.state);

        const int v0 = (int)g_state.volume;
        st_press("5K VOL SHORT", ROLE_VOL_UP, KEY_SHORT);
        WAC_LOG("AUTOTEST 5K VOL SHORT   -> vol %d->%d vol_on=%d", v0, (int)g_state.volume,
                page_clock_vol_mode() ? 1 : 0);

        st_tick(90); /* 1.8s：闪现该收掉了 */
        WAC_LOG("AUTOTEST 5K VOL HIDE    -> vol_on=%d (expect 0)",
                page_clock_vol_mode() ? 1 : 0);

        st_press("5K VOL LONG+", ROLE_VOL_UP, KEY_LONG);   /* 下一曲，只切一首 */
        st_press("5K VOL LONG-", ROLE_VOL_DOWN, KEY_LONG); /* 上一曲 */
    }
}
#endif

/* 手指落屏就收起焦点框：触摸屏上"你在第几个按钮"这种提示是多余的，
   手指压下去本身就有按下反馈。焦点本身不动，按实体键再亮回来。 */
static void on_touch_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    focus_set_visible(false);
}

int main(void)
{
    lv_init();
    WAC_LOG("wac start: lvgl %d.%d.%d  %dx%d (panel %dx%d rot %d)", LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR, LVGL_VERSION_PATCH, (int)WAC_SCR_W, (int)WAC_SCR_H,
            (int)WAC_PANEL_W, (int)WAC_PANEL_H, (int)WAC_ROT);

    if (!wac_disp_init()) return 1;

    /* 触摸要在建页面之前挂上：页面构建时就要能收点击 */
    lv_indev_t *touch = wac_touch_init(lv_display_get_default());
    if (touch) lv_indev_add_event_cb(touch, on_touch_pressed, LV_EVENT_PRESSED, NULL);

    /* 字体必须在建任何页面之前就位：页面构建时会立刻按像素取字体 */
    ui_font_init(NULL, NULL);

    screen_init();
    wac_state_init();
    {
        /* 真机上 Go 必须待在它自己的目录（要读 json / mp3），而我们的配置想跟
           程序放一起，所以路径允许用环境变量指定；不设就是当前目录的老行为。 */
        const char *ui_conf = getenv("WAC_UI_CONF");
        ui_set_load((ui_conf && *ui_conf) ? ui_conf : "wac_ui.conf");
    }
    theme_init();
    bg_init();
    ui_bright_init(); /* 调光遮罩挂 top layer，必须在 display 之后 */
    ui_alarm_init();  /* 响铃遮罩：要盖在调光遮罩之上 */
    page_init();

    keys_init();
    nav_init();
    keys_start();

    /* 滑动导航：左右切页、上下移焦点（没有触摸设备时什么也不做） */
    swipe_init(touch);

    /* 首次启动（没有 wac_keys.conf）走一遍绑定向导 */
    {
        const char *key_conf = getenv("WAC_KEYS_CONF");
        if (keys_load_config((key_conf && *key_conf) ? key_conf : "wac_keys.conf") != 0) {
            WAC_LOG("no wac_keys.conf -> bind wizard");
            wizard_start();
        }
        /* 模式由绑定数量推导，必须等配置读完再报（读之前说 4 键是假的） */
        WAC_LOG("keys: resolved %d-key mode", nav_mode());
    }

#ifdef WAC_SIM
    mock_go_init();
#else
    /* 真机：拉起 Go（argv: lvgl [播放器名]），stdout 事件喂给解析器 */
    go_proc_init();
#endif

#ifdef WAC_SIM
    if (getenv("WAC_AUTOTEST")) {
        selftest(touch);
        return 0;
    }

    /* 截屏核对：WAC_SHOT=<bmp> [WAC_SHOT_PAGE=0..2] [WAC_SHOT_FOCUS=n]
       WAC_SHOT_CMD="stop"   先像用户按了一下那样发一条命令
       WAC_SHOT_EVENT="ECHO xx" 再喂一条 Go 事件（测底部提示行用） */
    if (getenv("WAC_SHOT")) {
        st_tick(25);
        const char *pg = getenv("WAC_SHOT_PAGE");
        if (pg) page_set((wac_page_id_t)atoi(pg));
        const char *fi = getenv("WAC_SHOT_FOCUS");
        if (fi) focus_set_index(atoi(fi));
        const char *sc = getenv("WAC_SHOT_CMD");
        if (sc) {
            wac_go_write(sc);
            st_tick(4);
        }
        const char *se = getenv("WAC_SHOT_EVENT");
        if (se) {
            event_handle_line(se);
            st_tick(4);
        }
        /* 同上，但从文件读：Windows 下环境变量会被转成本地编码，
           中文事件只能走 UTF-8 文件，不然屏幕上全是方块 */
        const char *sf = getenv("WAC_SHOT_EVENT_FILE");
        if (sf) {
            FILE *ef = fopen(sf, "rb");
            if (ef) {
                char   eb[256];
                size_t en = fread(eb, 1, sizeof(eb) - 1, ef);
                eb[en]    = '\0';
                fclose(ef);
                event_handle_line(eb);
                st_tick(4);
            }
        }
        /* 截图要看焦点框：模拟"刚按过实体键"。时钟页恒隐，截它也没框。 */
        focus_set_visible(true);
        /* WAC_SHOT_TOUCH=1：真的向触摸 indev 发一次按下，验证"手指一碰就收框" */
        if (getenv("WAC_SHOT_TOUCH") && touch) {
            lv_indev_send_event(touch, LV_EVENT_PRESSED, NULL);
            st_tick(2);
        }
        /* WAC_SHOT_PRESS=1：截屏前按一次确认键（用来看"进入调节态"的显示） */
        if (getenv("WAC_SHOT_PRESS")) {
            nav_dispatch(ROLE_CONFIRM, KEY_SHORT);
            st_tick(2);
        }
        st_tick(25);
        dev_shot(getenv("WAC_SHOT"));
        return 0;
    }
#endif

    while (1) {
        /* 先读 Go 的 stdout：投屏时 LVGL 是停摆的，这条路不能挂在 lv_timer 上 */
        wac_go_poll();

        lv_lock();
        uint32_t idle = lv_timer_handler();
        lv_unlock();

        if (idle < 5) idle = 5;
        if (idle > 100) idle = 100;
        wac_sleep_ms(idle);
    }
}
