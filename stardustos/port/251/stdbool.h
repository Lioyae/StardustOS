/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STDBOOL_H
#define STDBOOL_H

/* bool 用 unsigned char：内核把 bool 用作结构体成员（如
 * star_task_slot_t.active），C51 的 bit 类型不能作为 struct 成员，
 * 故此处不能用 `typedef bit bool`。 */
typedef unsigned char bool;

#define true  1
#define false 0

#endif
