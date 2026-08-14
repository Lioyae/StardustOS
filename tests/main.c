/*
 * StardustOS - event-driven cooperative kernel for small MCUs
 * Copyright (c) 2026 Lioyae
 * https://github.com/Lioyae/StardustOS
 * SPDX-License-Identifier: Apache-2.0
 */
#include "star_test.h"

int g_asserts = 0;
int g_fails = 0;

extern void suite_queue(void);
extern void suite_timer(void);
extern void suite_task(void);
extern void suite_mail(void);
extern void suite_interleave(void);

int main(void)
{
    suite_queue();
    suite_timer();
    suite_task();
    suite_mail();
    suite_interleave();

    printf("%d asserts, %d failures\n", g_asserts, g_fails);
    if (g_fails) {
        printf("FAILED\n");
        return 1;
    }
    printf("ALL PASSED\n");
    return 0;
}
