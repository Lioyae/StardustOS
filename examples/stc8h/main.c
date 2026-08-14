/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 *
 * STC8H / STC8A 系列例程（Keil C51）
 *
 * 前置条件：
 *  1. 用 STC-ISP 软件生成器件头文件 STC8H.H（或具体型号 STC8H8K64U.H 等），
 *     加入 Keil 工程 include 路径。
 *  2. 工程加入内核源文件：stardustos/star.c、star_task.c、star_mail.c、
 *     stardustos/port/star_port.c；include 路径含 stardustos/ 与
 *     stardustos/port/8051/。
 *  3. 内部 RAM 紧张时，用 -DSTAR_RAM_CLASS=xdata 把内核大数组搬到 XRAM
 *     （STC8H 有 8KB XRAM），腾出内部 RAM 给堆栈。
 *
 * 本示例：24MHz 主频，Timer0 1ms tick，P0.0 每 500ms 翻转。
 */

#include "STC8H.H"
#include "star.h"

/* 事件 ID：从 0 连续编号（ID 即注册表下标，稀疏会浪费 Flash） */
enum {
    EVT_BLINK = 0,
    EVT_COUNT,
};

/* 24MHz、1T、Timer0 模式0（16 位自动重装）：
 * 1ms = 24000 拍，重装值 = 65536 - 24000 = 0xA240 */
#define T0_RELOAD_H 0xA2u
#define T0_RELOAD_L 0x40u

static void timer0_init_1ms(void)
{
    AUXR |= 0x80u;   /* T0x12=1：Timer0 1T 模式 */
    TMOD &= 0xF0u;   /* 清 Timer0 模式位 → 模式 0（16 位自动重装） */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;         /* Timer0 溢出中断使能 */
    TR0 = 1;         /* 启动 Timer0 */
    EA = 1;          /* 开全局中断 */
}
/* Timer0 溢出 ISR 由内核 port 层提供（interrupt 1，调用 star_tick()），
 * 无需在此编写。 */

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    P00 = !P00;      /* P0.0 LED 翻转 */
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
