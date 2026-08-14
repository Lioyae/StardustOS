<p align="right">
  <a href="README.md">简体中文</a> | English
</p>

<p align="center">
  <img src="brand/stardustos-icon.svg" alt="StardustOS Logo" width="160">
</p>

<h1 align="center">StardustOS</h1>

<p align="center">
  <strong>An event-driven cooperative kernel for small MCUs</strong><br>
  No standalone assembly files · Zero dynamic memory allocation · All resource usage fixed at compile time
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
  <a href="https://github.com/Lioyae/StardustOS/stargazers">
    <img src="https://img.shields.io/github/stars/Lioyae/StardustOS?style=for-the-badge&color=d69e2e" alt="Stars">
  </a>
  <a href="https://github.com/Lioyae/StardustOS/blob/main/LICENSE">
    <img src="https://img.shields.io/github/license/Lioyae/StardustOS?style=for-the-badge&color=38a169" alt="License">
  </a>
</p>

---

## About

StardustOS is a C99 event-driven cooperative kernel for small MCUs, focused on STC 8051 / 80251 microcontrollers (the kernel itself uses only ~280B of RAM by default).

- No assembly source files in the kernel (the port layer accesses SFR/registers directly), no dynamic memory allocation, no blocking delay APIs
- All RAM/Flash usage is fixed at compile time; CI automates unit tests, coverage, and SDCC compile checks
- Supports 8051 (STC8H/8A, STC89C52, STC8051U/8052U, Keil C51 / SDCC), 80251 (STC32G, Keil C251), and host (x86); interrupt latency = tick interrupt + kernel critical sections (event enqueue O(1); mailbox copy proportional to item_size; post_replace proportional to queue length). Critical-section duration depends on configuration and clock, and **must be measured per platform** (see the usage guide appendix A)

## Project Status (Important)

**Development preview (v0.x), not board-verified.**

- ✅ Verified: host unit/interleave tests (ASan/UBSan, multi-seed), assert-enabled build, worst-case config build (queue 255), **gcov coverage gate (≥85% lines)**, **cppcheck static analysis** — all automated by CI; **8051 (Keil C51 / SDCC) and 80251 (Keil C251) kernel compile cleanly with 0 warnings/0 errors** (C251 emits harmless C174 for unreferenced static functions)
- ❌ Not verified: the kernel has never run on real silicon. Interrupt timing, measured critical-section duration, 8051/251 idle-mode (PCON IDL) wakeup, and periodic-timer phase drift have no board-level measurements
- ⚠️ Before production use, complete the board-level verification per the [porting checklist](docs/porting.md).

## Supported Platforms

| Core | Compiler | Example chips |
|---|---|---|
| 8051 | Keil C51 / SDCC | STC8H / STC8A / STC89C52 / STC8051U / STC8052U |
| 80251 | Keil C251 | STC32G |
| x86 (host) | GCC | Runs kernel unit tests on PC |

> Except for the host, 8051/251 are verified by compilation only (SDCC in CI, Keil locally); **never run on hardware**.

## Resource Usage

| Item | Usage |
|---|---|
| Kernel RAM (default) | ~280B (event queue 16 slots + delayed 4 + task slots 4), compile-time fixed; on 8051 large arrays default to `idata`, define `STAR_RAM_XDATA=1` to move them to XRAM |
| Kernel Flash | Pure logic, compile-time fixed; on 8051/251 check linked size (Keil builds are not part of Linux CI) |
| Full blink example | See `examples/stc8h` / `stc89c52` / `stc32g` (STC8H/STC32G need STC-ISP generated device headers) |

## Modules

| Module | Description |
|---|---|
| Event queue | `star_event_post` / `star_event_post_replace` (latest wins per ID) / `star_event_post_delayed` (with `_replace` and `star_event_cancel_delayed`); drop counter `star_dropped_count()` |
| Registry | Sequential initializers, event ID is the index, O(1) dispatch, table lives in Flash (C51 has no designated initializers — fill `STAR_ENTRY` in order) |
| Timer | Statically defined; 32-bit wraparound-safe; list sorted by due time, expiry scan visits only due nodes (poll idle is O(1)); periodic timers fire on absolute phase (missed ticks coalesce, no cumulative drift); full-queue policies: retry / drop (strict deadline) / latest (replace semantics) |
| Task layer | Periodic-callback convenience layer: descriptors in Flash (handler + ctx + period), slot pool in RAM, inactive tasks use no RAM (optional). Not RTOS tasks — no preemption, handlers are called synchronously by the main loop |
| Mailbox | Static-slot deep copy, **enqueue-before-copy** atomic with event enqueue in one critical section; variable-length messages (1..item_size bytes per slot, item_size≤255, `recv` returns actual length, over-length rejected not truncated); invalid construction rejected at runtime (optional) |
| Low power | Deadline-aware: `star_next_due()` exposes the next expiry; the kernel sleeps (into `star_idle(next_due)`) only when the queue is empty and nothing is due; 8051/251 use PCON IDL idle mode (CPU stops, timers/interrupts keep running, any interrupt wakes). tickless (`STAR_TICKLESS=1`) is host/reference only, not supported on 8051/251 |
| Critical section | Save/restore style (EA on 8051/251), nesting-safe |
| Observability | `star_dropped_count()` unified drop counter + `star_set_drop_hook()` drop callback (event/mailbox APIs only inside the hook) |

