# StardustOS 移植教程（手把手 · STC 篇）

> 本文假设读者：会用 IDE 打开工程、会点编译按钮，仅此而已。不认识的词，第 0 章都翻译成大白话了；每一步都写了"点哪里、看到什么、为什么"。照着做就行，不需要提前懂原理——原理会在做的过程中自然看懂。
>
> 本文面向 STC 单片机：8051 内核（STC8H/8A、STC89C52、STC8051U/8052U，Keil C51 / SDCC）与 80251 内核（STC32G，Keil C251）。

---

## 第 0 章：先花几分钟，认识 StardustOS 需要的三样东西

StardustOS 是个"事件驱动的协作式内核"。把它想象成工厂的调度室：它不管你的产品具体怎么做，只负责维持工厂的节奏——**按时打铃（tick）、没活就打盹（PCON 空闲）、干活时挂"请勿打扰"牌子（EA 临界区）**。它要在你的芯片上开工，只向你要这三样东西。

### 0.1 先把这些词翻译成大白话

后面会反复出现的词，先统一在这里认识一遍。每个词都给了生活化的比喻，忘了就翻回来：

| 词 | 大白话解释 |
|---|---|
| **单片机** | 一块比指甲盖还小的电脑。没有屏幕键盘，但便宜、省电，藏在台灯、电饭煲、遥控器里 |
| **寄存器** | 芯片内部的一排排小开关。往里面写一个数字，等于把某排开关拨成某个状态，外设就听话了 |
| **时钟 / 主频（Hz）** | 芯片的心跳，一秒跳几次。24MHz 就是一秒跳 2400 万下，每跳一下执行一小步指令 |
| **1T / 12T** | 定时器走多快：1T 每 1 个时钟跳一下（快），12T 每 12 个时钟跳一下（慢、省电）。STC8/STC32 可切换，STC89 固定 12T |
| **中断** | 芯片正埋头干活，突然门铃响了：放下手里的活去开门，处理完再回来接着干 |
| **ISR（中断处理函数）** | 开门时干的那点活。铁规矩：越快越好，干完马上回去，别让客人久等 |
| **Timer0** | 芯片自带的"自动报时器"，绝大多数 8051 都有。到点就来按一次门铃，循环往复 |
| **重装值** | 报时器的"刻度"：计数器从重装值数到 65535 溢出响铃。算准它，铃声才能准点 |
| **tick（节拍）** | 报时器每 `STAR_TICK_MS`（默认 1 毫秒）响一次，这一响就是一个节拍，也就是内核的心跳 |
| **空闲模式（PCON IDL）** | 让芯片"闭眼打盹"的开关：CPU 停转，但定时器和中断照常走，几乎不耗电；门铃一响自动睁眼 |
| **临界区** | 干活时挂"请勿打扰"牌子的时间段。牌子挂着，门铃响了也先不接；摘了牌子才接 |
| **EA** | 门铃的"总开关"（IE 寄存器 0xA8 的 bit7）。EA=0 全部门铃不响；EA=1 全响 |
| **Flash** | 芯片的长期记忆：断电不忘，用来放程序 |
| **RAM** | 芯片的短期记忆：断电即忘。8051 内部只有 256B（idata），STC8H 另有 8KB XRAM |
| **编译 / 链接** | 把你写的代码翻译成机器语言（编译），再把各部分拼装成一份完整说明书（链接） |
| **hex 文件** | 烧录用的成品格式：Keil 编译后生成的 `.hex` 文件，STC-ISP 直接认它 |
| **烧录 / 下载** | 把编译好的程序抄进芯片的 Flash，断电也不丢 |
| **STC-ISP** | STC 官方的烧录软件，顺便能生成器件头文件（STC8H.H / STC32G.H）、把 STC 型号加进 Keil |
| **头文件（.h） / 源文件（.c）** | 说明书的目录（有哪些零件可用）/ 正文（零件怎么造、活怎么干） |
| **弱符号 / 强符号** | 两个同名函数共存时：弱的是"替身"，强的是"本尊"，本尊一出场替身自动让位。**注意：Keil C51/C251 不支持弱符号**，STC 移植用另一种办法（见第 3 章） |
| **时基** | 内核心里的"现在几点"。内核数着节拍过日子，时基就是它手腕上的表 |
| **事件 / 队列 / handler / 邮箱** | 纸条 / 传达室的筐 / 工人 / 快递柜。内核内部的运转方式，细节见《使用教程》 |

### 0.2 StardustOS 到底要哪三样东西

| 要的东西 | 大白话是什么 | 谁提供 |
|---|---|---|
| **tick**（节拍） | 每 1ms 来一次的"心跳"，由 Timer0 溢出中断产生，中断里调用 `star_tick()` | 你（在 main 里初始化 Timer0；ISR 由内核 port 层写好） |
| **空闲**（低功耗） | 没事干时让 CPU 打盹的开关，任意中断会自动醒 | 内核 port 层（`PCON` 的 IDL 位，已写好） |
| **临界区** | "关中断/开中断"两个操作，保护共享数据 | 内核 port 层（`EA` 保存/恢复，已写好） |

所以移植 = **把 Timer0 这口钟调到 1ms 响一次 + 确认芯片会打盹 + 内核自带开关中断**。没有汇编，没有链接脚本，没有魔法。

> 为什么要这三样？内核靠 tick 知道时间（闹钟才能响）、靠空闲在没事时省电（电池才能耐用）、靠临界区保护自己的账本（别人拿纸条时不会抄错）。

