/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#include "star.h"

#ifdef STAR_PORT_HOST

#include <stdlib.h>

uint32_t star_host_primask;
uint32_t star_host_idle_count;
uint32_t star_host_idle_last_due;

/* 宿主机不睡眠：记录调用供测试验证 star_sleep 的 deadline 判定 */
void star_idle(uint32_t next_due)
{
    star_host_idle_count++;
    star_host_idle_last_due = next_due;
}

/* 宿主机断言失败：直接 abort，让测试立刻变红而不是挂死 */
void star_assert_fail(const char *file, int line)
{
    (void)file;
    (void)line;
    abort();
}

#elif defined(STAR_PORT_8051) || defined(STAR_PORT_251)

/* ---- 8051 / 80251（STC）固定拍移植 ----
 *
 * tick 源：Timer0 溢出中断（interrupt 1），ISR 内调用 star_tick()。
 * 定时器初始化（TMOD/AUXR/TH0/TL0/TR0 等）由应用在 main() 里完成——
 * 不同芯片配置各异：STC8/STC32 有 1T 模式与 AUXR 分频，STC89 为 12T。
 *
 * 空闲：PCON IDL（空闲模式：CPU 停、定时器与中断继续，任意中断唤醒）。
 * 契约要求 tick 中断已使能（ET0=1）方能唤醒，否则请勿启用空闲（见
 * STAR_PORT_IDLE_NOOP）。
 *
 * tickless（STAR_TICKLESS=1）暂不支持 8051/251：第一版仅固定拍。
 * 需要时按 docs/porting.md 的 tickless 协议、结合具体定时器型号扩展。 */

#ifndef STAR_PORT_NO_TICK_ISR
/* Timer0 溢出 ISR（interrupt 1）。C51 无弱符号：若用户工程已占用
 * Timer0，定义 STAR_PORT_NO_TICK_ISR 排除本 ISR，并在自己的 ISR 里
 * 调用 star_tick()（二者只能留其一）。 */
void star_timer0_isr(void) interrupt 1
{
    star_tick();
}
#endif

void star_idle(uint32_t next_due)
{
    STAR_UNUSED_PARAM(next_due); /* 固定拍不使用 deadline */
#ifdef STAR_PORT_IDLE_NOOP
    /* 未使能 tick 中断唤醒，或需绝对安全时：空转不休眠 */
#else
    /* IDL=1 进入空闲（下一条指令后 CPU 停）；中断唤醒后清 IDL，
     * 下次 star_idle 才能再次进入空闲 */
    STAR_PCON = STAR_PCON | 0x01u;
    STAR_PCON = STAR_PCON & 0xFEu;
#endif
}

STAR_WEAK void star_assert_fail(const char *file, int line)
{
    STAR_UNUSED_PARAM(file);
    STAR_UNUSED_PARAM(line);
    for (;;) {
    }
}

#else

#error "Unsupported port: provide STAR_PORT_HOST / STAR_PORT_8051 / STAR_PORT_251 via star_port.h"

#endif
