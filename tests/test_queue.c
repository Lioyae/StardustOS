/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star.h"
#include "star_test.h"

static uint32_t s_last_evt = 0xFFFF;
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

static void test_post_and_dispatch(void)
{
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_event_post(0, STAR_P(42)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_calls == 1);
    TEST_ASSERT(s_last_evt == 0);
    TEST_ASSERT(s_last_param == 42);
    TEST_ASSERT(star_poll() == false);
}

static void test_queue_full_returns_error(void)
{
    star_init(table, 2);
    star_status_t st = STAR_OK;
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        st = star_event_post(0, STAR_P(i));
    }
    TEST_ASSERT(st == STAR_OK);
    TEST_ASSERT(star_event_post(0, STAR_P(99)) == STAR_ERR_FULL);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    TEST_ASSERT(s_last_param == STAR_EVT_QUEUE_SIZE - 1);
    TEST_ASSERT(star_poll() == false);
}

static void test_post_replace_overwrites(void)
{
    star_init(table, 2);
    s_calls = 0;
    TEST_ASSERT(star_event_post(0, STAR_P(1)) == STAR_OK);
    TEST_ASSERT(star_event_post_replace(0, STAR_P(2)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_param == 2);
    TEST_ASSERT(star_poll() == false); /* 只有一条事件 */
}

static void test_replace_on_full_queue(void)
{
    star_init(table, 2);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(0, STAR_P(i)) == STAR_OK);
    }
    /* 满队列替换同 ID：成功且不增加条目 */
    TEST_ASSERT(star_event_post_replace(0, STAR_P(777)) == STAR_OK);
    /* 满队列替换不同 ID：失败 */
    TEST_ASSERT(star_event_post_replace(1, STAR_P(888)) == STAR_ERR_FULL);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    TEST_ASSERT(s_last_param == 777);
    TEST_ASSERT(star_poll() == false);
}

static void test_unregistered_id_dropped(void)
{
    star_init(table, 2);
    s_last_evt = 0xFFFF;
    TEST_ASSERT(star_event_post(5, STAR_P(1)) == STAR_OK); /* ID 越界也入队 */
    TEST_ASSERT(star_poll() == true);  /* 被取出但安全丢弃 */
    TEST_ASSERT(s_last_evt == 0xFFFF); /* handler 未被调用 */
}

static void test_null_handler_dropped(void)
{
    static const star_evt_entry_t t2[] = {
        [3] = STAR_ENTRY(NULL, NULL), /* 显式空 handler */
    };
    star_init(t2, 4);
    s_last_evt = 0xFFFF;
    TEST_ASSERT(star_event_post(3, STAR_P(1)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_last_evt == 0xFFFF);
}

static void test_dropped_count(void)
{
    star_init(table, 2);
    TEST_ASSERT(star_dropped_count() == 0);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(0, STAR_P(i)) == STAR_OK);
    }
    TEST_ASSERT(star_event_post(0, STAR_P(9)) == STAR_ERR_FULL);
    TEST_ASSERT(star_event_post_replace(1, STAR_P(8)) == STAR_ERR_FULL);
    TEST_ASSERT(star_dropped_count() == 2);
}

static void test_crit_nesting(void)
{
    star_init(table, 2);

    /* 外部进入临界区后调用内核 API：内核内部的进入/退出
     * 必须恢复调用方状态，而不是强行打开中断 */
    star_crit_state_t cs = star_crit_enter();
    TEST_ASSERT(star_crit_active() == 1);
    TEST_ASSERT(star_event_post(0, STAR_P(1)) == STAR_OK);
    TEST_ASSERT(star_event_post_replace(0, STAR_P(2)) == STAR_OK);
    TEST_ASSERT(star_crit_active() == 1); /* 仍处于调用方关闭状态 */
    star_crit_exit(cs);
    TEST_ASSERT(star_crit_active() == 0);

    /* 嵌套两层 */
    star_crit_state_t c1 = star_crit_enter();
    star_crit_state_t c2 = star_crit_enter();
    TEST_ASSERT(star_crit_active() == 1);
    star_crit_exit(c2);
    TEST_ASSERT(star_crit_active() == 1);
    star_crit_exit(c1);
    TEST_ASSERT(star_crit_active() == 0);
}

static uint16_t s_hook_evt;
static uint32_t s_hook_calls;

static void drop_hook(uint16_t evt)
{
    s_hook_evt = evt;
    s_hook_calls++;
}

static void test_drop_hook(void)
{
    star_init(table, 2);
    s_hook_calls = 0;
    star_set_drop_hook(drop_hook);

    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(0, STAR_P(i)) == STAR_OK);
    }
    TEST_ASSERT(star_event_post(0, STAR_P(9)) == STAR_ERR_FULL);
    TEST_ASSERT(s_hook_calls == 1);
    TEST_ASSERT(s_hook_evt == 0);
    TEST_ASSERT(star_dropped_count() == 1);

    /* 未注册事件丢弃也触发钩子 */
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    TEST_ASSERT(star_event_post(7, STAR_P(1)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_hook_calls == 2);
    TEST_ASSERT(s_hook_evt == 7);

    /* 取消钩子后不再回调 */
    star_set_drop_hook(NULL);
    TEST_ASSERT(star_event_post(9, STAR_P(1)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_hook_calls == 2);
}

static uint16_t s_evil_evt;
static uint32_t s_evil_calls;

static void evil_hook(uint16_t evt)
{
    s_evil_evt = evt;
    s_evil_calls++;
    star_event_post(0, STAR_P(evt)); /* 钩子内再投递：再次满队触发丢弃 */
}

static void test_hook_reentrancy(void)
{
    star_init(table, 2);
    s_evil_calls = 0;
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(0, STAR_P(i)) == STAR_OK);
    }
    star_set_drop_hook(evil_hook);
    TEST_ASSERT(star_event_post(0, STAR_P(9)) == STAR_ERR_FULL);
    /* 防重入：钩子只被调用一次（其内部的丢弃不递归回调） */
    TEST_ASSERT(s_evil_calls == 1);
    TEST_ASSERT(s_evil_evt == 0);
    TEST_ASSERT(star_dropped_count() == 2); /* 外层 1 + 钩子内 1 */
    star_set_drop_hook(NULL);
}

void suite_queue(void)
{
    test_post_and_dispatch();
    test_queue_full_returns_error();
    test_post_replace_overwrites();
    test_replace_on_full_queue();
    test_unregistered_id_dropped();
    test_null_handler_dropped();
    test_dropped_count();
    test_drop_hook();
    test_hook_reentrancy();
    test_crit_nesting();
}