### 0.3 开始前准备一张对照表（后面会反复用）

不同芯片的"接线方案"收在不同的 port 目录里（port = 港口/接口，即"这颗芯片怎么接线的说明书"）。选对行，照着走就成：

| 你的芯片 | 用哪个编译器 | 用哪个 port 目录 | 器件头文件 |
|---|---|---|---|
| STC8H / STC8A | Keil C51（或 SDCC） | `stardustos/port/8051/` | STC8H.H（STC-ISP 生成） |
| STC89C52 | Keil C51 / SDCC | `stardustos/port/8051/` | Keil 自带 `reg52.h` / SDCC 自带 `<8051.h>` |
| STC8051U / 8052U | Keil C51 / SDCC | `stardustos/port/8051/` | STC-ISP 生成的头文件 |
| STC32G | Keil C251 | `stardustos/port/251/` | STC32G.H（STC-ISP 生成） |
| PC（单元测试） | GCC | `stardustos/port/host/` | 无需 |

> 8051 系列用 `port/8051`（Keil C51 与 SDCC 都认），80251 的 STC32G 用 `port/251`（Keil C251）。别拿错目录，拿错第一行就报错。

---

## 第 1 章：路线 A —— Keil C51 + STC8H（STC8A 同理）

这一章要干成的事：**在 Keil C51 里从零搭出一个空工程，把 StardustOS 接进去，让灯按 500ms 闪起来**。一路只做四件大事：建工程 → 抄 StardustOS 文件 → 接好 Timer0 这根线 → 烧录。STC8H 是 8051 家族的芯片，本章所有说法对第 0.3 节表格里"Keil C51"一列的芯片都通用，只是器件头文件按型号换一下。

### 1.1 准备工具：Keil C51 + STC-ISP

1. 安装 **Keil C51**（µVision5，8051 编译器）
2. 安装 **STC-ISP**（STC 官网下载）。它干三件事：烧录、生成器件头文件、把 STC 型号添加进 Keil
3. 打开 STC-ISP → 左侧选择芯片型号（如 STC8H8K64U）→ 切到 **"Keil仿真设置"** 标签页 → 点 **"添加型号和头文件到Keil中"**，按提示选 Keil 的安装目录，完成

> 不添加也能干：芯片选一个通用的 8051 型号（如 Atmel 的 AT89C52）——STC8H 指令兼容 8051，编译结果一样，只是工程里显示的名字不对。建议还是加上，省得以后混淆。

### 1.2 新建一个空白工程

工程 = 装你所有代码文件和配置的文件夹。µVision5 管着"这个文件夹里哪些文件要翻译、怎么翻译"。

1. 打开 Keil µVision5 → 菜单 `Project → New µVision Project...`
2. 选一个文件夹（比如 `D:\star_demo`），工程名填 `blink`，点保存
3. 弹出芯片选择框：搜索框输入 `STC8H8K64U`（或你加的型号），选中 → 点 OK
4. 弹出 "Manage Run-Time Environment" 窗口 → **直接点 Cancel**（我们用不到 Keil 的软件包）

### 1.3 复制 StardustOS 文件

把 StardustOS 仓库里这些文件复制到 `D:\star_demo\stardustos\`（保持目录结构）：

```
stardustos/star.c                ← 内核，必须
stardustos/star.h                ← 内核头文件，必须
stardustos/star_config.h         ← 配置文件，必须
stardustos/star_task.c           ← 任务层（默认开启，必须）
stardustos/star_mail.c           ← 邮箱（默认开启，必须）
stardustos/port/star_port.c      ← 移植实现（Timer0 ISR + 空闲 + 断言），必须
stardustos/port/8051/star_port.h ← 8051 移植头（EA 临界区），必须
stardustos/port/8051/stdint.h    ← C51 缺的 C99 类型补丁，必须
stardustos/port/8051/stdbool.h   ← C51 缺的 bool 补丁，必须
```

> 若在 `star_config.h` 里把任务层（`STAR_ENABLE_TASK`）或邮箱（`STAR_ENABLE_MAILBOX`）关成 0，对应的 `star_task.c` / `star_mail.c` 可以不加入工程。`star_port.c` 就是"接线工"：它替你把 Timer0 报时器的门铃接到了内核的 `star_tick()` 上。

### 1.4 把源文件加进工程

1. 左侧工程树右键 `Target 1` → `Add Group...`，名字输入 `StardustOS`（Group = 工程树里的一个"抽屉"，用来给文件分类）
2. 右键刚建的组 → `Add Existing Files to Group 'StardustOS'...`
3. 把 `star.c`、`star_task.c`、`star_mail.c`、`port/star_port.c` 四个文件加进来

### 1.5 添加头文件路径（最常见的报错根源）

1. 点工具栏的魔术棒按钮（`Options for Target`，快捷键 `Alt+F7`）
2. 切到 `C51` 标签页 → 找到 `Include Paths` 一栏，点右边的 `...` 按钮
3. 点文件夹加号，添加这两条路径：
   ```
   ..\star_demo\stardustos
   ..\star_demo\stardustos\port\8051
   ```
4. 依次点 OK 关闭

> 为什么 `port\8051` 这条**不能省**？Keil C51 的安装目录里没有 C99 标准的 `stdint.h` / `stdbool.h`（内核代码要 `#include <stdint.h>`），StardustOS 在 `port/8051/` 里放了补丁。记住第一条排查口诀：**报错先看头文件路径**。

