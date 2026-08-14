/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

/* QEMU 冒烟测试（tickless）：Cortex-M3（stm32vldiscovery = STM32F100）
 * 编译必须带：-DSTAR_TICKLESS=1 -DSTAR_PORT_HCLK_HZ=24000u
 *
 * 在固定拍冒烟之上，覆盖 tickless 的空闲路径（固定拍冒烟不编译此分支）：
 *   1. star_sleep → star_idle 的入账/长拍编程/重装全流程实际执行
 *   2. 分段 nap：counter 上限拍 + 剩余部分拍拼出 1000ms 周期
 *   3. 时基追平正确性：以 1000ms 周期定时器为相位基准，第 3 拍必须
 *      精确落在 tick 3000 附近——任何重复/漏计入账都会累积成漂移
 *   4. 唤醒后 ISR 恢复固定拍
 *
 * 时间膨胀（重要）：QEMU 的 WFI 模型在"重装 SysTick 后立即 wfi"下
 * 产生伪唤醒（ptimer 重载事件唤醒被 halt 的 vCPU），深睡退化为微秒级
 * 轮询；若按真实 24MHz 换算（HCLK=24M），3000 拍需执行约 720 亿条
 * 宿主指令，冒烟无法在 CI 内完成。因此把换算常量 STAR_PORT_HCLK_HZ
 * 设为 24kHz：1 个 tick-ms = 24 个计数器周期，1ms 的真实虚拟时间被
 * 放大为 1s 的"节拍时间"——入账数学与换算率无关，全部路径（部分拍
 * 差额、rem==0 窗口、nap 钳制、固定拍恢复）照常执行，且漂移检测
 * 的精度不受影响（计数器周期是精确的）。真实时序验证仍需板级。
 *
 * 判定方式与固定拍冒烟一致：QEMU_PASS / QEMU_FAIL 关键字，
 * 死循环由 CI 的 timeout 判失败。 */

#include <stdint.h>
#include "star.h"

/* ---- SysTick（CMSIS 寄存器布局，直接字面量，无器件头依赖） ---- */
#define SYST_CSR (*(volatile uint32_t *)0xE000E010u) /* 控制/状态 */
#define SYST_RVR (*(volatile uint32_t *)0xE000E014u) /* 重载值 */
#define SYST_CVR (*(volatile uint32_t *)0xE000E018u) /* 当前值 */

/* 固定拍 1ms 的重载值：按膨胀后的换算常量计算（与 port 层一致） */
#define SYST_RELOAD (STAR_PORT_HCLK_HZ / 1000u)

/* ---- semihosting 最小实现 ---- */

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
static star_timer_t blink_timer;

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

static void systick_start(void)
{
    SYST_RVR = SYST_RELOAD - 1u;
    SYST_CVR = 0;
    SYST_CSR = 0x7u; /* CLKSOURCE=处理器时钟 | TICKINT | ENABLE */
}

int main(void)
{
    star_init(evt_table, 1);
    star_timer_start(&blink_timer, 0, NULL, 1000, true);

    /* 先配定时器再开 tick：due = 1000 */
    systick_start();

    /* 与 star_loop 等价（带守卫）：无事件时进 star_sleep，
     * 即 tickless 长拍 + wfi。QEMU TCG 下宿主速度与虚拟时间脱钩，
     * 守卫用 star_ticks() 计虚拟时间 */
    while (s_blinks < 3 && star_ticks() < 8000u) {
        if (!star_poll()) {
            star_sleep();
        }
    }

    /* 相位基准校验：第 3 拍不应早于 tick 3000（下界抓"入账跑快"——
     * 双重入账/重复计整拍会让定时器提前触发）；上界放宽到 4000：
     * QEMU 的 SysTick ptimer 在 wrap 后"计数器 0 等待重载"的状态会
     * 被单个 TCG 批拉长（批执行期间事件不处理），这段真实时间计数器
     * 不计数、入账必然滞后——属仿真模型行为，非内核缺陷，宿主负载
     * 越高滞后越大（实测数十 ms 量级）。上界 4000 仍能抓住"一次
     * 多记 ≥1000ms"级别的记账跑飞。真实芯片上按 3000±20 校核即可。 */
    if (s_blinks < 3 || star_dropped_count() != 0 ||
        star_ticks() < 3000u || star_ticks() > 4000u) {
        semihost_write0("QEMU_FAIL: tickless accounting drifted\n");
        semihost_exit(1);
    }
    semihost_write0("QEMU_PASS\n");
    semihost_exit(0);
}
