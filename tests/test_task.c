/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star.h"
#include "star_test.h"

static uint32_t s_calls0;
static uint32_t s_calls1;
static uint16_t s_last_evt;
static void *s_ctx_seen;

static void tsk0(uint16_t evt, void *param, void *ctx)
{
    (void)param;
    (void)ctx;
    s_calls0++;
    s_last_evt = evt;
}

static void tsk1(uint16_t evt, void *param, void *ctx)
{
    (void)param;
    (void)ctx;
    s_calls1++;
    s_last_evt = evt;
}

static void tsk_ctx(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)param;
    s_ctx_seen = ctx;
}

static const star_task_desc_t tasks[] = {
    STAR_TASK_DEF(10, tsk0, NULL),
    STAR_TASK_DEF(20, tsk1, NULL),
};

static void test_task_periodic(void)
{
    star_init(NULL, 0);
    star_task_init(tasks, 2);
    s_calls0 = s_calls1 = 0;

    TEST_ASSERT(star_task_start(0) == STAR_OK);
    for (int i = 0; i < 30; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 3); /* 10/20/30ms */
    TEST_ASSERT(s_calls1 == 0); /* 未启动不执行 */

    TEST_ASSERT(star_task_start(1) == STAR_OK);
    for (int i = 0; i < 20; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 5);
    TEST_ASSERT(s_calls1 == 1);
    TEST_ASSERT(s_last_evt == STAR_EVT_TASK);
}

static void test_task_stop(void)
{
    star_init(NULL, 0);
    star_task_init(tasks, 2);
    s_calls0 = 0;

    TEST_ASSERT(star_task_start(0) == STAR_OK);
    for (int i = 0; i < 10; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 1);
    TEST_ASSERT(star_task_stop(0) == STAR_OK);
    TEST_ASSERT(star_task_stop(0) == STAR_ERR_NOT_FOUND);
    for (int i = 0; i < 20; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 1); /* 停止后不再执行 */

    TEST_ASSERT(star_task_start(9) == STAR_ERR_PARAM); /* 越界 id */
}

static void test_task_slot_pool(void)
{
    /* 槽数 = STAR_TASK_SLOT_MAX：定义 STAR_TASK_SLOT_MAX+2 个任务，
     * 同时活跃数不能超过槽数（任务数随构建配置变化，不写死） */
    static star_task_desc_t many[STAR_TASK_SLOT_MAX + 2];

    for (uint16_t i = 0; i < STAR_TASK_SLOT_MAX + 2; i++) {
        many[i].handler = tsk0;
        many[i].ctx = NULL;
        many[i].period_ms = 10;
    }

    star_init(NULL, 0);
    star_task_init(many, STAR_TASK_SLOT_MAX + 2);

    int ok = 0;
    for (uint16_t i = 0; i < STAR_TASK_SLOT_MAX + 2; i++) {
        if (star_task_start(i) == STAR_OK) {
            ok++;
        }
    }
    TEST_ASSERT(ok == STAR_TASK_SLOT_MAX);      /* 只能启动槽数个 */
    TEST_ASSERT(star_task_stop(0) == STAR_OK);  /* 释放一个槽 */
    TEST_ASSERT(star_task_start(0) == STAR_OK); /* 重新启动被停止的 */
    TEST_ASSERT(star_task_stop(0) == STAR_OK);
    TEST_ASSERT(star_task_start(STAR_TASK_SLOT_MAX) == STAR_OK); /* 空槽可复用 */
}

static void test_task_ctx(void)
{
    static uint32_t ctx_val = 1234;
    static const star_task_desc_t ctasks[] = {
        STAR_TASK_DEF(10, tsk_ctx, &ctx_val),
    };

    star_init(NULL, 0);
    star_task_init(ctasks, 1);
    s_ctx_seen = NULL;

    TEST_ASSERT(star_task_start(0) == STAR_OK);
    for (int i = 0; i < 10; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_ctx_seen == &ctx_val);
}

static void test_task_no_drift(void)
{
    /* 相位稳定回归（与定时器同语义）：迟到执行后，
     * 下一次执行仍在绝对相位上（旧实现 due = now + period 会漂移） */
    star_init(NULL, 0);
    star_task_init(tasks, 2); /* tasks[0] 周期 10ms */
    s_calls0 = 0;
    TEST_ASSERT(star_task_start(0) == STAR_OK);
    for (int i = 1; i <= 10; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 1); /* 第 10 拍首次执行 */
    /* 主循环繁忙：第 11~23 拍无人 poll */
    for (int i = 11; i <= 23; i++) {
        star_tick();
    }
    star_poll();
    TEST_ASSERT(s_calls0 == 2); /* 迟到执行（第 23 拍） */
    for (int i = 24; i <= 29; i++) {
        star_tick();
        star_poll();
    }
    TEST_ASSERT(s_calls0 == 2);
    star_tick(); /* 第 30 拍：相位保留，准时执行（旧实现漂到 33 拍） */
    star_poll();
    TEST_ASSERT(s_calls0 == 3);
    TEST_ASSERT(star_task_stop(0) == STAR_OK);
}

static void test_task_ms_bound(void)
{
    /* 与定时器同口径的周期运行时校验：
     * period_ms==0 会退化为每 poll 同步调用（与 spin 无异）；
     * period_ms≥2^31 破坏回绕比较数学。此前仅靠默认关闭的断言 */
    static const star_task_desc_t bad[] = {
        STAR_TASK_DEF(0, tsk0, NULL),            /* 0：拒绝 */
        STAR_TASK_DEF(0x80000000u, tsk0, NULL),  /* ≥2^31：拒绝 */
        STAR_TASK_DEF(0x7FFFFFFFu, tsk0, NULL),  /* 上限内：接受 */
    };

    star_init(NULL, 0);
    star_task_init(bad, 3);
    TEST_ASSERT(star_task_start(0) == STAR_ERR_PARAM);
    TEST_ASSERT(star_task_start(1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_task_start(2) == STAR_OK);
    TEST_ASSERT(star_task_stop(2) == STAR_OK);
}

void suite_task(void)
{
    test_task_periodic();
    test_task_stop();
    test_task_slot_pool();
    test_task_ctx();
    test_task_no_drift();
    test_task_ms_bound();
}
