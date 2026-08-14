# StardustOS 测试文档

> 本文汇总 StardustOS 的全部验证手段、**实测结果明细**与结论。
> 测试代码在 `tests/`，CI 定义在 `.github/workflows/build.yml`。
> 所有数字均来自仓库当前代码的本地实测（MinGW GCC 16.1 / CMake），非估计值。

---

## 测试矩阵

| # | 层 | 手段 | 验证什么 | CI job |
|---|---|---|---|---|
| 1 | 逻辑 | 宿主机单元测试 | 事件队列/定时器/任务/邮箱的 API 语义 | host-tests |
| 2 | 防御 | 断言开启构建 | `STAR_ASSERT` 路径被真实编译并运行 | host-tests（ctest 内置） |
| 3 | 边界 | 最坏配置构建（队列 255/延时 16/任务槽 16） | 极端配置下的正确性 | host-tests |
| 4 | 并发 | 交错测试（多种子，含 ASan/UBSan） | 对"建模并发语义"的一致性 + 内存安全（见局限声明） | host-tests / sanitizers |
| 5 | 可编译性 | SDCC 编译验证（8051，`--Werror`） | 8051 移植（内核 + STC89C52 例程）可编译 | sdcc-compile |
| 6 | 可编译性 | Keil C51/C251 本地编译 | 8051/80251 移植 0 警告 0 错误（C251 仅无害 C174） | 本地（商业软件，不进 CI） |
| 7 | 覆盖 | gcovr 行覆盖 ≥85% 门槛 | 测试没有大面积盲区 | coverage |
| 8 | 静态 | cppcheck（host 配置） | warning/performance/portability | cppcheck |

---

## 一、各测试手段说明

### 1. 宿主机单元测试（`tests/test_*.c`）

内核是纯逻辑，全部在 PC 上跑（`stardustos/port/host` 用共享变量模拟中断开关，
临界区是否破坏调用方中断状态可在宿主机直接断言）。CMake 定义三个目标，
均带 `-Wall -Wextra -Werror` 与 `STAR_TEST_INJECT_ENABLE=1`（见第 4 节）：

- `test_stardustos`：常规配置（默认：队列 16 / 延时槽 4 / 任务槽 4）
- `test_stardustos_assert`：断言开启构建（见第 2 节）
- `test_stardustos_max`：最坏配置构建（见第 3 节）

| 套件 | 覆盖点 |
|---|---|
| `suite_queue` | post/派发、满队报错、replace 覆盖、满队 replace、越界 ID 安全丢弃、空 handler 丢弃、丢弃计数、drop hook（含重入）、临界区嵌套 |
| `suite_timer` | 单次/周期、handler 内自停、restart（含重排）、tick 回绕、延时投递（含 replace/cancel）、满队三策略（RETRY/DROP/LATEST）、相位稳定无漂移、ms 边界运行时校验、policy 越界运行时校验、排序链表触发顺序、`star_next_due` deadline 计算、`star_sleep` 睡眠判定（宿主机 idle 观测） |
| `suite_task` | 周期触发、停止、槽池（随配置伸缩）、ctx 透传、相位无漂移、period_ms 边界校验 |
| `suite_mail` | 收发往返、满箱、空箱、变长收发、超长拒绝、满队整体失败不滞留（先入队后入箱，全有或全无）、钩子重入同一邮箱、非法构造运行时拒绝、槽长度域写坏运行时拒绝 |
| `suite_interleave` | 见第 4 节 |

**内存消毒（ASan/UBSan，CI：`sanitizers`）**：用
`-fsanitize=address,undefined -fno-omit-frame-pointer` 构建同一套测试并全跑
（含 5 个种子交错），捕获越界访问、使用未初始化内存、有符号溢出等未定义
行为。本地 MinGW 工具链不带 libasan/libubsan 运行时，仅由 CI（Ubuntu）执行。

### 2. 断言开启构建（`test_stardustos_assert`）

`star_config.h` 里 `STAR_ASSERT` 默认开启——回调弱符号 `star_assert_fail`
（`star_port.c` 的宿主机实现为 abort，8051/251 实现为停机死循环）。该目标用
`-include tests/star_assert.h` 强制在 `star_config.h` **之前**生效，
把 `STAR_ASSERT` 替换为"打印文件行号 + abort"版本，让失败现场一眼可读：

- 内核所有断言路径被真实编译进测试二进制并运行
- 测试全程触发任何断言 → 立即失败（CTest 报红）

### 3. 最坏配置构建（`test_stardustos_max`）

