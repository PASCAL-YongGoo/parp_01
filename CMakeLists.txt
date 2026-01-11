# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Add custom board directory (use list(APPEND) for compatibility with other board roots)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(parp_01)

target_sources(app PRIVATE
	src/main.c
	src/e310_protocol.c
	src/e310_test.c
	src/uart_router.c
	src/usb_hid.c
	src/usb_device.c
)
