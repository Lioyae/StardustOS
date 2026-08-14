<p align="right">
  简体中文 | <a href="README_EN.md">English</a>
</p>

<p align="center">
  <img src="brand/stardustos-icon.svg" alt="StardustOS Logo" width="160">
</p>

<h1 align="center">StardustOS</h1>

<p align="center">
  <strong>为小容量单片机而生的事件驱动协作式内核</strong><br>
  无独立汇编文件 · 零动态内存分配 · 全部资源占用编译期确定
</p>

<p align="center">
  <a href="https://github.com/Lioyae/StardustOS/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/Lioyae/StardustOS/build.yml?style=for-the-badge" alt="Build Status">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/tags">
    <img src="https://img.shields.io/github/v/tag/Lioyae/StardustOS?style=for-the-badge&color=2b6cb0" alt="Version">
  </a>
  <a href="https://github.com/Lioyae/StardustOS">
    <img src="https://img.shields.io/badge/language-C-2b6cb0?style=for-the-badge" alt="C">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/stargazers">
    <img src="https://img.shields.io/github/stars/Lioyae/StardustOS?style=for-the-badge&color=d69e2e" alt="Stars">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/Lioyae/StardustOS?style=for-the-badge&color=38a169" alt="License">
  </a>
</p>

---

## 简介

StardustOS 是面向小容量单片机的 C 语言事件驱动协作式内核，聚焦 STC 系列 8051 / 80251 单片机（默认配置内核本体仅约 280B RAM）。

- 内核无汇编源文件（移植层直接操作 SFR/寄存器）、无动态内存分配、无阻塞延时 API
- 全部 RAM/Flash 用量在编译期确定，链接器可验证；CI 自动化单元测试、覆盖率与 SDCC 编译验证
- 支持 8051（STC8H/8A、STC89C52、STC8051U/8052U，Keil C51 / SDCC）、80251（STC32G，Keil C251）与宿主机（x86）；中断延迟 = tick 中断 + 内核临界区（事件入队 O(1)；邮箱拷贝与 item_size 成正比；post_replace 与队列长度成正比）。临界区时长随配置与主频变化，**需按平台实测**（见 [使用教程附录 A](docs/usage.md)）

## 项目状态（重要）

**开发预览（v0.x），未经板级验证。**

- ✅ 已验证：宿主机单元测试/交错测试（含 ASan/UBSan、多随机种子）、断言开启构建、最坏配置构建（队列 255）、**gcov 覆盖率门槛（行覆盖 ≥85%）**、**cppcheck 静态分析**——由 CI 自动化；**8051（Keil C51 / SDCC）与 80251（Keil C251）内核全量编译 0 警告 0 错误**（C251 有未引用 static 函数的无害 C174 提示）
- ❌ 未验证：内核尚未在任何真实芯片上运行过。中断时序、临界区实测时长、8051/251 空闲模式（PCON IDL）唤醒、周期定时器相位漂移，均无板级实测数据
- ⚠️ 生产项目使用前，请先按 [移植检查清单](docs/porting.md) 完成板级验证。

## 支持平台

| 内核 | 编译器 | 芯片示例 |
|---|---|---|
| 8051 | Keil C51 / SDCC | STC8H / STC8A / STC89C52 / STC8051U / STC8052U |
| 80251 | Keil C251 | STC32G |
| x86（宿主机） | GCC | PC 上运行内核单元测试 |

> 除宿主机外，8051/251 均通过编译验证（SDCC 进 CI、Keil 本地），**尚未上板运行**。

## 资源占用

| 项 | 占用 |
|---|---|
| 内核 RAM（默认配置） | 约 280B（事件队列 16 槽 + 延时槽 4 + 任务槽 4），编译期确定；8051 下大数组默认放 `idata`，定义 `STAR_RAM_XDATA=1` 搬到 XRAM |
| 内核 Flash | 纯逻辑、编译期确定；8051/251 请以链接后 size 为准（Keil 编译未进 Linux CI） |
| 完整点灯例程 | 见 `examples/stc8h` / `stc89c52` / `stc32g`（STC8H/STC32G 需 STC-ISP 生成的器件头文件） |

## 模块

| 模块 | 说明 |
|---|---|
| 事件队列 | `star_event_post` / `star_event_post_replace`（同 ID 只留最新）/ `star_event_post_delayed`（含 `_replace` 与 `star_event_cancel_delayed`）；内置丢弃计数 `star_dropped_count()` |
| 注册表 | 顺序初始化，事件 ID 即下标，O(1) 派发，表常驻 Flash（C51 不支持指定初始化器，用 `STAR_ENTRY` 顺序填写） |
| 定时器 | 静态定义；32 位回绕安全；链表按到期时刻排序，到期扫描只遍历到期节点（poll 空转 O(1)）；周期定时器按绝对相位触发（错过拍合并追赶，无累积漂移）；满队策略可选：重试 / 丢弃（严格截止）/ 最新（replace 语义） |
| 任务层 | 周期回调便捷层：描述符在 Flash（handler + ctx + 周期），状态槽池在 RAM，未启动的任务不占 RAM（可选编译）。不是 RTOS 任务——不抢占、handler 被主循环直接同步调用 |
| 邮箱 | 静态槽深拷贝，**先入队后入箱**、与事件入队同一临界区原子完成；变长消息（每槽 1..item_size 字节且 item_size≤255，`recv` 返回实际存入长度，超长拒绝不截断）；非法构造运行时拒绝（可选编译） |
| 低功耗 | deadline 感知：`star_next_due()` 暴露最近到期时刻，空闲时队列空且无到期项才进 `star_idle(next_due)`；8051/251 默认空转（安全），定义 `STAR_PORT_IDLE` 可启用 PCON IDL 空闲模式（须先上板实测唤醒行为）。tickless（`STAR_TICKLESS=1`）8051/251 无实现，暂不支持 |
| 临界区 | 保存/恢复式（8051/251 用 EA），支持嵌套 |
| 可观测性 | `star_dropped_count()` 统一丢事件计数 + `star_set_drop_hook()` 丢事件回调（钩子仅限事件/邮箱 API） |