### 1.6 生成器件头文件 STC8H.H

1. 打开 STC-ISP → 切到 **"头文件"** 标签页
2. 选择型号 STC8H8K64U → 点生成，得到 `STC8H.H`（或按型号生成 `STC8H8K64U.H`）
3. 把文件放进工程目录，并加入工程（建一个 `App` 组加进去）

> STC8H.H 是芯片厂商给的"说明书目录"：`P00`、`TMOD`、`AUXR`、`ET0` 这些引脚和寄存器的名字都在里面。STC89C52 不需要这一步——它用 Keil 自带的 `reg52.h`。

### 1.7 粘贴例程代码

在工程里新建 `main.c`（`File → New`，另存为 `D:\star_demo\main.c`），内容直接抄 `examples/stc8h/main.c` 的完整代码（核心就是下面这份，LED 每 500ms 翻转）：

```c
#include "STC8H.H"
#include "star.h"

/* 事件 ID：从 0 连续编号（ID 即注册表下标，稀疏会浪费 Flash） */
enum {
    EVT_BLINK = 0,
    EVT_COUNT,
};

/* 24MHz、1T、Timer0 模式0（16 位自动重装）：
 * 1ms = 24000 拍，重装值 = 65536 - 24000 = 0xA240 */
#define T0_RELOAD_H 0xA2u
#define T0_RELOAD_L 0x40u

static void timer0_init_1ms(void)
{
    AUXR |= 0x80u;   /* T0x12=1：Timer0 1T 模式 */
    TMOD &= 0xF0u;   /* 清 Timer0 模式位 → 模式 0（16 位自动重装） */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;         /* Timer0 溢出中断使能 */
    TR0 = 1;         /* 启动 Timer0 */
    EA = 1;          /* 开全局中断 */
}
/* Timer0 溢出 ISR 由内核 port 层提供（interrupt 1，调用 star_tick()），
 * 无需在此编写。 */

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    P00 = !P00;      /* P0.0 LED 翻转 */
}

static const star_evt_entry_t evt_table[EVT_COUNT] = {
    STAR_ENTRY(blink_handler, NULL),   /* EVT_BLINK = 0（顺序初始化，C51 不支持 [0]= 写法） */
};

static star_timer_t blink_timer;

void main(void)
{
    timer0_init_1ms();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();   /* 永不返回 */
}
```

> 三个必须认识的记号：`STAR_REENTRANT`（告诉编译器函数会被"指针间接调用"，C51 下所有 handler 都必须加，漏了报 C212）；`STAR_UNUSED_PARAM(x)`（C51 不认 `(void)x`，用它引用未用参数）；事件表**必须顺序初始化**（C51 不支持 C99 指定初始化器，第几个元素就是几号事件）。

### 1.8 编译并生成 hex

1. 魔术棒 → `Output` 标签页 → 勾选 **`Create HEX File`**（不勾就没法烧录）
2. 按 `F7` 编译。出现 `0 Error(s) 0 Warning(s)` 即成功

### 1.9 STC89C52 变体：12T + 手动重装 + 自定义 ISR

STC89C52 和 STC8H 有两处不同，例程见 `examples/stc89c52/main.c`：

1. **定时器配置不同**：STC89 固定 12T、没有 AUXR。12MHz 主频下 12T 后定时器每秒跳 100 万下，1ms = 1000 拍，重装值 = 65536 - 1000 = **0xFC18**，用模式 1（16 位手动重装）：

```c
#define T0_RELOAD_H 0xFCu
#define T0_RELOAD_L 0x18u

static void timer0_init_1ms(void)
{
    TMOD &= 0xF0u;
    TMOD |= 0x01u;   /* Timer0 模式 1（16 位，手动重装） */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;
    TR0 = 1;
    EA = 1;
}
```

2. **ISR 要自己写**：STC89 无 16 位自动重装，溢出后计数器从 0 重新数，必须手动把重装值写回去。所以这个例程**自定义了 Timer0 ISR**，并在编译选项里定义 `STAR_PORT_NO_TICK_ISR` 排除内核自带的 ISR（为什么必须这样，第 3 章细讲）：

```c
void timer0_isr(void) interrupt 1
{
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    star_tick();
}
```

> 编译选项在哪定义宏？魔术棒 → `C51` 标签页 → `Define` 栏，填 `STAR_PORT_NO_TICK_ISR`（多个宏用逗号分隔）。SDCC 用户则在命令行加 `-DSTAR_PORT_NO_TICK_ISR`，并把中断函数写成 `void timer0_isr(void) __interrupt(1)`、头文件用 `<8051.h>`。

### 1.10 报错了？对照这张表

翻译官脾气直，报错信息不带翻译。对照下表查（C51 版）：

