# StardustOS 使用教程（从零开始）

> 本文写给**从来没有接触过单片机编程的读者**：不需要会 C 语言，不需要认识
> "寄存器""中断""编译"这些词。凡是正文里要用到的技术名词，第一次出现时都会用
> 生活中的东西打比方讲清楚。你唯一要做的准备：已经照着《移植教程》
> （`docs/porting.md`）把最基本的工程跑起来过一遍。遇到不认识的词，随时翻
> 第 0 章术语表。我们慢慢讲，不赶时间。

---

## 第 0 章：术语表（不认识哪个查哪个）
这一章是"字典"。后面正文里出现的**每一个**名词，这里都有一句话解释。
不用背，先扫一眼有个印象，读正文时忘了哪个词就回来查。

### 0.1 名字和缩写

| 词 | 一句话解释 |
|---|---|
| **event / evt** | 事件 = 一张纸条，写着"发生了什么事"，比如"按键按下了""串口来数据了""时间到了" |
| **handler** | 处理函数 = 专门处理某一类纸条的工人。纸条一到，内核就喊他干活 |
| **回调（callback）** | handler 这类函数的统称。特点：**不是你主动喊它，而是系统"反过来"喊它** |
| **param / ctx** | param = 纸条附带的"小口袋"（一个指针，装数值或指向数据）；ctx = 注册 handler 时交给他的"自备工作台"，原样转交，用不用随你 |
| **post** | 投递 = 把纸条塞进传达室门口筐（队列）里的动作，不是"马上干活"而是"排队"；队列先进先出 |
| **tick** | 节拍 = 内核心跳，每 `STAR_TICK_MS`（默认 1ms）跳一次。**来源是 Timer0 溢出中断**（interrupt 1） |
| **Timer0** | 芯片自带的"自动报时器"：数到设定值就按一次门铃（触发中断）。StardustOS 拿它当心跳来源 |
| **注册表** | 值班表：纸条编号 → 对应的工人。内核靠它知道"纸条该给谁" |
| **定时器（timer）** | 闹钟。到点自动往筐里投一张纸条 |
| **邮箱（mailbox）** | 快递柜。传递"一大块数据"（数组/结构体）用的 |
| **任务（task）** | 打卡机。周期性地固定喊某个 handler 来干活 |
| **ISR / 中断** | 门铃响时去开门干的那点活（一段特殊代码）。开门动作要快 |
| **临界区** | 干活时挂"请勿打扰"牌子的时间段。牌子上写的是"关 EA"——这段期间任何中断都进不来 |
| **EA** | 8051/80251 芯片的"总电闸"（IE 寄存器 bit7）。EA=1 所有中断能响，EA=0 全部哑火 |
| **空闲（IDL）** | 让芯片"打盹"：写 PCON 的 bit0，CPU 停转省电，但定时器和中断照常工作，门铃一响就醒 |

### 0.2 代码里的符号

| 写法 | 含义 |
|---|---|
| `_t` 结尾 | C 语言惯例：以 `_t` 结尾是"自定义类型"（`star_timer_t`、`uint32_t`） |
| `STAR_` 前缀 | StardustOS 专属标记，防止撞名 |
| `void *` | 万能指针：可以指向任何类型的数据。`param` 就用它 |
| `uint8_t / uint16_t / uint32_t` | 定宽整数：8/16/32 位无符号整数，跨芯片大小永远不变 |
| `static` | ① 修饰变量：永久存在（函数返回也不消失）；② 修饰函数：只在本文件内可见 |
| `const` | 只读。const 数据通常被放进 Flash（长期记忆），省 RAM |
| `enum` | 枚举：给编号起名字。`EVT_LED` 比 `0` 好记一万倍 |
| `STAR_REENTRANT` | handler 的"专用修饰词"。8051/80251 编译器要求多参数函数指针带 reentrant 修饰，**每个 handler 都必须加**（见 9 章 FAQ） |
| `STAR_UNUSED_PARAM(x)` | 引用未用参数。C51 对"写了不用的参数"会报错，用这个宏告诉编译器"我知道它没用" |

### 0.3 两个关键观念
**观念一：handler 是"被叫去的"，不是"主动跑"的。** 传统写法：你写 main，你控制
流程。StardustOS 写法：你写 handler（工人），**内核在纸条到来时喊他**，干完活必须
马上回来（毫秒级），把控制权交回内核。

**观念二：中断里只许"递纸条"，不许"干活"。** 中断 = 门铃响了去开门，开得越久，
其他事情就被拖得越久。所以开门时（中断里）只许干一件事：把纸条塞进筐里（post），
真正的活留给工人在主循环里干。

### 0.4 先认识那台"指甲盖电脑"
StardustOS 是给**单片机**用的。单片机（MCU）就是一块比指甲盖还小的电脑：大脑 +
记忆 + 引脚，程序指挥它去连外面的灯、按键、传感器。几个高频词：**寄存器** =
芯片内部一排排小开关；**时钟/主频** = 芯片的心跳（24MHz = 一秒跳 2400 万次）；
**中断** = 门铃；**Timer0/tick** = 自动报时器（到点按门铃提醒内核"又过去 1 毫秒
啦"）；**空闲（IDL）** = 打盹（写 PCON 的 IDL 位，门铃一响自动醒）；
**Flash / RAM / 栈** = 长期记忆 / 短期记忆 / RAM 里放临时草稿的区域；**时基** =
内核数着 tick 的响铃次数过日子。

---

## 第 1 章：快速开始——第一个程序（点灯）

### 1.1 一张图看懂内核
把 StardustOS 想象成一家小工厂：**事件** = 纸条，**handler** = 工人，**post** =
把纸条塞进传达室门口的筐（队列），**主循环（star_loop）** = 传达室大爷——不断从
筐里拿纸条、看编号、喊对应工人来干；筐空了、闹钟也没到点，他就眯一会儿（写 PCON
的 IDL 位打盹，tick 中断一响就醒）。

