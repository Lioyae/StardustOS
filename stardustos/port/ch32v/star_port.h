/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* WCH RISC-V（CH32V003/V007/V203/V307 等，WCH SPL）
 *
 * 器件头选择：默认 V2 代（CH32V003/V007）的 ch32v00x.h；
 * V3 代（CH32V203/V307）在工程宏定义里覆盖：
 *   -DSTAR_CH32_HAL_HEADER=<ch32v20x.h>   （V3 中型）
 *   -DSTAR_CH32_HAL_HEADER=<ch32v30x.h>   （V3 大型）
 * 各代 SDK 的器件头都在定义 IRQn_Type 之后才包含 core_riscv.h，
 * 因此换头后 __enable_irq/__disable_irq/SysTick 布局均可用。
 *
 * 必须包含器件头文件：IRQn_Type 在其中定义，
 * 且它在定义 IRQn_Type 之后才包含 core_riscv.h。
 * 直接包含 core_riscv.h 会因 IRQn_Type 未定义而编译失败。
 *
 * 临界区保存/恢复 INTSYSCR（CSR 0x800，青稞的中断系统控制寄存器）：
 *   __enable_irq  = 写 0x6088（bit7=1 中断开启）
 *   __disable_irq = 写 0x6000（bit7=0 中断关闭）
 * 完整保存恢复该寄存器，支持嵌套、不破坏调用方状态 */

#ifndef STAR_CH32_HAL_HEADER
#define STAR_CH32_HAL_HEADER <ch32v00x.h>
#endif
#include STAR_CH32_HAL_HEADER

/* 体系标签：star_port.c 的 tickless 实现据此选择青稞 SysTick
 * 访问方式（64 位比较寄存器、向上计数） */
#define STAR_PORT_CH32 1

/* 青稞中断系统控制寄存器（INTSYSCR）的 CSR 编号。
 * ⚠ 同一 port 头文件服务 V2 代（CH32V003/V007）与 V3 代（CH32V203/V307），
 * 不同代次手册的 CSR 映射存在差异，默认值 0x800 以本 SDK 的 core_riscv.h
 * 口径为准；若目标代次不同，用 -DSTAR_CH32_INTSYSCR=<值> 覆盖。
 * 此寄存器直接决定临界区与 WFI 行为，上板前必须按目标芯片手册实测核验。 */
#ifndef STAR_CH32_INTSYSCR
#define STAR_CH32_INTSYSCR 0x800
#endif

typedef uint32_t star_crit_state_t;

/* 弱符号关键字：star_port.c 的 SysTick_Handler 用弱符号定义，
 * 用户已有 SysTick 时直接重定义强符号即可接管，无需剔除 star_port.c */
#define STAR_WEAK __attribute__((weak))

static inline star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s;
    __asm volatile("csrr %0, %[csr]" : "=r"(s) : [csr] "i" (STAR_CH32_INTSYSCR));
    __disable_irq();
    return s;
}

static inline void star_crit_exit(star_crit_state_t s)
{
    /* ⚠ 操作数编号陷阱：位置号 %0 永远指向第一个列出的操作数。
     * 这里 s 必须写在 %[csr]（命名立即数）之前，否则 %0 会指到 CSR
     * 立即数、s 被静默丢弃——旧写法（csrw %[csr], %0 + "i" 在前）曾被
     * MounRiver 的 WCH 汇编器抓出 "Improper CSRxI immediate (2048)"，
     * 而上游 xpack 汇编器放行（静默生成错误代码，且 s 永不写回） */
    __asm volatile("csrw %[csr], %0" : : "r"(s),
                   [csr] "i" (STAR_CH32_INTSYSCR) : "memory");
}

static inline uint32_t star_crit_active(void)
{
    uint32_t s;
    __asm volatile("csrr %0, %[csr]" : "=r"(s) : [csr] "i" (STAR_CH32_INTSYSCR));
    return (s & 0x80u) ? 0u : 1u; /* bit7=0 表示中断被关闭 */
}

/* 本 SDK 的 core_riscv.h 未提供 SysTick_Config，这里补齐（CMSIS 兼容签名）：
 * 节拍时钟 = HCLK，比较匹配后自动清零计数并产生 SysTick 中断 */
__attribute__((always_inline)) static inline uint32_t SysTick_Config(uint32_t ticks)
{
    if (ticks == 0) {
        return 1;
    }
    SysTick->SR = 0;
    SysTick->CNT = 0;
    SysTick->CMP = ticks - 1;
    SysTick->CTLR = 0xF;  /* STE | STIE | STCLK(HCLK) | STRE */
    return 0;
}

#endif
