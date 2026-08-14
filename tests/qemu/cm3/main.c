/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

/* QEMU 冒烟测试：Cortex-M3（stm32vldiscovery = STM32F100）
 *
 * 验证范围（比交叉编译多一层）：
 *   1. 向量表/启动流程正确，Reset 后进入 main
 *   2. SysTick 向量正确接到 star_port.c 的弱符号 SysTick_Handler → star_tick
 *   3. tick 驱动定时器到期 → 事件投递 → handler 派发（完整事件流）
 *
 * 判定方式：stdout 打印 QEMU_PASS / QEMU_FAIL（CI 抓取关键字判定，
 * 不依赖 QEMU 平台相关的 semihosting 退出码透传），随后 SYS_EXIT 退出；
 * 若向量表/SysTick 断掉则程序死循环 → CI 超时判失败。
 *
 * 非板级验证：外设时序、临界区实测时长不在此列。 */

#include <stdint.h>
#include "star.h"

/* ---- SysTick（CMSIS 寄存器布局，直接字面量，无器件头依赖） ---- */
#define SYST_CSR (*(volatile uint32_t *)0xE000E010u) /* 控制/状态 */
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u) /* 重载值 */
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u) /* 当前值 */

/* STM32F100：SYSCLK 24MHz（QEMU 模型值），24000 分频 = 1ms。
 * 测试判定不依赖精确速率（守卫用虚拟 ticks 计），速率偏离只影响耗时 */
#define SYSCLK_HZ   24000000u
#define SYST_RELOAD (SYSCLK_HZ / 1000u)

/* ---- semihosting 最小实现 ----
 * 注意：串/退出码固定在 r1（register 变量钉住寄存器），
 * 避免 GCC 把入参分到 r0 后被 syscall 号覆盖 */

static void semihost_write0(const char *s)
{
    register const char *p __asm__("r1") = s;
    __asm volatile("movs r0, #0x04\nbkpt 0xAB" : : "r"(p) : "r0");
}

static void semihost_exit(int code)
{
    register int c __asm__("r1") = code;
    __asm volatile("movs r0, #0x18\nbkpt 0xAB" : : "r"(c) : "r0");
}

static volatile uint32_t s_blinks;

static void blink_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt;
    (void)param;
    (void)ctx;
    s_blinks++;
}

static const star_evt_entry_t evt_table[] = {
    [0] = STAR_ENTRY(blink_handler, NULL),
};

static star_timer_t blink_timer;

static void systick_start(void)
{
    SYST_RVR = SYST_RELOAD - 1u;
    SYST_CVR = 0;
    SYST_CSR = 0x7u; /* CLKSOURCE=处理器时钟 | TICKINT | ENABLE */
}

int main(void)
{
    star_init(evt_table, 1);
    star_timer_start(&blink_timer, 0, NULL, 500, true);

    /* 先配定时器再开 tick：star_timer_start 读到的 s_tick 为 0，
     * due = 500（第 500/1000 拍各触发一次） */
    systick_start();

    /* 守卫用 star_ticks() 计虚拟时间而非轮询次数——QEMU TCG 下宿主速度
     * 与虚拟时间脱钩。SysTick 若未接到（向量表问题）则 ticks 停在 0，
     * 死循环被 CI 的 timeout 杀掉判失败 */
    while (s_blinks < 2 && star_ticks() < 5000u) {
        star_poll();
    }

    if (s_blinks < 2) {
        semihost_write0("QEMU_FAIL: timer/event flow broken\n");
        semihost_exit(1);
    }
    semihost_write0("QEMU_PASS\n");
    semihost_exit(0);
}
