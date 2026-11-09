/**
 * go_proc.h -- Go 播放器子进程（真机）
 *
 * sim 构建不编译 go_proc.c，由 mock_go.c 顶上，两边都实现 wac_go_write()。
 */
#ifndef WAC_GO_PROC_H
#define WAC_GO_PROC_H

void go_proc_init(void);

/* 往 Go 的 stdin 打一行命令（sim 下由 mock_go.c 接管） */
void wac_go_write(const char *line);

/* 读子进程 stdout、回收死掉的子进程、到点重启。
 * **由主循环每轮调用**，不能用 lv_timer 驱动：投屏时整个 LVGL 是停摆的，
 * 挂在 timer 上就永远收不到 FBEND，画面回不来。 */
void wac_go_poll(void);

#endif /* WAC_GO_PROC_H */