```
门铃响（中断塞纸条） ─┐
闹钟到点（自动塞纸条） ─┼─▶ 筐（队列） ─▶ 大爷取纸条 ─▶ 查值班表 ─▶ 喊工人
你的代码（主动塞纸条） ─┘
```
写 StardustOS 程序 = **只做三件事**：① 定义"纸条有哪些种类"（事件 ID 枚举）；
② 定义"每类纸条谁来处理"（handler + 注册表）；③ 在需要的时候"塞纸条"（post）。
再记住一条：**工人干活要快（毫秒级），干不完就撕成几张小纸条分几次干。**

### 1.2 点灯程序（STC8H 系列，完整可编译）
以下代码就是 `examples/stc8h/main.c` 的核心（24MHz 主频，Timer0 每 1ms 报时，
P0.0 上的 LED 每 500ms 翻转一次）：
```c
#include "STC8H.H"           /* 器件头文件：用 STC-ISP 软件生成，加入工程 */
#include "star.h"            /* 内核头文件 = 说明书目录 */

/* 事件 ID：从 0 连续编号（ID 即注册表下标，稀疏会浪费 Flash，见铁律 4） */
enum {
    EVT_BLINK = 0,
    EVT_COUNT,
};

/* 24MHz、1T、Timer0 模式0（16 位自动重装）：1ms = 24000 拍，
 * 重装值 = 65536 - 24000 = 0xA240 */
#define T0_RELOAD_H 0xA2u
#define T0_RELOAD_L 0x40u

static void timer0_init_1ms(void)
{
    AUXR |= 0x80u;   /* T0x12=1：Timer0 1T 模式 */
    TMOD &= 0xF0u;   /* 清模式位 → 模式 0（16 位自动重装） */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;         /* Timer0 溢出中断使能 */
    TR0 = 1;         /* 启动 Timer0 */
    EA = 1;          /* 开全局中断（总电闸） */
}
/* Timer0 溢出 ISR 由内核 port 层提供（interrupt 1，内部调用 star_tick()），
 * 你不需要自己写。 */
static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    P00 = !P00;      /* 翻转 P0.0 上的 LED */
}

static const star_evt_entry_t evt_table[EVT_COUNT] = {
    STAR_ENTRY(blink_handler, NULL),   /* 顺序初始化：EVT_BLINK = 0 */
};

static star_timer_t blink_timer;

void main(void)
{
    timer0_init_1ms();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();     /* 永不返回 */
}
```
**工程怎么搭（前置条件）**：① 用 STC-ISP 软件生成器件头文件（STC8H.H 或具体型号
如 STC8H8K64U.H），加入 Keil 工程 include 路径；② 工程加入内核源文件
`stardustos/star.c`、`star_task.c`、`star_mail.c`、`stardustos/port/star_port.c`，
include 路径含 `stardustos/` 与 `stardustos/port/8051/`；③ 内部 RAM 紧张时定义
`STAR_RAM_XDATA=1` 把内核大数组搬到 XRAM；④ 编译、用 STC-ISP 烧录。STC32G
（80251 内核）把 include 路径换成 `stardustos/port/251/`，其余代码几乎一样。

**逐行白话版**：
1. `#include "STC8H.H"`：把芯片的"引脚说明书"抄进来——编译器这才认识 `P00`、
   `AUXR` 这些名字。
2. `timer0_init_1ms()`：给 Timer0 上发条，让它每 1 毫秒按一次门铃。24MHz、1T
   模式下 1ms = 24000 拍，重装到 65536-24000 = 0xA240，数满就溢出、触发中断。
   **这就是 tick 节拍的来源**。
3. `blink_handler`：定义工人，干的活就一行——把灯拨一下。注意三点：签名照着抄、
   **末尾必须写 `STAR_REENTRANT`**、用 `STAR_UNUSED_PARAM` 引用不用的参数。
4. `evt_table`：值班表，`STAR_ENTRY(找谁, 工作台ctx)` 一项一纸条；**顺序初始化**
   （Keil C51 不支持 C99 指定初始化器，第几行就是几号事件，见 FAQ Q2）。
5. `main` 里四步：开报时器 → `star_init` 交值班表 → `star_timer_start` 设定 500ms
   循环闹钟（响了投 0 号纸条）→ `star_loop()` 大爷上班，永不返回。

**程序跑起来后发生了什么？**
```
Timer0 每 1ms 溢出 → star_tick() → 心跳 +1 → 数到 500 → 闹钟到期 → 投 [EVT_BLINK]
→ 主循环取纸条 → 查表 → 喊 blink_handler → 灯翻转 → 筐空了 → PCON IDL 打盹
→ 1ms 后 Timer0 中断叫醒 → 继续数数……
```
**要点：灯闪得准不准，取决于闹钟（定时器），不取决于 handler 写得快慢——这就是实时性的来源。**

### 1.3 没有自动重装的芯片怎么办（STC89C52）
STC89C52 的 Timer0 没有 16 位自动重装，要用模式 1（手动重装）：溢出后 ISR 里
**手动**把 TH0/TL0 写回去，再推进心跳。此时要自己写 ISR，并在编译选项中定义
`STAR_PORT_NO_TICK_ISR`（Keil 在 Options → C51 里加，SDCC 用
`-DSTAR_PORT_NO_TICK_ISR`），排除内核默认 ISR，避免中断向量重复定义：
```c
#define T0_RELOAD_H 0xFCu
#define T0_RELOAD_L 0x18u
void timer0_isr(void) interrupt 1
{
    TH0 = T0_RELOAD_H;   /* 手动重装 */
    TL0 = T0_RELOAD_L;
    star_tick();         /* 推进内核心跳 */
}
```
初始化时模式位要设成模式 1：`TMOD &= 0xF0u; TMOD |= 0x01u;`。完整例程见
`examples/stc89c52/main.c`（Keil C51 / SDCC 双编译器都支持，LED 在 P1.0）。

### 1.4 配置一览（都在 `stardustos/star_config.h`）

