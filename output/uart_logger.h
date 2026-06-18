#ifndef OUTPUT_UART_LOGGER_H
#define OUTPUT_UART_LOGGER_H

// ===== UART Logger 输出层 =====
// 使用 UART0 (GPIO0 TX / GPIO1 RX)
// - 波特率: 115200, 8N1
// - 输出原始 HID key_event 文本日志

#if __cplusplus < 201703L
#error "uart_logger requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace output {

// UART0 引脚 (Pico 板载 UART0:
//   TX = GPIO0 (Pin 1)
//   RX = GPIO1 (Pin 2)
constexpr uint8_t kUartTXPin = 0;
constexpr uint8_t kUartRXPin = 1;
constexpr uint32_t kBaudRate = 115200;

class UartLogger {
public:
    UartLogger();
    ~UartLogger();

    // 初始化 UART0
    // 返回 false 表示失败（理论上不会失败，这里保持接口一致）
    bool initialize();

    // 输出一行原始 key_event
    // 格式：KEY: usage=0xXX pressed=X modifiers=0xXX
    void logKeyEvent(const usb_host::key_event& event);

    // 输出普通日志字符串（如需要额外信息）
    void logString(const char* msg);

    // 格式化输出（变长参数）
    void logFormat(const char* fmt, ...);

    // 是否已初始化
    bool isInitialized() const { return initialized_; }

private:
    bool initialized_;
};

} // namespace output

#endif // OUTPUT_UART_LOGGER_H
