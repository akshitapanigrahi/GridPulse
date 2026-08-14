# GRID PULSE - firmware build.
#
# Produces grid_pulse.uf2 for the Raspberry Pi Pico (RP2040).
#
# Build:
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   cmake -B build -S .
#   cmake --build build
#
# The resulting build/grid_pulse.uf2 is copied to firmware/prebuilt/ and committed,
# so a grader can flash the board without installing an ARM toolchain. ../run.sh
# --flash does the copy onto a Pico mounted in BOOTSEL mode.
#
# This file also defines an equivalent CTest target over the pure logic, for anyone
# who is building the firmware anyway. It is NOT on the test critical path:
# ./run.sh --test uses tests/native/Makefile, which needs only a C++ compiler and
# make. CMake is not installed by default on a stock macOS or Linux machine, and
# requiring it would break the "no exotic setup" promise.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(pico_sdk_import.cmake)

project(grid_pulse C CXX ASM)

pico_sdk_init()

# --- the pure game logic, shared by the firmware and the native tests -----------
#
# Nothing in this list may include a Pico SDK header. tests/native/Makefile enforces
# that mechanically with its `check-purity` target.
set(GRIDPULSE_PURE_SOURCES
    src/pure/rng.cpp
    src/pure/scoring.cpp
    src/pure/crc.cpp
    src/pure/protocol.cpp
    src/pure/healthmask.cpp
    src/pure/fsm.cpp
    src/pure/event.cpp
    src/pure/runevents.cpp
    src/pure/keyscan.cpp
    src/pure/selftest.cpp
)

# --- firmware -------------------------------------------------------------------

add_executable(grid_pulse
    src/main.cpp
    src/core1_game.cpp
    src/usb_descriptors.c
    src/hal/keys.cpp
    src/hal/leds.cpp
    src/hal/flashstore.cpp
    ${GRIDPULSE_PURE_SOURCES}
)

pico_generate_pio_header(grid_pulse ${CMAKE_CURRENT_LIST_DIR}/src/hal/ws2812.pio)

target_include_directories(grid_pulse PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_compile_options(grid_pulse PRIVATE
    -Wall
    -Wextra
    -Werror
    -Wshadow
    -Wdouble-promotion
)

target_link_libraries(grid_pulse
    pico_stdlib
    pico_multicore
    pico_unique_id
    pico_flash
    hardware_pio
    hardware_dma
    hardware_flash
    hardware_gpio
    tinyusb_device
    tinyusb_board
)

# This firmware speaks its own protocol on a raw TinyUSB CDC interface. Letting the
# SDK also claim stdio over USB would fight it for the same endpoint.
pico_enable_stdio_usb(grid_pulse 0)
pico_enable_stdio_uart(grid_pulse 0)

pico_add_extra_outputs(grid_pulse)

# --- native tests (optional convenience target) -----------------------------------
#
# Only defined when configuring for the host, i.e. when someone runs cmake in this
# directory without the Pico toolchain. The canonical way to run these is
# `make -C tests/native`.

if(NOT PICO_PLATFORM)
    enable_testing()
    add_executable(gridpulse_native_tests
        ${GRIDPULSE_PURE_SOURCES}
        ../tests/native/test_main.cpp
        ../tests/native/test_rng.cpp
        ../tests/native/test_scoring.cpp
        ../tests/native/test_crc.cpp
        ../tests/native/test_protocol.cpp
        ../tests/native/test_boardmap.cpp
        ../tests/native/test_healthmask.cpp
        ../tests/native/test_fsm.cpp
        ../tests/native/test_runevents.cpp
        ../tests/native/test_keyscan.cpp
        ../tests/native/test_selftest.cpp
        ../tests/native/test_queue_event.cpp
    )
    target_include_directories(gridpulse_native_tests PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/src
        ${CMAKE_CURRENT_LIST_DIR}/include
    )
    target_compile_options(gridpulse_native_tests PRIVATE
        -Wall -Wextra -Werror -Wshadow -Wconversion -Wsign-conversion
        -Wold-style-cast -Wpedantic
    )
    add_test(NAME gridpulse_native COMMAND gridpulse_native_tests)
endif()
