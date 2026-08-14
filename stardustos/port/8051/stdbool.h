/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STDBOOL_H
#define STDBOOL_H

#if defined(__SDCC) || defined(SDCC)
/* SDCC 自带 stdbool.h：转发到系统头（本文件仅在 Keil 下提供定义） */
#include_next <stdbool.h>
#else
/* Keil C51/C251：bool 用 unsigned char（可作结构体成员，如
 * star_task_slot_t.active）；C51 的 bit 类型不能作为 struct 成员，
 * 故此处不能用 `typedef bit bool`。 */
typedef unsigned char bool;

#define true  1
#define false 0
#endif

#endif
