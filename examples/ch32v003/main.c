/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * StardustOS 例程：CH32V003（WCH SPL）
 *
 * 功能：LED 周期闪烁（定时器）、UART 回环（邮箱，中断接收→事件处理→回发）、
 *       心跳任务（任务层，1s 翻转另一 LED）
 *
 * 工程配置（MRS / EIDE）：
 *   - 源文件：本文件 + stardustos/star.c + stardustos/star_task.c + stardustos/star_mail.c
 *             + stardustos/port/star_port.c
 *   - 头文件路径：stardustos/ 和 stardustos/port/ch32v/
 *   - 无需定义任何宏；SysTick 由 star_port.c 接管
 */

/* ---- tickless 低功耗：空闲时按下一 deadline 重装 SysTick 再 wfi ----
 * ⚠ 这两个宏必须工程级全局生效（star_port.c 也要编译到）：Keil/MRS 请在
 * 工程宏定义处设置，或直接在 stardustos/star_config.h 里定义；只在本文件定义
 * 会静默退化为固定拍。关掉即回到固定 1ms 拍（更简单、功耗更高）。
 * 使用 tickless 前先按 docs/porting.md 的 tickless 板级验证清单实测 */
#define STAR_TICKLESS 1
#define STAR_PORT_HCLK_HZ 48000000u  /* CH32V003 默认主频 48MHz */

#include "star.h"
#include "ch32v00x.h"

/* ---- 事件 ID（连续枚举，从 0 起） ---- */
enum {
    EVT_LED = 0,   /* 周期闪烁 */
    EVT_UART = 1,  /* 收到串口数据（邮箱事件） */
};

/* ---- 邮箱：32 槽 × 1 字节（逐字节收发：每字节一格、一条事件；
 * 邮箱契约：send 的 len 必须 ≤ item_size，recv 返回实际存入长度） ---- */
STAR_MAILBOX_DEF(uart_mb, EVT_UART, 32, 1);

static star_timer_t blink_timer;

/* ---- 任务层描述符（Flash，不启动不占 RAM） ---- */
static void heartbeat(uint16_t evt, void *param, void *ctx);
static const star_task_desc_t tasks[] = {
    STAR_TASK_DEF(1000, heartbeat, NULL),
};

/* ---- 事件处理器 ---- */
static void led_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt; (void)param; (void)ctx;
    GPIOC->OUTDR ^= GPIO_Pin_0;  /* 翻转 PC0 */
}

static void uart_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt; (void)ctx;
    star_mail_t *mb = (star_mail_t *)param;
    uint8_t c;

    /* 注意：这里等 TXE（数据寄存器空）即可——上一字节从寄存器搬进移位器
     * 后就能写下一字节，只等 0~1 个字节时间。
     * ⚠ 不要等 TC（传输完成）：那要等整个字节从引脚发完，逐字节回环会
     * 每字节阻塞约 87µs，数据一多违反铁律 1（handler 毫秒级返回）。
     * 更严谨的姿势是"环形缓冲 + TXE 发送中断"状态机（见 docs/usage.md
     * 附录 B），handler 完全不碰忙等 */
    while (star_mail_recv(mb, &c) > 0) {
        while (!(USART1->STATR & USART_STATR_TXE)) { }
        USART1->DATAR = c;
    }
}

static void heartbeat(uint16_t evt, void *param, void *ctx)
{
    (void)evt; (void)param; (void)ctx;
    GPIOC->OUTDR ^= GPIO_Pin_1;  /* 翻转 PC1 */
}

static const star_evt_entry_t evt_table[] = {
    [EVT_LED]  = STAR_ENTRY(led_handler, NULL),
    [EVT_UART] = STAR_ENTRY(uart_handler, NULL),
};

/* ---- 外设初始化 ---- */
static void gpio_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD
                      | RCC_APB2Periph_USART1;

    /* PC0/PC1：10MHz 推挽输出 */
    GPIOC->CFGLR &= ~(0xFF << 0);
    GPIOC->CFGLR |= (0x1 << 0) | (0x1 << 4);

    /* PD5 = USART1_TX：50MHz 复用推挽；PD6 = USART1_RX：浮空输入 */
    GPIOD->CFGLR &= ~(0xFF << 20);
    GPIOD->CFGLR |= (0xB << 20) | (0x4 << 24);

    USART1->BRR = SystemCoreClock / 115200;
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE | USART_CTLR1_RE
                    | USART_CTLR1_RXNEIE;
    NVIC_EnableIRQ(USART1_IRQn);
}

int main(void)
{
    SystemInit();
    gpio_init();

    /* 节拍：SysTick 中断由 port 层接管（SysTick_Handler → star_tick / tickless 长拍）。
     * 初始按 STAR_TICK_MS 配固定拍，tickless 空闲时 port 层会动态重装 */
    SysTick_Config(SystemCoreClock / (1000 / STAR_TICK_MS));

    star_init(evt_table, sizeof(evt_table) / sizeof(evt_table[0]));
    star_task_init(tasks, sizeof(tasks) / sizeof(tasks[0]));

    star_timer_start(&blink_timer, EVT_LED, NULL, 500, true);
    star_task_start(0);  /* 心跳任务 */

    star_loop();  /* 永不返回 */
}

/* 串口接收中断：只做深拷贝投递，中断最短 */
void USART1_IRQHandler(void)
{
    if (USART1->STATR & USART_STATR_RXNE) {
        uint8_t c = (uint8_t)(USART1->DATAR & 0xFF);
        star_mail_send(&uart_mb, &c, 1);
    }
}
