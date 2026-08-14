# 更新日志

## 未发布（2026-08-13，独立技术评审整改）

按外部技术评审（掉计数口径、钩子重入、tickless 覆盖、体积口径、契约文案）
整改：

### 修复

- **RETRY 重试不再计入丢弃统计（口径修复）**：单次 RETRY 定时器满队时的
  暂缓重试此前经 `star_event_post` 失败路径计入 `star_dropped_count()` 并
  触发丢事件钩子——事件最终仍送达却被记为"丢弃"，可靠性监测被假阳性
  污染。现在重试走不计数入队（`star_event_enqueue_raw`）；周期定时器
  满队丢当次、DROP 策略失败即弃等真实丢失仍照常计数。`star.h` 中
  `star_dropped_count()` 的文档口径同步收紧
- **邮箱入箱/入队顺序改为"先入队、后入箱"**：消除"count 已递增、回滚
  未发生"的中间状态——丢事件钩子（在 `star_note_dropped` 内、同一临界
  区中）重入同一邮箱时，观察到的邮箱状态始终自洽；回滚代码整段删除
  （入队失败时邮箱未动，全有或全无自然成立）
- **tickless 入账协议重写（P0，QEMU 冒烟实抓）**：旧协议回读计数器快照
  做入账锚点，写 VAL=0 后计数器存在 0 窗口（真实硬件一拍沿、分频时钟下
  可长达整拍；QEMU 的 ptimer 重载事件与 TCG 批执行之间同样有竞态），
  回读值为 0 时"锚点 - 剩余"整段下溢——QEMU 冒烟实测时基一次跳变约
  179 秒。新协议：锚点恒等于重装值（"重装值 - 剩余"结构上不可能下溢），
  wrap 靠 COUNTFLAG/CNTIF 读清零判别、ISR 与 idle 追平互斥入账整拍
  （消除部分拍与整拍的双重入账），rem==0 窗口特判；顺带消除全部 64 位
  软件除法（idle 关中断路径）
- **tickless 空闲路径消除 64 位软件除法**：`star_acc_flush_ms` 与 nap 上限
  计算改用编译期常量 `STAR_CYCLES_PER_MS`/`STAR_MAX_NAP_MS_CONST`
  （常数除数折叠为乘加移位），关中断路径不再有 `udivdi3` 量级的软件
  除法——旧实现连裸机链接都缺 `__aeabi_uldivmod` 符号；`star_idle`
  契约文案同步改写为与实现一致的口径。HCLK 非 1000 整数倍时 nap 向下
  取整——只早不迟，安全性优先
- **青稞 port 支持 V3 代 SDK**：`STAR_CH32_HAL_HEADER` 宏选择器件头
  （默认 `ch32v00x.h`；CH32V203/V307 用 `-DSTAR_CH32_HAL_HEADER=<ch32v20x.h>`
  等覆盖），同时新增 `STAR_PORT_CH32` 体系标签；tickless 编译新增平台
  标签守卫（缺失即 `#error`，不再静默落入青稞分支）
- **体积断言分账化**：CI 对内核三件套（star.o/star_task.o/star_mail.o）与
  移植层 `star_port.o` 分别设限（port text <512B、总 RAM <512B），移植层
  不再游离在断言之外；README/测试文档体积口径同步改写
- **README 措辞**："零汇编"改为"无独立汇编文件（临界区/休眠为内联汇编）"
- **邮箱 recv 长度域运行时校验（代码审查整改）**：手工构造邮箱的每槽长度
  域（`lens`）被写坏为 0 或 >`item_size` 时，`star_mail_recv` 此前会按该
  长度拷贝、最多越界读 255 字节；现在与结构字段校验同口径，写坏即返回
  -1 而不是崩溃，`star.h` 契约文案同步
- **QEMU 冒烟启动代码补 .data 拷贝（代码审查整改）**：`startup.c` 此前只
  清零 .bss、不拷贝 .data——任何带初值的全局变量会静默取到错误值（当前
  冒烟恰好没有 .data 而未暴露）；现在按链接脚本的 `_sdata/_edata/_etext`
  完整拷贝