| 报错信息 | 原因 | 怎么改 |
|---|---|---|
| `cannot open source input file "star.h"` | 头文件路径没加或加错 | 回 1.5 检查两条路径 |
| `cannot open source input file "stdint.h"` | include 路径缺 `port\8051`（补丁在那） | 补上 `..\star_demo\stardustos\port\8051` |
| 链接报 `star_timer0_isr` 重复定义（MULTIPLE PUBLIC） | 你自己写了 Timer0 ISR，内核默认 ISR 也还在 | 编译选项加 `STAR_PORT_NO_TICK_ISR`（见 1.9） |
| `C212: indirect call: parameters do not fit` | 事件 handler 漏了 `STAR_REENTRANT` | 函数定义处补上 `STAR_REENTRANT` |
| `C275: expression with possibly no effect` | 用 `(void)x` 引用未用参数，C51 不认 | 改用 `STAR_UNUSED_PARAM(x)` |
| `[0] = ...` 附近语法错误 | C51 不支持指定初始化器 | 改成顺序初始化（见 1.7） |
| 变量名 `data` 报错 | `data` 是 C51 关键字，不能当变量名 | 改名 |
| `struct` 里用 `bit` 成员报错 | C51 的 `bit` 不能作结构体成员 | 用 `bool`/`unsigned char`（`stdbool.h` 补丁已处理） |

### 1.11 烧录验证

烧录（下载）= 把翻译好的程序抄进芯片的 Flash，断电也不丢。STC 芯片的烧录有个招牌动作叫"冷启动"——先断电，点下载，再上电：

1. 接线：USB 转 TTL 串口，`TXD → P3.0`、`RXD → P3.1`、`GND → GND`（STC8H8K64U 等带 USB 的型号，也可以先按住 P3.2 再上电，用 USB 线直接下载）
2. 打开 STC-ISP → 选择型号 → 选串口号 → 打开刚才生成的 `.hex` 文件
3. 点 **"下载/编程"** → **给板子断电，再上电**
4. 预期现象：LED 每 500ms 翻转一次（定时器驱动）

> 灯闪了 = 三样东西（心跳、打盹、牌子）全部接线成功，本章完成。灯不闪 = 去第 6 章 FAQ 的 Q1 对号入座。

---

## 第 2 章：路线 B —— Keil C251 + STC32G

这一章要干成的事：**换一颗 32 位内核的芯片、换一个编译器，把第 1 章的流程再走一遍**。STC32G 是 80251 内核（32 位），IDE 换成 Keil C251。流程骨架一模一样，区别只在四件事：

| 第 1 章（STC8H） | 第 2 章（STC32G） |
|---|---|
| Keil **C51** 编译器 | Keil **C251** 编译器 |
| port 目录 `port/8051/` | port 目录 `port/251/` |
| 器件头文件 STC8H.H | 器件头文件 STC32G.H |
| 可能要把内核大数组搬 XRAM（第 5 章） | 内存空间足够，不用操心 |

> 80251 是 8051 的"大号升级版"：指令兼容 8051，中断系统也一样（Timer0 溢出还是 `interrupt 1`），所以临界区仍然用 EA。走完这章你就明白：**换芯片并不难，难的是第一颗**。

### 2.1 建工程 + 加文件（照第 1 章抄）

1. 用 Keil **C251** 新建工程，芯片选 `STC32G12K128`（先用 STC-ISP 的"Keil仿真设置"添加型号）
2. 复制 StardustOS 文件（照 1.3，把 `port/8051` 换成 `port/251`）：
   ```
   stardustos/star.c                ← 必须
   stardustos/star.h                ← 必须
   stardustos/star_config.h         ← 必须
   stardustos/star_task.c           ← 默认开启，必须
   stardustos/star_mail.c           ← 默认开启，必须
   stardustos/port/star_port.c      ← 必须
   stardustos/port/251/star_port.h  ← 251 移植头，必须
   stardustos/port/251/stdint.h     ← C251 缺的 C99 类型补丁，必须
   stardustos/port/251/stdbool.h    ← 同上，必须
   ```
3. 建组 `StardustOS`，把 `star.c`、`star_task.c`、`star_mail.c`、`port/star_port.c` 加进工程
4. 魔术棒 → `C251` 标签页 → Include Paths 加两条：
   ```
   ..\star_demo\stardustos
   ..\star_demo\stardustos\port\251
   ```

### 2.2 粘贴例程代码

用 STC-ISP 生成 `STC32G.H` 放进工程，main.c 内容直接抄 `examples/stc32g/main.c`——和 STC8H 那份几乎一模一样（24MHz、1T、Timer0 模式 0、重装值 0xA240、P00 翻灯），只有头文件从 `STC8H.H` 换成 `STC32G.H`。

### 2.3 编译烧录

1. 魔术棒 → `Output` → 勾 `Create HEX File` → `F7` 编译
2. 预期 `0 Error(s)`。注意：**C251 会报一条无害的 C174 提示**（未引用的 static 函数），不影响运行，忽略即可
3. STC-ISP 烧录：流程同 1.11（选型号 STC32G12K128 → 打开 hex → 点下载 → 冷启动上电）

### 2.4 报错对照表（C251 版）

| 报错信息 | 原因 | 怎么改 |
|---|---|---|
| `C138: expression with possibly no effect` | 用 `(void)x` 引用未用参数 | 改用 `STAR_UNUSED_PARAM(x)` |
| `C174: ... unreferenced function` 之类 | 未引用的 static 函数 | 无害，忽略 |
| `C212` / 间接调用参数放不下 | handler 漏了 `STAR_REENTRANT` | 补上 `STAR_REENTRANT` |
| 链接报 Timer0 ISR 重复定义 | 自己写了 ISR 但没排除内核的 | 编译选项加 `STAR_PORT_NO_TICK_ISR` |

---

## 第 3 章：已有工程怎么接入（自定义 tick ISR）

这一章要干成的事：**你手里已经有一个能跑的工程**（自己的 Timer0 中断、自己的延时函数、自己的外设代码），不想推倒重来，只想让 StardustOS"住进来"。好消息是：StardustOS 是客气的房客，你家的装修它一样不动——只借三个接口。