```
STAR_EVT_QUEUE_SIZE = 255   （replace 全扫描的最长路径、大队列回绕）
STAR_DELAYED_MAX    = 16
STAR_TASK_SLOT_MAX  = 16
```

跑全套单元测试 + 交错测试（多种子），验证极端配置下编译与并发语义正确性。

### 4. 交错测试（`tests/test_interleave.c`）

**设计**：单线程内用伪随机序列交错执行"主循环操作"与"伪中断操作"。
伪中断遵守建模硬件规则——临界区内不执行；同时模拟 tick 中断
（`star_tick`）驱动一个周期定时器，真实驱动 `star_process_timers` 路径。

**注入窗口**（`STAR_TEST_INJECT_ENABLE`，发布构建零开销，见 `star.h`）：

| 窗口 | 位置 | 覆盖的竞态 |
|---|---|---|
| `star_event_post` / `post_replace` | 临界区前后 | 入队与队列操作的交错 |
| `star_event_post_delayed` | 投递路径 | 延时槽池竞争 |
| `star_mail_send` | 入临界区前 | **先入队后入箱**顺序与失败原子性的竞态 |
| `star_poll` | 单步前 | 派发与投递的交错 |
| `star_process_timers` | 定时器列表遍历中 | 定时器派发与投递的交错 |

**总账等式**（每次运行必须精确成立）：

```
显式投递尝试 + 定时器触发 = 最终派发数 + 丢弃计数
且：定时器触发数 > 50（证明 tick 真的驱动了定时器）
邮箱无滞留（末态 recv 返回 -1）
派发的普通事件数与邮箱出货量对得上
每步之后临界区不泄漏（star_crit_active() == 0）
```

**种子**：默认固定种子（0x12345678）；环境变量 `STAR_TEST_SEED` 可换轨迹。
CI 分别以 1..20（常规）、1..10（最坏配置）、1..5（sanitizers）多随机种子跑。

**局限声明（重要）**：交错测试验证的是内核对**测试自行定义的建模语义**
（"临界区内 ISR 不执行"是测试假设的硬件行为）的一致性。它能证明
"内核符合我假设的模型"，证明不了"内核符合真实硬件"——后者需要板级验证。

### 5. SDCC 编译验证（CI：`sdcc-compile`）

SDCC 是开源 8051 编译器，可在 Linux CI 上验证 8051 移植的编译正确性。
编译内核四个源文件 + STC89C52 例程，全部 `--Werror`（警告即失败）：

```bash
# 内核 4 文件（star.c / star_task.c / star_mail.c / port/star_port.c）
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o build-sdcc/star.rel stardustos/star.c
# ... 其余三个文件同法，输出 star_task.rel / star_mail.rel / star_port.rel

# STC89C52 例程（SDCC 自带 <8051.h>，CI 可直接编译）
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o build-sdcc/stc89c52_main.rel examples/stc89c52/main.c
```

- 只编译到 `.rel` 目标文件，不链接——验证的是"8051 移植可编译"，不是"可运行"
- STC8H / STC32G 例程需要 STC-ISP 生成的器件头文件，CI 无法获取，
  由本地 Keil 编译验证（见下节）

### 6. Keil C51/C251 本地编译（Windows 商业软件，不进 CI）

Keil C51（8051）/ C251（80251）是 Windows 商业软件，无法进入 Linux CI，
由本地命令行编译验证（对应 `docs/porting.md` 的移植检查项）：

```bat
rem 内核 4 文件（C51；C251 同理换 C251.EXE）
C51.EXE star.c      INCDIR("D:\...\stardustos","D:\...\stardustos\port\8051") OBJECT(build\star.obj)
C51.EXE star_task.c INCDIR("D:\...\stardustos","D:\...\stardustos\port\8051") OBJECT(build\star_task.obj)
C51.EXE star_mail.c INCDIR("D:\...\stardustos","D:\...\stardustos\port\8051") OBJECT(build\star_mail.obj)
C51.EXE star_port.c INCDIR("D:\...\stardustos","D:\...\stardustos\port\8051") OBJECT(build\star_port.obj)
rem STC89C52 例程（reg52.h 为 C51 自带；例程要求 -DSTAR_PORT_NO_TICK_ISR）
C51.EXE main.c INCDIR("D:\...\stardustos","D:\...\stardustos\port\8051","D:\...\examples\stc89c52") OBJECT(build\main.obj)
```

- `INCDIR(...)` 指定包含搜索路径（逗号分隔多个），`OBJECT(...)` 指定输出目标文件
- 实测结果：**C51 内核 0 警告 0 错误**、C51 编译 STC89C52 例程通过；
  **C251 内核 0 错误**（仅有无害的 C174——未引用 static 函数的提示）