- **CMake 对 MSVC 生成器给出明确指引（代码审查整改）**：宿主机测试依赖
  C99 指定初始化器与 GCC 旗标（`-Wall/-Wextra/-Werror`、`-include`），
  MSVC 下配置报 D8021 晦涩错误；现在配置期直接报错并提示 Windows 使用
  MinGW 生成器
- **仓库卫生（代码审查整改）**：`.gitignore` 构建目录匹配改 `build*/`、
  新增 `*.exe`，忽略文档站依赖与站点输出（`stardustos_docs/node_modules/`、
  `stardustos_docs/site/`）；恢复 `.github/workflows/build.yml` 中 CH32V003
  SDK 钉 SHA 步骤注释行的乱码（按 git 历史还原原文）

### 测试

- `test_timer`：`test_one_shot_survives_full_queue` 新增 RETRY 不计丢弃断言
- `test_mail`：新增 `test_hook_reenter_same_mailbox`（钩子重入同一邮箱，
  验证无残留、计数一致）；`test_invalid_mailbox_rejected` 新增槽长度域
  写坏（0 / >item_size）必须返回 -1 的断言
- **QEMU tickless 冒烟**（`tests/qemu/cm3/main_tickless.c`）：tickless 空闲
  路径首次被实际执行。QEMU 的 WFI 模型在"重装 SysTick 后立即 wfi"时产生
  伪唤醒（ptimer 重载事件唤醒被 halt 的 vCPU），深睡退化为微秒级轮询；
  故采用**时间膨胀**（`STAR_PORT_HCLK_HZ=24000`，1 tick-ms = 24 计数器
  周期，入账数学与换算率无关），1000ms 周期定时器 3 拍后 `star_ticks()`
  落在 [3000, 4000]（下界抓入账跑快；上界为 QEMU SysTick ptimer 在
  wrap 后"计数器 0 等待重载"状态被 TCG 批拉长导致的时间滞后留余量——
  仿真模型行为，非内核缺陷），丢事件计数必须为 0；CI `qemu-smoke`
  新增该档。该冒烟已实抓旧入账协议的下溢 bug（见上）——tickless 路径
  不再零执行覆盖。局限：深睡（wfi 长保持）与 wrap 标志路径在 QEMU 下
  无法验证，仍需板级实测

### 文档

- **教程与实现逐项核对（代码审查整改）**：`docs/porting.md` / `docs/usage.md`
  / `docs/test.md` 全面校对——
  - porting 第 3 章修正误导："自写 `star_idle` 省电"实为**强符号**，留用
    `star_port.c` 时再写同名函数会链接冲突；默认实现本就是 wfi
  - porting 第 4 章章节编号修复（两节重复编号 4.3），补 4.5 断言失败
    处理（`star_assert_fail` 弱符号）；临界区模板补 `STAR_WEAK` 与平台
    标签说明（沿用 `star_port.c` tickless 参考实现的必要条件）
  - porting 0.3 补 WCH V3 代 SDK 指引：CH32V203/V307 需工程级
    `-DSTAR_CH32_HAL_HEADER=<ch32v20x.h>/<ch32v30x.h>`，并上板核验
    `STAR_CH32_INTSYSCR`；Q7 补 `STAR_TICK_MS` 1..1000 编译期校验
  - usage 铁律 3 表补可观测 API（`star_ticks`/`star_dropped_count`/
    `star_set_drop_hook` 任意上下文可用）；Q5 补"未注册 ID 的丢弃计入
    计数并触发钩子"；邮箱注意项补长度域写坏拒绝；排查表补"任务不执行"
  - test 实测数字刷新：断言数 4843/8452（邮箱长度域校验 +2）、覆盖率
    93.7%（star_mail.c 67 行/64 覆盖）、体积注明随工具链版本浮动
    （gcc 14.3.1 实测三件套 2343B，CI 阈值 <2560 为准）
  - `stardustos/port/star_port_template.c` 注释补 `STAR_WEAK`/平台标签说明，
    并明确临界区 API 属于 `star_port.h` 侧；文档站源副本已同步并重新构建

## v0.5.0 - 2026-08-13（开发预览，破坏性变更）

按技术评审整改：定时器排序、deadline 感知睡眠 + tickless、校验口径统一、
延时事件 API 补齐、邮箱运行时校验。

