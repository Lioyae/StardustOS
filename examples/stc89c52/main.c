/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 *
 * STC89C52 系列例程（Keil C51 / SDCC 双编译器）
 *
 * 前置条件：
 *  1. Keil C51：使用自带 reg52.h；SDCC：使用自带 <8051.h>（含 P1_0 等 sbit）。
 *  2. 工程加入内核源文件：stardustos/star.c、star_task.c、star_mail.c、
 *     stardustos/port/star_port.c；include 路径含 stardustos/ 与
 *     stardustos/port/8051/。
 *  3. 本示例用自定义 Timer0 ISR（模式 1 需手动重装），须在 Keil C51
 *     编译选项中定义 STAR_PORT_NO_TICK_ISR（SDCC 侧用 -DSTAR_PORT_NO_TICK_ISR），
 *     排除内核默认 Timer0 ISR，避免中断向量重复定义。
 *
 * 本示例：12MHz 主频，Timer0 1ms tick，P1.0 每 500ms 翻转。
 */

#if defined(__SDCC) || defined(SDCC)
#include <8051.h>          /* SDCC 自带 8051 头（含 P1_0 等 sbit） */
#define LED P1_0
#else
#include "reg52.h"         /* Keil C51 自带 */
sbit LED = P1 ^ 0;         /* reg52.h 不提供单个引脚位名 */
#endif

#include "star.h"

/* 事件 ID：从 0 连续编号 */
enum {
    EVT_BLINK = 0,
    EVT_COUNT,
};

/* 12MHz、12T、Timer0 模式1（16 位手动重装）：
 * 1ms = 1000 拍，重装值 = 65536 - 1000 = 0xFC18 */
#define T0_RELOAD_H 0xFCu
#define T0_RELOAD_L 0x18u

static void timer0_init_1ms(void)
{
    TMOD &= 0xF0u;
    TMOD |= 0x01u;   /* Timer0 模式 1（16 位，手动重装） */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;
    TR0 = 1;
    EA = 1;
}

/* STC89C52 无 16 位自动重装：自定义 Timer0 ISR，手动重装 + 推进时基。
 * 使用自定义 ISR 时务必定义 STAR_PORT_NO_TICK_ISR（见文件头）。 */
#if defined(__SDCC) || defined(SDCC)
void timer0_isr(void) __interrupt(1)
#else
void timer0_isr(void) interrupt 1
#endif
{
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    star_tick();
}

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    LED = !LED;      /* P1.0 LED 翻转 */
}

static const star_evt_entry_t evt_table[EVT_COUNT] = {
    STAR_ENTRY(blink_handler, NULL),   /* EVT_BLINK = 0 */
};

static star_timer_t blink_timer;

void main(void)
{
    timer0_init_1ms();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();   /* 永不返回 */
}
