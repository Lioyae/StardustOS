/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star.h"

#if STAR_ENABLE_TASK

typedef struct {
    uint16_t id;
    uint32_t due;
    bool active;
} star_task_slot_t;

static const star_task_desc_t *s_task_table;
static uint16_t s_task_count;
static star_task_slot_t s_slots[STAR_TASK_SLOT_MAX];

void star_task_init(const star_task_desc_t *table, uint16_t count)
{
    s_task_table = table;
    s_task_count = count;
    for (uint8_t i = 0; i < STAR_TASK_SLOT_MAX; i++) {
        s_slots[i].active = false;
    }
}

star_status_t star_task_start(uint16_t id)
{
    if (id >= s_task_count) {
        return STAR_ERR_PARAM;
    }
    /* 与定时器同口径的运行时校验（此前仅靠默认关闭的 STAR_ASSERT）：
     * period_ms==0 会在 catch-up 空转后每 poll 同步调用一次 handler，
     * 与 spin 无异；period_ms≥2^31 会使回绕比较数学失效 */
    if (s_task_table[id].period_ms == 0 ||
        s_task_table[id].period_ms >= 0x80000000u) {
        return STAR_ERR_PARAM;
    }
    for (uint8_t i = 0; i < STAR_TASK_SLOT_MAX; i++) {
        if (s_slots[i].active && s_slots[i].id == id) {
            return STAR_OK; /* 已启动 */
        }
    }
    for (uint8_t i = 0; i < STAR_TASK_SLOT_MAX; i++) {
        if (!s_slots[i].active) {
            s_slots[i].id = id;
            s_slots[i].due = star_ticks() + s_task_table[id].period_ms;
            s_slots[i].active = true;
            return STAR_OK;
        }
    }
    return STAR_ERR_FULL; /* 槽池耗尽 */
}

star_status_t star_task_stop(uint16_t id)
{
    for (uint8_t i = 0; i < STAR_TASK_SLOT_MAX; i++) {
        if (s_slots[i].active && s_slots[i].id == id) {
            s_slots[i].active = false;
            return STAR_OK;
        }
    }
    return STAR_ERR_NOT_FOUND;
}

void star_process_tasks(void)
{
    uint32_t now = star_ticks();

    for (uint8_t i = 0; i < STAR_TASK_SLOT_MAX; i++) {
        star_task_slot_t *s = &s_slots[i];
        if (s->active && (int32_t)(now - s->due) >= 0) {
            const star_task_desc_t *d = &s_task_table[s->id];
            /* 与定时器同语义：相位稳定推进（due += period），
             * 落后超 STAR_TIMER_CATCHUP_MAX 拍重建相位 */
            uint32_t n = 0;
            do {
                s->due += d->period_ms;
            } while ((int32_t)(now - s->due) >= 0 &&
                     ++n < STAR_TIMER_CATCHUP_MAX);
            if ((int32_t)(now - s->due) >= 0) {
                s->due = now + d->period_ms;
            }
            d->handler(STAR_EVT_TASK, NULL, d->ctx);
        }
    }
}

#endif
