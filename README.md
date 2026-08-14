<p align="right">
  简体中文 | <a href="README_EN.md">English</a>
</p>

<p align="center">
  <img src="brand/stardustos-icon.svg" alt="StardustOS Logo" width="160">
</p>

<h1 align="center">StardustOS</h1>

<p align="center">
  <strong>为小容量单片机而生的事件驱动协作式内核</strong><br>
  无独立汇编文件（临界区/休眠为内联汇编） · 零动态内存分配 · 全部资源占用编译期确定
</p>

<p align="center">
  <a href="https://github.com/Lioyae/StardustOS/actions/workflows/build.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/Lioyae/StardustOS/build.yml?style=for-the-badge" alt="Build Status">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/tags">
    <img src="https://img.shields.io/github/v/tag/Lioyae/StardustOS?style=for-the-badge&color=2b6cb0" alt="Version">
  </a>
  <a href="https://github.com/Lioyae/StardustOS">
    <img src="https://img.shields.io/badge/language-C99-2b6cb0?style=for-the-badge" alt="C99">
  </a>
  <a href="https://stardustos.zane-leo.top/">
    <img src="https://img.shields.io/badge/Docs-中文文档站-dd6b20?style=for-the-badge" alt="StardustOS Docs">
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

StardustOS 是面向小容量单片机（2KB RAM / 16KB Flash 级别）的 C99 事件驱动协作式内核。

- 内核无汇编源文件（移植层使用内联汇编；仍需厂商启动文件/向量表）、无动态内存分配、无阻塞延时 API
- 全部 RAM/Flash 用量在编译期确定，链接器可验证，CI 交叉编译并断言内核体积
- 支持 8051（STC8H/8A、STC89C52、STC8051U/8052U，Keil C51）、80251（STC32G，Keil C251）与宿主机（x86）；中断延迟 = tick 中断 + 内核临界区（事件入队 O(1)；邮箱拷贝与 item_size 成正比；post_replace 与队列长度成正比）。临界区时长随配置与主频变化，**需按平台实测**（估算公式与实测方法见 [使用教程附录 A](docs/usage.md)）

## 项目状态（重要）

**开发预览（v0.x），未经板级验证。**

- ✅ 已验证：宿主机单元测试/交错测试（含 ASan/UBSan、多随机种子）、断言开启构建、最坏配置构建（队列 255）、**gcov 覆盖率门槛（行覆盖 ≥85%）**、**cppcheck 静态分析**——由 CI 自动化；**8051（Keil C51）与 80251（Keil C251）内核全量编译 0 警告 0 错误**（C251 有未引用 static 函数的无害 C174 提示）
- ❌ 未验证：内核尚未在任何真实芯片上运行过。中断时序、临界区实测时长、8051/251 空闲模式（PCON IDL）唤醒、周期定时器相位漂移，均无板级实测数据
- ⚠️ 生产项目使用前，请先按 [移植检查清单](docs/porting.md) 完成板级验证。v1.0.x 的"生产就绪"标签已撤销（见 [更新日志](CHANGELOG.md)）

## 支持平台

| 内核 | 编译器 | 芯片示例 |
|---|---|---|
| 8051 | Keil C51 | STC8H / STC8A / STC89C52 / STC8051U / STC8052U |
| 80251 | Keil C251 | STC32G |
| x86（宿主机） | GCC | PC 上运行内核单元测试 |

> 除宿主机外，以上平台均**仅通过交叉编译验证，未上板运行**。

## 资源占用

| 项 | 占用 |
|---|---|
| 内核 RAM（默认配置） | 约 280B（事件队列 16 槽 + 延时槽 4 + 任务槽 4），编译期确定；8051 下大数组默认放 `idata`，可 `-DSTAR_RAM_CLASS=xdata` 搬到 XRAM |
| 内核 Flash | 纯逻辑、编译期确定；8051/251 请以链接后 size 为准（Keil 编译未进 Linux CI） |
| 完整点灯例程 | 见 `examples/stc8h` / `stc89c52` / `stc32g`（需 STC-ISP 生成的器件头文件） |

## 模块

