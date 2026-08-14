<p align="center">
  <img src="brand/stardustos-icon.svg" alt="StardustOS Logo" width="160">
</p>

<h1 align="center">StardustOS</h1>

<p align="center">
  <strong>An event-driven cooperative kernel for tiny MCUs</strong><br>
  No standalone assembly files (inline asm for critical sections/sleep) · No dynamic memory allocation · Resource usage fixed at compile time
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
    <img src="https://img.shields.io/badge/Docs-中文文档站-dd6b20?style=for-the-badge" alt="StardustOS Docs (Chinese)">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/stargazers">
    <img src="https://img.shields.io/github/stars/Lioyae/StardustOS?style=for-the-badge&color=d69e2e" alt="Stars">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/Lioyae/StardustOS?style=for-the-badge&color=38a169" alt="License">
  </a>
</p>

<p align="right">
  <a href="README.md">简体中文</a> | English
</p>

---

## About

StardustOS is a C99 event-driven cooperative kernel for small MCUs (2KB RAM / 16KB Flash class).

- No assembly source files in the kernel (port layer uses inline assembly; vendor startup files/vector tables are still required), no dynamic memory allocation, no blocking delay APIs
- All RAM/Flash usage is fixed at compile time; CI cross-compiles and asserts kernel size
- Supports 8051 (STC8H/8A, STC89C52, STC8051U/8052U, Keil C51), 80251 (STC32G, Keil C251), and host (x86); interrupt latency = tick interrupt + kernel critical sections (event enqueue O(1); mailbox copy proportional to item_size; post_replace proportional to queue length). Critical-section duration depends on configuration and clock, and **must be measured per platform** (estimation formulas and measurement methods in the [usage guide appendix A](docs/usage.md))

## Project Status (Important)

**Development preview (v0.x), not board-verified.**

- ✅ Verified: host unit/interleave tests (ASan/UBSan, multi-seed), assert-enabled build, worst-case config build (queue 255), **gcov coverage gate (≥85% lines)**, **cppcheck static analysis** — all automated by CI; **8051 (Keil C51) and 80251 (Keil C251) kernel compile cleanly with 0 warnings/0 errors** (C251 emits harmless C174 for unreferenced static functions)
- ❌ Not verified: the kernel has never run on real silicon. Interrupt timing, measured critical-section duration, 8051/251 idle-mode (PCON IDL) wakeup, and periodic-timer phase drift have no board-level measurements
- ⚠️ Before production use, complete the board-level verification per the [porting checklist](docs/porting.md). The v1.0.x "production ready" tags have been retracted (see [CHANGELOG](CHANGELOG.md))

## Supported Platforms

| Core | Compiler | Example chips |
|---|---|---|
| 8051 | Keil C51 | STC8H / STC8A / STC89C52 / STC8051U / STC8052U |
| 80251 | Keil C251 | STC32G |
| x86 (host) | GCC | Runs kernel unit tests on PC |

> Except for the host, all platforms above are **verified by cross-compilation only; never run on hardware**.

## Resource Usage

| Item | Usage |
|---|---|
| Kernel RAM (default) | ~280B (event queue 16 slots + delayed 4 + task slots 4), compile-time fixed; on 8051 large arrays default to `idata`, use `-DSTAR_RAM_CLASS=xdata` to move them to XRAM |
| Kernel Flash | Pure logic, compile-time fixed; on 8051/251 check linked size (Keil builds are not part of Linux CI) |
| Full blink example | See `examples/stc8h` / `stc89c52` / `stc32g` (requires STC-ISP generated device headers) |

## Modules

| Module | Description |
|---|---|
| Event queue | `star_event_post` / `star_event_post_replace` (latest wins per ID) / `star_event_post_delayed` (with `_replace` and `star_event_cancel_delayed`); drop counter `star_dropped_count()` |
| Dispatch table | C99 designated initializers; event ID is the index; O(1) dispatch; table lives in Flash |
| Timers | Statically defined; 32-bit wraparound safe; list sorted by due time so expiry scanning only visits due nodes (idle poll is O(1)); periodic timers fire on absolute phase (missed ticks coalesce, no cumulative drift); selectable full-queue policy: retry / drop (strict deadline) / latest (replace semantics) — note: **periodic timers drop the beat on a full queue and proceed next beat; one-shot RETRY timers retry on the next tick until delivered (retries do not count as drops or fire the drop hook)** |
| Task layer | Periodic-callback convenience layer: descriptors in Flash (handler + ctx + period), state slot pool in RAM; inactive tasks consume no RAM (optional). Note: **not RTOS tasks** — no preemption, handlers are called synchronously by the main loop, unrelated to the event queue |
| Mailbox | Static slots with deep copy; slot insert and event enqueue are atomic within one critical section (all-or-nothing, no race window); variable-length items (1..item_size bytes per slot with item_size≤255, `recv` returns the actual stored length, oversize sends are rejected — never truncated, +1 byte length overhead per slot); invalid constructions (slots==0, NULL buffers, etc.) are rejected at runtime (optional) |
| Low power | Deadline-aware: `star_next_due()` exposes the next expiry; the kernel sleeps (into `star_idle(next_due)`) only when the queue is empty and nothing is due; optional **tickless** idle (`STAR_TICKLESS=1`) reloads SysTick to the next deadline before wfi and restores the fixed rate on wake. Race handling is correct by reasoning, but **WFI behavior on each chip (especially QingKe) is not board-verified** |
| Critical section | Save/restore style (PRIMASK / INTSYSCR), nesting-safe |
| Observability | `star_dropped_count()` unified drop counter + `star_set_drop_hook()` drop callback (event/mailbox APIs only inside the hook) |

