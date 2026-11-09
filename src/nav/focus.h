/**
 * focus.h -- 焦点模型
 *
 * LVGL 的 lv_group 是 1D 链表，我们的导航是"页面 + 页内若干项"，
 * 所以 group 只当焦点登记簿用，选谁由这里算。
 * 焦点视觉统一用 LV_STATE_FOCUSED 的样式（outline + transform_width/height，
 * 绝不用 transform_scale —— v9 里一旦 scale 会走离屏 layer 重采样）。
 */
#ifndef WAC_FOCUS_H
#define WAC_FOCUS_H

#include "wac.h"

/* 单页最多几项可聚焦。设置页现在 7 项（6 项 + 有背光时的亮度），留点余量 */
#define FOCUS_MAX_ITEMS 12

typedef struct {
    lv_obj_t *items[FOCUS_MAX_ITEMS];
    int       n;
    int       cur;
} focus_list_t;

typedef void (*activate_fn)(lv_obj_t *obj);

/* 子项调节态：焦点落在"能在原地调值"的控件上（播放页的音量条）时，
 * 确认键不是立刻执行动作，而是进入调节态 —— 方向键改成调值，
 * 再按确认退出回到外层。dir = ±1 调值，0 表示"进入/退出，刷新一次显示"。
 *
 * 只对当前焦点项生效，换个焦点自动失效，页面不用记得清理。 */
typedef void (*focus_edit_fn)(int dir);

void focus_list_init(focus_list_t *l);
void focus_list_add(focus_list_t *l, lv_obj_t *o);

/* 切页时调用：把该页的焦点列表设为活跃，并落焦到第 0 项 */
void focus_use(focus_list_t *l);

focus_list_t *focus_active_list(void);
lv_obj_t     *focus_obj(void);
int           focus_index(void);

/* dir: +1 下一项 / -1 上一项；跳过隐藏项，循环 */
void focus_move(int dir);

/* 不循环地走一步：走到头返回 false（由调用方决定翻页）。
 * 页内可见项 <= 1 时也返回 false —— 一页只有一项时，上下就是翻页。 */
bool focus_step(int dir);

/* 指定索引落焦（触摸点选时用） */
void focus_set_index(int idx);

/* 按对象落焦（触摸点到音量条这类非按钮控件时用；不在列表里就什么都不做） */
void focus_set_obj(lv_obj_t *o);

/* 触发当前焦点项 */
void focus_activate(void);

/* 页面设置自己的激活回调（比如播放页的按钮动作） */
void focus_set_activate(activate_fn cb);

/* 焦点框是否画出来。
 *
 * 触摸屏上手指是主角，选中框常亮着反而脏 —— 手指一碰就收起（传 false），
 * 按实体键再亮出来（传 true）。焦点本身不因此改变，只是不画描边。
 * 时钟页（WAC_PAGE_CLOCK）无论传什么都恒隐，那页的焦点项是整块迷你条。 */
void focus_set_visible(bool v);
bool focus_visible(void);

/* 声明"当前焦点项支持调节"。页面在 activate 回调里判断到是音量条就调它，
 * 紧接着调 focus_enter_edit() 进去。传 NULL 表示当前项不支持。 */
void focus_edit_support(focus_edit_fn cb);

bool focus_enter_edit(void);  /* 成功进入返回 true */
bool focus_exit_edit(void);   /* 刚才在调节态就退出并返回 true */
bool focus_in_edit(void);
void focus_edit_step(int dir); /* 只在调节态里有效 */

#endif /* WAC_FOCUS_H */