### 破坏性变更

- **`star_idle()` 签名变更**：`void star_idle(void)` → `void star_idle(uint32_t next_due)`
  （内核传入下一到期节拍，`STAR_TICK_NONE` = 无到期项）。固定拍移植忽略入参即可；
  自写 port 需同步改签名（参考 `port/star_port.c` 与移植模板）
- **`STAR_MAILBOX_DEF` 新增编译期约束**：`item_bytes ≤ 255`（与每槽 uint8_t
  长度记录自洽），超限直接编译报错
- **单次 RETRY 定时器重试节奏**：满队失败由"下一次 poll 重试"改为"下一拍重试"
  （due 后移一拍重排）；"至少一次送达"语义不变，最坏晚 1 拍

### 新增

- **deadline 感知睡眠 + tickless**（评审"假低功耗"）：
  - `star_next_due()`：内核已知的下一到期节拍（定时器表头 + 延时槽）
  - `star_tick_advance(ms)`：可变步长时基推进（tickless 必需）
  - `STAR_TICKLESS=1` + `STAR_PORT_HCLK_HZ`：空闲时按下一 deadline 重装
    SysTick 再 wfi，唤醒后恢复固定拍；进入 idle 先追平提前唤醒已流逝时基
    （周期余数累计，零漂移）；Cortex-M（24 位裸寄存器访问）与青稞（64 位
    比较寄存器）均已实现，nap 上限受计数器位宽与 2^31 回绕数学共同约束
  - 三份例程启用 tickless（宏须工程级全局定义，已注释说明）
- **延时事件 API 补齐**（评审"API 残缺"）：
  - `star_event_post_delayed_replace()`：同 evt 只留最新在路上（不占新槽）
  - `star_event_cancel_delayed()`：取消未到期的延时投递（evt+param 匹配）
- **定时器排序链表**（评审"O(n) 扫描"）：链表按 due 升序，到期扫描只遍历
  到期节点（poll 空转 O(1)）；周期定时器相位推进/单次 RETRY 重试后按新
  due 重排；`star_timer_restart` 变更 due 后自动重排

### 修复

- **ch32v 临界区恢复的操作数编号 bug（P0，板级构建实抓）**：
  `star_crit_exit` 的 `csrw %[csr], %0` 把 `%0` 指到了 CSR 立即数操作数
  （`[csr] "i"` 是第一个输入），保存的中断状态 `s` 被静默丢弃——旧代码
  在 MounRiver 的 WCH 汇编器上报 "Improper CSRxI immediate (2048)"，
  而 xpack 上游汇编器放行并静默生成错误代码（临界区退出后中断状态
  永远无法恢复）。修复：`s` 移到第一个操作数位置；已在 WCH gcc
  8.2/12.2/15.2 三种工具链实测编译通过。此 bug 由真实 MounRiver
  工程构建暴露——正是"未经板级验证"类风险的一次实锤
- **timer policy 运行时校验**（评审"校验口径"）：policy 越界返回
  `STAR_ERR_PARAM`（此前仅靠可关闭的 STAR_ASSERT，生产构建静默 fallback）
- **邮箱运行时校验**：手工构造的 `star_mail_t` 若 `slots==0`（除零）、
  `buf/lens` 为空指针、`item_size` 越界、head/count 越界，send/recv 一律
  返回 `STAR_ERR_PARAM` / -1 而不是崩溃
- 删除死 typedef `star_evt_id_t`

### 测试

- `test_timer` 新增：排序触发顺序、restart 重排、policy 越界、
  `star_next_due`（空/多定时器/延时槽/混合）、延时 replace/cancel、
  `star_sleep` 睡眠判定（宿主机 idle 观测：无到期项/未来 deadline 睡眠、
  已过期/队列非空不睡）
- `test_mail` 新增：非法构造邮箱运行时拒绝
- 既有用例适配 RETRY 下一拍重试语义

### 体积

- 评审整改（deadline 感知睡眠/延时 API/运行时校验）使内核三件套
  RV32 实测 2004B → 2740B：延时槽扫描共用一段临界区、next_due 拆
  锁内/锁外版本等微优化后仍超旧阈值，**RV32 体积断言由 <2560 上调至
  <2816（2.75KB），M0+ 维持 <2560（实测 2238B）**；README/porting/
  test 文档数字同步刷新（2.7KB 内核对 16KB Flash 的 CH32V003 仍仅占 17%）