### 7. 覆盖率（gcovr）

`--coverage -O0` 构建三个测试目标全跑后统计；CI 以 `--fail-under-line 85`
守护，覆盖回归直接判红。gcovr 8.x 对多目标合并更严格（断言构建的
`-include` 会平移行号），CI 命令已加 `--merge-mode-functions=separate` 避免误判。

### 8. 静态分析（cppcheck）

```
cppcheck --enable=warning,performance,portability --std=c99 \
         --inline-suppr --error-exitcode=1 \
         -Istardustos -Istardustos/port/host -Itests \
         stardustos/star.c stardustos/star_task.c stardustos/star_mail.c \
         stardustos/port/star_port.c tests
```

任何 error 即失败。**只做 host 配置**：8051/251 port 头含 `sfr`/`sbit` 等
Keil 扩展，cppcheck 无法解析，故通过 `-Istardustos/port/host` 让内核按
host 配置展开后分析。

---

## 二、测试结果明细（本地实测）

### 2.1 宿主机单元测试

| 构建 | 断言数 | 失败 | 结果 |
|---|---|---|---|
| `test_stardustos`（默认配置） | 4843 | 0 | ALL PASSED |
| `test_stardustos_assert`（断言开启） | 4843 | 0 | ALL PASSED（全程未触发任何内核断言） |
| `test_stardustos_max`（队列 255 等最坏配置） | 8452 | 0 | ALL PASSED |

### 2.2 交错测试多种子

| 构建 | 种子范围 | 结果 |
|---|---|---|
| 常规（本地 MinGW） | 1~20 | 20/20 通过 |
| 最坏配置（本地 MinGW） | 1~10 | 10/10 通过 |
| CI 常规 job | 1~20 | 通过（自动化） |
| CI 最坏配置 job | 1~10 | 通过（自动化） |
| CI ASan job | 1~5 | 通过（自动化，Ubuntu） |

> 本地 MinGW 工具链不带 libasan/libubsan 运行时，ASan/UBSan 仅由 CI
> （Ubuntu）执行——这是本地环境限制，不是测试缺口。

### 2.3 覆盖率（gcovr，`--coverage -O0`，三目标全跑）

```
stardustos/star.c               308 行  288 覆盖   93%   （未覆盖：STAR_DELAYED_MAX=0 分支、
                                                        tick 回绕保护路径、任务层关闭分支等）
stardustos/star_mail.c           64 行   61 覆盖   95%   （未覆盖：非对齐拷贝头/结构写坏分支）
stardustos/star_task.c           41 行   39 覆盖   95%
stardustos/port/host/star_port.h  9 行    9 覆盖  100%
stardustos/port/star_port.c       6 行    4 覆盖   66%   ← 仅宿主分支参与 gcovr；8051/251 分支
                                                        （Timer0 ISR / PCON IDL 空闲）由
                                                        SDCC/Keil 编译验证，运行时行为待板级实测
TOTAL                       428 行  401 覆盖   93.7%
```

```
lines:     93.7% (401/428)   ← CI 门槛 85%
functions: 95.6% (43/45)
branches:  84.1% (222/264)
```

### 2.4 静态分析（cppcheck）

```
10/10 files checked 100% done
0 告警，exit=0
```

### 2.5 交叉编译验证

- SDCC（`-mmcs51 -c --Werror`）：内核 4 文件 + STC89C52 例程全部编译通过
  （CI `sdcc-compile` job）
- Keil C51：内核 4 文件 + STC89C52 例程 0 警告 0 错误（本地）
- Keil C251：内核 4 文件 0 错误，仅无害 C174（未引用 static 函数）（本地）

> 以上全部是**编译验证**：8051/251 移植能通过各自编译器编译，但未链接、
> 未上板运行——运行时正确性不在验证范围内。

---

## 三、结论

### 3.1 每条宣称的支撑证据对照