| 模块 | 说明 |
|---|---|
| 事件队列 | `star_event_post` / `star_event_post_replace`（同 ID 只留最新）/ `star_event_post_delayed`（含 `_replace` 与 `star_event_cancel_delayed`）；内置丢弃计数 `star_dropped_count()` |
| 注册表 | C99 指定初始化器，事件 ID 即下标，O(1) 派发，表常驻 Flash |
| 定时器 | 静态定义；32 位回绕安全；链表按到期时刻排序，到期扫描只遍历到期节点（poll 空转 O(1)）；周期定时器按绝对相位触发（错过拍合并追赶，无累积漂移）；满队策略可选：重试 / 丢弃（严格截止）/ 最新（replace 语义）——注意：**周期定时器满队时丢当次、下一拍正常；单次 RETRY 定时器满队时下一拍重试至送达（重试不计丢弃数、不触发钩子）** |
| 任务层 | 周期回调便捷层：描述符在 Flash（handler + ctx + 周期），状态槽池在 RAM，未启动的任务不占 RAM（可选编译）。注意：**不是 RTOS 任务**——不抢占、handler 被主循环直接同步调用、与事件队列无关 |
| 邮箱 | 静态槽深拷贝，**先入队后入箱**、与事件入队同一临界区原子完成（入队失败邮箱不动，全有或全无，无回滚窗口）；变长消息（每槽 1..item_size 字节且 item_size≤255，`recv` 返回实际存入长度，超长拒绝不截断，每槽额外 1 字节长度开销）；非法构造（slots==0/空指针等）运行时拒绝（可选编译） |
| 低功耗 | deadline 感知：`star_next_due()` 暴露最近到期时刻，空闲时队列空且无到期项才进 `star_idle(next_due)`；可选 **tickless**（`STAR_TICKLESS=1`）按下一 deadline 重装 SysTick 再 wfi，唤醒后恢复固定拍。竞态处理经推理正确，但**各芯片（尤其青稞）的 WFI 行为未经板级验证** |
| 临界区 | 保存/恢复式（8051/251 用 EA、ARM 用 PRIMASK），支持嵌套 |
| 可观测性 | `star_dropped_count()` 统一丢事件计数 + `star_set_drop_hook()` 丢事件回调（钩子仅限事件/邮箱 API） |

## 快速开始

```c
#include "star.h"

enum { EVT_BLINK = 0 };

static star_timer_t blink_timer;

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    led_toggle();
}

static const star_evt_entry_t evt_table[] = {
    STAR_ENTRY(blink_handler, NULL),  /* 顺序初始化：EVT_BLINK=0（C51 不支持指定初始化器） */
};

int main(void)
{
    tick_start(1);  /* 1ms tick：8051 用 Timer0、ARM 用 SysTick，ISR 内调用 star_tick() */
    star_init(evt_table, sizeof(evt_table) / sizeof(evt_table[0]));
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);

    star_loop();  /* 永不返回 */
}
```

## 文档

- 🌐 [StardustOS 中文文档（在线）](https://stardustos.zane-leo.top/)：使用教程 / 移植教程 / 提问指南
- [移植教程](docs/porting.md)：Keil / MounRiver 工程集成、SysTick 冲突处理、非 CMSIS 芯片移植、检查清单
- [使用教程](docs/usage.md)：术语表、事件 / 定时器 / 邮箱 / 任务层逐行详解、完整实战项目
- [测试文档](docs/test.md)：测试矩阵、交错测试设计、覆盖率与静态分析、本地运行方法

## 使用规则

1. handler 非阻塞，毫秒级内返回；长流程拆状态机（内核不提供阻塞延时）
2. 事件 param 只传全局/静态指针，或 ≤32bit 值用 `STAR_P()/STAR_U32()`；大块数据走邮箱
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
#define STAR_TICKLESS       1    /* tickless 空闲（需下面这项） */
#define STAR_PORT_HCLK_HZ   48000000u  /* 内核主频 Hz，仅 tickless 使用 */
```

> `STAR_TICKLESS` / `STAR_PORT_HCLK_HZ` 必须**工程级全局定义**（port 层
> `star_port.c` 也要编译到），不能只定义在某个 .c 文件里。tickless 使用前
> 请完成 [移植教程](docs/porting.md) 中的 tickless 板级验证清单。

## 构建与测试

内核为纯逻辑，单元测试在 PC 上运行（`ctest` 默认跑三档：常规配置、断言开启构建 `test_stardustos_assert`、最坏配置构建 `test_stardustos_max`——队列 255/延时槽 16/任务槽 16）：

```bash
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

> 交错测试验证的是内核对**建模并发语义**（临界区内不抢占）的一致性，
> 伪中断注入窗口覆盖 `star_event_post*` / `star_mail_send`（入临界区前）/
> `star_poll`（单步前）/ `star_process_timers`（定时器遍历中）。
> 这不构成硬件验证；真实硬件时序以板级验证为准（见上方项目状态）。

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

本项目采用 [Apache License 2.0](LICENSE)。
