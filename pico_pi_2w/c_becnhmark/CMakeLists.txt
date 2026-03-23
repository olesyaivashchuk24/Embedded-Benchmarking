cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(c_pico_bench C CXX ASM)

pico_sdk_init()

add_executable(c_pico_bench
    c_pico_bench.c
)

pico_set_program_name(c_pico_bench "c_pico_bench")
pico_set_program_version(c_pico_bench "0.1")

pico_enable_stdio_uart(c_pico_bench 0)
pico_enable_stdio_usb(c_pico_bench 1)

target_link_libraries(c_pico_bench
    pico_stdlib
    hardware_timer
    pico_cyw43_arch_lwip_threadsafe_background
)

target_include_directories(c_pico_bench PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(c_pico_bench)