| 宣称 | 证据 | 强度 |
|---|---|---|
| API 语义正确（队列/定时器/任务/邮箱） | 4843~8452 条断言全绿 | 强 |
| 周期定时器/任务无相位漂移 | 漂移回归测试（含迟到触发场景） | 强（逻辑层面） |
| ms/period/policy 边界运行时校验生效 | test_ms_bound / test_task_ms_bound / test_policy_invalid | 强 |
| deadline 计算与睡眠判定（next_due/sleep） | test_next_due / test_sleep_deadline | 强（逻辑层面） |
| 并发账目一致性（投递=派发+丢弃，邮箱无滞留，临界区不泄漏） | 交错测试多种子全绿 | 强（**对建模语义**） |
| 断言路径真实可编译、常规路径不触发断言 | test_stardustos_assert 全绿 | 中（无负向触发用例，见局限 6） |
| 内存安全/无 UB | ASan/UBSan（CI，Ubuntu） | 中（CI 环境，非板级） |
| 8051 移植可编译 | SDCC `--Werror` 内核 + STC89C52 例程 | 强（编译层面） |
| 8051/80251 移植可编译且无警告 | Keil C51/C251 本地编译 | 强（编译层面） |
| 覆盖无大面积盲区 | 行覆盖 93.7%（门槛 85%） | 中 |
| 无静态分析告警 | cppcheck 0 告警 | 中 |

### 3.2 不能下的结论（诚实边界）

1. **未板级验证**：内核尚未在任何真实芯片上运行过；中断时序、临界区
   实测时长、周期定时器相位漂移的实际值，宿主机测试全部测不出来
2. **8051 空闲唤醒未实测**：`star_idle` 的 PCON IDL 空闲依赖 tick 中断
   （ET0=1）唤醒——IDL 唤醒后是否按预期恢复、唤醒延迟多长，均需上板
   实测；未使能 tick 中断唤醒时可用 `STAR_PORT_IDLE_NOOP` 兜底空转
3. **编译验证 ≠ 运行验证**：SDCC/Keil 只证明移植代码可编译，未链接成
   固件、未上板；8051/251 的运行时行为（Timer0 ISR、空闲模式）无任何
   实测数据
4. **建模并发语义**：交错测试的伪中断规则由测试自行定义，证明不了
   "内核符合真实硬件"
5. **C251 的 C174**：未引用 static 函数提示为无害警告（0 错误），但
   说明 251 移植仍有可清理项
6. **断言负向路径**：没做过"故意触发断言"的测试（需进程级用例）

### 3.3 总体结论

- **内核逻辑是可信的**：API 语义、并发一致性（建模语义下）、边界校验、
  内存安全（CI）、可编译性（GCC/SDCC/Keil C51/C251）、覆盖率、静态
  分析，均有自动化或本地实测证据支撑——对一个 v0.x 开发预览项目，
  这已经是相当厚的验证层
- **硬件行为是未知的**：8051/251 的 Timer0 tick、PCON IDL 空闲唤醒、
  临界区真实时长等所有"真实世界"数字全部悬空。**任何生产决策都不应
  基于未经板级验证的行为**
- 定位与 README「项目状态」一致：值得作为**学习项目与内核逻辑参考**
  使用；"生产就绪"不成立，上板前必须按 `docs/porting.md` 的移植检查
  清单完成验证

---

## 四、CI 工作流（`.github/workflows/build.yml`，共 5 个 job）

| job | 内容 |
|---|---|
| host-tests | `-Werror` 构建 + ctest 三目标（常规/断言/最坏配置）+ 20/10 种子交错 |
| sanitizers | ASan/UBSan 构建 + ctest + 5 种子 |
| coverage | gcovr，行覆盖 <85% 判红（`--merge-mode-functions=separate`） |
| cppcheck | host 配置静态分析，error 即失败 |
| sdcc-compile | SDCC `-mmcs51 -c --Werror` 编译内核 4 文件 + STC89C52 例程 |

---

## 五、本地运行

```bash
# 全部测试（常规 + 断言开启 + 最坏配置三目标）
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure

# 交错测试多种子
STAR_TEST_SEED=1 ./build/test_stardustos           # Linux/macOS
$env:STAR_TEST_SEED=1; ./build/test_stardustos.exe # PowerShell

# 覆盖率
cmake -S . -B build-cov -G "MinGW Makefiles" -DCMAKE_C_FLAGS="-Werror --coverage -O0"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --exclude tests --exclude 'build.*' \
  --merge-mode-functions=separate --print-summary

# SDCC 编译验证（需 sdcc，apt install sdcc）
mkdir -p build-sdcc
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o build-sdcc/star.rel stardustos/star.c
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o build-sdcc/star_port.rel stardustos/port/star_port.c
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o build-sdcc/stc89c52_main.rel examples/stc89c52/main.c
# （star_task.c / star_mail.c 同法）

# Keil C51/C251 本地编译（Windows，见第一节第 6 条的命令行用法）

# 静态分析
cppcheck --enable=warning,performance,portability --std=c99 \
  --inline-suppr --error-exitcode=1 \
  -Istardustos -Istardustos/port/host -Itests \
  stardustos/star.c stardustos/star_task.c stardustos/star_mail.c \
  stardustos/port/star_port.c tests
```
