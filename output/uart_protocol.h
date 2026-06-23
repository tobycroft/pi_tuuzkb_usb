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
//
// CHECKSUM = XOR of [0x57] ^ [0xAB] ^ [LEN] ^ [TYPE] ^ [DATA0] ^ ... ^ [DATAN-1]
//
// 设计原则：
//   - USB HID 层只产生 key_event struct（不关心 UART 编码）
//   - 本模块负责 frame 编码 & 输出
//   - 不使用 printf 输出 key 信息（仅调试信息可走 printf）
//   - 输出到 UART0：TX=GPIO0, RX=GPIO1, 9600/8N1

#if __cplusplus < 201703L
#error "uart_protocol requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace output {

// ===== 帧常量 =====
// 注: 使用 std::uint8_t / std::size_t 以严格符合 C++ <cstdint> 规范
//     （消除 IDE "未定义标识符 uint8_t" 的静态分析告警）
constexpr std::uint8_t kFrameHdr1 = 0x57;
constexpr std::uint8_t kFrameHdr2 = 0xAB;

// 键盘数据帧使用三字节头 57 AB 77（自定义协议，与 CH9350 区分）
constexpr std::uint8_t kFrameHdr3Kb = 0x77;

// 设备事件帧使用三字节头 57 AB 71（自定义协议，完整设备描述符）
constexpr std::uint8_t kFrameHdr3Dev = 0x71;

// 帧长度常量
// 键盘帧: 57 AB 77 <usage> <pressed> <modifiers> <checksum> = 7 字节
constexpr std::size_t kKeyboardFrameLen   = 7;   // 3+3+1
// 设备帧: 57 AB 71 <dev_addr><mounted><vid><pid><bcd_usb><dev_class><dev_subclass><dev_protocol>
//         <max_pkt0><bcd_dev><num_itf><cfg_val><attr><power><itf_num><itf_class><itf_subclass>
//         <itf_protocol><interval><instance><checksum> = 28 字节
constexpr std::size_t kDeviceFrameLen     = 28;  // 3+24+1

// ===== 公共 API =====

// 初始化 UART0 协议层（需在发送任何帧之前调用一次）
// - UART0: 9600/8N1, TX=GPIO0, RX=GPIO1
// - FIFO enabled for stable output timing
void uart_protocol_init();

// 编码并发送一个 keyboard event 二进制帧
// 帧内容：57 AB 77 <usage> <pressed> <modifiers> <XOR>
// 实现保证：
//   - 原子调用：单次 uart_write_blocking 输出整个 frame，避免 printf 干扰
//   - 不通过 printf 打印 key 信息
void uart_send_key_event(const usb_host::key_event& e);

// 编码并发送一个设备事件二进制帧
// 帧内容：57 AB 71 <dev_addr><mounted><vid><pid><bcd_usb><dev_class><dev_subclass><dev_protocol>
//         <max_pkt0><bcd_dev><num_itf><cfg_val><attr><power><itf_num><itf_class><itf_subclass>
//         <itf_protocol><interval><instance><checksum>
// 参数：info - 设备信息（包含完整描述符），mounted - true=设备插入，false=设备拔出
void uart_send_device_info(const usb_host::device_info& info, bool mounted);

// 通用帧发送接口（供 hid_encoder 等模块使用）
// 直接发送原始字节流到 UART0
void uart_send_frame(const std::uint8_t* data, std::size_t len);

// 是否已初始化（用于上层断言 / 调试检查）
bool uart_protocol_is_initialized();

} // namespace output

#endif // OUTPUT_UART_PROTOCOL_H