| 宏 | 默认 | 含义 | 边界 |
|---|---|---|---|
| `STAR_TICK_MS` | 1 | 节拍毫秒数（报时器多久响一次） | 1..1000 |
| `STAR_EVT_QUEUE_SIZE` | 16 | 事件队列槽数（筐多大） | 1..255 |
| `STAR_DELAYED_MAX` | 4 | 延时投递最大并发数 | ≥0 |
| `STAR_ENABLE_TASK` | 1 | 任务层开关 | 0/1 |
| `STAR_TASK_SLOT_MAX` | 4 | 同时活跃任务上限 | 1..255 |
| `STAR_ENABLE_MAILBOX` | 1 | 邮箱开关 | 0/1 |
| `STAR_TICKLESS` | 0 | tickless 空闲 | **8051/251 暂不支持，保持 0** |
| `STAR_TIMER_CATCHUP_MAX` | 1000 | 周期定时器落后多少拍后重建相位 | — |

越界配置在编译期直接 `#error`，防止静默内存损坏。`STAR_ASSERT` 默认开启：违反
内部不变量时回调 `star_assert_fail()`（port 层弱符号，默认死循环，可重定义）；
生产要省 Flash 可自行定义 `-DSTAR_ASSERT(x)=((void)0)`。

---

## 第 2 章：纸条（事件）怎么递——post 三兄弟
先想清楚"纸条代表的是'发生次数'还是'当前状态'"，再选寄法：普通寄（post）、
覆盖寄（replace）、定时寄（delayed）。

### 2.1 普通投递 star_event_post
```c
star_status_t r = star_event_post(EVT_BLINK, NULL);
```
谁都能递：**中断里、handler 里、main 里**都行。返回值必须看一眼：`STAR_OK` =
递进去了；`STAR_ERR_FULL` = 筐满了（默认 16 张）。满的时候自己决定：重试、丢弃、
还是调大 `STAR_EVT_QUEUE_SIZE`。

### 2.2 覆盖投递 star_event_post_replace
场景：按键手抖，中断风暴一口气递了 20 张"按键按下"纸条——筐爆了。
```c
star_event_post_replace(EVT_KEY, NULL);
```
效果：筐里如果已有 EVT_KEY 纸条，**只更新最新那张，不再新增**，筐里永远只有一张
按键纸条。**什么时候用**：事件代表"当前状态"而不是"发生次数"时——按键、ADC 当前
值、界面刷新，一律用 replace。

### 2.3 延时投递 star_event_post_delayed
就像给未来的自己寄一张定时明信片："请 100ms 后把这张纸条放进筐。"
```c
star_event_post_delayed(EVT_BLINK, NULL, 100);   /* 100ms 后再放进筐 */
/* 覆盖版：同一 EVT 只留最新一张在路上（重复调用不占新格子） */
star_event_post_delayed_replace(EVT_KEY, NULL, 100);
/* 取消：把还在路上的延时纸条收回来。返回 STAR_ERR_NOT_FOUND = 没找到 */
star_event_cancel_delayed(EVT_KEY, NULL);
```
适合"延迟关屏""开机 3 秒后自检"。同时最多 `STAR_DELAYED_MAX`（默认 4）张在路上，
超了返回 `STAR_ERR_FULL`；`ms` 传 0 或 ≥2^31 返回 `STAR_ERR_PARAM`（与定时器同
口径的运行时校验）。典型用途：按键按下 3 秒后休眠，3 秒内再按键就把旧的 cancel
掉再 post 一张新的（或用 replace 版直接覆盖）。

### 2.4 纸条上带东西：STAR_P / STAR_U32
纸条的 param 是个 `void *`（万能指针），像纸条上缝的"小口袋"：可以塞一张写着
数字的便签，也可以塞一张"门牌号"（指向某块数据的位置）。
```c
uint16_t adc_val;                    /* 全局变量：永远存在，可以粘 */
star_event_post(EVT_ADC, STAR_P(adc_val));   /* 中断里：把数值刻在纸条上 */
static void adc_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    uint16_t v = (uint16_t)STAR_U32(param);   /* handler 里：还原成数值 */
    if (v > 3000) { /* 电压超了 */ }
}
```
**8051 的特殊限制（重要）**：8051 的"通用指针"只有 3 字节，塞进指针里的数值
**≤16 位可以无损传，32 位值会被截断**——这是 8051 的硬件限制，不是内核的问题。
要传完整的 32 位数据，请走第 4 章的邮箱；STC32G（C251）下也建议统一按 ≤16 位传。

**为什么不能粘栈变量？** 反面教材：
```c
void some_function(void)
{
    uint8_t data[4] = {1,2,3,4};     /* data 是局部变量，住在"栈"上 */
    star_event_post(EVT_RX, data);   /* 把 data 的地址粘到纸条上 */
}   /* ← 函数返回，栈上这块内存立刻被回收、被别的东西占用 */
```
栈 = RAM 里的"草稿纸区"：函数一返回，草稿纸就被收走、给别的函数接着用。规则就
一句话：**粘全局/静态变量（永远在），或粘 STAR_P(数值)；大块数据走第 4 章邮箱。**

### 2.5 返回值的四种"回执"

| 返回值 | 生活比喻 | 常见场景 |
|---|---|---|
| `STAR_OK` | 顺利签收 | post 递进筐、mail send 入柜、闹钟设定成功 |
| `STAR_ERR_FULL` | 地方满了，装不下 | 筐满、延时槽满、柜子格满、任务槽满 |
| `STAR_ERR_PARAM` | 你给的要求不合理，被当场退回 | 时长传 0 或 ≥2^31、超格长度、非法的策略值 |
| `STAR_ERR_NOT_FOUND` | 没找到你要找的东西 | 取消延时纸条时纸条已寄出 / 编号对不上 |

---

## 第 3 章：闹钟（定时器）怎么定
定时器 = 闹钟：设好"几点响、响几次"，到点它自动往筐里投一张纸条，你什么都不用
管。闹钟的设定/关停/改时间**只能在主循环上下文里做**（handler 或 main），中断里
不许碰（铁律 3）。

### 3.1 单次闹钟（响一次就扔）
```c
static star_timer_t t;                    /* 闹钟变量必须 static 或全局 */
star_timer_start(&t, EVT_XXX, NULL, 1000, false);
```
五个参数逐个翻译：① `&t` 告诉内核"用这个闹钟"；② `EVT_XXX` 响了投几号纸条；
③ `NULL` 纸条上不粘东西（需要粘数值就写 `STAR_P(x)`）；④ `1000` 毫秒后响；
⑤ `false` 只响一次，响完闹钟自己作废。

