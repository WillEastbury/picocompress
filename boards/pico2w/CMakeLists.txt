cmake_minimum_required(VERSION 3.13)

include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(picocompress_bench C CXX ASM)
set(CMAKE_C_STANDARD 11)

pico_sdk_init()

add_executable(picocompress_bench
    main.c
    pc_micro.c
    pc_minimal.c
    pc_balanced.c
    pc_q3.c
    pc_q4.c
    heatshrink_encoder.c
    heatshrink_decoder.c
)

target_include_directories(picocompress_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(picocompress_bench PRIVATE
    HEATSHRINK_DYNAMIC_ALLOC=1
    HEATSHRINK_USE_INDEX=0
    PC_NO_BITSCAN=1
    PC_NO_HW_CRC32=1
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
