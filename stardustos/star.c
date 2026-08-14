/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#include "star.h"

typedef struct {
    uint16_t evt;
    void *param;
} star_qitem_t;

/* 事件队列：体积最大（每槽 evt+param），8051 下放 idata/xdata（见
 * STAR_STATIC）；标量仍放 data（直接寻址，临界区路径最快） */
STAR_STATIC struct {
    star_qitem_t items[STAR_EVT_QUEUE_SIZE];
    uint8_t head;
    uint8_t count;
} s_q;

static const star_evt_entry_t *s_evt_table;
static uint16_t s_evt_count;

static volatile uint32_t s_tick;
static star_timer_t *s_timers;

static uint32_t s_dropped;
static star_drop_hook_t s_drop_hook;
static uint8_t s_in_drop_hook;

#ifdef STAR_TEST_INJECT_ENABLE
void (*s_test_inject)(void);
void star_test_inject_set(void (*fn)(void))
{
    s_test_inject = fn;
}
#endif

#if STAR_DELAYED_MAX > 0
STAR_STATIC struct {
    uint32_t due;
    uint16_t evt;
    void *param;
    uint8_t used;
} s_delayed[STAR_DELAYED_MAX];
#endif

/* ---------------- 事件队列 ---------------- */

static void star_q_push(uint16_t evt, void *param)
{
    unsigned idx = (unsigned)s_q.head + s_q.count;

    STAR_ASSERT(s_q.count < STAR_EVT_QUEUE_SIZE);
    if (idx >= STAR_EVT_QUEUE_SIZE) {
        idx -= STAR_EVT_QUEUE_SIZE; /* head+count < 2*SIZE，一次减法足够 */
    }
    s_q.items[idx].evt = evt;
    s_q.items[idx].param = param;
    s_q.count++;
}

static bool star_q_pop(uint16_t *evt, void **param)
{
    if (s_q.count == 0) {
        return false;
    }
    *evt = s_q.items[s_q.head].evt;
    *param = s_q.items[s_q.head].param;
    if (s_q.head + 1 >= STAR_EVT_QUEUE_SIZE) {
        s_q.head = 0;
    } else {
        s_q.head++;
    }
    s_q.count--;
    return true;
}

void star_init(const star_evt_entry_t *evt_table, uint16_t evt_count)
{
    s_q.head = 0;
    s_q.count = 0;
    s_evt_table = evt_table;
    s_evt_count = evt_count;
    s_tick = 0;
    s_timers = NULL;
    s_dropped = 0;
#if STAR_DELAYED_MAX > 0
    {
        int i;

        for (i = 0; i < STAR_DELAYED_MAX; i++) {
            s_delayed[i].used = 0;
        }
    }
#endif
}

uint32_t star_dropped_count(void)
{
    uint32_t n;
    star_crit_state_t cs = star_crit_enter();
    n = s_dropped;
    star_crit_exit(cs);
    return n;
}

void star_set_drop_hook(star_drop_hook_t hook)
{
    /* 钩子可能在中断上下文被读取/调用，赋值必须与中断互斥。
     * 建议仅在启动时（中断开启前）调用本函数；运行中更换也安全 */
    star_crit_state_t cs = star_crit_enter();
    s_drop_hook = hook;
    star_crit_exit(cs);
}

void star_note_dropped(uint16_t evt)
{
    s_dropped++;
    /* 防重入：钩子内再触发丢弃只计数、不再递归回调 */
    if (s_drop_hook != NULL && !s_in_drop_hook) {
        s_in_drop_hook = 1;
        s_drop_hook(evt);
        s_in_drop_hook = 0;
    }
}

/* 入队（调用者须已持有临界区）；满队返回 STAR_ERR_FULL，不计丢弃、
 * 不触发钩子。供"暂缓重试"类路径使用：重试最终会送达，途中失败
 * 不是丢弃，若计入会让 star_dropped_count() 被假阳性污染 */
static star_status_t star_event_enqueue_raw(uint16_t evt, void *param)
{
    if (s_q.count >= STAR_EVT_QUEUE_SIZE) {
        return STAR_ERR_FULL;
    }
    star_q_push(evt, param);
    return STAR_OK;
}

star_status_t star_event_enqueue(uint16_t evt, void *param)
{
    star_status_t st = star_event_enqueue_raw(evt, param);

    if (st != STAR_OK) {
        star_note_dropped(evt);
    }
    return st;
}