### 3.2 循环闹钟（一直响）
```c
star_timer_start(&t, EVT_XXX, NULL, 50, true);   /* 每 50ms 响一次 */
```
最后一位换成 `true` 就是"循环闹钟"：响完自动定下一次，永远不退休。想停就用 stop。

### 3.3 关掉 / 改时间
```c
star_timer_stop(&t);               /* 关掉。没开过也安全，不会出错 */
star_timer_restart(&t, 2000);      /* 把时间改成 2 秒后响。前提：它当前是开着的 */
```
`star_timer_restart` 对**未启动**的闹钟返回 `STAR_ERR_NOT_FOUND`；周期闹钟 restart
会同时更新周期。

### 3.4 五个要注意的点
1. **变量必须 static**（或全局）：闹钟要持续存在直到响（原因见 1.2）。
2. **handler 里可以随意开/关闹钟**——这是把"长流程拆成多步"的官方姿势（铁律 1）。
3. **中断里不能碰定时器**——定时器 API 只能在主循环上下文用（铁律 3）。
4. **时长上限约 24.8 天**（2^31-1 ms，回绕比较的数学边界），超出直接返回
   `STAR_ERR_PARAM`（运行时校验）。说说"回绕"：内核用 32 位计数器记时间，像
   摩托车的里程表，满格了会转回 0 重新数。内核比较"谁先到点"用对回绕安全的比较
   方法，碰上"翻表"那一刻也不会失灵；但数学上能表示的时长最多 2^31-1 ms（约
   24.8 天），这是刻意的安全边界。
5. **闹钟内部按到期时刻排队**：没到点的闹钟每次 poll 只需看一眼队头（O(1)），
   闹钟多了也不拖慢主循环。

### 3.5 队列满时的三种策略（重要）
先讲故事：筐（队列）满了，可闹钟偏偏这时候响了，纸条塞不进去。三种策略就是三种
面对"满筐"的做法：**DROP** = 塞不进去就当场撕掉，闹钟作废（适合超时检测）；
**RETRY** = 捏在手里下一拍再塞（适合灯闪烁这类迟到的）；**LATEST** = 同编号的
纸条只留最新一张（适合 ADC 值这类状态通知）。
```c
/* 策略一 DROP（严格截止）：到期即投，失败即弃并释放定时器。
 * 适合超时检测——事件要么准时出现，要么永不出现 */
star_timer_start_ex(&t, EVT_TMO, NULL, 100, false, STAR_TIMER_POLICY_DROP);

/* 策略二 RETRY（默认）：单次定时器满队重试，事件"至少一次"送达但可能迟到。
 * 适合 LED 闪烁这类迟到无所谓的场景 */
star_timer_start_ex(&t, EVT_LED, NULL, 500, false, STAR_TIMER_POLICY_RETRY);

/* 策略三 LATEST：replace 语义，队列里同 ID 只留最新一份。
 * 适合状态类事件（ADC 值、位置更新），天然防堆积 */
star_timer_start_ex(&t, EVT_ADC, NULL, 20, true, STAR_TIMER_POLICY_LATEST);
```
**选型速查**：超时检测、协议截止时间 → DROP；迟到无所谓（闪烁、心跳）→ RETRY
（默认）；状态类、只关心最新值 → LATEST。

默认的 `star_timer_start` = RETRY（单次）/ DROP（周期，满队丢当次并计入
`star_dropped_count()`）。RETRY 满队时定时器不释放，**下一拍**自动重试（所以是
"至少一次"，最坏晚一拍）；**重试的失败只是"暂缓"，不计入 `star_dropped_count()`
也不触发丢事件钩子**——事件最终会送达，不是丢弃（周期定时器满队丢当次才是真
丢弃，照常计数）。policy 传越界值返回 `STAR_ERR_PARAM`（运行时校验）。

### 3.6 循环闹钟的相位稳定（重要）
循环闹钟按**绝对相位**触发：每次到期，下次到期时刻是 `上次到期 + 周期`，而不是
"本次触发时刻 + 周期"。① handler 忙了 3ms（周期 10ms）：下一次仍在 20/30/40ms
触发，不会漂移到 23/33/43ms——延迟不会逐周期累积；② 一口气错过好几拍：只补投
一张纸条（合并），相位照旧；③ 极端落后（超过 `STAR_TIMER_CATCHUP_MAX`，默认
1000 拍）：放弃旧相位、从当前时刻重新对齐（防御性，防止推进循环过长）。对时、
采样时刻、协议心跳这类需要**相位稳定**的场景直接依赖这一语义。

---

## 第 4 章：快递柜（邮箱）——大块数据怎么传
纸条的小口袋只能粘一个小数值。要传**一坨数据**（串口字节流、传感器报文），用
邮箱。邮箱 = 快递柜：发送方把货**复印一份**放进格子，接收方凭纸条来取；原数据
之后怎么变都不影响柜子里的副本。
```c
STAR_MAILBOX_DEF(uart_mb,      /* 柜子名字，随便起（宏会帮你生成对应变量） */
                 EVT_UART,     /* 有货到柜时，投几号纸条（取件通知） */
                 32,           /* 柜子有几个格子 */
                 1);           /* 每个格子多大（字节）：串口逐字节收发 → 1 字节一格 */
```
四个参数翻译成大白话：① `uart_mb` 柜子名字，宏会帮你变出真正的柜子变量（和它的
数据区、长度表）；② `EVT_UART` 有货进柜时自动投的"取件通知"纸条；③ `32` 一共
32 个格子（全满返回 `STAR_ERR_FULL`）；④ `1` 每格 1 字节，串口逐字节收发正好。

> **长度契约（重要）**：每格最大 `item_size` 字节（1..255，`STAR_MAILBOX_DEF`
> 编译期强制——传 0 或超 255 直接编译不过）。`star_mail_send` 的 `len` 必须
> 1..item_size：超长或为 0 返回 `STAR_ERR_PARAM`（**直接拒绝，不静默截断**）。
> `star_mail_recv` 返回**实际存入的字节数**（1..item_size），空柜返回 -1——不会
> 回吐整格残留垃圾。每格额外花 1 字节 RAM 记录实际长度（变长消息支持）。