### 3.1 先说个重要区别：C51 没有弱符号

很多教程会教："内核自带的 tick 中断是弱符号（替身），你自己写一个同名强符号（本尊），链接时自动覆盖它。" **这套在 Keil C51/C251 上不成立**——C51 没有弱符号，两个同名函数同时存在直接报 `MULTIPLE PUBLIC` 链接错误。

所以 STC 移植的接入办法是**反过来**：用宏把内核自带的 Timer0 ISR 关掉，自己写一个，在自己的 ISR 里喊 `star_tick()`。这个宏叫 `STAR_PORT_NO_TICK_ISR`。

### 3.2 接入步骤

1. **编译选项里定义 `STAR_PORT_NO_TICK_ISR`**（魔术棒 → C51 标签页 → Define 栏；SDCC 用 `-DSTAR_PORT_NO_TICK_ISR`）。它告诉 `star_port.c`："Timer0 我自己管，你别生成默认 ISR。" `star_port.c` **照常加入工程**，其余照 1.5 加头文件路径。
2. **在你自己的 Timer0 中断函数里加一行** `star_tick()`。如果你原来没有 Timer0 中断，新建一个（必须是 `interrupt 1`，这是 Timer0 的固定中断号）：

```c
void timer0_isr(void) interrupt 1   /* SDCC 写 __interrupt(1) */
{
    /* ← 在这里喊 star_tick()，告诉内核"过了一拍" */
    star_tick();
    /* ...你原来的代码：手动重装 TH0/TL0、延时计数等 */
}
```

> 白话解释：Timer0 报时器每次响铃，电话都会打到这个函数（8051 的中断入口登记的就是它）。你开门时多喊一嗓子 `star_tick()`，原来开门时干的活原样保留，互不打扰。注意：**`STAR_PORT_NO_TICK_ISR` 定义了就必须有自己的 ISR 调 `star_tick()`**，否则心跳彻底没了，灯永远不闪。

3. **空闲不用你写**：`star_port.c` 自带的 `star_idle()` 就是 PCON IDL 打盹，低功耗默认就有。注意它是**强符号**，不能一边留着 `star_port.c` 一边自己再写一个同名函数（会报 `star_idle` 重复定义）。只有你**把 `star_port.c` 整个剔除、自己写全套 port**（第 4 章）时才需要自己写。

### 3.3 想保留自己的主循环？用 star_poll()

`star_loop()` 是内核自己的无限循环，一进去就不出来。你想保留自己的 `while(1)`？可以——用 `star_poll()` 手动驱动内核：每圈喊它"查一次筐"（处理事件、响闹钟）：

```c
while (1) {
    /* ...你自己的轮询代码 */
    if (!star_poll()) {
        /* 没事件可处理。想省电就调 star_sleep()（内核会判断
         * 队列空且无到期项才真睡） */
    }
}
```

> `star_sleep()` 只是你递了个"想睡"的请求，睡不睡、睡多久由内核判断（队列空且没有到点的事才真睡，判定和睡觉在同一临界区内原子完成，不会睡过头）。

---

## 第 4 章：没有现成 port 的芯片（照模板抄）

这一章要干成的事：**第 0.3 节表格里没有你的芯片（比如别的 8051 兼容型号、或者要换用 Timer1）时，自己动手写"接线方案"**。好消息是：不用发明，只对照抄。打开 `stardustos/port/star_port_template.c`，它就是全部答案（一份填空式参考答案，注释里列了四步）。四步 = 内核要的三样东西（tick、空闲、临界区）+ 一个断言失败处理（4.4 节）。

### 4.1 tick：让定时器中断叫内核

8051/251 上 tick 源固定是 **Timer0 溢出中断（interrupt 1）**。模板里第一步就是它：

```c
/* 例：你的 1ms 定时器中断（8051/251 写 interrupt 1） */
void Timer0_ISR(void) interrupt 1
{
    star_tick();       /* 内核内部只做 count++，极快 */
    /* 必要时清除中断标志 / 手动重装 */
}
```

中断里**只干这一件事**（加上必须的重装）。所有"到点了要做什么"都由内核在主循环里完成。

> 白话解释：报时器响铃 → 你开门喊 `star_tick()`（记一笔账："过了一拍"）。别把任何"干活"塞进这里——开门的人待得越久，其他事情被耽误得越久。

定时器的**初始化**（TMOD/AUXR/TH0/TL0/TR0/ET0）写在你的 main 里，按芯片主频算重装值：每拍数 = 主频 ÷ 12（12T）或 ÷ 1（1T），1ms 的拍数 = 每拍数 ÷ 1000，重装值 = 65536 − 拍数。现成答案：STC8H/STC32G 24MHz 1T → 24000 拍 → **0xA240**；STC89C52 12MHz 12T → 1000 拍 → **0xFC18**（参考第 1 章代码）。

### 4.2 空闲：PCON IDL 打盹

```c
void star_idle(uint32_t next_due)
{
    STAR_UNUSED_PARAM(next_due);   /* 固定拍不用 deadline，收下不看 */
    STAR_PCON = STAR_PCON | 0x01u; /* IDL=1：下一条指令后 CPU 停，进入空闲 */
    STAR_PCON = STAR_PCON & 0xFEu; /* 中断唤醒后清 IDL，下次才能再进空闲 */
}
```

