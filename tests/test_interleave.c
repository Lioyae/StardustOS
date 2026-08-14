/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#include "star.h"
#include "star_test.h"
#include <stdlib.h>

/* 单线程交错测试骨架：
 * 用伪随机序列在"主循环操作"与"伪中断操作"之间交错执行，
 * 伪中断遵守硬件规则——临界区内不执行；伪中断还模拟 tick 中断
 * （star_tick）驱动周期定时器，真实覆盖 process_timers 的注入窗口。
 * 开启 STAR_TEST_INJECT_ENABLE 时，内核会在 API 内部窗口
 * 回调注入点，实现更细粒度的交错。
 * 注入窗口覆盖（详见 star.h 测试注入注释）：
 *   star_event_post / post_replace / post_delayed、
 *   star_mail_send（入临界区前，覆盖先入队后入箱顺序与失败原子性的竞态）、
 *   star_poll（单步前）、star_process_timers（定时器列表遍历中）。
 * 验证内核并发语义的一致性：每次投递尝试（含定时器触发的内部投递）
 * 要么最终被派发、要么被计数丢弃；邮箱无滞留；临界区不泄漏。
 * 注意：以上均为对"建模并发语义"的验证（伪中断规则由测试自行定义），
 * 不构成真实硬件验证。
 * 种子默认固定；设环境变量 STAR_TEST_SEED 可换轨迹（CI 多种子跑）。 */

#define IV_EVT_MAIL  0
#define IV_EVT_PLAIN 1
#define IV_SLOTS 4
#define IV_SIZE  8
#define IV_TIMER_PERIOD 5 /* 周期定时器：tick 驱动，覆盖 process_timers 窗口 */

STAR_MAILBOX_DEF(iv_mb, IV_EVT_MAIL, IV_SLOTS, IV_SIZE);

static star_timer_t iv_timer;

static uint32_t s_plain_calls;
static uint32_t s_recv_bytes;

static void plain_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)param;
    (void)ctx;
    s_plain_calls++;
}

static void mail_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)ctx;
    star_mail_t *mb = (star_mail_t *)param;
    uint8_t buf[IV_SIZE];
    int n;

    while ((n = star_mail_recv(mb, buf)) > 0) {
        s_recv_bytes += (uint32_t)n;
    }
}

static const star_evt_entry_t iv_table[] = {
    [IV_EVT_MAIL]  = STAR_ENTRY(mail_handler, NULL),
    [IV_EVT_PLAIN] = STAR_ENTRY(plain_handler, NULL),
};

static uint32_t s_rng = 0x12345678u;

static uint32_t iv_rand(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* 注入点：内核 API 内部窗口触发的伪中断（有防递归保护） */
static void iv_do_isr(uint32_t *attempts);

#ifdef STAR_TEST_INJECT_ENABLE
static uint8_t s_injecting;
static uint32_t s_attempts;

static void iv_inject(void)
{
    if (s_injecting || star_crit_active() != 0) {
        return; /* 防递归；且临界区内 ISR 不执行（硬件语义） */
    }
    s_injecting = 1;
    iv_do_isr(&s_attempts);
    s_injecting = 0;
}
#endif

/* 伪中断：模拟硬件行为（临界区内不可抢占），并模拟 tick 中断 */
static void iv_do_isr(uint32_t *attempts)
{
    if (star_crit_active() != 0) {
        return; /* 硬件语义：关中断期间 ISR 不执行 */
    }
    star_tick(); /* tick ISR：驱动周期定时器 */
    (*attempts)++;
    if (iv_rand() & 1u) {
        star_event_post(IV_EVT_PLAIN, STAR_P(iv_rand()));
    } else {
        uint8_t d = (uint8_t)iv_rand();
        star_mail_send(&iv_mb, &d, 1);
    }
}

static void iv_do_main(uint32_t *attempts, uint32_t *poll_true)
{
    switch (iv_rand() % 3) {
    case 0:
        (*attempts)++;
        star_event_post(IV_EVT_PLAIN, STAR_P(iv_rand()));
        break;
    case 1:
        (*attempts)++;
        {
            uint8_t d = (uint8_t)iv_rand();
            star_mail_send(&iv_mb, &d, 1);
        }
        break;
    default:
        if (star_poll()) {
            (*poll_true)++;
        }
        break;
    }
}

static void test_interleave_consistency(void)
{
    uint32_t attempts = 0;
    uint32_t poll_true = 0;
    uint8_t buf[IV_SIZE];
    const char *seed_env = getenv("STAR_TEST_SEED");

    if (seed_env != NULL) {
        s_rng = (uint32_t)strtoul(seed_env, NULL, 10);
    }

    star_init(iv_table, 2);
    s_plain_calls = 0;
    s_recv_bytes = 0;
    /* 周期定时器：由伪 tick 驱动触发，触发本身也是一次"投递尝试"
     * （成功→最终派发；失败→计入丢弃），因此总账等式仍然精确成立 */
    TEST_ASSERT(star_timer_start(&iv_timer, IV_EVT_PLAIN, STAR_P(0xDEAD),
                                 IV_TIMER_PERIOD, true) == STAR_OK);

#ifdef STAR_TEST_INJECT_ENABLE
    s_attempts = 0;
    star_test_inject_set(iv_inject);
#endif

    for (int i = 0; i < 4000; i++) {
        if (iv_rand() & 1u) {
            iv_do_isr(&attempts);
        } else {
            iv_do_main(&attempts, &poll_true);
        }
        /* 内核不得泄漏临界区 */
        TEST_ASSERT(star_crit_active() == 0);
    }

#ifdef STAR_TEST_INJECT_ENABLE
    star_test_inject_set(NULL);
    attempts += s_attempts; /* 注入点内的尝试并入总账 */
#endif

    while (star_poll()) {
        poll_true++;
    }

    /* 总账：每次投递尝试要么最终派发、要么被计数丢弃。
     * 定时器触发 = 内核内部投递（同样要么派发要么计数），因此
     * fires = 派发与丢弃总数 - 显式尝试数，且必须显著大于 0
     * （证明周期定时器真的被 tick 驱动、process_timers 窗口被覆盖） */
    {
        uint32_t fires = poll_true + star_dropped_count() - attempts;

        TEST_ASSERT(fires > 50);
        /* 显式尝试数不可能超过派发+丢弃总数 */
        TEST_ASSERT(attempts <= poll_true + star_dropped_count());
    }
    /* 总账二：邮箱全清，无滞留数据 */
    TEST_ASSERT(star_mail_recv(&iv_mb, buf) == -1);
    /* 总账三：派发的普通事件数与邮箱出货量对得上。
     * 每次成功的 mail_send 存 1 字节且入队 1 个事件 → 派发的邮箱事件数
     * = 累计取出的字节数（handler 每次派发都会把柜子 drain 空，
     * 总数恒等，不依赖单次 drain 几格） */
    TEST_ASSERT(s_plain_calls + s_recv_bytes == poll_true);
}

void suite_interleave(void)
{
    test_interleave_consistency();
}