star_status_t star_event_post(uint16_t evt, void *param)
{
    star_status_t st;
    star_crit_state_t cs;

    STAR_TEST_INJECT();
    cs = star_crit_enter();
    st = star_event_enqueue(evt, param);
    star_crit_exit(cs);
    STAR_TEST_INJECT();
    return st;
}

star_status_t star_event_post_replace(uint16_t evt, void *param)
{
    star_status_t st;
    star_crit_state_t cs;
    uint8_t i;

    STAR_TEST_INJECT();
    cs = star_crit_enter();

    /* 从新到旧查找：覆盖最新一条同 ID 事件（latest wins） */
    for (i = s_q.count; i > 0; i--) {
        unsigned idx = (unsigned)s_q.head + i - 1;

        if (idx >= STAR_EVT_QUEUE_SIZE) {
            idx -= STAR_EVT_QUEUE_SIZE; /* head+i-1 < 2*SIZE，一次减法足够 */
        }
        if (s_q.items[idx].evt == evt) {
            s_q.items[idx].param = param;
            star_crit_exit(cs);
            STAR_TEST_INJECT();
            return STAR_OK;
        }
    }
    if (s_q.count >= STAR_EVT_QUEUE_SIZE) {
        st = STAR_ERR_FULL;
    } else {
        star_q_push(evt, param);
        st = STAR_OK;
    }
    if (st == STAR_ERR_FULL) {
        star_note_dropped(evt);
    }
    star_crit_exit(cs);
    STAR_TEST_INJECT();
    return st;
}

/* ---------------- 时基与定时器 ---------------- */

void star_tick(void)
{
    star_tick_advance(STAR_TICK_MS);
}

void star_tick_advance(uint32_t ms)
{
    /* 统一契约：s_tick 的一切访问都在临界区内完成（含本函数）。
     * M0+ 上 32 位读改写非原子，依赖"关中断"保证与主循环侧读写互斥 */
    star_crit_state_t cs = star_crit_enter();
    s_tick += ms;
    star_crit_exit(cs);
}

void star_tick_set(uint32_t ticks)
{
    star_crit_state_t cs = star_crit_enter();
    s_tick = ticks;
    star_crit_exit(cs);
}

uint32_t star_ticks(void)
{
    uint32_t t;
    star_crit_state_t cs = star_crit_enter();
    t = s_tick; /* M0+ 上 32 位读非原子，关中断保证完整性 */
    star_crit_exit(cs);
    return t;
}

/* 链表不变量：s_timers 按 due 升序排列（回绕安全比较），
 * start/restart/process 之后恒有序；头节点即最早到期者 */
static bool star_timer_unlink(star_timer_t *t)
{
    star_timer_t **pp;
    bool found = false;
    star_crit_state_t cs;

    /* 链表操作统一进临界区：定时器 API 契约虽是主循环专用，
     * 但误用于中断上下文时（最难排查的那类竞态）也不至于腐坏链表 */
    cs = star_crit_enter();
    for (pp = &s_timers; *pp != NULL; pp = &(*pp)->next) {
        if (*pp == t) {
            *pp = t->next;
            t->next = NULL;
            found = true;
            break;
        }
    }
    star_crit_exit(cs);
    return found;
}

/* 按 due 升序插入（调用者须已持有临界区） */
static void star_timer_insert_sorted(star_timer_t *t)
{
    star_timer_t **pp = &s_timers;

    while (*pp != NULL && (int32_t)((*pp)->due - t->due) < 0) {
        pp = &(*pp)->next;
    }
    t->next = *pp;
    *pp = t;
}

star_status_t star_timer_start(star_timer_t *t, uint16_t evt, void *param,
                               uint32_t ms, bool periodic)
{
    return star_timer_start_ex(t, evt, param, ms, periodic,
                               STAR_TIMER_POLICY_RETRY);
}