> 白话解释：置 IDL=1 后 CPU 停转，但定时器和中断照常走，门铃（Timer0 中断）一响自动睁眼，然后立刻把 IDL 清掉——不清的话下次想睡就睡不进去了（8051 的空闲位唤醒后要靠软件清）。代码里的 `STAR_PCON` 是电源控制寄存器 PCON（地址 **0x87**）的带前缀裸声明（`sfr STAR_PCON = 0x87;`，写在 port 头里，不依赖厂商头文件），IDL 就是它的 bit0。

**注意三个契约**（内核依赖它们消除"漏睡"竞态）：

1. **本函数在关中断状态下被调用**（"关中断 → 查队列确实为空且无到期项 → 执行空闲 → 恢复中断"）。所以 `star_idle()` 要极短（几条寄存器操作）。
2. **8051 的空闲模式与 EA 的唤醒关系因厂商而异，且未经板级验证**。因此内核**默认把 `star_idle` 实现为空转不休眠**（绝对安全，省电没了但绝不睡死）。要启用 PCON IDL 省电，定义 `STAR_PORT_IDLE`，并**必须先上板实测**：关中断进 IDL 后 tick 中断能否唤醒、空闲电流是否下降。若无法唤醒，只能自己在 `EA=1` 下进 IDL 并处理"查空到入睡"之间的丢事件窗口（固定拍下每 1ms tick 兜底，最坏丢唤醒 ≤1 拍）。
3. **8051/251 目前只有固定拍，没有 tickless**（`STAR_TICKLESS=1` 无 8051/251 实现）。固定拍 + 默认空转已可运行；低功耗（IDL）与 tickless 都留待后续版本与上板验证。

> 这种"闭眼打盹"靠心跳（Timer0）叫醒，所以事件最多被耽误 1 拍。别用会停掉定时器的深度睡眠模式（如掉电模式 STOP）——那等于把心脏也停了，内核没有别的唤醒手段，需要你自己设计唤醒源，别指望它。

### 4.3 临界区：三个小函数（保存/恢复式）

先白话讲清临界区为什么存在：中断（门铃）随时可能打断正在干活的代码。假如主循环正在往账本上写"第 5 号事件"，写到一半门铃响了、门铃那位也来写——账本就被写花了。对策：**写账本期间挂"请勿打扰"牌子（关 EA），写完再摘（恢复 EA）**。

新建 `star_port.h`，提供中断状态类型加三个函数（8051 现成实现见 `port/8051/star_port.h`，直接抄即可）：

```c
#ifndef STAR_PORT_H
#define STAR_PORT_H

#include <stdint.h>

#define STAR_PORT_8051 1            /* 平台标签：star_port.c 据此选择实现 */

typedef unsigned char star_crit_state_t;   /* 保存 EA（0/1） */

static star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s = STAR_EA;  /* 记下牌子原状态 */
    STAR_EA = 0;                    /* 挂"请勿打扰" */
    return s;                       /* 把原状态带回 */
}

static void star_crit_exit(star_crit_state_t s)
{
    STAR_EA = s;                    /* 按小纸条恢复，不是无条件开中断！ */
}

static uint32_t star_crit_active(void)
{
    return STAR_EA ? 0u : 1u;       /* 1 = 牌子挂着（关中断中） */
}

#endif
```

> 白话解释：`star_crit_enter()` 挂牌子（先把原状态记在小纸条上带回来）；`star_crit_exit(s)` 摘牌子（**按小纸条恢复原状**，谁挂的谁摘，不碰别人的牌子）；`star_crit_active()` 问一句"牌子现在挂着吗？"。为什么必须保存/恢复而不是"关/开"？因为内核可能在你已经关了中断的上下文里运行，无条件开中断会破坏调用方的原子性——这是经典的隐蔽 bug。

> **EA 从哪来？** 上面代码里的 `STAR_EA` 是带前缀的裸声明（`sfr STAR_IE = 0xA8; sbit STAR_EA = STAR_IE ^ 7;`），写在 port 头里，**不依赖厂商头文件**，也不会和 STC8H.H / reg52.h 里已有的 `EA` 定义冲突。EA 就是 IE 寄存器（地址 **0xA8**）的 bit7。

**移植一个新芯片时，`star_port.h` 还要提供这几个编译器兼容宏**（照 `port/8051` 抄）：

| 宏 | 作用 | Keil C51/C251 | SDCC |
|---|---|---|---|
| `STAR_STATIC` | 内核大数组存储类 | `static idata`（或 `static xdata`） | `static __idata`（或 `__xdata`） |
| `STAR_INLINE` | 内联关键字（C51 不支持 inline） | `static` | `static inline` |
| `STAR_INTERRUPT(n)` | 中断函数语法 | `interrupt n` | `__interrupt(n)` |
| `STAR_WEAK` | 弱符号（C51/C251 不支持） | 空 | 空 |

### 4.4 断言失败处理：star_assert_fail（第四步）

内核的 `STAR_ASSERT` 默认开启，触发时会回调 `star_assert_fail(file, line)`。模板里自带一个"停机死循环"的默认实现；你可以把停机换成先记录再复位：

```c
void star_assert_fail(const char *file, int line)
{
    /* 例：把 file/line 记进日志区，然后复位芯片 */
    STAR_UNUSED_PARAM(file);
    STAR_UNUSED_PARAM(line);
    for (;;) { }   /* 默认：停机，方便调试器抓现场 */
}
```

