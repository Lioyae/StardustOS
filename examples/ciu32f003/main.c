/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
/*
 * StardustOS 例程：CIU32F003（华大电子，Cortex-M0+）
 *
 * 功能：LED 周期闪烁（定时器）、UART 回环（邮箱，中断接收→事件处理→回发）、
 *       心跳任务（任务层，1s 翻转另一 LED）
 *
 * 依赖：华大电子 CIU32F003_STDLib（ciu32f003_std.h 伞形头文件）+
 *       CMSIS 器件头 ciu32f003.h / core_cm0plus.h
 *
 * 工程配置：
 *   - 源文件：本文件 + stardustos/star.c + stardustos/star_task.c + stardustos/star_mail.c
 *             + stardustos/port/star_port.c
 *   - 头文件路径：stardustos/ 和 stardustos/port/cm0plus/
 *   - 无需定义任何宏；SysTick 由 star_port.c 接管（startup 里是弱符号）
 */

/* ---- tickless 低功耗：空闲时按下一 deadline 重装 SysTick 再 wfi ----
 * ⚠ 这两个宏必须工程级全局生效（star_port.c 也要编译到）：Keil 请在
 * 工程宏定义处设置，或直接在 stardustos/star_config.h 里定义；只在本文件定义
 * 会静默退化为固定拍。关掉即回到固定 1ms 拍（更简单、功耗更高）。
 * 使用 tickless 前先按 docs/porting.md 的 tickless 板级验证清单实测 */
#define STAR_TICKLESS 1
#define STAR_PORT_HCLK_HZ 24000000u  /* CIU32F003 RCH 默认 24MHz，按实配改 */

/* StardustOS 内核不依赖 CMSIS，无包含顺序要求；
 * 本例程外设代码使用华大电子 CIU32F003_STDLib */
#include "ciu32f003_std.h"
#include "star.h"

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
    std_gpio_toggle_pin(GPIOB, GPIO_PIN_1);  /* PB1：野火核心板 LED */
}

static void uart_handler(uint16_t evt, void *param, void *ctx)
{
    (void)evt; (void)ctx;
    star_mail_t *mb = (star_mail_t *)param;
    uint8_t c;

    /* 等 TXE（数据寄存器空）只等 0~1 个字节时间，handler 不会长阻塞。
     * ⚠ 更严谨的姿势是"环形缓冲 + TXE 发送中断"状态机
     * （见 docs/usage.md 附录 B），handler 完全不碰忙等 */
    while (star_mail_recv(mb, &c) > 0) {
        while (!(UART1->ISR & UART_FLAG_TXE)) { }
        std_uart_tx_write_data(UART1, c);
    }
}

static void heartbeat(uint16_t evt, void *param, void *ctx)
{
    (void)evt; (void)param; (void)ctx;
    std_gpio_toggle_pin(GPIOB, GPIO_PIN_0);  /* PB0：第二颗 LED */
}

static const star_evt_entry_t evt_table[] = {
    [EVT_LED]  = STAR_ENTRY(led_handler, NULL),
    [EVT_UART] = STAR_ENTRY(uart_handler, NULL),
};

/* ---- 时钟：RCH 48MHz ---- */
static void clock_init(void)
{
    std_flash_set_latency(FLASH_LATENCY_1CLK);

    std_rcc_rch_enable();
    while (std_rcc_get_rch_ready() != RCC_CSR1_RCHRDY) { }

    std_rcc_set_sysclk_source(RCC_SYSCLK_SRC_RCH);
    while (std_rcc_get_sysclk_source() != RCC_SYSCLK_SRC_STATUS_RCH) { }

    std_rcc_set_ahbdiv(RCC_HCLK_DIV1);
    std_rcc_set_apbdiv(RCC_PCLK_DIV1);
    SystemCoreClock = RCH_VALUE;
}

/* ---- 外设初始化 ---- */
static void gpio_init(void)
{
    std_gpio_init_t cfg = { 0 };

    /* 时钟：GPIOA（UART1 引脚）、GPIOB（LED）、UART1 */
    std_rcc_gpio_clk_enable(RCC_PERIPH_CLK_GPIOA);
    std_rcc_gpio_clk_enable(RCC_PERIPH_CLK_GPIOB);
    std_rcc_apb2_clk_enable(RCC_PERIPH_CLK_UART1);

    /* LED：PB0/PB1 推挽输出，上拉 */
    cfg.pin = GPIO_PIN_0;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull = GPIO_PULLUP;
    cfg.output_type = GPIO_OUTPUT_PUSHPULL;
    std_gpio_init(GPIOB, &cfg);

    cfg.pin = GPIO_PIN_1;
    std_gpio_init(GPIOB, &cfg);

    /* ⚠ UART1 引脚：查你的板子原理图/数据手册填写！
     * AF1 与 AF5 是两套可选映射（可用 std_uart_pin_swap_enable 切换），
     * 下面以 PA2/PA3 + AF1 为例，按实际接线改 port/pin/alternate。 */
    cfg.pin = GPIO_PIN_2 | GPIO_PIN_3;
    cfg.mode = GPIO_MODE_ALTERNATE;
    cfg.pull = GPIO_PULLUP;
    cfg.alternate = GPIO_AF1_UART1;
    std_gpio_init(GPIOA, &cfg);
}

static void uart_init(void)
{
    std_uart_init_t ucfg;

    std_uart_struct_init(&ucfg);
    ucfg.direction = UART_DIRECTION_SEND_RECEIVE;
    ucfg.baudrate = 115200;
    ucfg.wordlength = UART_WORDLENGTH_8BITS;
    ucfg.stopbits = UART_STOPBITS_1;
    ucfg.parity = UART_PARITY_NONE;
    std_uart_init(UART1, &ucfg);

    std_uart_cr1_interrupt_enable(UART1, UART_CR1_INTERRUPT_RXNE);
    NVIC_EnableIRQ(UART1_IRQn);
}

int main(void)
{
    clock_init();
    gpio_init();
    uart_init();

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
void UART1_IRQHandler(void)
{
    if (UART1->ISR & UART_FLAG_RXNE) {
        uint8_t c = (uint8_t)std_uart_rx_read_data(UART1);
        star_mail_send(&uart_mb, &c, 1);
    }
}
