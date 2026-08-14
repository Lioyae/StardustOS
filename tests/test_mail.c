/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star.h"
#include "star_test.h"
#include <string.h>

#define MB_SLOTS 3
#define MB_SIZE  8

static uint8_t s_recv_buf[MB_SIZE];
static int s_recv_len = -1;

STAR_MAILBOX_DEF(mb, 0, MB_SLOTS, MB_SIZE);

static void mail_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)ctx;
    s_recv_len = star_mail_recv((star_mail_t *)param, s_recv_buf);
}

static const star_evt_entry_t table[] = {
    [0] = STAR_ENTRY(mail_handler, NULL),
    [1] = STAR_ENTRY(NULL, NULL), /* 空 handler，用于占满队列 */
};

static void test_send_recv_roundtrip(void)
{
    uint8_t data[MB_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    star_init(table, 1);
    s_recv_len = -1;

    TEST_ASSERT(star_mail_send(&mb, data, sizeof(data)) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_recv_len == MB_SIZE);
    TEST_ASSERT(memcmp(s_recv_buf, data, MB_SIZE) == 0);
    TEST_ASSERT(star_poll() == false);
}

static void test_send_full(void)
{
    uint8_t data = 0x55;

    star_init(table, 1);
    for (int i = 0; i < MB_SLOTS; i++) {
        TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_OK);
    }
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_ERR_FULL);
    /* 全部取出，满槽状态恢复；每箱实际存入 1 字节 */
    for (int i = 0; i < MB_SLOTS; i++) {
        TEST_ASSERT(star_poll() == true);
        TEST_ASSERT(s_recv_len == 1);
        TEST_ASSERT(s_recv_buf[0] == 0x55);
    }
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_OK);
    TEST_ASSERT(star_poll() == true); /* 收尾 drain，恢复空箱 */
}

static void test_recv_empty(void)
{
    static uint8_t buf[MB_SIZE];
    STAR_MAILBOX_DEF(mb2, 1, MB_SLOTS, MB_SIZE);

    star_init(table, 1);
    TEST_ASSERT(star_mail_recv(&mb2, buf) == -1);
}

static void test_send_variable_len_roundtrip(void)
{
    uint8_t data[MB_SIZE] = { 1, 2, 3, 4, 5, 6, 7, 8 };

    star_init(table, 1);
    s_recv_len = -1;
    /* 变长契约：len ≤ item_size，recv 返回实际存入长度，
     * 不再回吐整格残留垃圾 */
    TEST_ASSERT(star_mail_send(&mb, data, 3) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_recv_len == 3);
    TEST_ASSERT(memcmp(s_recv_buf, data, 3) == 0);
    TEST_ASSERT(star_poll() == false);
}

static void test_send_oversize_rejected(void)
{
    uint8_t data[16];

    memset(data, 0x5A, sizeof(data));
    star_init(table, 1);
    s_recv_len = -1;
    /* 超长不再静默截断：拒绝且不入箱、不投事件 */
    TEST_ASSERT(star_mail_send(&mb, data, sizeof(data)) == STAR_ERR_PARAM);
    TEST_ASSERT(star_poll() == false);
    TEST_ASSERT(star_mail_recv(&mb, s_recv_buf) == -1);
    /* len == 0 同样拒绝 */
    TEST_ASSERT(star_mail_send(&mb, data, 0) == STAR_ERR_PARAM);
}

static void test_send_rollback_on_queue_full(void)
{
    uint8_t data = 0x55;
    static uint8_t buf[MB_SIZE];

    star_init(table, 2);
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(1, STAR_P(i)) == STAR_OK);
    }
    /* 队列满：send 必须整体失败，邮箱不得滞留数据 */
    uint32_t d0 = star_dropped_count();
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_ERR_FULL);
    TEST_ASSERT(star_dropped_count() == d0 + 1); /* 口径统一：拒绝计入丢弃 */
    TEST_ASSERT(star_mail_recv(&mb, buf) == -1);

    /* 队列清空后 send 恢复正常 */
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true); /* 丢弃占位事件 */
    }
    s_recv_len = -1;
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_recv_len == 1);
    TEST_ASSERT(s_recv_buf[0] == 0x55);
}