## Quick Start

```c
#include "star.h"

enum { EVT_BLINK = 0 };

static star_timer_t blink_timer;

static void blink_handler(uint16_t evt, void *param, void *ctx)
{
    led_toggle();
}

static const star_evt_entry_t evt_table[] = {
    [EVT_BLINK] = STAR_ENTRY(blink_handler, NULL),
};

int main(void)
{
    systick_start(1);  /* 1ms tick, call star_tick() in the ISR */
    star_init(evt_table, sizeof(evt_table) / sizeof(evt_table[0]));
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);

    star_loop();  /* Never returns */
}
```

## Documentation

- 🌐 [Online documentation site (Chinese)](https://stardustos.zane-leo.top/): usage, porting, and asking guides
- [Porting guide](docs/porting.md) (Chinese): Keil / MounRiver integration, SysTick conflicts, non-CMSIS chips, checklist
- [Usage guide](docs/usage.md) (Chinese): glossary, line-by-line walkthrough of events / timers / mailboxes / tasks, full example project
- [Test documentation](docs/test.md) (Chinese): test matrix, interleave test design, QEMU smoke, coverage & static analysis, local run instructions

## Rules

1. Handlers must be non-blocking and return within milliseconds; split long flows into state machines (the kernel provides no blocking delay)
2. Event params must point to global/static storage, or hold ≤32-bit values via `STAR_P()/STAR_U32()`; use mailboxes for large data
3. Timer/task APIs are main-loop-context only; `star_event_post*` and `star_mail_send` may be called from ISRs
4. Event IDs are a contiguous enum starting from 0 (the ID is the dispatch table index)

## Configuration

All tunables live in `stardustos/star_config.h`:

```c
#define STAR_TICK_MS        1    /* tick period in milliseconds */
#define STAR_EVT_QUEUE_SIZE 16   /* event queue slots */
#define STAR_DELAYED_MAX    4    /* concurrent delayed posts */
#define STAR_ENABLE_TASK    1    /* task layer switch */
#define STAR_TASK_SLOT_MAX  4    /* max simultaneously active tasks */
#define STAR_ENABLE_MAILBOX 1    /* mailbox switch */
#define STAR_TICKLESS       1    /* tickless idle (requires the next line) */
#define STAR_PORT_HCLK_HZ   48000000u  /* core clock in Hz, tickless only */
```

> `STAR_TICKLESS` / `STAR_PORT_HCLK_HZ` must be defined **project-wide**
> (`star_port.c` compiles against them too), not just in one .c file.
> Complete the tickless board-verification checklist in the [porting guide](docs/porting.md) before use.

## Build and Test

The kernel is pure logic; unit tests run on PC (`ctest` runs three builds by default: regular config, assert-enabled `test_stardustos_assert`, and worst-case `test_stardustos_max` — queue 255 / delayed 16 / task slots 16):

```bash
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

> The interleave tests verify the kernel's consistency against a **modeled concurrency semantics** (no preemption inside critical sections); pseudo-interrupt injection windows cover `star_event_post*` / `star_mail_send` (before critical section) / `star_poll` (before each step) / `star_process_timers` (during list traversal). They do not constitute hardware verification; real hardware timing must be verified on the board (see Project Status above).

## Directory Structure

```
stardustos/
├── star.h / star.c          # kernel
├── star_config.h            # single configuration point
├── star_task.c              # task layer (optional)
├── star_mail.c              # mailbox (optional)
└── port/                    # porting layer (per core: ch32v / cm0plus / cm3 / host)
examples/                    # per-chip examples
tests/                       # PC unit tests
docs/                        # porting and usage guides
brand/                       # brand assets
```

## License

Licensed under the [Apache License 2.0](LICENSE).
