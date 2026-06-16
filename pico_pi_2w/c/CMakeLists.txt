cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(pico_wifi_example C CXX ASM)

# Указываем путь к SDK (если не стандартный)
set(PICO_SDK_PATH "/Users/olesaivasuk/pico-sdk-new")
list(APPEND CMAKE_PREFIX_PATH "${PICO_SDK_PATH}")

pico_sdk_init()

add_executable(pico_wifi_example c_pico_bench.c)

target_link_libraries(pico_wifi_example
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
)

pico_enable_stdio_usb(pico_wifi_example 1)
pico_enable_stdio_uart(pico_wifi_example 0)

pico_add_extra_outputs(pico_wifi_example)