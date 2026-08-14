/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * StardustOS 移植模板（非 CMSIS 内核/裸机工具链用）
 *
 * 移植只需四步（无需编写汇编源文件）：
 *
 * 1) 让某个硬件定时器以 STAR_TICK_MS 为周期产生中断，
 *    在中断服务函数里调用 star_tick()；
 *    不要做任何其他事，中断越短越好。
 *
 * 2) 提供 star_idle(next_due)：进入芯片低功耗模式。
 *    ARM/RISC-V 内核一条 wfi 即可；其他架构换成对应指令。
 *    next_due 为内核已知的下一到期节拍（STAR_TICK_NONE = 无到期项），
 *    需要 tickless（按 deadline 重装 tick 定时器）时参考
 *    port/star_port.c 的 STAR_TICKLESS 参考实现与 docs/porting.md 4.3
 *    的入账协议（要点：以"重装值 - 计数器剩余"入账已流逝时间，
 *    不要回读计数器做入账锚点——写 VAL=0 后存在 0 窗口，会下溢）；
 *    固定拍移植可忽略。
 *    ⚠ 沿用 port/star_port.c 的 SysTick 弱符号与 tickless 参考实现时，
 *    你的 star_port.h 还需定义 STAR_WEAK（弱符号关键字）与平台标签
 *    STAR_PORT_CORTEXM / STAR_PORT_CH32（tickless 下选择 SysTick 访问
 *    方式，缺失即编译期 #error）——照抄 port/cm0plus 或 port/ch32v。
 *
 * 3) 提供临界区 API（保存/恢复式，支持嵌套）——注意：这三个 inline
 *    函数写在你自己的 star_port.h 里（本模板只覆盖 .c 侧）：
 *      star_crit_state_t  中断状态类型
 *      star_crit_enter()  保存当前状态并关中断，返回保存的状态
 *      star_crit_exit(s)  恢复保存的状态（禁止无条件打开中断）
 *      star_crit_active() 查询当前是否处于关中断（1=关）
 *    参考现成实现：Cortex-M 用 PRIMASK（port/cm0plus、port/cm3），
 *    WCH RISC-V 用 INTSYSCR（port/ch32v），AVR 用 SREG。
 *
 * 4) 提供 star_assert_fail()：断言失败处理（默认 STAR_ASSERT 开启）。
 *    可直接调用芯片复位，或在停机前记录 file/line。
 *
 * 把本文件改名为 star_port.c 放进工程，并把本目录加入头文件搜索路径。
 */

#include "star.h"

/* 例：你的 1ms 定时器中断 */
void Timer1_ISR(void)
{
    star_tick();
    /* 清除中断标志 */
    clear_timer1_flag();
}

void star_idle(uint32_t next_due)
{
    (void)next_due;
    /* ARM / RISC-V 通用低功耗指令；其他架构换成对应指令 */
    __asm volatile("wfi");
}

void star_assert_fail(const char *file, int line)
{
    /* 例：记录后复位芯片（NVIC_SystemReset / SYS_Reset 等） */
    (void)file;
    (void)line;
    for (;;) {
    }
}