### 4.1 经典用法：中断放货，handler 取货（STC 串口）
```c
/* 串口中断里——放货（中断里唯一允许的"数据类"操作）。
 * 注意：send 在关中断状态下执行（与事件入队同一临界区原子完成）；
 * 中断延迟与拷贝字节数成正比——延迟预算与实测方法见附录 A */
void uart_isr(void) interrupt 4
{
    uint8_t c;
    if (RI) {              /* 收到一个字节 */
        RI = 0;            /* 清接收标志 */
        c = SBUF;          /* 读串口数据寄存器 */
        star_mail_send(&uart_mb, &c, 1);
        /* 三个参数：柜子 → 货物的地址（&c = c 的位置）→ 复印多少字节 */
    }
}

/* handler 里——取货 */
static void uart_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    uint8_t c;
    star_mail_t *mb = (star_mail_t *)param;   /* param 就是"哪个柜子来货了" */
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(ctx);

    while (star_mail_recv(mb, &c) > 0) {   /* 取一格；空柜返回 -1 */
        /* 处理 c，直到把所有格子清空 */
    }
}
```
逐段白话：**中断里的活**——串口收到一个字节（门铃响了），`star_mail_send` 把它
**复印**一份塞进柜子，同时自动投一张 `EVT_UART` 取件通知，放完货马上回去。
**工人（handler）的活**——从纸条口袋里掏出柜子门牌号（`param`），然后一个劲儿
`star_mail_recv` 取货，每取一格处理一格，直到柜子空了（返回 -1）。收货和投递都
在**挂"请勿打扰"牌子的时间段（临界区）**里一口气完成，原子 = 要么全做完要么全没做。

### 4.2 格子数怎么算（防丢数据）
公式：`格子数 ≥ 中断最坏情况下一口气来的字节数 ÷ 每格字节数`。例：串口 115200bps
= 每秒约 11520 字节。假设最忙时 handler 10ms 没空处理，来了 115 字节：每格 1 字节
→ 格子数 ≥ 115，取 128；每格 64 字节（按帧收）→ 取 3，4~8 更保险。`star_mail_send`
返回 `STAR_ERR_FULL` = 格子全满 = 配置小了；`STAR_ERR_PARAM` = `len` 超格或为 0。

### 4.3 放货的顺序为什么是"先入队、后入箱"
一笔放货其实是两件事：① 投"取件通知"纸条；② 把货复印进格子。内核的顺序是
**先投纸条、投进去了才放货**，两步合并在同一个临界区里一口气做完：投纸条成功 →
复印进格子；投纸条失败（筐满）→ 直接收工，格子一个没动（无残留）。反过来
（先放货、后投纸条）的话，纸条没投进去、货却躺在格子里，永远不会有人来取，还占
着格子——这就是"残留"。代价是：中断里每放一次货都要挂着"请勿打扰"牌子做完拷贝，
拷贝越多、牌子挂得越久。这笔账怎么算、怎么量，见附录 A。

---

## 第 5 章：打卡机（任务层）——周期性的活
> 定位说明：任务层本质是**"带上下文的周期回调"便捷层**——到点直接调 handler，
> 不走事件队列、不吃 post_replace 语义，适合固定节奏的扫描类工作。它**不是抢占式
> RTOS 的"任务"**：不抢占、无独立栈、handler 被主循环直接同步调用，只是名字顺口。

闹钟和打卡机的区别：

| | 闹钟（定时器） | 打卡机（任务层） |
|---|---|---|
| 工作方式 | 响一次 → 递一张纸条 → handler 收纸条干活 | 到点直接喊 handler 干活，不走纸条筐 |
| 状态 | 无（闹钟自己没记忆） | 有专属状态槽 |
| 适合 | 零散事件、事件流 | 固定的周期工作：按键扫描、屏刷、喂狗 |

```c
static void key_scan(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
}
static const star_task_desc_t tasks[] = {
    STAR_TASK_DEF(10, key_scan, NULL),   /* STAR_TASK_DEF(周期ms, handler, ctx) */
};
int main(void)
{
    star_task_init(tasks, 1);   /* 把名单交给内核。1 = 名单上有 1 个任务 */
    star_task_start(0);         /* 0 号任务打卡上班（按名单上的顺序编号） */
}
```
白话解读：打卡机 = 一排固定轮班的岗位。`STAR_TASK_DEF(10, key_scan, NULL)` 的
意思是"每 10 毫秒，喊 key_scan 来干一次活"。和闹钟最大的不同：闹钟是"响了 → 塞
纸条 → 工人被纸条叫来"（绕了传达室一圈），打卡机是"到点直接喊人"（不走纸条筐）。

**槽位池**：`STAR_TASK_SLOT_MAX`（默认 4）= 同时上班的任务上限。名单可以写 20 个
任务，但同时只能有 4 个在打卡（第 5 个 `star_task_start` 返回 `STAR_ERR_FULL`），
停掉一个就能再开一个。任务 handler 收到的 evt 固定是 `STAR_EVT_TASK`。

**周期校验**：描述符 `period_ms` 为 0 或 ≥2^31（约 24.8 天）的任务无法启动，
`star_task_start` 返回 `STAR_ERR_PARAM`（运行时校验，与定时器同口径）。0 周期会
退化成"每 poll 直接喊一次 handler"，等同忙循环，别想钻空子。想停用
`star_task_stop(0)`（未启动返回 `STAR_ERR_NOT_FOUND`）。

---

## 第 6 章：低功耗与空闲——PCON IDL 打盹
单片机项目常常要省电（电池供电、USB 取电受限）。StardustOS 的省电姿势：**没事干
就睡，有事干才醒**——而且知道下一件"到点的事"在哪一刻，不会睡过头。

