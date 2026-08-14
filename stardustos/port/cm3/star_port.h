/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* ARM Cortex-M3（STM32F103 等）
 * 临界区用裸内联汇编实现（PRIMASK/CPSID），不依赖 CMSIS 头文件：
 * 无包含顺序要求，任何工程都能直接编译。
 * 同时兼容 GCC / ArmClang(AC6) 与 armcc(AC5) */

#include <stdint.h>

typedef uint32_t star_crit_state_t;

/* 体系标签：star_port.c 的 tickless 实现据此选择 SysTick 访问方式
 * （Cortex-M：24 位向下计数，裸寄存器地址访问） */
#define STAR_PORT_CORTEXM 1

/* 弱符号关键字：star_port.c 的 SysTick_Handler 用弱符号定义，
 * 用户已有 SysTick 时直接重定义强符号即可接管，无需剔除 star_port.c */
#if defined(__CC_ARM)
#define STAR_WEAK __weak
#else
#define STAR_WEAK __attribute__((weak))
#endif

static inline star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s;
#if defined(__CC_ARM)
    __asm { MRS s, PRIMASK }
    __asm { CPSID i }
    __asm { DSB }
    __asm { ISB }
#else
    __asm volatile ("mrs %0, primask" : "=r"(s));
    __asm volatile ("cpsid i" ::: "memory");
    __asm volatile ("dsb 0xF" ::: "memory");
    __asm volatile ("isb 0xF" ::: "memory");
#endif
    return s;
}

static inline void star_crit_exit(star_crit_state_t s)
{
#if defined(__CC_ARM)
    __asm { MSR PRIMASK, s }
#else
    __asm volatile ("msr primask, %0" : : "r"(s) : "memory");
#endif
}

static inline uint32_t star_crit_active(void)
{
    uint32_t s;
#if defined(__CC_ARM)
    __asm { MRS s, PRIMASK }
#else
    __asm volatile ("mrs %0, primask" : "=r"(s));
#endif
    return s;
}

#endif
