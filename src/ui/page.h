/**
 * page.h -- 页面栈
 *
 * 只有两页（时钟 / 播放），但按栈设计：以后加设置页、歌单页时键位表不用动。
 *   nav_back()  弹一层
 *   nav_home()  清空栈回时钟页
 */
#ifndef WAC_PAGE_H
#define WAC_PAGE_H

#include "wac.h"

typedef void (*page_build_fn)(lv_obj_t *root);
typedef void (*page_show_fn)(void);

void page_register(wac_page_id_t id, page_build_fn build, page_show_fn on_show);
void page_init(void);

void          page_show(wac_page_id_t id);
/* 压栈后显示（返回键用） */
void          page_push(wac_page_id_t id);
/* 直接切到某页并把栈重置成单页（循环翻页用：返回键永远回时钟页） */
void          page_set(wac_page_id_t id);
/* 同上，但带方向做一次滑入动效：dir = +1 从右边进来，-1 从左边进来，0 不做动效 */
void          page_set_dir(wac_page_id_t id, int dir);
wac_page_id_t page_current(void);
lv_obj_t     *page_root(wac_page_id_t id);

/* 返回上一层 / 回主页（4 键模式 BACK 的短按 / 长按） */
void nav_back(void);
void nav_home(void);

/* 每页自己注册的构建函数 */
void page_clock_register(void);
void page_player_register(void);
void page_settings_register(void);

/* 设置页改了选项后调用：时钟页据此重排（秒的开关会改变时钟字号） */
void page_clock_settings_changed(void);

/* 时钟页当前是不是在音量调节模式（底部显示音量条）。
   自检/截图核对用，UI 逻辑不依赖它。 */
bool page_clock_vol_mode(void);

#endif /* WAC_PAGE_H */