### 6.1 空闲的判定：谁来决定睡不睡
主循环 `star_loop()` 每一圈做两件事：
```c
void star_loop(void)
{
    for (;;) {
        if (!star_poll()) {   /* 处理定时器/任务，分发一个事件 */
            star_sleep();     /* 没分发出任何事件 → 看能不能睡 */
        }
    }
}
```
`star_sleep()` 的判定（内部实现，应用不需要调用）：**筐里一张纸条都没有，而且
最近一个到期的闹钟/延时纸条还没到点**（或根本没有待到期项）——满足才睡，否则
立刻回去干活。

### 6.2 睡下去之后：PCON 的 IDL 位
8051/80251 的 port 层把"睡觉"实现为**空闲模式**：
```c
STAR_PCON = STAR_PCON | 0x01u;   /* PCON 的 bit0 (IDL) 置 1 → CPU 停转 */
STAR_PCON = STAR_PCON & 0xFEu;   /* 醒来后清 IDL，下次才能再睡 */
```
- 空闲模式下 **CPU 停止运行**（省电），但**定时器、中断系统照常工作**；任意中断
  （比如 Timer0 的 tick 中断）都能唤醒芯片，唤醒后程序从下一条指令继续跑
- **前提（重要）**：tick 中断必须已使能（`ET0=1`）才能靠它唤醒；如果把 Timer0
  关了或需要绝对安全，可定义 `STAR_PORT_IDLE_NOOP` 让空闲变成空转（不休眠）

### 6.3 下一件到点的事：star_next_due
```c
uint32_t due = star_next_due();   /* 下一到期节拍；无待到期项返回 STAR_TICK_NONE */
```
返回值是"节拍时刻"（单位 = `STAR_TICK_MS`），不是"剩余毫秒"；要算"还差多久"用
回绕安全比较：`(int32_t)(due - star_ticks()) > 0` 表示还没到期。自带临界区，任意
上下文都能调；主要给 tickless 移植层用，固定拍（8051/251）下 `star_idle` 不需要它。

### 6.4 关于 tickless（8051/251 读者请直接跳过）
`STAR_TICKLESS=1` 是"无节拍空闲"：空闲时把 tick 定时器拨到下一 deadline 再睡，
能省更多电。**但 8051/251 的移植目前只支持固定拍，tickless 暂不支持**——
`STAR_TICKLESS` 保持默认 0 即可，别去改它。

---

## 第 7 章：完整实战——会眨眼的电子门牌
把前面几章的东西拼起来做一个完整的 STC8H 项目：

**需求**：LED 每 500ms 闪烁（门牌"眨眼"）；按键（接 INT0）每按一次计一个数，并
把计数通过**邮箱**发给显示 handler；按奇数次按键 LED 变成 100ms 快闪，偶数次
恢复 500ms 慢闪。
```c
#include "STC8H.H"
#include "star.h"

enum {
    EVT_BLINK = 0,    /* 定时器到点：翻转 LED */
    EVT_KEY,          /* 按键按下（中断里 replace 投递） */
    EVT_KEYMSG,       /* 邮箱来消息（柜子指针在 param 里） */
    EVT_COUNT,
};

static uint16_t g_key_count;     /* 按键次数 */
static uint8_t g_fast_blink;     /* 1 = 快闪，0 = 慢闪 */

STAR_MAILBOX_DEF(key_mb, EVT_KEYMSG, 4, 2);   /* 4 格，每格 2 字节 */
static star_timer_t blink_timer;

#define T0_RELOAD_H 0xA2u
#define T0_RELOAD_L 0x40u

static void timer0_init_1ms(void)
{
    AUXR |= 0x80u;
    TMOD &= 0xF0u;
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;
    TR0 = 1;
    EA = 1;
}

static void int0_init(void)
{
    IT0 = 1;    /* 下降沿触发 */
    EX0 = 1;    /* 开外部中断 0 */
}

static void blink_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);
    P00 = !P00;              /* 翻转 LED */
}

static void key_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    uint8_t msg[2];

    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(param);
    STAR_UNUSED_PARAM(ctx);

    g_key_count++;
    msg[0] = (uint8_t)(g_key_count >> 8);      /* 高 8 位 */
    msg[1] = (uint8_t)(g_key_count & 0xFFu);   /* 低 8 位 */
    star_mail_send(&key_mb, msg, 2);
}

static void keymsg_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    uint8_t msg[2];
    int n;
    star_mail_t *mb = (star_mail_t *)param;   /* param = 哪个柜子来货了 */

    STAR_UNUSED_PARAM(evt);
    STAR_UNUSED_PARAM(ctx);

    for (;;) {
        n = star_mail_recv(mb, msg);          /* 返回实际长度；空柜 -1 */
        if (n <= 0) {
            break;                            /* 取空了 */
        }
        if (n == 2 && (msg[0] & 0x01u) != 0u) {
            g_fast_blink = 1;
            star_timer_restart(&blink_timer, 100);   /* 快闪 */
        } else {
            g_fast_blink = 0;
            star_timer_restart(&blink_timer, 500);   /* 慢闪 */
        }
    }
}

static const star_evt_entry_t evt_table[EVT_COUNT] = {
    STAR_ENTRY(blink_handler, NULL),    /* EVT_BLINK = 0 */
    STAR_ENTRY(key_handler, NULL),      /* EVT_KEY = 1 */
    STAR_ENTRY(keymsg_handler, NULL),   /* EVT_KEYMSG = 2 */
};

void int0_isr(void) interrupt 0        /* SDCC 写成 __interrupt(0) */
{
    star_event_post_replace(EVT_KEY, NULL);   /* 按键状态：只留最新一张 */
}

void main(void)
{
    timer0_init_1ms();
    int0_init();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();     /* 永不返回 */
}
```
**拆解：为什么这么设计**：中断全部一行流（递纸条/放货）→ 中断里待的时间最短；
每个功能一个 handler，互不干扰；按键用 `post_replace` 防抖；计数用 `star_mail_send`
深拷贝进柜子（`msg` 是栈变量也没关系，对照 2.4 反面教材）；取货时用
`star_timer_restart` 改周期，正好演示"handler 里随意开关闹钟"（铁律 3 允许）。

---

