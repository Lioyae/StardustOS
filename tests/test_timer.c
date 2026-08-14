/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star.h"
#include "star_test.h"

static uint32_t s_last_evt;
static uint32_t s_last_param;
static uint32_t s_calls;

static void handler(uint16_t evt, void *param, void *ctx)
{
    (void)ctx;
    s_last_evt = evt;
    s_last_param = STAR_U32(param);
    s_calls++;
}

static const star_evt_entry_t table[] = {
    [0] = STAR_ENTRY(handler, NULL),
    [1] = STAR_ENTRY(handler, NULL),
};

static void test_timer_one_shot(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(7), 10, false) == STAR_OK);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_calls == 1);
    TEST_ASSERT(s_last_param == 7);
    TEST_ASSERT(star_poll() == false); /* 单次定时器不再触发 */
}

static void test_timer_periodic(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t, 1, STAR_P(0), 5, true) == STAR_OK);
    for (int i = 0; i < 15; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls == 3); /* 第 5/10/15 拍各一次 */
}

static star_timer_t s_self;
static uint32_t s_self_calls;

static void self_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)param;
    (void)ctx;
    s_self_calls++;
    star_timer_stop(&s_self);
}

static const star_evt_entry_t self_table[] = {
    [0] = STAR_ENTRY(self_handler, NULL),
};

static void test_stop_self_in_handler(void)
{
    star_init(self_table, 1);
    s_self_calls = 0;
    TEST_ASSERT(star_timer_start(&s_self, 0, NULL, 1, true) == STAR_OK);
    for (int i = 0; i < 5; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_self_calls == 1); /* 第一次触发后自我停止 */
}

static void test_restart(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(1), 10, false) == STAR_OK);
    for (int i = 0; i < 5; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(star_timer_restart(&t, 10) == STAR_OK); /* 重新计时 */
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 1);

    /* 未启动的定时器 restart 报错 */
    static star_timer_t t2;
    TEST_ASSERT(star_timer_restart(&t2, 10) == STAR_ERR_NOT_FOUND);
}

static void test_tick_overflow(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    star_tick_set(0xFFFFFFF0u);
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(5), 100, false) == STAR_OK);
    for (int i = 0; i < 99; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 5);
    TEST_ASSERT(star_poll() == false);
}

static void test_post_delayed(void)
{
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(3), 10) == STAR_OK);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 3);

    /* 槽耗尽 */
    for (int i = 0; i < STAR_DELAYED_MAX; i++) {
        TEST_ASSERT(star_event_post_delayed(0, STAR_P(i), 100) == STAR_OK);
    }
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(9), 100) == STAR_ERR_FULL);
    /* 到期后槽释放可复用 */
    for (int i = 0; i < 100; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(10), 1) == STAR_OK);
}

static void test_one_shot_survives_full_queue(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(1, STAR_P(i)) == STAR_OK);
    }
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(7), 5, false) == STAR_OK);
    for (int i = 0; i < 5; i++) {
        star_tick();
    }
    /* 到期但队列满：事件未入队，定时器 due 后移一拍保留重试 */
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    /* 口径回归：RETRY 暂缓重试不计丢弃（事件最终送达，不是丢失） */
    TEST_ASSERT(star_dropped_count() == 0);
    /* 队列空了，但 RETRY 重试发生在下一拍 */
    TEST_ASSERT(star_poll() == false);
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 7);
    TEST_ASSERT(star_dropped_count() == 0);
    TEST_ASSERT(star_poll() == false); /* 单次定时器已释放 */
}

static void test_drop_policy_strict_deadline(void)
{
    static star_timer_t t;

    star_init(table, 2);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(1, STAR_P(i)) == STAR_OK);
    }
    TEST_ASSERT(star_timer_start_ex(&t, 0, STAR_P(7), 5, false,
                                    STAR_TIMER_POLICY_DROP) == STAR_OK);
    for (int i = 0; i < 5; i++) {
        star_tick();
    }
    TEST_ASSERT(star_poll() == true); /* 到期：投递失败被计数，定时器释放 */
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE - 1; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    /* DROP 策略：事件不迟到 */
    TEST_ASSERT(star_poll() == false);
    TEST_ASSERT(s_last_param != 7);
    /* 定时器已释放 */
    TEST_ASSERT(star_timer_restart(&t, 10) == STAR_ERR_NOT_FOUND);
}

static void test_latest_coalesces(void)
{
    static star_timer_t t;

    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start_ex(&t, 0, STAR_P(42), 5, true,
                                    STAR_TIMER_POLICY_LATEST) == STAR_OK);
    /* 预塞同 ID 事件，模拟"上一拍还没被 handler 处理" */
    TEST_ASSERT(star_event_post(0, STAR_P(1)) == STAR_OK);
    for (int i = 0; i < 5; i++) {
        star_tick();
    }
    TEST_ASSERT(star_poll() == true);  /* 到期 replace：队列仍 1 条 → 派发 */
    TEST_ASSERT(s_last_param == 42);   /* 派发的是最新参数 */
    TEST_ASSERT(star_poll() == false); /* 无第二条积压 */
    star_timer_stop(&t);
}

