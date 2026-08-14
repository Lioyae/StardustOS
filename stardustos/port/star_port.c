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
 * 空闲：默认空转不休眠（绝对安全，主循环退化为轮询）。定义 STAR_PORT_IDLE
 * 才启用 PCON IDL 空闲模式——但 8051 空闲唤醒条件在不同变体间存在分歧
 * （"已使能中断"是否含 EA 各厂家不一，且 STC 未经板级验证），启用前须
 * 上板实测（见 star_idle 内注释与 docs/porting.md）。
 *
 * tickless（STAR_TICKLESS=1）暂不支持 8051/251：第一版仅固定拍。 */

#ifndef STAR_PORT_NO_TICK_ISR
/* Timer0 溢出 ISR（interrupt 1）。C51 无弱符号：若用户工程已占用
 * Timer0，定义 STAR_PORT_NO_TICK_ISR 排除本 ISR，并在自己的 ISR 里
 * 调用 star_tick()（二者只能留其一）。 */
void star_timer0_isr(void) STAR_INTERRUPT(1)
{
    star_tick();
}
#endif

void star_idle(uint32_t next_due)
{
    STAR_UNUSED_PARAM(next_due); /* 固定拍不使用 deadline */
    /* 默认空转：绝对安全，主循环退化为轮询，不进入低功耗模式。 */
#ifdef STAR_PORT_IDLE
    /* 启用 PCON IDL 空闲（拆雷版）：
     * 8051 空闲唤醒依赖"已使能中断"，而本函数在关中断（EA=0）下被调用，
     * 直接进 IDL 可能睡死（不同厂商对 EA 的要求不一）。故由本函数自己
     * 处理 EA：进 IDL 前临时开中断、唤醒后重新关中断，把唤醒交给 tick
     * 中断——用户只需定义 STAR_PORT_IDLE，无需自写 EA 逻辑。
     * 代价：开中断到进 IDL 之间投递的事件会延迟到下一 tick 才处理
     * （≤1 拍，固定拍下每 1ms 兜底）——8051 无 ARM wfi 原子性的固有妥协，
     * 远好过睡死。启用前仍建议上板实测空闲电流与唤醒是否正常。 */
    STAR_EA = 1;                       /* 临时开中断，让 IDL 可被 tick 唤醒 */
    STAR_PCON = STAR_PCON | 0x01u;     /* IDL=1：进空闲 */
    STAR_PCON = STAR_PCON & 0xFEu;     /* 唤醒后清 IDL */
    STAR_EA = 0;                       /* 恢复关中断，满足"返回时处临界区"契约 */
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
