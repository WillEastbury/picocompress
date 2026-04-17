cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picocompress_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(picocompress_bench
    main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/picocompress.c
)

target_include_directories(picocompress_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_compile_definitions(picocompress_bench PRIVATE
    PC_HASH_BITS=11u
    PC_HASH_CHAIN_DEPTH=2u
    PC_HISTORY_SIZE=2048u
    PC_LAZY_STEPS=2u
)

target_compile_options(picocompress_bench PRIVATE -O2 -Wall)

target_link_libraries(picocompress_bench
    pico_stdlib
    hardware_timer
    hardware_clocks
)

# Output to USB serial
pico_enable_stdio_usb(picocompress_bench 1)
pico_enable_stdio_uart(picocompress_bench 0)

pico_add_extra_outputs(picocompress_bench)