### CI

- 新增三档 tickless 交叉编译（M0+/M3/RV32，`STAR_TICKLESS=1` +
  `STAR_PORT_HCLK_HZ`）；真实 SDK 例程编译步骤补全局 `-D`（例程已启用
  tickless，port 层必须拿到同值）

### 文档

- porting.md：star_idle 新契约、tickless 参考实现协议与**板级验证清单**
  （HCLK 换算、提前唤醒追平、计数器位宽、青稞 WFI 交互）
- usage.md：延时 replace/cancel 用法、定时器排序说明、RETRY 节奏、
  邮箱 item_size≤255；Q7 更新（定时器不再全表扫描）
- README/README_EN：模块表（低功耗/tickless/定时器/邮箱）、配置示例、
  tickless 全局定义警告

## v0.4.2 - 2026-08-13（开发预览）

### 修复

- **邮箱变长契约（P0 数据错误）**：此前 `star_mail_send` 接受小于格子的
  `len` 且静默截断超长，而 `star_mail_recv` 永远回吐整格 `item_size`——
  槽内残留垃圾会被当数据送出（串口逐字节回环每收 1 字节发 32 字节垃圾）。
  现在每槽额外 1 字节记录实际存入长度：`send` 要求 `len ∈ 1..item_size`
  （超长/为 0 返回 `STAR_ERR_PARAM`，不再静默截断），`recv` 返回实际存入
  字节数。三份例程同步改为 32 槽 × 1 字节的逐字节邮箱
- **定时器链表操作进临界区**（P1）：`star_timer_start_ex` 的链表头插入与
  全部字段写入、`star_timer_unlink` 的摘链统一包临界区——定时器 API
  误用于中断上下文时不再腐坏链表（契约仍为主循环专用，此为纵深防御）
- **环形索引去取模**（P2）：事件队列 push/pop/replace 扫描与邮箱索引由
  `% SIZE` 改为条件减法——任意队列/槽数都不再引入 `__aeabi_udiv` 等
  libgcc 除法依赖（M0+ 无硬件除法），与"零隐藏依赖"口径一致
- **STAR_ASSERT 默认开启**（P2）：默认实现回调弱符号 `star_assert_fail()`
  （目标机停机、宿主机 abort，可重定义为复位/记录），生产构建可显式
  定义 `-DSTAR_ASSERT(x)=((void)0)` 关闭；移植模板补充该移植步骤
- **青稞 INTSYSCR 编号可配置**（P1）：CSR 编号收敛为 `STAR_CH32_INTSYSCR`
  宏（默认 0x800，可按代次 `-D` 覆盖），并注明 V2/V3 代次差异须按手册
  上板核验

### 文档

- README/README_EN：删除"零汇编"措辞（改为"内核无汇编源文件，仍需厂商
  启动文件"）；满队策略表补明"周期定时器满队丢当次、仅单次定时器重试至
  送达"；邮箱模块说明变长契约
- usage.md 第 4 章/附录 A、porting.md 5.4/Q2 同步变长契约与逐字节邮箱用法

### 测试

- `test_mail`：新增变长往返用例、超长/零长拒绝用例（替换原"静默截断"用例），
  满队/回滚用例断言改实际存入长度
- `test_interleave`：邮箱总账等式改按实际字节数核算

## v0.4.1 - 2026-08-13（开发预览）

### 修复

- **例程违反铁律 1**（P1）：三份例程的 UART 回环由"等 TC（传输完成，
  32 字节阻塞约 2.8ms）"改为"等 TXE（数据寄存器空，0~1 字节时间）"，
  并加注释指向"环形缓冲 + 发送中断"的正确姿势；使用教程新增
  「附录 B：非阻塞串口发送」正反例对照
- **任务层周期校验对齐定时器**（P1 漏网）：`star_task_start` 对
  `period_ms==0 / ≥2^31` 运行时返回 `STAR_ERR_PARAM`（此前仅靠默认关闭的
  断言，0 周期会退化为每 poll 同步调用、与 spin 无异）；
  `star_event_post_delayed` 补拒绝 `ms==0`（口径与定时器统一）