star_status_t star_timer_start_ex(star_timer_t *t, uint16_t evt, void *param,
                                  uint32_t ms, bool periodic,
                                  star_timer_policy_t policy)
{
    star_crit_state_t cs;

    /* ms 上限运行时校验（此前仅靠默认关闭的 STAR_ASSERT，生产构建会静默失效）：
     * 回绕比较的数学边界，上限约 24.8 天 */
    if (t == NULL || ms == 0 || ms >= 0x80000000u) {
        return STAR_ERR_PARAM;
    }
    /* policy 同样运行时校验：越界值此前会静默落入 default 分支
     * 变成 RETRY 语义 */
    if ((uint8_t)policy > STAR_TIMER_POLICY_LATEST) {
        return STAR_ERR_PARAM;
    }
    star_timer_stop(t); /* 重复 start 视为重启 */
    cs = star_crit_enter();
    /* s_tick 由 tick 中断更新，读取必须关中断；字段写入与排序插入
     * 同区完成，杜绝"字段半写 + 中断窥视"窗口 */
    t->due = s_tick + ms;
    t->period = periodic ? ms : 0;
    t->evt = evt;
    t->param = param;
    t->policy = (uint8_t)policy;
    star_timer_insert_sorted(t);
    star_crit_exit(cs);
    return STAR_OK;
}

star_status_t star_timer_stop(star_timer_t *t)
{
    if (t == NULL) {
        return STAR_ERR_PARAM;
    }
    star_timer_unlink(t);
    return STAR_OK;
}

star_status_t star_timer_restart(star_timer_t *t, uint32_t ms)
{
    star_crit_state_t cs;

    if (t == NULL || ms == 0 || ms >= 0x80000000u) {
        return STAR_ERR_PARAM;
    }
    if (!star_timer_unlink(t)) {
        return STAR_ERR_NOT_FOUND;
    }
    cs = star_crit_enter();
    t->due = s_tick + ms;
    if (t->period != 0) {
        t->period = ms;
    }
    star_timer_insert_sorted(t); /* due 变更后按新到期时刻重排 */
    star_crit_exit(cs);
    return STAR_OK;
}

#if STAR_DELAYED_MAX > 0
/* 延时槽池查找助手（调用者须已持有临界区）。
 * 三个 delayed API 共用，避免各自内联一份线性扫描 */
static int star_delayed_find_free(void)
{
    int i;

    for (i = 0; i < STAR_DELAYED_MAX; i++) {
        if (!s_delayed[i].used) {
            return i;
        }
    }
    return -1;
}

static int star_delayed_find_evt(uint16_t evt, void *param, bool match_param)
{
    int i;

    for (i = 0; i < STAR_DELAYED_MAX; i++) {
        if (s_delayed[i].used && s_delayed[i].evt == evt &&
            (!match_param || s_delayed[i].param == param)) {
            return i;
        }
    }
    return -1;
}
#endif

star_status_t star_event_post_delayed(uint16_t evt, void *param, uint32_t ms)
{
    /* 时长运行时校验（与定时器 API 同口径，不依赖可被关闭的 STAR_ASSERT）：
     * ms==0 语义含糊（等于"下一拍投递"），直接拒绝；
     * ms≥2^31 破坏回绕比较的数学边界（约 24.8 天） */
#if STAR_DELAYED_MAX > 0
    star_status_t st;
    star_crit_state_t cs;
#endif
    if (ms == 0 || ms >= 0x80000000u) {
        return STAR_ERR_PARAM;
    }
#if STAR_DELAYED_MAX > 0
    cs = star_crit_enter();

    {
        int i = star_delayed_find_free();

        if (i < 0) {
            star_note_dropped(evt); /* 口径统一：被拒绝的延时投递也计入 */
            st = STAR_ERR_FULL;
        } else {
            s_delayed[i].used = 1;
            s_delayed[i].due = s_tick + ms;
            s_delayed[i].evt = evt;
            s_delayed[i].param = param;
            st = STAR_OK;
        }
    }
    star_crit_exit(cs);
    STAR_TEST_INJECT();
    return st;
#else
    (void)evt;
    (void)param;
    (void)ms;
    {
        /* 与其它调用点一致：note_dropped 必须在临界区内调用 */
        star_crit_state_t cs = star_crit_enter();
        star_note_dropped(evt); /* 口径统一：功能关闭时视为拒绝 */
        star_crit_exit(cs);
    }
    return STAR_ERR_FULL;
#endif
}

