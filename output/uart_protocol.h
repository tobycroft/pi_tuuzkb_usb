#ifndef OUTPUT_UART_PROTOCOL_H
#define OUTPUT_UART_PROTOCOL_H

// ===== UART 二进制协议输出层（CH9350L 风格） =====
//
// Frame format（LSB first，无流控）：
//   ┌─────────┬─────────┬─────┬──────┬──────────────┬──────────┐
//   │  HEADER │  HEADER │ LEN │ TYPE │    DATA      │ CHECKSUM │
//   │  0x57   │  0xAB   │     │      │              │   XOR    │
//   └─────────┴─────────┴─────┴──────┴──────────────┴──────────┘
//
// LEN = total frame length = 2 (header) + 1 (LEN) + 1 (TYPE) + N (DATA) + 1 (CHECKSUM)
//     对 keyboard event（DATA=3B）: LEN = 8
//
// TYPE = 0x01 —— keyboard event
// DATA = [usage_code:1B][pressed:1B][modifiers:1B] （共 3 字节）
//
// CHECKSUM = XOR of [0x57] ^ [0xAB] ^ [LEN] ^ [TYPE] ^ [DATA0] ^ ... ^ [DATAN-1]
//
// 设计原则：
//   - USB HID 层只产生 key_event struct（不关心 UART 编码）
//   - 本模块负责 frame 编码 & 输出
//   - 不使用 printf 输出 key 信息（仅调试信息可走 printf）
//   - 输出到 UART0：TX=GPIO0, RX=GPIO1, 115200/8N1

#if __cplusplus < 201703L
#error "uart_protocol requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace output {

// ===== CH9350L 风格帧常量 =====
// 注: 使用 std::uint8_t / std::size_t 以严格符合 C++ <cstdint> 规范
//     （消除 IDE "未定义标识符 uint8_t" 的静态分析告警）
constexpr std::uint8_t kFrameHdr1 = 0x57;
constexpr std::uint8_t kFrameHdr2 = 0xAB;

// TYPE 定义
constexpr std::uint8_t kTypeKeyboardEvent = 0x01;  // RP2040 → Host: 按键事件
constexpr std::uint8_t kTypePing          = 0x02;  // Host → RP2040: PING 请求
constexpr std::uint8_t kTypePong          = 0x03;  // RP2040 → Host: PONG 回应

// keyboard event DATA 长度（固定 3 字节）
constexpr std::size_t kKeyboardDataLen = 3;

// PING/PONG DATA 长度（固定 1 字节 payload）
constexpr std::size_t kPingPongDataLen = 1;

// 一个 keyboard event frame 的总长度
// 2(header) + 1(len) + 1(type) + 3(data) + 1(checksum) = 8
constexpr std::size_t kKeyboardFrameLen =
    2 + 1 + 1 + kKeyboardDataLen + 1;  // 8

// PING/PONG frame 总长度
// 2(header) + 1(len) + 1(type) + 1(data) + 1(checksum) = 6
constexpr std::size_t kPingPongFrameLen =
    2 + 1 + 1 + kPingPongDataLen + 1;  // 6

// —— UART0 引脚和波特率定义为实现细节，保存在 uart_protocol.cpp ——
// （避免与 output/uart_logger.h 中已存在的同名常量冲突，二者共享同一硬件 UART0）

// ===== 公共 API =====

// 初始化 UART0 协议层（需在发送任何帧之前调用一次）
// - UART0: 115200/8N1, TX=GPIO0, RX=GPIO1
// - FIFO enabled for stable output timing
void uart_protocol_init();

// 编码并发送一个 keyboard event 二进制帧
// 帧内容：57 AB 08 01 <usage> <pressed> <modifiers> <XOR>
// 实现保证：
//   - 原子调用：单次 uart_write_blocking 输出整个 frame，避免 printf 干扰
//   - 不通过 printf 打印 key 信息
void uart_send_key_event(const usb_host::key_event& e);

// 发送 PONG 回应帧
// 帧内容：57 AB 06 03 <payload> <XOR>
// payload: 收到的 PING payload，原样回显
void uart_send_pong(std::uint8_t payload);

// 发送 PING 帧（RP2040 主动发向主机，心跳/存活检测）
// 帧内容：57 AB 10 03（4 字节原始帧，无 LEN/TYPE/CHECKSUM）
void uart_send_ping();

// UART RX 轮询（在主循环中调用）
// 状态机解析接收帧，自动处理 PING → PONG
// 非阻塞：每次调用最多读取 1 字节，逐步推进状态
void uart_poll_rx();

// 是否已初始化（用于上层断言 / 调试检查）
bool uart_protocol_is_initialized();

} // namespace output

#endif // OUTPUT_UART_PROTOCOL_H