## 第 8 章：四条铁律（违反的后果都演示给你看）
StardustOS 运转良好全靠这四条规矩。它们不是可选项——踩了任何一条，程序都会
"看似能跑，偶尔抽风"。

### 铁律 1：handler 必须毫秒级返回，绝不阻塞
工人被喊来干活，干完必须马上回去。他一旦赖着不走，传达室大爷就卡住，后面的纸条
全都没人处理。**反面教材**（千万别这么写）：
```c
static void bad_handler(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    while (!RI) { }   /* 死等串口数据 → 全厂停工！ */
}
```
后果：主循环卡死，其他 handler 全部饿死（系统假死）。**正确姿势**：把长流程拆成
多步，用闹钟/延时纸条推进：
```c
static void step1(uint16_t evt, void *param, void *ctx) STAR_REENTRANT
{
    start_something();                          /* 启动动作（不等待） */
    star_event_post_delayed(EVT_STEP2, NULL, 100);   /* 100ms 后再走下一步 */
}
```
就像煮面：不是傻站着等水开，而是"先把锅架上，设个闹钟，100ms 后闹钟响再回来
下面"。

### 铁律 2：纸条只粘永久变量或数值
纸条上粘的地址，必须指向"永远都在"的东西（全局变量、static 变量），或者干脆用
`STAR_P(数值)` 把数字直接刻在纸条上。粘"草稿纸"（栈变量）= 等工人找过去，纸已经
被人改写了（反面教材见 2.4）。**8051 上注意：`STAR_P` 塞的数值 ≤16 位才无损，
32 位会截断；完整 32 位数据走邮箱。**

### 铁律 3：哪些 API 能在哪里调（背不下来就抄）

| API | 中断里 | handler/主循环里 |
|---|---|---|
| `star_event_post*`（含 `_delayed` / `_delayed_replace`） | 可以 | 可以 |
| `star_event_cancel_delayed` | 可以 | 可以 |
| `star_mail_send` | 可以 | 可以 |
| `star_tick` / `star_tick_advance` | 可以（移植层专用） | 可以 |
| `star_next_due` | 可以 | 可以 |
| `star_ticks` / `star_dropped_count` / `star_set_drop_hook` | 可以（自带临界区） | 可以 |
| `star_timer_start/stop/restart` | 不行 | 可以 |
| `star_task_start/stop` | 不行 | 可以 |
| `star_mail_recv` | 不行 | 可以 |

口诀：**开门（中断）时只许递纸条、放快递；闹钟、打卡机、取货，都回到传达室再干。**
补一句：`star_loop()` 是"大爷全自动上班"；想保留自己的 `while(1)` 主循环，可改用
`star_poll()` 单步驱动内核（每次处理一件事），没事干时喊 `star_sleep()` 打盹——
具体写法见《移植教程》。

### 铁律 4：事件 ID 从 0 连续枚举
ID 就是值班表的下标，表按"最大 ID+1"占 Flash。ID 写成 200 号，前面 200 格就白占
了——就像 200 个座位的礼堂只坐最后一排，前面全空着浪费。编号从 0 连续排，座位
才能紧挨着坐满。

---

## 第 9 章：出毛病了？排查表 + FAQ

### 9.1 排查表

| 症状 | 最可能的原因 | 查哪里 |
|---|---|---|
| LED 完全不闪 | **tick 没接上** | Timer0 初始化调了没（TR0/ET0/EA）？`port/star_port.c` 加进工程没？自定义 ISR 时 `STAR_PORT_NO_TICK_ISR` 定义没？ |
| 编译报 C212 / error 92 | handler 忘了 `STAR_REENTRANT` | 每个 handler 签名末尾都写上 `STAR_REENTRANT` |
| 编译报 C275 / C138 | 参数写了没用 | 用 `STAR_UNUSED_PARAM(参数名)` 引用它 |
| 事件递了没反应 | 值班表没登记 / ID 超界 | `STAR_ENTRY(...)` 写了没？表大小传对没？ID 从 0 连续枚举没？ |
| 任务（打卡机）不执行 | 没调 `star_task_init` / `star_task_start`，或 id 越界 | main 里两步都调了没？看 `star_task_start` 返回值（`STAR_ERR_PARAM` = id 越界或周期非法，`STAR_ERR_FULL` = 槽位已满） |
| 偶尔丢数据 | 队列/柜子小了 | `STAR_EVT_QUEUE_SIZE`、邮箱槽数调大，注意返回值 |
| 想监控丢了多少事件 | — | 读 `star_dropped_count()`：因队列满/事件无效被**实际丢弃**的累计数（单次 RETRY 定时器的满队重试不计入——它最终会送达）。想知道"丢的是哪个事件"，用 `star_set_drop_hook()` 注册回调（钩子在关中断上下文运行，只允许事件/邮箱 API，禁止定时器/任务 API；钩子内再次触发的丢弃不会递归回调本钩子——防重入） |
| 中断里改全局变量偶发抽风 | 中断和 handler 抢数据 | 数据只走纸条/柜子传，共享变量加临界区 |
| 省不了电 | `star_idle` 没生效 | 见 6.2：tick 中断（ET0）必须使能才能靠它唤醒；特殊情况下可定义 `STAR_PORT_IDLE_NOOP` 明确不休眠 |
| 系统周期性卡一下 | 某个 handler 太慢 | 用 `star_ticks()` 在 handler 头尾打点计时 |

**看时间**：`star_ticks()` 返回系统节拍数（单位 `STAR_TICK_MS`），就是内核心里的
"现在几点"。**在 PC 上先测逻辑**：内核可在电脑上跑（`cmake --build build &&
ctest`），业务逻辑先验证再上板，事半功倍。

### 9.2 FAQ

**Q1：handler 里能 sleep/延时吗？** 没有这个 API。想要"过一会再干"→
`star_event_post_delayed` 或定时器。

**Q2：事件表为什么要"顺序初始化"？** Keil C51 不支持 C99 的指定初始化器
（`[EVT_X] = ...`），所以事件表必须按 ID 顺序一行一行填 `STAR_ENTRY(handler, ctx),`，
第几行就是几号事件。这正好呼应铁律 4：ID 必须从 0 连续。