static void test_periodic_no_drift(void)
{
    static star_timer_t t;

    /* 相位稳定回归：主循环/处理延迟不得造成周期相位逐周期累积漂移
     * （旧实现 due = now + period，迟到触发后相位永久后移） */
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(0), 10, true) == STAR_OK);
    for (int i = 1; i <= 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                      /* 第 10 拍 */
    TEST_ASSERT(star_poll() == true); /* 第一次到期 */
    TEST_ASSERT(s_calls == 1);
    /* 主循环繁忙：tick 走到第 23 拍才有机会 poll */
    for (int i = 11; i <= 23; i++) {
        star_tick();
    }
    TEST_ASSERT(star_poll() == true); /* 迟到触发（第 23 拍，不可避免） */
    TEST_ASSERT(s_calls == 2);
    /* 相位被保留：下一次到期仍是第 30 拍（旧实现会漂到第 33 拍） */
    for (int i = 24; i <= 29; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                      /* 第 30 拍 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_calls == 3);
    star_timer_stop(&t);
}

static void test_ms_bound(void)
{
    static star_timer_t t;

    /* 时长运行时校验：ms==0 与 ms>=2^31 必须返回 STAR_ERR_PARAM，
     * 不能依赖默认关闭的 STAR_ASSERT（生产构建下会静默失效） */
    star_init(table, 2);
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(0), 0, false)
                == STAR_ERR_PARAM);
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(0), 0x80000000u, false)
                == STAR_ERR_PARAM);
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(0), 0)
                == STAR_ERR_PARAM);
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(0), 0x80000000u)
                == STAR_ERR_PARAM);
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(0), 0x7FFFFFFFu) == STAR_OK);
    TEST_ASSERT(star_timer_start(&t, 0, STAR_P(0), 0x7FFFFFFFu, false)
                == STAR_OK);
    TEST_ASSERT(star_timer_restart(&t, 0x80000000u) == STAR_ERR_PARAM);
    TEST_ASSERT(star_timer_restart(&t, 0) == STAR_ERR_PARAM);
    TEST_ASSERT(star_timer_restart(&t, 0x7FFFFFFFu) == STAR_OK);
    star_timer_stop(&t);
}

static void test_policy_invalid(void)
{
    static star_timer_t t;

    /* policy 越界必须运行时拒绝（此前仅靠可关闭的 STAR_ASSERT，
     * 生产构建会静默 fallback 成 RETRY 语义） */
    star_init(table, 2);
    TEST_ASSERT(star_timer_start_ex(&t, 0, NULL, 10, false,
                                    (star_timer_policy_t)0xFF)
                == STAR_ERR_PARAM);
    TEST_ASSERT(star_timer_start_ex(&t, 0, NULL, 10, false,
                                    STAR_TIMER_POLICY_LATEST) == STAR_OK);
    star_timer_stop(&t);
}

static void test_sorted_fire_order(void)
{
    static star_timer_t t1, t2, t3;

    /* 乱序启动：链表按 due 排序，触发顺序必须按到期时刻 */
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t1, 0, STAR_P(1), 30, false) == STAR_OK);
    TEST_ASSERT(star_timer_start(&t2, 0, STAR_P(2), 10, false) == STAR_OK);
    TEST_ASSERT(star_timer_start(&t3, 0, STAR_P(3), 20, false) == STAR_OK);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                       /* 第 10 拍：t2 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 2);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                       /* 第 20 拍：t3 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 3);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                       /* 第 30 拍：t1 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 1);
    TEST_ASSERT(star_poll() == false);
}

static void test_restart_resorts(void)
{
    static star_timer_t t1, t2;

    /* restart 改变 due 后必须重排：t1 原 100 拍后到期，第 10 拍 restart
     * 成 5 拍 → due=15，应早于 t2 的 50 拍触发 */
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_timer_start(&t1, 0, STAR_P(1), 100, false) == STAR_OK);
    TEST_ASSERT(star_timer_start(&t2, 0, STAR_P(2), 50, false) == STAR_OK);
    for (int i = 0; i < 10; i++) {
        star_tick();
    }
    TEST_ASSERT(star_timer_restart(&t1, 5) == STAR_OK);
    for (int i = 0; i < 4; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                       /* 第 15 拍：重排后的 t1 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 1);
    for (int i = 0; i < 34; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();                       /* 第 50 拍：t2 */
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 2);
}

