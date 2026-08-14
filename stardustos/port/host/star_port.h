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

/* 与其它 port 统一的内核存储/链接宏：GCC 下等价于 static */
#define STAR_STATIC static
#define STAR_WEAK __attribute__((weak))
#define STAR_INLINE static inline

typedef uint32_t star_crit_state_t;

extern uint32_t star_host_primask;

/* 测试观测：star_idle 调用次数与最近一次传入的 next_due */
extern uint32_t star_host_idle_count;
extern uint32_t star_host_idle_last_due;

STAR_INLINE star_crit_state_t star_crit_enter(void)
{
    star_crit_state_t s = star_host_primask;
    star_host_primask = 1;
    return s;
}

STAR_INLINE void star_crit_exit(star_crit_state_t s)
{
    star_host_primask = s;
}

STAR_INLINE uint32_t star_crit_active(void)
{
    return star_host_primask;
}

#endif
