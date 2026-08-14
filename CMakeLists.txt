# StardustOS - event-driven cooperative kernel for small MCUs
# Copyright (c) 2026 Lioyae
# https://github.com/Lioyae/StardustOS
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.16)
project(stardustos C)

# 宿主机测试依赖 GCC 兼容编译器：C99 指定初始化器、-Wall/-Wextra/-Werror、
# 断言构建的 -include 强制头均非 MSVC 语义（MSVC 下会报 D8021 等晦涩错误）。
# Windows 请使用 MinGW 生成器：
#   cmake -S . -B build -G "MinGW Makefiles"
if(MSVC)
    message(FATAL_ERROR
        "StardustOS host tests require a GCC-compatible compiler. "
        "On Windows, configure with: cmake -S . -B build -G \"MinGW Makefiles\"")
endif()

enable_testing()

set(STARDUSTOS_TEST_SOURCES
    stardustos/star.c
    stardustos/star_task.c
    stardustos/star_mail.c
    stardustos/port/star_port.c
    tests/main.c
    tests/test_queue.c
    tests/test_timer.c
    tests/test_task.c
    tests/test_mail.c
    tests/test_interleave.c
)

add_executable(test_stardustos ${STARDUSTOS_TEST_SOURCES})

target_include_directories(test_stardustos PRIVATE
    stardustos
    stardustos/port/host
    tests
)

target_compile_definitions(test_stardustos PRIVATE STAR_TEST_INJECT_ENABLE=1)

target_compile_options(test_stardustos PRIVATE -Wall -Wextra -Werror)

add_test(NAME stardustos_unit COMMAND test_stardustos)

# 断言开启构建：star_config.h 的 STAR_ASSERT 默认已开启（回调弱符号
# star_assert_fail，宿主机实现为 abort）。本目标强制 -include tests/star_assert.h
# （在 star_config.h 之前生效），把 STAR_ASSERT 替换为"打印位置 + abort"，
# 使内核断言路径被真实编译并运行、触发即打印并终止；测试全程不应触发任何断言。
add_executable(test_stardustos_assert ${STARDUSTOS_TEST_SOURCES})

target_include_directories(test_stardustos_assert PRIVATE
    stardustos
    stardustos/port/host
    tests
)

target_compile_definitions(test_stardustos_assert PRIVATE STAR_TEST_INJECT_ENABLE=1)

target_compile_options(test_stardustos_assert PRIVATE -Wall -Wextra -Werror
    -include ${CMAKE_CURRENT_SOURCE_DIR}/tests/star_assert.h)

add_test(NAME stardustos_unit_assert COMMAND test_stardustos_assert)

# 最坏配置构建：队列 255 / 延时槽 16 / 任务槽 16。
# 验证极端配置下（临界区最长路径：replace 全扫描 255、大队列回绕）
# 编译与并发语义正确性；与 CI 交叉编译的 -DSTAR_EVT_QUEUE_SIZE=255 对应。
add_executable(test_stardustos_max ${STARDUSTOS_TEST_SOURCES})

target_include_directories(test_stardustos_max PRIVATE
    stardustos
    stardustos/port/host
    tests
)

target_compile_definitions(test_stardustos_max PRIVATE
    STAR_TEST_INJECT_ENABLE=1
    STAR_EVT_QUEUE_SIZE=255
    STAR_DELAYED_MAX=16
    STAR_TASK_SLOT_MAX=16
)

target_compile_options(test_stardustos_max PRIVATE -Wall -Wextra -Werror)

add_test(NAME stardustos_unit_max COMMAND test_stardustos_max)