static void test_next_due(void)
{
    static star_timer_t t1, t2;

    star_init(table, 2);
    TEST_ASSERT(star_next_due() == STAR_TICK_NONE); /* 无任何到期项 */

    TEST_ASSERT(star_timer_start(&t1, 0, NULL, 100, false) == STAR_OK);
    TEST_ASSERT(star_next_due() == 100);
    TEST_ASSERT(star_timer_start(&t2, 0, NULL, 30, false) == STAR_OK);
    TEST_ASSERT(star_next_due() == 30); /* 表头即最早 */
    star_timer_stop(&t2);
    TEST_ASSERT(star_next_due() == 100);

    /* 延时投递混入：比定时器更早 */
    TEST_ASSERT(star_event_post_delayed(0, NULL, 20) == STAR_OK);
    TEST_ASSERT(star_next_due() == 20);
    TEST_ASSERT(star_event_cancel_delayed(0, NULL) == STAR_OK);
    TEST_ASSERT(star_next_due() == 100);

    /* 定时器到期触发后链表清空 → NONE */
    for (int i = 0; i < 100; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(star_next_due() == STAR_TICK_NONE);
}

static void test_delayed_replace(void)
{
    star_init(table, 2);
    s_calls = 0;

    /* replace：同 evt 只留最新（更早的 deadline、新的 param） */
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(1), 100) == STAR_OK);
    TEST_ASSERT(star_event_post_delayed_replace(0, STAR_P(2), 50) == STAR_OK);
    for (int i = 0; i < 49; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 2); /* 被替换为新 param */
    TEST_ASSERT(s_calls == 1);      /* 只有一份，没有两份 */
    for (int i = 0; i < 60; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false); /* 旧 deadline 不再投递 */
    }

    /* replace 不额外占槽：槽池满时对已存在 evt 的 replace 仍成功 */
    for (uint16_t evt = 2; evt < 2 + STAR_DELAYED_MAX; evt++) {
        TEST_ASSERT(star_event_post_delayed(evt, NULL, 1000) == STAR_OK);
    }
    TEST_ASSERT(star_event_post_delayed_replace(3, STAR_P(9), 500) == STAR_OK);
    /* 无同 evt 且槽池满 → FULL */
    TEST_ASSERT(star_event_post_delayed_replace(99, STAR_P(9), 500)
                == STAR_ERR_FULL);
    /* ms 校验同口径 */
    TEST_ASSERT(star_event_post_delayed_replace(3, STAR_P(9), 0)
                == STAR_ERR_PARAM);
}

static void test_delayed_cancel(void)
{
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(7), 10) == STAR_OK);
    TEST_ASSERT(star_event_cancel_delayed(0, STAR_P(7)) == STAR_OK);
    TEST_ASSERT(star_event_cancel_delayed(0, STAR_P(7))
                == STAR_ERR_NOT_FOUND); /* 已取消，无匹配 */
    for (int i = 0; i < 15; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false); /* 取消后不再投递 */
    }

    /* param 参与匹配：不同 param 不受影响 */
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(1), 10) == STAR_OK);
    TEST_ASSERT(star_event_post_delayed(0, STAR_P(2), 10) == STAR_OK);
    TEST_ASSERT(star_event_cancel_delayed(0, STAR_P(1)) == STAR_OK);
    for (int i = 0; i < 9; i++) {
        star_tick();
        TEST_ASSERT(star_poll() == false);
    }
    star_tick();
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 2); /* 只剩未被取消的那份 */
}

static void test_sleep_deadline(void)
{
    static star_timer_t t;

    star_init(table, 2);
    star_host_idle_count = 0;
    star_host_idle_last_due = 0;

    /* 无到期项：睡眠，收到 STAR_TICK_NONE */
    star_sleep();
    TEST_ASSERT(star_host_idle_count == 1);
    TEST_ASSERT(star_host_idle_last_due == STAR_TICK_NONE);

    /* 有未来到期项：睡眠，deadline 正确传参 */
    TEST_ASSERT(star_timer_start(&t, 0, NULL, 100, false) == STAR_OK);
    star_sleep();
    TEST_ASSERT(star_host_idle_count == 2);
    TEST_ASSERT(star_host_idle_last_due == 100);

    /* 已过期（tick 越过 due）：不睡，主循环立即处理 */
    star_tick_set(200);
    star_sleep();
    TEST_ASSERT(star_host_idle_count == 2);
    star_timer_stop(&t);

    /* 队列非空：不睡 */
    star_tick_set(0);
    TEST_ASSERT(star_event_post(0, NULL) == STAR_OK);
    star_sleep();
    TEST_ASSERT(star_host_idle_count == 2);
    (void)star_poll(); /* 清空队列 */
}

void suite_timer(void)
{
    test_timer_one_shot();
    test_timer_periodic();
    test_stop_self_in_handler();
    test_restart();
    test_tick_overflow();
    test_post_delayed();
    test_one_shot_survives_full_queue();
    test_drop_policy_strict_deadline();
    test_latest_coalesces();
    test_periodic_no_drift();
    test_ms_bound();
    test_policy_invalid();
    test_sorted_fire_order();
    test_restart_resorts();
    test_next_due();
    test_delayed_replace();
    test_delayed_cancel();
    test_sleep_deadline();
}