star_status_t star_event_post_delayed_replace(uint16_t evt, void *param,
                                              uint32_t ms)
{
    /* 时长运行时校验：与 star_event_post_delayed 同口径 */
#if STAR_DELAYED_MAX > 0
    star_status_t st;
    star_crit_state_t cs;
#endif
    if (ms == 0 || ms >= 0x80000000u) {
        return STAR_ERR_PARAM;
    }
#if STAR_DELAYED_MAX > 0
    cs = star_crit_enter();

    {
        /* replace 语义：同 evt 已存在则原地替换为最新（只留最新一份） */
        int i = star_delayed_find_evt(evt, NULL, false);

        if (i >= 0) {
            s_delayed[i].due = s_tick + ms;
            s_delayed[i].param = param;
            star_crit_exit(cs);
            STAR_TEST_INJECT();
            return STAR_OK;
        }
        i = star_delayed_find_free();
        if (i >= 0) {
            s_delayed[i].used = 1;
            s_delayed[i].due = s_tick + ms;
            s_delayed[i].evt = evt;
            s_delayed[i].param = param;
            st = STAR_OK;
            star_crit_exit(cs);
            STAR_TEST_INJECT();
            return st;
        }
    }
    star_note_dropped(evt); /* 口径统一：槽池满视为拒绝 */
    star_crit_exit(cs);
    STAR_TEST_INJECT();
    return STAR_ERR_FULL;
#else
    (void)evt;
    (void)param;
    (void)ms;
    {
        star_crit_state_t cs = star_crit_enter();
        star_note_dropped(evt);
        star_crit_exit(cs);
    }
    return STAR_ERR_FULL;
#endif
}

star_status_t star_event_cancel_delayed(uint16_t evt, void *param)
{
#if STAR_DELAYED_MAX > 0
    star_crit_state_t cs = star_crit_enter();

    {
        int i = star_delayed_find_evt(evt, param, true);

        if (i >= 0) {
            s_delayed[i].used = 0;
            star_crit_exit(cs);
            return STAR_OK;
        }
    }
    star_crit_exit(cs);
    return STAR_ERR_NOT_FOUND;
#else
    (void)evt;
    (void)param;
    return STAR_ERR_NOT_FOUND;
#endif
}

/* ---------------- 分发与主循环 ---------------- */

static void star_process_timers(void)
{
    uint32_t now = star_ticks(); /* 单一快照：M0+ 上裸读 32 位 s_tick 会撕裂 */
    star_timer_t **pp = &s_timers;

    /* 链表按 due 升序：头节点未到期则全部未到期，提前终止——
     * poll 空转 O(1)，与定时器总数无关；到期节点从表头摘除处理，
     * 相位推进/重试后按新 due 重插，保持有序 */
    while (*pp != NULL) {
        star_timer_t *t = *pp;

        STAR_TEST_INJECT(); /* 交错测试窗口：定时器列表遍历期间伪中断可插入 */
        if ((int32_t)(now - t->due) < 0) {
            break;
        }
        *pp = t->next; /* 先摘除再处理 */
        t->next = NULL;

        if (t->period != 0) {
            /* 相位稳定：due 按周期推进而不是"从现在重算"（due += period），
             * handler/主循环延迟不会造成相位逐周期累积漂移；
             * 错过多拍只合并投递一次（本拍），相位照旧；
             * 落后超过 STAR_TIMER_CATCHUP_MAX 拍时放弃旧相位重新对齐，
             * 防止极端落后时的长循环 */
            uint32_t n = 0;
            do {
                t->due += t->period;
            } while ((int32_t)(now - t->due) >= 0 &&
                     ++n < STAR_TIMER_CATCHUP_MAX);
            if ((int32_t)(now - t->due) >= 0) {
                t->due = now + t->period;
            }
            star_timer_insert_sorted(t); /* 相位推进后重插，保持有序 */
            if (t->policy == STAR_TIMER_POLICY_LATEST) {
                star_event_post_replace(t->evt, t->param);
            } else {
                /* 周期：RETRY/DROP 都等同丢当次（下一拍正常） */
                star_event_post(t->evt, t->param);
            }
        } else {
            switch (t->policy) {
            case STAR_TIMER_POLICY_DROP:
                /* 严格截止：失败即弃并释放（已摘除即释放） */
                star_event_post(t->evt, t->param);
                break;
            case STAR_TIMER_POLICY_LATEST:
                /* replace 失败说明满队且无同 ID：截止已过，释放 */
                star_event_post_replace(t->evt, t->param);
                break;
            default:
                /* RETRY（至少一次）：送达才释放，满队时把 due 后移一拍
                 * 并重插，下一拍重试（避免同 poll 内反复投递死循环）。
                 * 重试路径用 raw 入队：失败只是暂缓、事件最终仍送达，
                 * 不计丢弃数、不触发丢事件钩子（否则"最终送达的事件"
                 * 会被误记为丢弃，掉计数口径污染可靠性监测） */
                {
                    star_crit_state_t tcs = star_crit_enter();
                    star_status_t tst = star_event_enqueue_raw(t->evt, t->param);

                    star_crit_exit(tcs);
                    if (tst == STAR_OK) {
                        break;
                    }
                }
                t->due = now + 1;
                star_timer_insert_sorted(t);
                break;
            }
        }
    }

#if STAR_DELAYED_MAX > 0
    {
        /* 整个槽池扫描共用一段临界区（此前每槽一对临界区）：
         * 更短代码、更稳的中断延迟；事件投递的嵌套临界区安全 */
        star_crit_state_t cs = star_crit_enter();
        int i;

        for (i = 0; i < STAR_DELAYED_MAX; i++) {
            uint16_t evt;
            void *param;
            bool fire = s_delayed[i].used &&
                        (int32_t)(s_tick - s_delayed[i].due) >= 0;

            if (fire) {
                s_delayed[i].used = 0;
                evt = s_delayed[i].evt;
                param = s_delayed[i].param;
                star_event_post(evt, param); /* 失败计丢失数 */
            }
        }
        star_crit_exit(cs);
    }
#endif
}