## 快速开始

以 STC89C52（Keil C51）为例，P1.0 LED 每 500ms 翻转：

```c
#include "reg52.h"
#include "star.h"

sbit LED = P1 ^ 0;

enum { EVT_BLINK = 0, EVT_COUNT };

/* 12MHz、12T、Timer0 模式1（16 位手动重装），1ms tick */
#define T0_RELOAD_H 0xFCu
#define T0_RELOAD_L 0x18u

static void timer0_init_1ms(void)
{
    TMOD &= 0xF0u;
    TMOD |= 0x01u;   /* Timer0 模式 1 */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;         /* Timer0 中断使能 */
    TR0 = 1;         /* 启动 Timer0 */
    EA = 1;          /* 开全局中断 */
}

/* 模式 1 需手动重装：自定义 Timer0 ISR，工程需定义 STAR_PORT_NO_TICK_ISR */
void timer0_isr(void) interrupt 1
{
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    star_tick();
}

static star_timer_t blink_timer;

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    LED = !LED;
}

static const star_evt_entry_t evt_table[EVT_COUNT] = {
    STAR_ENTRY(blink_handler, NULL),   /* 顺序初始化：EVT_BLINK = 0 */
};

void main(void)
{
    timer0_init_1ms();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();   /* 永不返回 */
}
```

> C51 下 handler 必须加 `STAR_REENTRANT`（多参数函数指针走 reentrant 栈）；事件表用顺序初始化（C51 不支持 C99 指定初始化器）。完整例程见 `examples/stc8h`、`examples/stc89c52`、`examples/stc32g`。

## 文档

- [移植教程](docs/porting.md)：Keil C51/C251 工程集成、Timer0 tick 接线、EA 临界区、自定义 tick ISR、STC-ISP 头文件、移植检查清单
- [使用教程](docs/usage.md)：术语表、事件 / 定时器 / 邮箱 / 任务层逐行详解、完整实战项目
- [测试文档](docs/test.md)：测试矩阵、交错测试设计、覆盖率与静态分析、SDCC/Keil 编译验证、本地运行方法

## 使用规则

1. handler 非阻塞，毫秒级内返回；长流程拆状态机（内核不提供阻塞延时）
2. 事件 param 只传全局/静态指针，或 ≤32bit 值用 `STAR_P()/STAR_U32()`；大块数据走邮箱（**8051 下通用指针 3 字节，≤16bit 值可无损传，32bit 值会截断**）
3. 定时器/任务 API 仅限主循环上下文；`star_event_post*`、`star_mail_send` 可进中断
4. 事件 ID 从 0 连续枚举（ID 即注册表下标）

## 配置

所有可配置项集中在 `stardustos/star_config.h`：

```c
#define STAR_TICK_MS        1    /* 节拍毫秒 */
#define STAR_EVT_QUEUE_SIZE 16   /* 事件队列槽数 */
#define STAR_DELAYED_MAX    4    /* 延时投递并发数 */
#define STAR_ENABLE_TASK    1    /* 任务层开关 */
#define STAR_TASK_SLOT_MAX  4    /* 同时活跃任务上限 */
#define STAR_ENABLE_MAILBOX 1    /* 邮箱开关 */
#define STAR_TIMER_CATCHUP_MAX 1000  /* 周期定时器追赶上限 */
```

> `STAR_TICKLESS` / `STAR_PORT_HCLK_HZ` 无 8051/251 实现，STC 上勿开启；8051/251 的空闲为默认空转（见上方「模块-低功耗」）。

## 构建与测试

内核为纯逻辑，单元测试在 PC 上运行（`ctest` 默认跑三档：常规配置、断言开启构建 `test_stardustos_assert`、最坏配置构建 `test_stardustos_max`——队列 255/延时槽 16/任务槽 16）：

```bash
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

8051 的编译验证走两条路（详见 [测试文档](docs/test.md)）：

```bash
# SDCC（进 CI，--Werror）
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o star.rel stardustos/star.c

# Keil C51 / C251（本地，Windows 商业软件不进 Linux CI）
C51.EXE star.c INCDIR(stardustos;stardustos\port\8051) OBJECT(star.OBJ) SMALL
C251.EXE star.c INCDIR(stardustos;stardustos\port\251) OBJECT(star.OBJ)
```

> 交错测试验证的是内核对**建模并发语义**（临界区内不抢占）的一致性，伪中断注入窗口覆盖 `star_event_post*` / `star_mail_send`（入临界区前）/ `star_poll`（单步前）/ `star_process_timers`（定时器遍历中）。这不构成硬件验证；真实硬件时序以板级验证为准。

## 目录结构

```
stardustos/
├── star.h / star.c          # 内核
├── star_config.h            # 唯一配置点
├── star_task.c              # 任务层（可选编译）
├── star_mail.c              # 邮箱（可选编译）
└── port/                    # 移植层（按内核分目录：8051 / 251 / host）
examples/                    # STC 例程（stc8h / stc89c52 / stc32g）
tests/                       # PC 单元测试
docs/                        # 移植与使用教程
brand/                       # 品牌资源
```

## 开源协议

Apache License 2.0，见 [LICENSE](LICENSE)。
