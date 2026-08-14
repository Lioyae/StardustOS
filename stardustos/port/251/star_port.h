/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* 80251 内核移植（STC32G 等，Keil C251）
 *
 * STC32G 是 32 位 80251 内核，中断系统兼容 8051：
 * 临界区同样用 EA（IE 寄存器 0xA8 的 bit7）保存/恢复。
 * C251 支持 inline，内存空间足够，无需 idata/xdata 显式指定。 */

#include <stdint.h>

/* 体系标签：star_port.c 据此选择 8051/STC 实现 */
#define STAR_PORT_251 1

/* C251 内存空间足够：内核静态数据用默认存储类即可 */
#define STAR_STATIC static

/* 与 8051 一致：弱符号置空（如需自定义 tick，定义 STAR_PORT_NO_TICK_ISR） */
#define STAR_WEAK

/* C251 与 C51 一致不支持 inline 关键字：降级为 static */
#define STAR_INLINE static

/* C251 中断函数语法与 C51 相同 */
#define STAR_INTERRUPT(n) interrupt n

/* 临界区状态：保存 EA（0/1），用 unsigned char 承载 */
typedef unsigned char star_crit_state_t;

/* 裸声明 SFR（不依赖厂商头文件，STAR_ 前缀避免与厂商头冲突） */
sfr STAR_IE   = 0xA8;         /* 中断使能寄存器 IE */
sbit STAR_EA  = STAR_IE ^ 7;  /* 全局中断使能 EA（IE bit7） */
sfr STAR_PCON = 0x87;         /* 电源控制寄存器 PCON（IDL=bit0） */

STAR_INLINE star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s = (star_crit_state_t)STAR_EA;

    STAR_EA = 0;
    return s;
}

STAR_INLINE void star_crit_exit(star_crit_state_t s)
{
    STAR_EA = s; /* s ∈ {0,1}：恢复 EA */
}

STAR_INLINE uint32_t star_crit_active(void)
{
    return STAR_EA ? 0u : 1u; /* EA==0 → 中断关闭（处于临界区） */
}

#endif
