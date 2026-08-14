/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

/* 最小 Cortex-M3 启动代码（QEMU 冒烟测试专用，无器件依赖）：
 * 向量表（含 SysTick → star_port.c 的弱符号 SysTick_Handler）、
 * 零初始化 .bss、跳转 main。 */

#include <stdint.h>

extern int main(void);
extern void SysTick_Handler(void);

extern uint32_t _estack;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _etext;
extern uint32_t _sbss;
extern uint32_t _ebss;

static void default_handler(void)
{
    for (;;) {
        /* 意外异常：原地停住 → QEMU 超时 → CI 判失败 */
    }
}

void reset_handler(void)
{
    uint32_t *p;
    const uint32_t *src;

    /* .data 初始化值存于 FLASH（_etext 之后），拷贝到 RAM。
     * 缺失此步时，任何带初值的全局变量会静默取到错误值 */
    for (p = &_sdata, src = &_etext; p < &_edata; p++, src++) {
        *p = *src;
    }
    for (p = &_sbss; p < &_ebss; p++) {
        *p = 0;
    }
    main();
    for (;;) {
    }
}

/* Cortex-M3 前 16 个向量（0: SP, 1: Reset, 15: SysTick） */
__attribute__((section(".isr_vector"), used)) const uint32_t isr_vectors[] = {
    (uint32_t)&_estack,
    (uint32_t)reset_handler,
    (uint32_t)default_handler, /* NMI */
    (uint32_t)default_handler, /* HardFault */
    (uint32_t)default_handler, /* MemManage */
    (uint32_t)default_handler, /* BusFault */
    (uint32_t)default_handler, /* UsageFault */
    0, 0, 0, 0,                /* 保留 */
    (uint32_t)default_handler, /* SVCall */
    (uint32_t)default_handler, /* DebugMonitor */
    0,                         /* 保留 */
    (uint32_t)default_handler, /* PendSV */
    (uint32_t)SysTick_Handler, /* SysTick（弱符号，star_port.c 提供） */
};
