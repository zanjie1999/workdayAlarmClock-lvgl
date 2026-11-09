/**
 * dev_shot.h -- 开发用截屏（只在 sim 下有效）
 */
#ifndef WAC_DEV_SHOT_H
#define WAC_DEV_SHOT_H

/* 把当前屏幕内容存成 24bit BMP，路径由调用方给。
 * 真机上是个空函数：只为了 main.c 里不用写条件编译。 */
void dev_shot(const char *path);

#endif /* WAC_DEV_SHOT_H */