- `star_set_drop_hook` 赋值包临界区（钩子可能在中断上下文被读取/调用），
  契约成文（推荐启动时调用一次）
- **SysTick_Handler 改弱符号**（`STAR_WEAK`，兼容 GCC/ArmClang/armcc）：
  已有 SysTick 的工程直接重定义强符号即可接管，无需剔除 star_port.c；
  移植教程第 3 章与报错表同步简化

### 测试 / CI

- **交错测试注入窗口扩展**：`star_mail_send`（入临界区前，覆盖入箱回滚
  竞态）、`star_poll`（单步前）、`star_process_timers`（定时器列表遍历中）；
  伪中断新增 `star_tick` 模拟 + 周期定时器，让 process_timers 窗口被真实
  驱动（总账等式扩展为"显式尝试 + 定时器触发 = 派发 + 丢弃"，并断言
  定时器触发数显著大于 0）
- **QEMU 冒烟测试**（`tests/qemu/cm3`，CI 新 job）：stm32vldiscovery
  （Cortex-M3）上验证启动、向量表、SysTick、tick→定时器→事件流；
  semihosting 打印 QEMU_PASS/QEMU_FAIL + CI 关键字判定（退出码跨平台
  透传不可靠，不依赖）；向量表断掉则死循环被 timeout 判失败
- **CI 新增三 job**：`qemu-smoke`、`coverage`（gcovr，行覆盖 ≥85% 门槛）、
  `cppcheck`（warning/performance/portability，error 即失败）
- 新增 `test_task_ms_bound`；`test_ms_bound` 补 `ms==0` 用例

## v0.4.0 - 2026-08-13（开发预览）

> **版本回退说明**：v1.0.0/v1.0.1 标签已撤销——"生产就绪"宣称撤回：
> 本内核从未在任何真实芯片上运行，无板级实测数据。回到 v0.x 开发预览，
> 以 README「项目状态」为准。原 v1.0.x 的修复内容全部保留（并入本版）。

### 修复

- **周期定时器相位漂移**（P0）：到期推进由 `due = now + period` 改为
  `due += period`（绝对相位 + 错过拍合并追赶）；落后超过
  `STAR_TIMER_CATCHUP_MAX`（默认 1000 拍）放弃旧相位重新对齐。
  任务层同修。handler/主循环延迟不再逐周期累积漂移
- **s_tick 访问统一走临界区**（P0）：`star_tick` 自带临界区；
  `star_timer_start_ex`/`star_timer_restart` 的时基读取包临界区
  （此前 M0+ 上存在 32 位撕裂风险）；`star_process_timers` 改用
  `star_ticks()` 单一快照，消除"检查与重排两次裸读"
- **ms 时长上限改为运行时校验**（P0）：`star_timer_start*`/
  `star_timer_restart`/`star_event_post_delayed` 对 `ms ≥ 2^31` 返回
  `STAR_ERR_PARAM`。此前仅靠默认关闭的 `STAR_ASSERT`，生产构建静默失效
- 移除 `star_poll` 中与"未注册事件安全丢弃"设计相矛盾的断言
  （该路径是受支持的运行时行为，非内部不变量；断言开启构建下
  恶意/越界事件也必须走到丢弃路径）

### CI / 测试

- **断言开启构建**（P1）：新目标 `test_stardustos_assert` 强制
  `-include tests/star_assert.h`，内核断言路径首次被真实编译并运行
- **最坏配置构建**（P1）：新目标 `test_stardustos_max`（队列 255 / 延时槽 16 /
  任务槽 16）宿主机跑全套测试 + 多种子交错；交叉编译 job 增加
  M0+/RV32 的 `-DSTAR_EVT_QUEUE_SIZE=255` 构建
- 新增回归测试：定时器/任务层相位漂移、ms 上限运行时校验
- 修正 `test_task_slot_pool` 对槽数配置的硬编码假设

### 文档

- README（中英）：新增「项目状态」——明示 v0.x 开发预览、未经板级验证，
  撤销"生产就绪"与"微秒级临界区"等未经实测的宣称；资源占用表区分
  CI 断言与手动实测，并提示 712B 占 2KB RAM 的 35%
