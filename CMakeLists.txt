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

set(PICO_BOARD pico CACHE STRING "Board type")

include(pico_sdk_import.cmake)

project(pi_tuuzkb_usb C CXX ASM)

pico_sdk_init()

# ====== CH9350L 风格二进制协议桥固件结构 ======
# 阶段控制：ENABLE_USB（CACHE 变量，可在 IDE / 命令行覆盖）
#   ENABLE_USB=0: UART 二进制协议已就绪，USB Host 栈暂不启动（调试优先）
#   ENABLE_USB=1: 完整启用 USB Host HID 键盘解析 → 二进制协议输出
#
# 调试控制：ENABLE_DEBUG_TEXT（CACHE 变量）
#   ENABLE_DEBUG_TEXT=0（默认）: 纯二进制帧输出 —— UART0 只有 CH9350L 风格协议帧
#   ENABLE_DEBUG_TEXT=1:         同时输出文本日志（BOOT OK / LOOP TICK），用于 bring-up
#
# 命令行切换示例：
#   cmake -DENABLE_USB=0 -DENABLE_DEBUG_TEXT=0 ..    # 发布模式（默认）
#   cmake -DENABLE_USB=1 -DENABLE_DEBUG_TEXT=1 ..    # 开发/调试模式
#
# 分层架构：
#   /usb_host  - TinyUSB host 配置与回调（仅产出 key_event struct，不编码二进制帧）
#   /hid       - HID boot protocol 解析（仅产出 key_event struct）
#   /output    - uart_protocol.cpp（CH9350L 风格二进制帧编码 + UART0 输出）
#              - uart_logger.cpp（文本日志，保留，默认关闭）
#   main.cpp   - 初始化 + onKeyEvent 路由到 uart_protocol + 主循环 + tuh_task()

set(ENABLE_USB 0 CACHE STRING "Enable USB Host HID subsystem (0=disabled, 1=enabled)")
set(ENABLE_DEBUG_TEXT 0 CACHE STRING "Enable debug text output on UART0 (0=binary-only, 1=with-text)")

add_executable(pi_tuuzkb_usb
    main.cpp
    usb_host/usb_callbacks.cpp
    hid/hid_parser.cpp
    output/uart_protocol.cpp
    output/uart_logger.cpp
)

# 将宏传递到所有编译单元
target_compile_definitions(pi_tuuzkb_usb PRIVATE
    ENABLE_USB=${ENABLE_USB}
    ENABLE_DEBUG_TEXT=${ENABLE_DEBUG_TEXT}
)

target_include_directories(pi_tuuzkb_usb PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/usb_host
    ${CMAKE_CURRENT_LIST_DIR}/hid
    ${CMAKE_CURRENT_LIST_DIR}/output
)

# ---- 基础链接库（始终需要）----
# pico_stdlib = 提供 stdio_init_all / sleep_ms / to_ms_since_boot / get_absolute_time
# hardware_uart = 提供 uart_init / uart_set_format / uart_write_blocking / uart_set_fifo_enabled
# hardware_gpio = 提供 gpio_set_function（配置 UART0 引脚）
target_link_libraries(pi_tuuzkb_usb
    pico_stdlib
    hardware_uart
    hardware_gpio
)

# ---- USB Host 相关链接库（仅 ENABLE_USB=1 时）----
if(ENABLE_USB EQUAL 1)
    target_link_libraries(pi_tuuzkb_usb
        tinyusb_host
        tinyusb_board
    )
    message(STATUS "[pi_tuuzkb_usb] USB Host HID ENABLED (ENABLE_USB=1)")
else()
    message(STATUS "[pi_tuuzkb_usb] USB Host HID DISABLED (ENABLE_USB=0)")
endif()

# ---- 调试文本模式状态 ----
if(ENABLE_DEBUG_TEXT EQUAL 1)
    message(STATUS "[pi_tuuzkb_usb] UART0: binary frames + debug text")
else()
    message(STATUS "[pi_tuuzkb_usb] UART0: binary frames only (纯二进制模式)")
endif()

# 启用/禁用 stdio UART：
# - 发布模式（ENABLE_DEBUG_TEXT=0）：禁用 stdio UART —— 防止 Pico SDK 污染二进制协议流
# - 调试模式（ENABLE_DEBUG_TEXT=1）：启用 stdio UART —— 允许 printf 输出文本
if(ENABLE_DEBUG_TEXT EQUAL 1)
    pico_enable_stdio_uart(pi_tuuzkb_usb 1)
else()
    pico_enable_stdio_uart(pi_tuuzkb_usb 0)
endif()
pico_enable_stdio_usb(pi_tuuzkb_usb 0)  # 始终禁用 USB CDC device（与 USB Host 冲突）

pico_set_program_name(pi_tuuzkb_usb "pi_tuuzkb_usb_ch9350l")
pico_set_program_version(pi_tuuzkb_usb "0.2")

pico_add_extra_outputs(pi_tuuzkb_usb)
