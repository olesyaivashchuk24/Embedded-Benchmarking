cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH "/Users/olesaivasuk/pico-sdk-new")
set(PICO_BOARD pico2_w)

include(pico_sdk_import.cmake)

project(pico_bench C CXX ASM)

pico_sdk_init()

add_executable(pico_bench main.c)

target_link_libraries(pico_bench
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    hardware_gpio
    hardware_timer
)

target_include_directories(pico_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

pico_enable_stdio_usb(pico_bench 1)
pico_enable_stdio_uart(pico_bench 0)

pico_add_extra_outputs(pico_bench)