/* 钩子重入同一邮箱：丢弃钩子在 star_note_dropped 内（临界区中）向触发
 * 本次丢弃的同一个邮箱再发送。先入队后入箱的实现下，钩子观察到的邮箱
 * 状态必须自洽：外层失败不残留数据、钩子的发送结果与邮箱计数一致 */
static uint16_t s_hook_evt;
static uint32_t s_hook_attempts;
static star_status_t s_hook_st;

static void reenter_mail_hook(uint16_t evt)
{
    uint8_t d = 0xAA;

    s_hook_evt = evt;
    s_hook_attempts++;
    s_hook_st = star_mail_send(&mb, &d, 1);
}

static void test_hook_reenter_same_mailbox(void)
{
    uint8_t data = 0x55;
    static uint8_t buf[MB_SIZE];

    star_init(table, 2);
    s_hook_attempts = 0;
    s_hook_st = STAR_OK;
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_event_post(1, STAR_P(i)) == STAR_OK);
    }
    star_set_drop_hook(reenter_mail_hook);
    /* 队列满 → 外层 send 失败 → 钩子重入同邮箱 → 队列仍满 → 钩子也失败；
     * 但邮箱不得因两次失败残留任何数据、计数不得被吞 */
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_ERR_FULL);
    TEST_ASSERT(s_hook_attempts == 1);
    TEST_ASSERT(s_hook_evt == 0);
    TEST_ASSERT(s_hook_st == STAR_ERR_FULL); /* 钩子看到的是满队列，一致 */
    TEST_ASSERT(star_mail_recv(&mb, buf) == -1); /* 无残留 */
    star_set_drop_hook(NULL);

    /* 排空队列后，邮箱应完好可用：入箱/出箱往返正常 */
    for (int i = 0; i < STAR_EVT_QUEUE_SIZE; i++) {
        TEST_ASSERT(star_poll() == true);
    }
    s_recv_len = -1;
    TEST_ASSERT(star_mail_send(&mb, &data, 1) == STAR_OK);
    TEST_ASSERT(star_poll() == true);
    TEST_ASSERT(s_recv_len == 1);
    TEST_ASSERT(s_recv_buf[0] == 0x55);
}

static void test_invalid_mailbox_rejected(void)
{
    static uint8_t buf[8];
    static uint8_t lens[2];
    star_mail_t bad;

    /* 手工构造的非法邮箱（slots==0 → 除零、空指针、item_size 越界等）
     * 必须运行时拒绝而不是崩溃 */
    memset(&bad, 0, sizeof(bad));
    bad.buf = buf;
    bad.lens = lens;
    bad.slots = 0;            /* 除零风险 */
    bad.item_size = 8;
    TEST_ASSERT(star_mail_send(&bad, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    bad.slots = 2;
    bad.item_size = 0;        /* 非法格宽 */
    TEST_ASSERT(star_mail_send(&bad, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    bad.item_size = 300;      /* 超出 lens(uint8_t) 表达范围 */
    TEST_ASSERT(star_mail_send(&bad, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    bad.item_size = 8;
    bad.lens = NULL;          /* 空长度表 */
    TEST_ASSERT(star_mail_send(&bad, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    bad.lens = lens;
    bad.buf = NULL;           /* 空数据区 */
    TEST_ASSERT(star_mail_send(&bad, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    /* 槽长度域写坏：lens 值越界必须拒绝而不是按该长度越界读
     * （lens 是随结构手工构造的内存，垃圾值不可信任） */
    bad.buf = buf;
    bad.slots = 2;
    bad.item_size = 8;
    bad.head = 0;
    bad.count = 1;
    lens[0] = 200; /* > item_size：越界读风险 */
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);
    lens[0] = 0;   /* 0 同样非法 */
    TEST_ASSERT(star_mail_recv(&bad, buf) == -1);

    TEST_ASSERT(star_mail_send(NULL, buf, 1) == STAR_ERR_PARAM);
    TEST_ASSERT(star_mail_recv(NULL, buf) == -1);
}

void suite_mail(void)
{
    test_send_recv_roundtrip();
    test_send_full();
    test_recv_empty();
    test_send_variable_len_roundtrip();
    test_send_oversize_rejected();
    test_send_rollback_on_queue_full();
    test_hook_reenter_same_mailbox();
    test_invalid_mailbox_rejected();
}
