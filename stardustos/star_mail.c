/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#include "star.h"

#if STAR_ENABLE_MAILBOX

/* 内核不依赖 libc：对齐感知的拷贝（32 位字拷贝 + 头尾字节）。
 * M0+/RV32EC 不支持非对齐访存，源地址未对齐时按字节组装字 */
static void star_copy(void *dst, const void *src, uint16_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n != 0 && ((uintptr_t)d & 3u) != 0u) {
        *d++ = *s++;
        n--;
    }
    while (n >= 4) {
        uint32_t w = (uint32_t)s[0]
                   | ((uint32_t)s[1] << 8)
                   | ((uint32_t)s[2] << 16)
                   | ((uint32_t)s[3] << 24);
        /* 字节存储而非强转指针写入：规避严格别名 UB；
         * 编译器 -Os 会自动合并为字存储 */
        d[0] = (uint8_t)(w >> 0);
        d[1] = (uint8_t)(w >> 8);
        d[2] = (uint8_t)(w >> 16);
        d[3] = (uint8_t)(w >> 24);
        d += 4;
        s += 4;
        n -= 4;
    }
    while (n != 0) {
        *d++ = *s++;
        n--;
    }
}

/* 邮箱字段合法性：手工构造的 star_mail_t 可能出现 slots==0（除零）、
 * 缓冲区/长度表为空指针（野指针）、item_size 越界（lens 为 uint8_t，
 * 长度会截断）、head/count 越界（越界读）等，运行时一律拒绝而不是崩溃 */
static bool star_mail_invalid(const star_mail_t *mb)
{
    return mb == NULL || mb->buf == NULL || mb->lens == NULL ||
           mb->slots == 0 || mb->item_size == 0 || mb->item_size > 255 ||
           mb->head >= mb->slots || mb->count > mb->slots;
}

star_status_t star_mail_send(star_mail_t *mb, const void *data, uint16_t len)
{
    star_status_t st;
    star_crit_state_t cs;

    /* 定长上限 + 变长下限：len 必须 1..item_size。
     * 此前超长静默截断、不足 item_size 时 recv 回吐整格残留垃圾，
     * 接收端无法知道实际长度；现在每槽记录实际存入长度 */
    if (star_mail_invalid(mb) || data == NULL || len == 0 ||
        len > mb->item_size) {
        return STAR_ERR_PARAM;
    }

    STAR_TEST_INJECT(); /* 交错测试窗口：入临界区前伪中断可插入 */

    /* 入箱与事件入队必须在同一个临界区内完成：
     * 中断不可能插进拷贝与入队之间。
     * 顺序为"先入队、后入箱"：入队失败时邮箱未动、无需回滚，
     * 消除了"count 已递增、回滚未发生"的中间状态。旧顺序（先入箱
     * 后入队）下，丢事件钩子在 star_note_dropped 内重入本函数向同一
     * 邮箱发送时，外层回滚会吞掉内层消息的计数。先入队后，钩子在
     * 任何时刻看到的邮箱状态都是自洽的。
     * 事件先于数据入队无可观测窗口：本临界区内中断关闭、主循环
     * 不可能并发消费队列；出临界区前数据必已就位（全有或全无） */
    cs = star_crit_enter();
    if (mb->count >= mb->slots) {
        star_note_dropped(mb->evt); /* 口径统一：被拒绝的入箱也计入 */
        st = STAR_ERR_FULL;
    } else {
        st = star_event_enqueue(mb->evt, (void *)mb);
        if (st == STAR_OK) {
            unsigned idx = (unsigned)mb->head + mb->count;

            STAR_ASSERT(mb->count < mb->slots);
            if (idx >= mb->slots) {
                idx -= mb->slots; /* head+count < 2*slots，一次减法足够 */
            }
            star_copy(&mb->buf[idx * mb->item_size], data, len);
            mb->lens[idx] = (uint8_t)len;
            mb->count++;
        }
    }
    star_crit_exit(cs);

    return st;
}

int star_mail_recv(star_mail_t *mb, void *data)
{
    int n;
    star_crit_state_t cs;

    if (star_mail_invalid(mb) || data == NULL) {
        return -1;
    }

    cs = star_crit_enter();
    if (mb->count == 0) {
        n = -1;
    } else if (mb->count > mb->slots || mb->head >= mb->slots) {
        n = -1; /* 结构被写坏：拒绝而不是越界读 */
    } else {
        n = mb->lens[mb->head]; /* 实际存入长度，不是整格 */
        /* 每槽长度域校验：lens 是随邮箱结构一并手工构造的内存，
         * 被写坏为 0 或 >item_size 时按该值拷贝会越界读——
         * 与结构字段校验同口径，写坏即拒绝而不是崩溃 */
        if (n < 1 || n > mb->item_size) {
            n = -1;
        } else {
            star_copy(data, &mb->buf[mb->head * mb->item_size], (uint16_t)n);
            if (mb->head + 1 >= mb->slots) {
                mb->head = 0;
            } else {
                mb->head++;
            }
            mb->count--;
        }
    }
    star_crit_exit(cs);

    return n;
}

#endif