bool star_poll(void)
{
    uint16_t evt;
    void *param;
    bool got;
    star_crit_state_t cs;

    STAR_TEST_INJECT(); /* 交错测试窗口：主循环单步前伪中断可插入 */
    star_process_timers();
#if STAR_ENABLE_TASK
    star_process_tasks();
#endif
    cs = star_crit_enter();
    got = star_q_pop(&evt, &param);
    star_crit_exit(cs);

    if (got) {
        if (evt < s_evt_count && s_evt_table != NULL) {
            const star_evt_entry_t *e = &s_evt_table[evt];
            if (e->handler != NULL) {
                e->handler(evt, param, e->ctx);
                return true;
            }
        }
        /* 未注册/越界事件：安全丢弃并计数。
         * 这是受支持的运行时行为（用户忘记登记 ID），不是内部不变量，
         * 因此不设断言——断言构建下恶意/越界事件也必须走到这条丢弃路径 */
        cs = star_crit_enter();
        star_note_dropped(evt);
        star_crit_exit(cs);
        return true;
    }
    return false;
}

/* 内核已知的下一到期节拍（调用者须已持有临界区）。
 * 定时器链表有序 → 表头即最早（O(1)）；延时槽线性扫（O(STAR_DELAYED_MAX)） */
static uint32_t star_next_due_locked(void)
{
    uint32_t best = (s_timers != NULL) ? s_timers->due : STAR_TICK_NONE;
#if STAR_DELAYED_MAX > 0
    int i;
#endif

#if STAR_DELAYED_MAX > 0
    for (i = 0; i < STAR_DELAYED_MAX; i++) {
        if (s_delayed[i].used &&
            (best == STAR_TICK_NONE ||
             (int32_t)(s_delayed[i].due - best) < 0)) {
            best = s_delayed[i].due;
        }
    }
#endif
    return best;
}

/* 内核已知的下一到期节拍；无待到期项返回 STAR_TICK_NONE。
 * 自带临界区，任意上下文安全，供 tickless 移植层与低功耗应用
 * 决策休眠时长。注意：返回的是"节拍时刻"而非"剩余毫秒"，
 * 须与 star_ticks() 做回绕安全比较 ((int32_t)(due - now) > 0 表示未到期) */
uint32_t star_next_due(void)
{
    uint32_t best;
    star_crit_state_t cs = star_crit_enter();

    best = star_next_due_locked();
    star_crit_exit(cs);
    return best;
}

/* 临界区内检查并睡眠：消除"查空 → 中断投递 → WFI 漏睡"竞态。
 * deadline 感知：队列空且最近到期项未到（或根本无到期项）才睡，
 * 否则立即返回让主循环处理到期工作；判定与睡眠同临界区原子完成。
 * ARM/RISC-V 的 wfi 在 pending 中断存在时立即唤醒，
 * 唤醒后先恢复中断再返回，事件不会睡过头 */
void star_sleep(void)
{
    star_crit_state_t cs = star_crit_enter();

    if (s_q.count == 0) {
        uint32_t next_due = star_next_due_locked();

        if (next_due == STAR_TICK_NONE || (int32_t)(next_due - s_tick) > 0) {
            star_idle(next_due); /* 在关中断状态下调用（契约见 star.h） */
        }
    }
    star_crit_exit(cs);
}

void star_loop(void)
{
    for (;;) {
        if (!star_poll()) {
            star_sleep();
        }
    }
}
