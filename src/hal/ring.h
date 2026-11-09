/* ring.h -- 定长槽位 SPSC 环形队列
 *
 * 用途：Go 子进程的 stdout 由独立线程按行读入，LVGL 主线程定时排空。
 * 只允许一个生产者线程 + 一个消费者线程，无需加锁（C11 atomics）。
 * 满了就丢最旧之外的新条目并计数，绝不阻塞读取线程（否则 Go 的 stdout 管道会堵住）。
 */
#ifndef WAC_RING_H
#define WAC_RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#define RING_SLOT_LEN 512

typedef struct {
    char slot[RING_SLOT_LEN];
} ring_item_t;

typedef struct {
    ring_item_t *items;
    unsigned     cap;      /* 必须是 2 的幂 */
    _Atomic unsigned head; /* 生产者推进 */
    _Atomic unsigned tail; /* 消费者推进 */
    _Atomic unsigned dropped;
} ring_t;

static inline void ring_init(ring_t *r, ring_item_t *storage, unsigned cap)
{
    r->items = storage;
    r->cap   = cap;
    atomic_store_explicit(&r->head, 0, memory_order_relaxed);
    atomic_store_explicit(&r->tail, 0, memory_order_relaxed);
    atomic_store_explicit(&r->dropped, 0, memory_order_relaxed);
}

/* 生产者：拷贝一行（不含换行）。成功返回 true。 */
static inline int ring_push(ring_t *r, const char *line, size_t len)
{
    unsigned head = atomic_load_explicit(&r->head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (head - tail >= r->cap) {
        atomic_fetch_add_explicit(&r->dropped, 1, memory_order_relaxed);
        return 0;
    }
    if (len >= RING_SLOT_LEN) len = RING_SLOT_LEN - 1;
    memcpy(r->items[head & (r->cap - 1)].slot, line, len);
    r->items[head & (r->cap - 1)].slot[len] = '\0';
    atomic_store_explicit(&r->head, head + 1, memory_order_release);
    return 1;
}

/* 消费者：取出下一行到 out（容量 RING_SLOT_LEN）。无数据返回 false。 */
static inline int ring_pop(ring_t *r, char *out)
{
    unsigned tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&r->head, memory_order_acquire);
    if (tail == head) return 0;
    strcpy(out, r->items[tail & (r->cap - 1)].slot);
    atomic_store_explicit(&r->tail, tail + 1, memory_order_release);
    return 1;
}

static inline unsigned ring_dropped(const ring_t *r)
{
    return atomic_load_explicit(&r->dropped, memory_order_relaxed);
}

#endif /* WAC_RING_H */
