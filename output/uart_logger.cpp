#include "uart_logger.h"

// ===== UART Logger 实现 =====
// 注意：UART0 引脚常量仅在 .cpp 内部定义，避免与 uart_protocol.h 符号冲突。

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace output {

namespace {

// —— UART0 硬件参数（实现细节，不对外暴露）——
constexpr std::uint8_t  kUartTXPin    = 0;
constexpr std::uint8_t  kUartRXPin    = 1;
constexpr std::uint32_t kBaudRate     = 9600;

} // namespace

UartLogger::UartLogger()
    : initialized_(false) {
}

UartLogger::~UartLogger() {
    if (initialized_) {
        uart_deinit(uart0);
        initialized_ = false;
    }
}

bool UartLogger::initialize() {
    if (initialized_) return true;

    // 初始化 UART0，波特率 9600
    uart_init(uart0, kBaudRate);

    // 配置 GPIO 引脚功能（复用为 UART）
    gpio_set_function(kUartTXPin, GPIO_FUNC_UART);
    gpio_set_function(kUartRXPin, GPIO_FUNC_UART);

    // 设置数据格式：8 数据位, 1 停止位, 无校验
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);

    // 开启 FIFO，减少中断带来的抖动
    uart_set_fifo_enabled(uart0, true);

    initialized_ = true;
    return true;
}

void UartLogger::logKeyEvent(const usb_host::key_event& event) {
    if (!initialized_) return;

    // 格式： KEY: usage=0xXX pressed=X modifiers=0xXX\n
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf),
        "KEY: usage=0x%02X pressed=%u modifiers=0x%02X\n",
        static_cast<unsigned>(event.usage_code),
        event.pressed ? 1u : 0u,
        static_cast<unsigned>(event.modifiers));

    if (n > 0) {
        std::size_t len = static_cast<std::size_t>(n);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        uart_write_blocking(uart0, reinterpret_cast<const std::uint8_t*>(buf), len);
    }
}

void UartLogger::logString(const char* msg) {
    if (!initialized_ || msg == nullptr) return;
    std::size_t len = std::strlen(msg);
    if (len == 0) return;
    uart_write_blocking(uart0, reinterpret_cast<const std::uint8_t*>(msg), len);
}

void UartLogger::logFormat(const char* fmt, ...) {
    if (!initialized_ || fmt == nullptr) return;

    char buf[128];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n <= 0) return;
    std::size_t len = static_cast<std::size_t>(n);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    uart_write_blocking(uart0, reinterpret_cast<const std::uint8_t*>(buf), len);
}

} // namespace output
