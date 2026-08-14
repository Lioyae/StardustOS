/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* 8051 内核移植（STC8H/8A、STC89C52、STC8051U/8052U 等）
 * 同时支持 Keil C51 与 SDCC 两种编译器。
 *
 * 临界区：EA（IE 寄存器 0xA8 的 bit7）保存/恢复。
 * 用带前缀的 SFR 裸声明，不依赖厂商头文件（零依赖契约），
 * 也不与 STC8H.H / reg52.h 里已有的 EA 定义冲突。
 *
 * 内存模型：内核静态大数组（事件队列/延时槽/任务槽）默认放 idata
 * （256B 内部 RAM，STC89/STC8 通用）；STC8H 有 8KB XRAM，定义
 * STAR_RAM_XDATA=1 即可搬到 XRAM（自动映射到各编译器关键字）。 */

#include <stdint.h>

/* 体系标签：star_port.c 据此选择 8051/STC 实现 */
#define STAR_PORT_8051 1

/* ---- 编译器差异（Keil C51 vs SDCC） ---- */
#if defined(__SDCC) || defined(SDCC)
  /* SDCC（开源 C99 编译器，mcs51 目标） */
  #define STAR_WEAK
  #define STAR_INLINE static inline
  #define STAR_INTERRUPT(n) __interrupt(n)
  #ifdef STAR_RAM_XDATA
  #define STAR_RAM_CLASS __xdata
  #else
  #define STAR_RAM_CLASS __idata
  #endif
  __sfr  __at(0xA8) STAR_IE;    /* 中断使能寄存器 IE */
  __sbit __at(0xAF) STAR_EA;    /* 全局中断使能 EA（IE.7，位地址 0xAF） */
  __sfr  __at(0x87) STAR_PCON;  /* 电源控制寄存器 PCON（IDL=bit0） */
#else
  /* Keil C51 */
  #define STAR_WEAK
  #define STAR_INLINE static
  #define STAR_INTERRUPT(n) interrupt n
  #ifdef STAR_RAM_XDATA
  #define STAR_RAM_CLASS xdata
  #else
  #define STAR_RAM_CLASS idata
  #endif
  sfr STAR_IE   = 0xA8;
  sbit STAR_EA  = STAR_IE ^ 7;
  sfr STAR_PCON = 0x87;
#endif

#define STAR_STATIC static STAR_RAM_CLASS

/* 临界区状态：保存 EA（0/1），用 unsigned char 承载 */
typedef unsigned char star_crit_state_t;

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