**Q3：handler 忘加 STAR_REENTRANT 会怎样？** 编译直接报错：Keil C51 报 **C212**，
SDCC 报 **error 92**。原因：C51/C251 的非 reentrant 函数参数走固定内存区，多参数
函数指针调用放不下；`STAR_REENTRANT` 让参数走 reentrant 栈。宿主机（GCC）上这
个宏展开为空，PC 上测不出来、上板才炸——**写代码时就加上**。

**Q4：一个 handler 能注册多个事件吗？ctx 参数是干嘛的？** 能。值班表里多写几行
`STAR_ENTRY(h, NULL)`，handler 里用 `evt` 参数区分。ctx 是 `STAR_ENTRY(handler,
ctx)` 的第二个参数，原样传给 handler 第三个参数——给 handler 配"工作台"：
`STAR_ENTRY(h, &my_device_config)`，一个 handler 服务多个设备。不需要就传 NULL。

**Q5：post 的纸条一定按顺序处理吗？** 筐是 FIFO（先进先出），最多排队
`STAR_EVT_QUEUE_SIZE`（默认 16）张。

**Q6：没有注册的 ID 递进去会怎样？** 纸条被内核默默丢掉，不会崩。这是安全网，但
说明值班表漏登记了。丢弃会计入 `star_dropped_count()` 并触发丢事件钩子（如果注册
了）——漏登记的事件在可靠性监控里是"看得见"的。

**Q7：任务和"定时器+事件"到底选哪个？** 任务 = 固定节奏的周期活（扫描、刷新、
喂狗），且不需要在别处被触发；定时器+事件 = 触发式、灵活（暂停/改周期/事件混流）。

**Q8：STAR_DELAYED_MAX 用完了还能递延时纸条吗？** 返回 `STAR_ERR_FULL`。要么调大
配置，要么改用定时器。注意延时槽是固定池（每次 poll 线性扫描 `STAR_DELAYED_MAX`
个槽），而定时器是**按到期时刻排序**的链表（每次 poll 只遍历到期节点，空转
O(1)），几十个定时器没问题，不建议堆上百个。

**Q9：STAR_P 传 32 位值会坏吗？** 8051 下会截断（见 2.4）。要传完整 32 位数据，
用邮箱。

---

## 附录 A：临界区时长实测（8051 用示波器量"EA 关断时长"）
**门铃响了多久才有人开门，就是"中断延迟"。** 内核里最影响它的是"请勿打扰"牌子
（临界区）挂多久——牌子挂着的这段时间（EA=0），门铃响了也没人开。

**先声明：内核尚未在任何真实芯片上做过板级验证，下面是估算，实际值必须按平台
实测。** 中断延迟大致由三部分组成：
```
中断延迟 ≈ 硬件中断响应时间 + tick 处理 + 内核临界区（取最长者）
```
临界区时长取决于配置与主频，来源（按最坏路径）：

| 操作 | 临界区内做的事 | 规模 |
|---|---|---|
| `star_event_post` | 队列入队 | O(1)，十几条指令 |
| `star_event_post_replace` | 从新到旧扫描队列找同 ID | O(队列长度)，最坏 = `STAR_EVT_QUEUE_SIZE` |
| `star_event_post_delayed` / `_replace` / `star_event_cancel_delayed` | 线性扫描延时槽池 | O(`STAR_DELAYED_MAX`) |
| `star_timer_start_ex` / `star_timer_restart` | 排序链表插入（找插入点） | O(定时器数量)，几十个定时器仍很短 |
| `star_mail_send` | 拷贝 len 字节（≤ item_size）+ 事件入队 | O(len)，每 4 字节约几条指令 |
| `star_tick` | 关中断 + 自增 + 恢复 | 约 10 条指令 |

> 前五项里，post/replace/delayed/mail_send 是**中断可调用** API（中断延迟 =
> 中断响应 + 其中最长者）；定时器 API 虽仅限主循环，其临界区同样会延迟中断响应。
> `star_next_due` 与睡眠判定只发生在主循环/空闲路径，不计入中断延迟。

**8051 上的实测方法（示波器 + GPIO 打点）**：8051 的 EA 是内部寄存器位，没有引脚
可以直连示波器，所以用"GPIO 打点法"：在要测量的临界区前后翻转一个 GPIO，示波器
量脉冲宽度。

**方法一：应用层打点（不动内核，最省事）**。量整个 API 调用时长（含进出临界区的
开销），测最坏路径即可：
```c
P10 = 1;                          /* 打点开始 */
star_mail_send(&uart_mb, &c, 1);  /* 被测的最坏路径 */
P10 = 0;                          /* 打点结束，示波器量高电平宽度 */
```
**方法二：移植层打点（能量到"纯临界区"）**。临时改 `port/8051/star_port.h`：
在 `star_crit_enter()` 里 `STAR_EA = 0;` 之后加 `P10 = 1;`，在 `star_crit_exit()`
里恢复 EA 之前加 `P10 = 0;`，测完恢复。量出的脉冲宽度就是"EA 刚关断"到"EA 恢复
前"的纯临界区时长。
量出的脉冲宽度 = **中断被屏蔽的最长时间**。对照你的预算判断：tick 是 1ms 一次，
脉宽只要远小于 1ms（比如 <100µs）对 tick 精度影响可忽略；但串口/按键等其它中断
的实时性预算也要逐个核对。**粗算参考**（24MHz、-Os）：普通 post 约 30 周期 ≈ 1µs
量级；64 字节格子邮箱 send 约 500 周期 ≈ 20µs 量级；队列 255 的 replace 全扫描
约 2500~4000 周期 ≈ 100µs 量级。

实测值超预算时的对策（按性价比排序）：① 缩小邮箱 `item_size`（延迟与它成正比）；
② 缩小 `STAR_EVT_QUEUE_SIZE`（replace 扫描与它成正比）；③ 大块数据改走"指针 +
所有权移交"（自己保证生命周期，不拷贝）。

> 本内核**没有任何官方板级实测数据**，使用前请自行测量并在你的预算内做决定。这
> 也是《移植教程》最终检查清单的正式步骤之一，两篇文档口径一致。
