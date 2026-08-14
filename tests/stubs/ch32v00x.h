/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

/* CI 交叉编译专用桩头文件：仅为语法/体积检查提供最小声明，
 * 不参与任何真实编译，不能用于芯片工程 */

#ifndef CH32V00X_STUB_H
#define CH32V00X_STUB_H

#include <stdint.h>

typedef struct {
    volatile uint32_t SR;
    volatile uint32_t CNT;
    volatile uint32_t CMP;
    volatile uint32_t CTLR;
} SysTick_TypeDef;

#define SysTick ((SysTick_TypeDef *)0xE000F000u)

static inline void __disable_irq(void) { }
static inline void __enable_irq(void) { }
static inline void __WFI(void) { }

#endif
