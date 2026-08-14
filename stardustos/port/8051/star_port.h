/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* 8051 内核移植（STC8H/8A、STC89C52、STC8051U/8052U 等，Keil C51）
 *
 * 临界区：EA（IE 寄存器 0xA8 的 bit7）保存/恢复。
 * 用带前缀的 sfr/sbit 裸声明，不依赖任何厂商头文件（零依赖契约），
 * 也不会与 STC8H.H / reg52.h 里已有的 EA 定义冲突。
 *
 * 内存模型：内核静态大数组（事件队列/延时槽/任务槽）默认放 idata
 * （256B 内部 RAM，STC89/STC8 通用）；STC8H 有 8KB XRAM，可
 * -DSTAR_RAM_CLASS=xdata 把内核数据搬到 XRAM、腾出内部 RAM 给堆栈。 */

#include <stdint.h>

/* 体系标签：star_port.c 据此选择 8051/STC 实现 */
#define STAR_PORT_8051 1

/* 内核静态大数组的存储类（见文件头说明） */
#ifndef STAR_RAM_CLASS
#define STAR_RAM_CLASS idata
#endif
#define STAR_STATIC static STAR_RAM_CLASS

/* C51 无弱符号：STAR_WEAK 置空（tick ISR / star_assert_fail 为强符号，
 * 用户如需自定义 tick，定义 STAR_PORT_NO_TICK_ISR 排除默认 ISR） */
#define STAR_WEAK

/* C51 旧版本不支持 inline：降级为 static（每编译单元一份拷贝） */
#define STAR_INLINE static

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