## Quick Start

STC89C52 (Keil C51), P1.0 LED toggling every 500ms:

```c
#include "reg52.h"
#include "star.h"

sbit LED = P1 ^ 0;

enum { EVT_BLINK = 0, EVT_COUNT };

/* 12MHz, 12T, Timer0 mode 1 (16-bit manual reload), 1ms tick */
#define T0_RELOAD_H 0xFCu
#define T0_RELOAD_L 0x18u

static void timer0_init_1ms(void)
{
    TMOD &= 0xF0u;
    TMOD |= 0x01u;   /* Timer0 mode 1 */
    TH0 = T0_RELOAD_H;
    TL0 = T0_RELOAD_L;
    ET0 = 1;         /* Timer0 interrupt enable */
    TR0 = 1;         /* start Timer0 */
    EA = 1;          /* enable global interrupts */
}

/* Mode 1 needs manual reload: custom Timer0 ISR; define STAR_PORT_NO_TICK_ISR */
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
    STAR_ENTRY(blink_handler, NULL),   /* sequential: EVT_BLINK = 0 */
};

void main(void)
{
    timer0_init_1ms();
    star_init(evt_table, EVT_COUNT);
    star_timer_start(&blink_timer, EVT_BLINK, NULL, 500, true);
    star_loop();   /* never returns */
}
```

> On C51, handlers must be marked `STAR_REENTRANT` (multi-parameter function pointers use the reentrant stack); the event table uses sequential initializers (C51 has no C99 designated initializers). Full examples in `examples/stc8h`, `examples/stc89c52`, `examples/stc32g`.

## Documentation

- [Porting guide](docs/porting.md) (Chinese): Keil C51/C251 integration, Timer0 tick wiring, EA critical section, custom tick ISR, STC-ISP headers, checklist
- [Usage guide](docs/usage.md) (Chinese): glossary, event / timer / mailbox / task layer walkthrough, full project
- [Test documentation](docs/test.md) (Chinese): test matrix, interleave test design, coverage & static analysis, SDCC/Keil compile checks, local run instructions

## Usage Rules

1. Handlers must be non-blocking and return within milliseconds; split long flows into state machines (no blocking delay APIs)
2. Event params must point to global/static storage, or hold ≤32-bit values via `STAR_P()/STAR_U32()`; use mailboxes for large data (**on 8051 the generic pointer is 3 bytes: ≤16-bit values round-trip losslessly, 32-bit values truncate**)
3. Timer/task APIs are main-loop-context only; `star_event_post*` and `star_mail_send` may be called from ISRs
4. Event IDs are consecutive from 0 (ID is the registry index)

## Configuration

All tunables live in `stardustos/star_config.h`:

```c
#define STAR_TICK_MS        1    /* tick period in milliseconds */
#define STAR_EVT_QUEUE_SIZE 16   /* event queue slots */
#define STAR_DELAYED_MAX    4    /* concurrent delayed posts */
#define STAR_ENABLE_TASK    1    /* task layer switch */
#define STAR_TASK_SLOT_MAX  4    /* max simultaneously active tasks */
#define STAR_ENABLE_MAILBOX 1    /* mailbox switch */
#define STAR_TIMER_CATCHUP_MAX 1000  /* periodic timer catch-up limit */
```

> `STAR_TICKLESS` / `STAR_PORT_HCLK_HZ` are host/reference only; tickless is not supported on 8051/251.

## Build & Test

The kernel is pure logic; unit tests run on PC (`ctest` runs three builds by default: regular config, assert-enabled `test_stardustos_assert`, and worst-case `test_stardustos_max` — queue 255 / delayed 16 / task slots 16):

```bash
cmake -G "MinGW Makefiles" -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

8051 compile verification runs on two tracks (see the [test documentation](docs/test.md)):

```bash
# SDCC (in CI, --Werror)
sdcc -mmcs51 -c --Werror -Istardustos -Istardustos/port/8051 \
  -o star.rel stardustos/star.c

# Keil C51 / C251 (local; Windows commercial software, not in Linux CI)
C51.EXE star.c INCDIR(stardustos;stardustos\port\8051) OBJECT(star.OBJ) SMALL
C251.EXE star.c INCDIR(stardustos;stardustos\port\251) OBJECT(star.OBJ)
```

> The interleave tests verify the kernel's consistency against a **modeled concurrency semantics** (no preemption inside critical sections); pseudo-interrupt injection windows cover `star_event_post*` / `star_mail_send` (before critical section) / `star_poll` (before each step) / `star_process_timers` (during list traversal). This is not hardware verification; real timing must be verified on the board.

## Directory Layout

```
stardustos/
├── star.h / star.c          # kernel
├── star_config.h            # single configuration point
├── star_task.c              # task layer (optional)
├── star_mail.c              # mailbox (optional)
└── port/                    # port layer (per core: 8051 / 251 / host)
examples/                    # STC examples (stc8h / stc89c52 / stc32g)
tests/                       # PC unit tests
docs/                        # porting & usage guides
brand/                       # brand assets
```

## License

Apache License 2.0, see [LICENSE](LICENSE).
