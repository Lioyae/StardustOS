/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef STDINT_H
#define STDINT_H

/* Keil C51/C251 的 INC 目录不含 C99 的 stdint.h/stdbool.h。
 * 内核只用定宽整数与"指针↔整数"，此处提供最小兼容定义。
 * （host 单元测试用 GCC 自带的 stdint.h，不经过本文件） */

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef signed int         int16_t;
typedef unsigned int       uint16_t;
typedef signed long        int32_t;
typedef unsigned long      uint32_t;

/* 指针 → 整数：C51 通用指针 3 字节、C251 数据指针 4 字节，
 * 均以 unsigned long 容纳 */
typedef unsigned long      uintptr_t;
typedef signed long        intptr_t;

#endif
