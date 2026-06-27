#ifndef OUTPUT_UART_LOGGER_H
#define OUTPUT_UART_LOGGER_H

// ===== UART Logger 输出层 =====
// 使用 UART0 (GPIO0 TX / GPIO1 RX)
// - 波特率: 9600, 8N1
// - 输出原始 HID key_event 文本日志
//
// 注意：
//   本模块作为调试辅助存在，默认关闭（ENABLE_DEBUG_TEXT=0 时不参与 stdio 初始化）。
//   UART0 引脚常量仅在 uart_logger.cpp 内部定义，避免与 uart_protocol.h 的符号冲突。

#if __cplusplus < 201703L
#error "uart_logger requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace output {

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