> 断言 = 内核给自己设的"保险丝"——发现账本出现不可能的状态（内部 bug）就熔断。默认熔断方式是"原地停机"，调试器一看就知道停在哪；产品里常换成"记录位置 → 复位"。生产环境想彻底关掉断言（省 Flash/周期），构建时定义 `-DSTAR_ASSERT(x)=((void)0)` 即可。

### 4.5 四步总览

| 步骤 | 干什么 | 放在哪 |
|---|---|---|
| 1. tick | Timer0 溢出中断里调 `star_tick()` | `star_port.c`（或自己的 ISR + `STAR_PORT_NO_TICK_ISR`） |
| 2. 空闲 | `star_idle()` 置/清 PCON IDL 位 | `star_port.c` |
| 3. 临界区 | `star_crit_enter/exit/active` + 兼容宏 | `star_port.h` |
| 4. 断言 | `star_assert_fail()` 停机或复位 | `star_port.c` |

把模板改名为 `star_port.c` 放进工程，并把你的 port 头目录加入 include 搜索路径（内核通过 `star.h → star_port.h` 找到你的实现）——移植完成。

---

## 第 5 章：内存模型与 C51 语法限制

### 5.1 内核的大数组住哪：idata 还是 xdata？

内核用 `STAR_STATIC` 声明几块大数组（事件队列、延时槽、任务槽，默认配置共约 280B）。8051 的 port 头把它映射到 `static idata`（Keil）/ `static __idata`（SDCC）——放在**内部 RAM**。STC8H/STC89 的内部 RAM 只有 256B，你的堆栈、局部变量也挤在这里。

STC8H 还有 **8KB XRAM**。内部 RAM 紧张时，定义 **`STAR_RAM_XDATA=1`** 把内核大数组整体搬到 XRAM，腾出内部 RAM 给堆栈：

```
魔术棒 → C51 标签页 → Define 栏填：STAR_RAM_XDATA=1
（SDCC 用：-DSTAR_RAM_XDATA=1）
```

port 头里对应地变成 `static xdata`（Keil）/ `static __xdata`（SDCC）——宏是**工程级全局**定义，`star_port.c` 也要编译到，所以别只写在某个 .c 文件里。

> 白话解释：内部 RAM 是"寸土寸金"的市中心，XRAM 是郊外大仓库。大数组搬到仓库去，市中心留给堆栈。代价：访问 xdata 比 idata 慢一点，但对内核这种低频访问完全无感。**XRAM 很小的型号（如 STC89 系只有 512B）一般不值得搬**，优先压缩配置，或干脆不定义这个宏。

### 5.2 8051 和 251 的差别

STC32G（80251）的内存空间比 8051 大得多，`port/251/star_port.h` 里 `STAR_STATIC` 直接定义为 `static`（用默认存储类即可），**不需要** XRAM 那套搬移。其余（EA 临界区、PCON 空闲、Timer0 ISR）和 8051 完全一致。

### 5.3 为什么 include 路径必须含 port 目录？

Keil C51/C251 的安装目录**没有 C99 标准的 `stdint.h` / `stdbool.h`**，而内核代码开头就 `#include <stdint.h>`。StardustOS 在 `port/8051/` 和 `port/251/` 里各放了一份补丁：Keil 下补丁自己定义 `uint8_t`/`uint32_t` 等类型（`bool` 用 `unsigned char`，因为 C51 的 `bit` 不能作 struct 成员，不能用 `typedef bit bool`）；SDCC 下补丁用 `#include_next` 转发到系统自带的头文件，不影响。

这就是第 1.5/2.1 节必须把 `port/8051`（或 `port/251`）加进 Include Paths 的原因——缺了它，`stdint.h` 找不到，第一行就报错。

### 5.4 C51 语法限制速查（写应用代码时注意）

| 限制 | 例子 | 对策 |
|---|---|---|
| 不支持 C99 指定初始化器 | `[0] = STAR_ENTRY(...)` | 事件表顺序初始化：第几个元素就是几号事件 |
| 不支持 for 内声明与混合声明 | `for (int i=0;...)` | 声明提到函数开头，`for (i=0;...)` |
| 不支持 `inline` 关键字 | `static inline` | 用 `STAR_INLINE`（Keil 下退化为 `static`） |
| `data` 是关键字 | 变量名叫 `data` | 改名（如 `data_buf`） |
| `bit` 不能作 struct 成员 | `struct { bit f; }` | 用 `bool`/`unsigned char`（`stdbool.h` 已处理） |
| 不支持 `(void)x` 引用未用参数 | `(void)param;` | 用 `STAR_UNUSED_PARAM(param)` |

---

## 第 6 章：FAQ（踩坑大全）

**Q1：LED 不闪，程序像死了一样？**
按顺序查四件事：① `timer0_init_1ms()` 写了没有（`TR0=1` 启动、`ET0=1` 使能、`EA=1` 开总中断，三样缺一不可）；② `star_port.c` 加进工程没有；③ 编译选项里**没**误加 `STAR_PORT_NO_TICK_ISR`（加了就得有自己的 ISR 调 `star_tick()`）；④ 主循环是不是 `star_loop()`，事件表是不是顺序初始化的。排查顺序就是"先看表拨没拨，再看线接没接，最后看人上没上班"。

