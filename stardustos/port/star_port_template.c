/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * StardustOS 移植模板（新架构/裸机工具链用）
 *
 * 移植只需四步（无需编写汇编源文件）：
 *
 * 1) 让某个硬件定时器以 STAR_TICK_MS 为周期产生中断，在中断服务函数里
 *    调用 star_tick()；不要做任何其他事，中断越短越好。
 *    （8051/251 是 void xxx(void) interrupt N，见 port/star_port.c）
 *
 * 2) 提供 star_idle(next_due)：进入芯片低功耗模式。
 *    8051/251 用 PCON IDL 空闲位（见 port/star_port.c），
 *    ARM/RISC-V 用一条 wfi；其他架构换成对应指令。
 *    next_due 为内核已知的下一到期节拍（STAR_TICK_NONE = 无到期项）；
 *    固定拍移植可忽略该参数（用 STAR_UNUSED_PARAM 引用）。
 *
 * 3) 提供临界区 API（保存/恢复式，支持嵌套）——这三个 inline 函数写在
 *    你自己的 star_port.h 里（本模板只覆盖 .c 侧）：
 *      star_crit_state_t  中断状态类型
 *      star_crit_enter()  保存当前状态并关中断，返回保存的状态
 *      star_crit_exit(s)  恢复保存的状态（禁止无条件打开中断）
 *      star_crit_active() 查询当前是否处于关中断（1=关）
 *    参考现成实现：8051/251 用 EA（port/8051、port/251，含 sfr/sbit 裸声明
 *    与 STAR_STATIC/STAR_WEAK/STAR_INLINE 三个编译器兼容宏），
 *    宿主机用共享变量模拟（port/host）。
 *
 * 4) 提供 star_assert_fail()：断言失败处理（默认 STAR_ASSERT 开启）。
 *    可直接复位芯片，或停机前记录 file/line。
 *
 * 把本文件改名为 star_port.c 放进工程，并把你的 port 头目录加入 include
 * 搜索路径（内核通过 star.h → star_port.h 找到你的实现）。
 */

#include "star.h"

/* 例：你的 1ms 定时器中断 */
void Timer_ISR(void)
{
    star_tick();
    /* 清除中断标志 */
    clear_timer_flag();
}

void star_idle(uint32_t next_due)
{
    STAR_UNUSED_PARAM(next_due);
    /* 进低功耗：8051/251 是 PCON |= 0x01（空闲，中断唤醒）；
     * ARM/RISC-V 是 wfi；其他架构换成对应指令 */
}

void star_assert_fail(const char *file, int line)
{
    /* 例：记录后复位芯片 */
    STAR_UNUSED_PARAM(file);
    STAR_UNUSED_PARAM(line);
    for (;;) {
    }
}
