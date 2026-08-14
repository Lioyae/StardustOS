/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#include "star.h"

#ifdef STAR_PORT_HOST

#include <stdlib.h>

uint32_t star_host_primask;
uint32_t star_host_idle_count;
uint32_t star_host_idle_last_due;

/* 宿主机不睡眠：记录调用供测试验证 star_sleep 的 deadline 判定 */
void star_idle(uint32_t next_due)
{
    star_host_idle_count++;
    star_host_idle_last_due = next_due;
}

/* 宿主机断言失败：直接 abort，让测试立刻变红而不是挂死 */
void star_assert_fail(const char *file, int line)
{
    (void)file;
    (void)line;
    abort();
}

#else

#if STAR_TICKLESS

/* ---- tickless 空闲：按下一 deadline 重装 SysTick 再 wfi ----
 *
 * 入账协议（详见 docs/porting.md「tickless 空闲」）：
 *  - 计数器每次重装后都从 s_nap_cycles 倒数，因此"自上次重装以来
 *    已流逝周期"恒等于 s_nap_cycles - remaining（remaining 可能因
 *    硬件 0 窗口读到 0，此时判 elapsed=0，绝不会下溢）。
 *  - 拍界（wrap）靠 COUNTFLAG/CNTIF 读清零判别：谁先读到标志谁
 *    把整拍入账一次（SysTick_Handler 与 star_idle 互斥消费，杜绝
 *    重复入账）；入账点在 ISR 或 idle ① 处，两者均调用同一函数。
 *  - 无入账锚点变量：锚点恒等于 s_nap_cycles（重装值），
 *    "锚点 - remaining" 结构上不可能下溢（旧实现回读计数器快照
 *    做锚点，写 VAL=0 后存在 0 窗口竞态，会整段炸掉时基）。
 *  - s_acc_cycles 保留未达 1ms 的周期余数，跨唤醒零漂移。
 *  - s_nap_cycles==0 表示启动后尚未 tickless 编程过（固定拍运行）。
 *  - 已知局限：两次入账之间若计数器 wrap 超过一次（主循环被长 handler
 *    阻塞超过一拍），多余拍数无法从单比特标志恢复，会漏计入账——
 *    铁律 1（handler 毫秒级返回）排除该场景；nap 上限亦远大于一拍。 */

static volatile uint32_t s_nap_ms = STAR_TICK_MS; /* 当前编程拍长（ms） */
static volatile uint32_t s_nap_cycles;            /* 当前编程拍长（周期） */
static volatile uint32_t s_acc_cycles;            /* 已流逝未入账周期 */

#if defined(STAR_PORT_CORTEXM)
/* Cortex-M SysTick：24 位向下计数。裸寄存器地址访问，
 * 不依赖 CMSIS 头文件（port 层零依赖契约） */
#define STAR_SYST_CTRL (*(volatile uint32_t *)0xE000E010u)
#define STAR_SYST_LOAD (*(volatile uint32_t *)0xE000E014u)
#define STAR_SYST_VAL  (*(volatile uint32_t *)0xE000E018u)
#define STAR_SYST_MAX_CYCLES 0x00FFFFFFu
#elif defined(STAR_PORT_CH32)
/* WCH 青稞 SysTick：64 位比较寄存器、向上计数（ch32v00x.h 提供寄存器）。
 * 单次 nap 上限取 31 位（回绕比较数学留裕量） */
#define STAR_SYST_MAX_CYCLES 0x7FFFFFFFu
#else
#error "STAR_TICKLESS requires a platform port tag (STAR_PORT_CORTEXM or STAR_PORT_CH32)"
#endif

/* 单拍周期数（编译期常量；STAR_PORT_HCLK_HZ≥1000 已由配置校验）。
 * HCLK 非 1000 整数倍时取整导致 nap 最多早醒 <1ms——只早不迟，
 * 安全性优先于精度（早醒后 star_idle 会按锚点差额重新入账） */
#define STAR_CYCLES_PER_MS (STAR_PORT_HCLK_HZ / 1000u)