- 使用教程：新增「附录 A：中断延迟预算」（临界区时长估算公式 +
  DWT CYCCNT / GPIO 示波器两套实测方法，明确标注为估算值）；
  新增 3.6 周期相位稳定语义；任务层文档明确"不是抢占式 RTOS 任务"
- 移植教程：检查清单新增中断延迟/低功耗唤醒/周期相位三项板级验证项；
  注明青稞 WFI 与 INTSYSCR 的交互需板级确认；修正 Q10 的"实测经验值"表述
- 交错测试定位澄清：验证内核对建模并发语义（临界区内不抢占）的一致性，
  不构成硬件验证

### 保留自原 v1.0.x 的修复

- Cortex-M 移植层去 CMSIS 依赖（裸汇编临界区，兼容 GCC/ArmClang/armcc）；
  STM32F103 例程改用 CMSIS 器件头，CI 双仓库钉版本编译
- 配置边界编译期 `#error` 块；WFI 竞态处理与 `star_idle` 契约成文；
  `star_init` 契约成文

## ~~v1.0.1~~ - 2026-08-13（已撤销，内容并入 v0.4.0）

### 修复

- Cortex-M 移植层（cm0plus/cm3）不再依赖 CMSIS 头文件：临界区改为裸内联汇编
  （`MRS PRIMASK` / `CPSID i` / `MSR`，兼容 GCC / ArmClang / armcc）。
  修复内核在真实工程中因 `core_cm3.h` 依赖器件头定义 `IRQn_Type`
  而无法独立编译的问题，包含顺序不再有任何要求
- STM32F103 例程改用 CMSIS 器件头 `stm32f1xx.h`（补器件选择宏），
  引脚宏改为位字面量，兼容 SPL 与 Cube 两套头文件
- CI：STM32F103 例程真实头文件编译改为双仓库钉版本
  （CMSIS_5 的 `CMSIS/Core/Include` + STMicroelectronics/cmsis-device-f1）

## ~~v1.0.0~~ - 2026-08-13（已撤销，内容并入 v0.4.0）

> 原"首个生产就绪版本"宣称已撤回：无板级验证支撑。修复内容本身有效，
> 详见 v0.4.0「保留自原 v1.0.x 的修复」。

### 本版修复（最终审查收尾）

- **P1 配置边界校验**：`star_config.h` 增加编译期 `#error` 块（`STAR_TICK_MS` 1~1000、
  `STAR_EVT_QUEUE_SIZE` 1~255、`STAR_TASK_SLOT_MAX` 1~255），杜绝 uint8_t 计数回绕/
  除零/死循环类静默内存损坏；`STAR_MAILBOX_DEF` 增加槽数与格大小的编译期检查
- **P2 WFI 竞态**：睡眠改为"关中断 → 查空 → wfi → 恢复中断"的临界区模式，
  消除"查空与睡眠之间投递事件导致漏睡"的竞态；`star_idle()` 契约成文
  （关中断上下文、必须 wfi 级、深度睡眠不支持）
- **P2 时长边界**：定时器/延时投递增加 `ms < 2^31`（约 24.8 天）断言与文档说明
- **P2 star_tick 契约成文**：仅允许单一 tick 中断源调用
- **P2 任务周期校验**：`period_ms = 0` 增加断言（避免退化为每 poll 触发）
- **P3 star_init 契约成文**：仅启动时调用一次，不重置钩子与注入点
- **P3 STM32F103 例程进入 CI**：真实 CMSIS 头文件（钉 ARM-software/CMSIS_5 提交）编译

## v0.3.3 - 2026-08-13

### 修复

- 邮箱拷贝写侧改为字节存储：消除严格别名 UB（此前 `*(uint32_t *)d = w` 强转写入，字面上违反 C 标准）
- `STAR_TEST_INJECT` 宏改为 do-while 形式，`-pedantic` 兼容
- CI：WCH SDK 改为单次 fetch（git init + fetch SHA），省一半网络时间

### 其他

- README（中英）新增 tag 版本徽章

## v0.3.2 - 2026-08-13

### 修复与加固

