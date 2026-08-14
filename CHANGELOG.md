# 更新日志

本文件记录 StardustOS 的变更。StardustOS 从 [MoteOS](https://github.com/Lioyae/MoteOS) 更名而来，定位聚焦 STC 系列 8051 / 80251 单片机。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.0.0/)，版本号遵循[语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### 变更（Changed）
- **项目更名 MoteOS → StardustOS**：源码目录 `moteos/` → `stardustos/`，标识符前缀 `mote_`/`MOTE_` → `star_`/`STAR_`，文件名 `mote.*` → `star.*`，品牌图标重绘为"星尘"（金色四角星 + 星尘颗粒）
- **定位聚焦 STC 单片机**：README、移植教程、使用教程、测试文档全面重写为 STC 语境
- **定位明确为"事件驱动调度框架"**：README 简介改为"协作式内核（事件队列 + 软定时器 + 邮箱 + 可选任务层，非抢占式 RTOS）"，弱化"OS"一词的误导

### 新增（Added）
- **8051 移植层**（`stardustos/port/8051/`）：同时支持 Keil C51 与 SDCC，覆盖 STC8H/8A、STC89C52、STC8051U/8052U；临界区用 EA（IE 0xA8 bit7）保存/恢复，tick 用 Timer0 溢出中断（interrupt 1），空闲用 PCON IDL 空闲模式
- **80251 移植层**（`stardustos/port/251/`）：Keil C251，覆盖 STC32G
- **STC 例程**：`examples/stc8h`、`examples/stc89c52`（Keil C51 / SDCC 双编译）、`examples/stc32g`
- **编译器兼容层**：为 Keil C51/C251 提供 `stdint.h`/`stdbool.h`；新增 `STAR_STATIC`（内核大数组存储类）、`STAR_REENTRANT`（多参数函数指针 reentrant）、`STAR_INTERRUPT(n)`（中断函数语法）、`STAR_UNUSED_PARAM(x)`（引用未用参数）、`STAR_RAM_XDATA=1`（内核大数组搬 XRAM）等宏
- **SDCC 编译验证进 CI**：`sdcc-compile` job 以 `--Werror` 编译 8051 内核与 STC89C52 例程

### 移除（Removed）
- ARM Cortex-M0+/M3 移植层（`cm3`/`cm0plus`）、WCH RISC-V 移植层（`ch32v`）
- 对应例程：`examples/stm32f103`、`examples/ciu32f003`、`examples/ch32v003`
- QEMU 冒烟测试（`tests/qemu/`）与芯片头文件桩（`tests/stubs/`）
- CI 中的 ARM/RISC-V 交叉编译与 QEMU 冒烟 job

### 修复（Fixed）
- 内核代码 **C89 化**：移除 for 内声明、混合声明、C99 指定初始化器（Keil C51 不支持）
- 多参数函数指针 **reentrant 化**：修复 Keil C51 报 C212、SDCC 报 error 92（参数放不进寄存器）
- `star_mail_send`/`star_mail_recv` 参数 `data` 改名（`data` 是 C51 存储类关键字）
- 内核大数组内存模型：8051 下默认放 `idata`（避免 128B `data` 溢出），可 `STAR_RAM_XDATA=1` 搬 XRAM
- **8051/251 低功耗死机隐患**：`star_idle` 默认空转（原实现把 ARM wfi 的"关中断 + pending 唤醒"语义硬搬到 8051，可能上电睡死）；定义 `STAR_PORT_IDLE` 启用 PCON IDL 时，由实现自行"进 IDL 前临时开中断、唤醒后重新关中断"（接受 ≤1 tick 丢唤醒兜底），不再要求用户自写 EA 逻辑
- **清理 ARM 遗产**：`star_copy` 在 8051/251/SDCC 下改用字节拷贝（32 位拼装是负优化）；注释中 M0+/wfi/SysTick 等 ARM 语境改为 8051/中性描述；事件队列内存口径修正为"随指针宽度而变"
- `star_timer_stop` 幂等语义写入头文件契约；统一 SDCC 检测宏为 `defined(__SDCC) || defined(SDCC)`
- **8051 内存模型修复（链接期 DATA 溢出）**：SMALL 模型下内核 DATA 需求约 190B 超 128B，链接报 `L107 ADDRESS SPACE OVERFLOW`；工程模板改用 **Large 模型 + `STAR_RAM_XDATA=1` + 配置 `STARTUP.A51`（`XBPSTACK=1` 启用 reentrant 栈）**，STC89C52 例程加 `AUXR |= 0x02` 使能内部 XRAM；STC89C52RC 因 8KB Flash 需精简配置（关任务层/邮箱）

### 文档（Docs）
- 使用教程补齐三处：临界区 API（`star_crit_enter`/`star_crit_exit`）的公开用法与示例、`STAR_MAILBOX_DEF` 须放 .c 文件（放头文件会多 TU 各一份）、`star_init` 不重置丢事件钩子的重启坑

---

## 历史存档（MoteOS 时期）

> 以下为更名前的 MoteOS（ARM Cortex-M0+/M3 / RISC-V 定位）开发历史，仅作存档，不再维护。相关代码已随 STC 移植移除。

MoteOS 是一个面向小容量单片机的 C99 事件驱动协作式内核，支持 Cortex-M0+/M3、WCH 青稞 RISC-V 与宿主机。其设计要点（事件队列、定时器链表、延时投递、邮箱变长契约、tickless 入账协议、宿主机交错测试、gcov 覆盖率门槛、cppcheck 静态分析等）由 StardustOS 继承并保留。完整的 MoteOS 历史可查阅原仓库的 git 提交记录。