/* 单次 nap 上限（ms）：受 SysTick 计数器位宽与 2^31 回绕数学共同约束。
 * 全为编译期常量——idle 路径不再需要运行时 64 位除法/乘法 */
#define STAR_NAP_CAP_MS ((STAR_SYST_MAX_CYCLES) / (STAR_CYCLES_PER_MS))
#if STAR_NAP_CAP_MS > 0x40000000u
#define STAR_MAX_NAP_MS_CONST 0x40000000u
#elif STAR_NAP_CAP_MS < 1u
#define STAR_MAX_NAP_MS_CONST 1u
#else
#define STAR_MAX_NAP_MS_CONST STAR_NAP_CAP_MS
#endif

/* 当前拍剩余周期数 */
static uint32_t star_systick_remaining(void)
{
#if defined(STAR_PORT_CORTEXM)
    return STAR_SYST_VAL & STAR_SYST_MAX_CYCLES;
#else
    return (uint32_t)(SysTick->CMP - SysTick->CNT);
#endif
}
/* 本拍是否已匹配（读清零：Cortex-M COUNTFLAG / 青稞 CNTIF）。
 * 返回 true 时标志已消费，后续调用者不会再看到 */
static bool star_systick_matched(void)
{
#if defined(STAR_PORT_CORTEXM)
    return (STAR_SYST_CTRL & 0x00010000u) != 0u;
#else
    return (SysTick->SR & 1u) != 0u;
#endif
}

/* 把累计周期入账为毫秒（保留周期余数，跨唤醒零漂移）。
 * 返回本次可推进时基的毫秒数。
 * 除数为编译期常量：编译器折叠为乘加移位，关中断路径无 64 位
 * 软件除法（旧实现 (acc*1000)/HCLK 的 64 位除法在此处执行，
 * 是 idle 契约"wfi 级别"的最大违背点） */
static uint32_t star_acc_flush_ms(void)
{
    uint32_t ms = s_acc_cycles / STAR_CYCLES_PER_MS;

    if (ms != 0u) {
        s_acc_cycles -= ms * STAR_CYCLES_PER_MS;
    }
    return ms;
}

/* 时基入账（ISR 与 star_idle ① 共用，调用方处于临界区内）：
 * 累加"自上次重装以来"的流逝周期并推进时基。
 * wrap 判别靠读清零标志：ISR 与 idle ① 谁先读标志谁入账整拍，
 * 另一方后续读到标志已清，只入账部分拍——互斥，不会重复。
 * 计数读序：先读标志（消费）再读计数器；两者之间的极小窗口内
 * 若恰好 wrap，该拍由下一次入账点按部分拍入账，最坏漏记一拍。
 * rem==0 特判：写 VAL=0 后与 wrap 后计数器都有 0 窗口（真实硬件
 * 一拍沿/分频时钟整拍，QEMU 的 ptimer 重载事件与 TCG 批执行竞态
 * 同样产生），此时 elapsed 只能取整拍或 0，取差会假性记满拍或
 * 负溢出。 */
static void star_systick_account(void)
{
    bool wrapped = star_systick_matched();
    uint32_t rem = star_systick_remaining();
    uint32_t elapsed;

    if (rem == 0u) {
        elapsed = wrapped ? s_nap_cycles : 0u;
    } else {
        elapsed = s_nap_cycles - rem; /* rem ≤ cycles 恒成立，无下溢 */
        if (wrapped) {
            elapsed += s_nap_cycles;
        }
    }
    s_acc_cycles += elapsed;
    star_tick_advance(star_acc_flush_ms());
}

/* 重装 SysTick 为指定毫秒拍长（从当前时刻起算，计数器复位）。
 * 32 位无溢出依据：ms 被钳制在 STAR_MAX_NAP_MS_CONST 内 →
 * ms*STAR_CYCLES_PER_MS ≤ STAR_SYST_MAX_CYCLES。
 * HCLK 非 1000 整数倍时 ms*cycles/ms 向下取整，nap 至多少于
 * 半毫秒——只早不迟 */
