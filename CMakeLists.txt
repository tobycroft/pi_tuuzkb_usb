# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pi_tuuzkb_usb C CXX ASM)

pico_sdk_init()

add_executable(pi_tuuzkb_usb
    pi_tuuzkb_usb.cpp
    src/TinyUsbManager.cpp
    src/UsbApplication.cpp
)

pico_set_program_name(pi_tuuzkb_usb "pi_tuuzkb_usb")
pico_set_program_version(pi_tuuzkb_usb "0.2")

pico_generate_pio_header(pi_tuuzkb_usb ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

pico_enable_stdio_uart(pi_tuuzkb_usb 1)
pico_enable_stdio_usb(pi_tuuzkb_usb 1)

target_link_libraries(pi_tuuzkb_usb
        pico_stdlib
)

target_include_directories(pi_tuuzkb_usb PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(pi_tuuzkb_usb
        hardware_dma
        hardware_pio
        hardware_timer
        hardware_watchdog
        hardware_uart
        hardware_gpio
)

pico_add_extra_outputs(pi_tuuzkb_usb)
