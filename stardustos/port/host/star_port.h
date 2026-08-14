/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STAR_PORT_H
#define STAR_PORT_H

/* 主机（PC 单元测试）移植：
 * 用共享变量模拟"全局中断开关"，完整实现保存/恢复语义，
 * 内核临界区是否破坏调用方中断状态可在宿主机上直接断言 */

#include <stdint.h>

#define STAR_PORT_HOST

typedef uint32_t star_crit_state_t;

extern uint32_t star_host_primask;

/* 测试观测：star_idle 调用次数与最近一次传入的 next_due */
extern uint32_t star_host_idle_count;
extern uint32_t star_host_idle_last_due;

static inline star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s = star_host_primask;
    star_host_primask = 1;
    return s;
}

static inline void star_crit_exit(star_crit_state_t s)
{
    star_host_primask = s;
}

static inline uint32_t star_crit_active(void)
{
    return star_host_primask;
}

#endif