- `star_event_post_delayed` 禁用分支的丢弃计数包临界区（契约统一：note_dropped 调用方持临界区）
- 邮箱拷贝由字节循环升级为对齐感知的 32 位字拷贝（头尾字节处理非对齐），临界区内延迟常数因子显著下降
- `star_timer_start_ex` 增加 policy 范围断言
- drop hook 文档收紧：仅允许事件/邮箱类 API，禁止定时器/任务 API
- CI：WCH SDK 钉 commit（2ac6803）；宿主机与 ASan 任务各跑 1+20 / 1+5 个随机种子

### 测试

- 交错测试支持内核内部注入点（`STAR_TEST_INJECT_ENABLE`，发布构建零开销）与多种子轨迹

## v0.3.1 - 2026-08-13

### 修复

- CI RISC-V 工具链问题：新版 GCC 需显式 `zicsr` 扩展（`-march=rv32imc_zicsr`）
- 内核移除 libc 依赖（邮箱拷贝改为内置字节循环），裸工具链/无 newlib 环境可编译
- CI 改用 xpack riscv-none-elf-gcc（自带 newlib），支持真实 SDK 例程编译

## v0.3.0 - 2026-08-13

### 新增

- 定时器满队策略：`star_timer_start_ex` + `STAR_TIMER_POLICY_RETRY / DROP / LATEST`
  （至少一次送达 / 严格截止 / replace 语义只留最新）
  注：`star_timer_start` 默认策略为单次=RETRY、周期=DROP（与旧行为等价，兼容）
- 交错测试骨架：随机交错主循环与伪中断操作，验证并发一致性（投递账目、邮箱无滞留、临界区不泄漏）
- CI：真实 WCH SDK（openwch/ch32v003）编译 CH32V003 例程；体积断言收紧为 text <2.5KB / RAM <512B（-Os 实测）
- drop hook 防重入保护

### 修复

- `star_event_post_delayed` 在 `STAR_DELAYED_MAX==0` 时漏记丢弃（口径统一）

### 文档

- 使用教程：定时器三种策略与选型速查表
- README：资源占用改为实测数据（内核 RV32EC 2.0KB / M0+ 1.2KB / RAM 280B；完整点灯工程 2.7KB/712B）

## v0.2.0 - 2026-08-13

### 破坏性变更

- `STAR_TASK_DEF` 由 2 参数改为 3 参数（新增 `ctx`），`star_task_desc_t` 增加 `ctx` 字段
- 临界区接口由宏（`STAR_ENTER_CRITICAL` / `STAR_EXIT_CRITICAL`）改为函数（`star_crit_enter` / `star_crit_exit` / `star_crit_active`），采用保存/恢复语义；自定义移植层需按新接口实现

### 新增

- `star_dropped_count()`：统一丢事件计数（post / replace / delayed / 邮箱拒绝 / 未注册事件全部计入）
- `star_set_drop_hook()`：丢事件回调（关中断上下文调用）
- 单次定时器"至少一次送达"：队列满时保留重试，事件绝不蒸发
- 任务层支持每任务 `ctx` 上下文
- CI 三档：`-Werror` 宿主机测试、ASan/UBSan、ARM（M0+/M3）与 RISC-V 交叉编译 + 内核体积断言

### 修复

- 临界区改为保存/恢复式（Cortex-M 用 PRIMASK，WCH RISC-V 用 INTSYSCR），支持嵌套、不破坏调用方中断状态
- `star_ticks()` 原子读（M0+ 上 32 位读非原子）
- 邮箱 send 原子化：入箱与事件入队在同一临界区内完成，回滚无竞态窗口
- `STAR_P` / `STAR_U32` 改用 `uintptr_t`，消除 64 位宿主机指针转换告警

### 文档

- 移植教程：临界区新接口与三内核实现对照
- 使用教程：定时器超时语义警告、邮箱中断延迟预算、丢事件监控
- README：中断延迟口径修正、模块表同步

## v0.1.0 - 2026-08-13

- 首个版本：事件队列（post / post_replace / post_delayed）、注册表 O(1) 派发、定时器、任务层、邮箱、低功耗、四套移植层（ch32v / cm0plus / cm3 / host）、移植与使用教程、PC 单元测试
