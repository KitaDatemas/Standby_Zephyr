# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(blinky_sample_test_1)

target_sources(app PRIVATE src/main.c src/flash.c)
# include(${ZEPHYR_BASE}/../modules/mcuboot/boot/zephyr/gen_recovery_linker_file.cmake)