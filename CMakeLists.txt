# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

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
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pi_tuuzkb_usb C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(pi_tuuzkb_usb pi_tuuzkb_usb.cpp )

pico_set_program_name(pi_tuuzkb_usb "pi_tuuzkb_usb")
pico_set_program_version(pi_tuuzkb_usb "0.1")

# Generate PIO header
pico_generate_pio_header(pi_tuuzkb_usb ${CMAKE_CURRENT_LIST_DIR}/blink.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pi_tuuzkb_usb 0)
pico_enable_stdio_usb(pi_tuuzkb_usb 0)

# Add the standard library to the build
target_link_libraries(pi_tuuzkb_usb
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(pi_tuuzkb_usb PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

# Add any user requested libraries
target_link_libraries(pi_tuuzkb_usb 
        hardware_dma
        hardware_pio
        hardware_timer
        hardware_watchdog
        )

pico_add_extra_outputs(pi_tuuzkb_usb)

