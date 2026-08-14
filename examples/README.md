# STC 例程与 Keil 工程模板

本目录提供 STC 系列单片机的例程和可直接打开的 Keil µVision5 工程模板。

## 目录一览

| 目录 | 芯片 | 内核 | 编译器 | 工程文件 | 器件头文件 |
|---|---|---|---|---|---|
| `stc8h/` | STC8H / STC8A | 8051 | Keil C51（或 SDCC） | `stc8h.uvprojx` | STC8H.H（STC-ISP 生成） |
| `stc89c52/` | STC89C52 | 8051 | Keil C51 / SDCC | `stc89c52.uvprojx` | Keil 自带 `reg52.h` / SDCC 自带 `<8051.h>` |
| `stc32g/` | STC32G | 80251 | Keil C251 | `stc32g.uvprojx` | STC32G.H（STC-ISP 生成） |

每个例程都是同一件事：**Timer0 做 1ms tick，LED 每 500ms 翻转**。差别只在芯片主频、定时器分频（1T/12T）和器件头文件——正好覆盖三种典型情况。

## 打开工程前要做什么

1. 装好对应编译器：STC8H/STC89C52 用 **Keil C51**，STC32G 用 **Keil C251**。
2. **STC8H / STC32G 需要器件头文件**：打开 STC-ISP 软件 → 切到「Keil 仿真设置」标签 → 点「添加型号和头文件到 Keil」，生成 `STC8H.H` / `STC32G.H`，并把对应头文件放到工程目录（`stc8h/` 或 `stc32g/`）里。工程的 include 路径已含当前目录 `.\`，头文件放进来即可编译。
   - STC89C52 不需要这步：`reg52.h` 是 Keil C51 自带的。
3. 用 STC-ISP 的「添加型号到 Keil」把 STC 芯片型号加进 Keil（可选，见下）。

## 打开与编译

1. 双击 `.uvprojx`，用 Keil µVision5 打开。
2. 点「Build」编译，生成 `.hex` 文件（输出在 `Objects\` 目录）。
3. 用 STC-ISP 打开 `.hex`，选对芯片型号、串口，点「下载/编程」烧录。

## 常见问题

**打开工程提示「Device not found」或器件未识别？**
工程模板用通用型号占位（8051 用 AT89C52、80251 用 8xC251SB），如果你的 Keil 没装这些型号，µVision 会提示。解决：`Project → Options for Target → Device` 里手动选一个你 Keil 里有的 8051/251 型号（或先用 STC-ISP 添加 STC 型号再选）。**这不会影响编译结果**——STC8H/STC89C52 指令兼容 8051，STC32G 兼容 80251。

**想在 STC8H 上把内核数据搬到 XRAM？**
在 `Project → Options for Target → C51` 的「Define」里加 `STAR_RAM_XDATA=1`，并在 `Target` 标签把 XRAM 配成实际大小（STC8H 有 8KB）。详见 `docs/porting.md` 第 5 章。

**SDCC 怎么编译？**
SDCC 是开源 8051 编译器，Linux/Windows 都能用，已进 CI。命令行：

```bash
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o star.rel stardustos/star.c
# 其余三个文件同理：star_task.c / star_mail.c / stardustos/port/star_port.c
# 例程：
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o stc89c52_main.rel examples/stc89c52/main.c
```

## 例程代码说明

三个 `main.c` 结构一致，都做了四件事：

1. **配 Timer0** 产生 1ms 中断（STC8H/STC32G 用 1T 模式 0 自动重装；STC89C52 用 12T 模式 1 手动重装）
2. **写 handler** 处理事件（`STAR_REENTRANT` 修饰，LED 翻转）
3. **填事件表**（顺序初始化 `STAR_ENTRY`，C51 不支持指定初始化器）
4. **主循环** `star_init` → `star_timer_start` → `star_loop`

完整的 API 说明见 `docs/usage.md`，工程搭建步骤见 `docs/porting.md`。