**Q2：我的工程已经占用了 Timer0，还能用 StardustOS 吗？**
能。内核默认 ISR 固定在 Timer0（interrupt 1）。如果你要自己用 Timer0，就按第 3 章：定义 `STAR_PORT_NO_TICK_ISR` 排除内核 ISR，在自己的 Timer0 ISR 里调 `star_tick()`。**想改用别的定时器（如 Timer1）当 tick？** 那等于换"接线方案"，按第 4 章自己写 `star_port.c`（把 interrupt 1 换成对应中断号）。

**Q3：handler 里能调什么 API？**
handler 运行在主循环上下文，所以**全部 API 都能调**：`star_event_post*`、`star_mail_send/recv`、`star_timer_start/stop`、`star_task_start/stop` 都没问题。

**Q4：中断里能调什么？**
只有 `star_event_post*`、`star_mail_send`、`star_tick`。定时器、任务 API 都只能在主循环上下文用。另外 handler 必须带 `STAR_REENTRANT`（C51 下漏了直接报 C212）。

**Q5：handler 里写 while 等标志行不行？**
不行。handler 必须毫秒级返回，否则整个系统卡死（协作式内核，谁拖堂全班停摆）。长流程拆状态机 + 定时器，或分多步投递事件给自己。

**Q6：空闲没省电？**
内核默认把 `star_idle` 实现为空转（省电是关闭的）。要省电：定义 `STAR_PORT_IDLE` 启用 PCON IDL，再上板实测空闲电流是否下降、tick 是否准时唤醒；8051 空闲唤醒与 EA 的关系因厂商而异，未经实测不要启用（见 4.2 契约 2）。

**Q7：STAR_TICK_MS 改成 10 会怎样？**
tick 变成 10ms 一拍，但**重装值必须重算**——C51 没有自动换算的公式，全靠手动：`重装值 = 65536 - (拍数/秒 × 0.01s)`。注意 Timer0 是 16 位计数器，最多数 65535 拍：24MHz 1T 下 1ms=24000 拍，**最多约 2.7ms**，改成 10ms 计数器直接溢出，必须同时把 Timer0 切到 12T 或降低分频。取值范围 1..1000，越界会编译期 `#error` 拦下。

**Q8：事件 ID 可以不连续吗？**
可以，但注册表是数组，ID 越大 Flash 浪费越多（中间的空号也占位置）。建议枚举连续定义从 0 开始。

**Q9：队列满了怎么办？**
`star_event_post` 返回 `STAR_ERR_FULL`，你可以重试或丢弃；状态类事件用 `star_event_post_replace`（同 ID 只保留最新）。"按键当前是按下还是松开"这类只关心最新状态的，用 replace 版最合适。

**Q10：handler 执行时间有没有上限？**
协作式内核没有强制上限，全靠自觉（铁律 1）。参考值（按指令条数估算，**无实测数据背书**）：24MHz 下 handler 控制在 1ms 内返回，10ms 级事件调度余量充足。务必按第 7 章检查清单实测。

---

## 第 7 章：移植成功检查清单

这一章要干成的事：**在说"移植完成"之前，逐项打勾**。前两项是编译和体积的"纸面账"，后面的每一项都要求上板实测——因为本仓库**没有任何官方板级实测数据**（内核目前只在 PC 上做过单元测试、在 Keil/SDCC 下交叉编译验证过 0 警告 0 错误），你的板子只有你自己能验。

- [ ] 编译 0 error 0 warning（Keil C51 / SDCC：0 警告 0 错误；C251 允许无害的 C174 提示）
- [ ] 内核体积核对：map 文件里内核三件套（star.c + star_task.c + star_mail.c）与 port 层各占多少字节，记录在案（README 给的参考：默认配置内核 RAM 约 280B）
- [ ] **Timer0 tick 实测**：逻辑分析仪/示波器量 Timer0 中断引脚，间隔 ≈ `STAR_TICK_MS`（1ms 就是 1ms，不是"感觉差不多"）
- [ ] LED 闪烁周期实测 ≈ 设定值（500ms 就是 500ms）
- [ ] **空闲电流实测**：空转时电流明显下降（IDL 生效），且 tick 准时唤醒、事件不睡过头
- [ ] **中断延迟实测**：进临界区前置高一个引脚、退出拉低，示波器量脉冲宽度，确认符合你的延迟预算（方法见《使用教程》附录 A）
- [ ] 串口高波特率（115200 以上）连续收发不丢字（邮箱槽数够不够、队列够不够）
- [ ] 周期定时器相位实测：示波器观察连续周期触发间隔，确认无累积漂移（尤其 handler 慢的场景）
- [ ] 重启 100 次无异常（看门狗场景下无卡死）
- [ ] 定义 `STAR_RAM_XDATA=1` 的工程（STC8H/STC32G）：确认内部 RAM 腾出、功能正常

> 白话补充几个词：**map 文件**是翻译官交的"拼装详单"，写着每个函数最终放在哪、占多少字节，体积核对就是翻这份详单；**逻辑分析仪 / 示波器**是能看见电信号波形的仪表，灯闪得准不准、睡眠电流降没降，用仪器看波形、读电流，而不是"目测差不多"；**看门狗**是芯片里的"防死机保险丝"，程序卡死就自动重启，有它的时候，重启 100 次看会不会卡在哪个环节。

全部打勾，恭喜：这颗 STC 芯片的 StardustOS 移植完成。欢迎把实测数据回馈给仓库（尤其 Timer0 中断延迟、IDL 空闲电流这些本文没有的硬数据），让下一颗芯片的移植者少踩一个坑。