static void star_systick_set(uint32_t ms)
{
    uint32_t cycles;

    if (ms > STAR_MAX_NAP_MS_CONST) {
        ms = STAR_MAX_NAP_MS_CONST;
    }
    cycles = ms * STAR_CYCLES_PER_MS;
    if (cycles > STAR_SYST_MAX_CYCLES) {
        cycles = STAR_SYST_MAX_CYCLES;
    }
    if (cycles < 1u) {
        cycles = 1u;
    }
    s_nap_cycles = cycles;
#if defined(STAR_PORT_CORTEXM)
    STAR_SYST_LOAD = cycles - 1u;
    STAR_SYST_VAL = 0; /* 先 LOAD 后 VAL：VAL 清零触发立即从新 LOAD 重载 */
#else
    SysTick->CNT = 0;
    SysTick->CMP = (uint64_t)cycles - 1u;
#endif
}

/* 弱符号：用户已有自己的 SysTick（延时函数等）时，只需在工程里
 * 重定义一个强符号 SysTick_Handler（记得在里面调用 star_tick()
 * 或 star_tick_advance()），链接器会自动选强符号，
 * 无需把 star_port.c 从工程剔除 */
STAR_WEAK void SysTick_Handler(void)
{
    if (s_nap_cycles == 0u) {
        star_tick(); /* 启动后尚未 tickless 编程：固定拍 */
        return;
    }
    /* 若拍界标志已被 star_idle 抢先消费（中断被关期间匹配），
     * account 只入账部分拍；否则整拍在此入账。二者互斥不重复 */
    star_systick_account();
    if (s_nap_ms != STAR_TICK_MS) {
        /* 长拍到期：恢复固定拍 */
        star_systick_set(STAR_TICK_MS);
        s_nap_ms = STAR_TICK_MS;
    }
}

void star_idle(uint32_t next_due)
{
    uint32_t nap;
    bool do_sleep = true;

    /* ① 时基追平：把"上次重装以来"的流逝周期入账——
     *    含中断被关期间匹配已发生但 ISR 尚未处理的整拍 */
    if (s_nap_cycles != 0u) {
        star_systick_account();
    }
    /* ② 按下一 deadline 计算 nap */
    nap = STAR_MAX_NAP_MS_CONST; /* 无到期项：长睡等任意中断 */
    if (next_due != STAR_TICK_NONE) {
        uint32_t now = star_ticks();

        if ((int32_t)(next_due - now) <= 0) {
            do_sleep = false; /* 追平后已到期：不睡，主循环立即处理 */
        } else {
            uint32_t d = next_due - now;

            if (d < nap) {
                nap = d;
            }
        }
    }
    if (!do_sleep) {
        return;
    }
    /* ③ 重装 SysTick 后 wfi（关中断状态下调用；pending 中断会唤醒。
     *    注意：若仿真/硬件模型对"重装后立即 wfi"存在伪唤醒
     *    （如 QEMU 的 ptimer 重载事件唤醒被 halt 的 vCPU），主循环
     *    会退化为轮询，但入账协议仍按计数器差额正确推进时基，
     *    只是失去低功耗收益） */
    star_systick_set(nap);
    s_nap_ms = nap;
#if defined(__CC_ARM)
    __asm { wfi }
#else
    __asm volatile ("wfi" ::: "memory");
#endif
}

#else /* !STAR_TICKLESS：固定拍 */

/* 弱符号：用户已有自己的 SysTick（延时函数等）时，只需在工程里
 * 重定义一个强符号 SysTick_Handler（记得在里面调用 star_tick()），
 * 链接器会自动选强符号，无需把 star_port.c 从工程剔除 */
STAR_WEAK void SysTick_Handler(void)
{
    star_tick();
}

void star_idle(uint32_t next_due)
{
    (void)next_due;
#if defined(__CC_ARM)
    __asm { wfi }
#else
    __asm volatile ("wfi" ::: "memory");
#endif
}

#endif /* STAR_TICKLESS */

/* 断言失败默认处理：停机。弱符号，用户可重定义为自己的错误处理
 * （记录断言位置后复位、进入 bootloader 等）。 */
STAR_WEAK void star_assert_fail(const char *file, int line)
{
    (void)file;
    (void)line;
    for (;;) {
    }
}

#endif /* !STAR_PORT_HOST */
