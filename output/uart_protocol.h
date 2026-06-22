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

// ===== CH9350L 风格帧常量 =====
// 注: 使用 std::uint8_t / std::size_t 以严格符合 C++ <cstdint> 规范
//     （消除 IDE "未定义标识符 uint8_t" 的静态分析告警）
constexpr std::uint8_t kFrameHdr1 = 0x57;
constexpr std::uint8_t kFrameHdr2 = 0xAB;

// TYPE 定义
constexpr std::uint8_t kTypeKeyboardEvent = 0x01;
constexpr std::uint8_t kTypePing          = 0x02;
constexpr std::uint8_t kTypePong          = 0x03;
constexpr std::uint8_t kTypeDeviceMount   = 0x04;
constexpr std::uint8_t kTypeDeviceUmount  = 0x05;
constexpr std::uint8_t kTypeDeviceInfo    = 0x06;  // USB 设备详细信息

// 帧长度常量
constexpr std::size_t kKeyboardFrameLen   = 8;   // 2+1+1+3+1
constexpr std::size_t kPingPongFrameLen   = 6;   // 2+1+1+1+1
constexpr std::size_t kDeviceEventFrameLen= 6;   // 2+1+1+1+1
constexpr std::size_t kDeviceInfoFrameLen = 14;  // 2+1+1+9+1 (dev_addr+VID+PID+bInterval+itf_num+itf_protocol+instance)

// ===== 公共 API =====

// 初始化 UART0 协议层（需在发送任何帧之前调用一次）
// - UART0: 9600/8N1, TX=GPIO0, RX=GPIO1
// - FIFO enabled for stable output timing
void uart_protocol_init();

// 编码并发送一个 keyboard event 二进制帧
// 帧内容：57 AB 08 01 <usage> <pressed> <modifiers> <XOR>
// 实现保证：
//   - 原子调用：单次 uart_write_blocking 输出整个 frame，避免 printf 干扰
//   - 不通过 printf 打印 key 信息
void uart_send_key_event(const usb_host::key_event& e);

// 发送一个 PING 帧（上行 TX 原始帧：57 AB 10 03）
void uart_send_ping();

// 发送一个 PONG 帧（DATA 携带 payload）
// 帧：57 AB 06 03 <payload> <XOR>
void uart_send_pong(std::uint8_t payload);

// 发送设备挂载通知帧
// 帧：57 AB 06 04 <dev_addr> <XOR>
void uart_send_device_mount(std::uint8_t dev_addr);

// 发送设备卸载通知帧
// 帧：57 AB 06 05 <dev_addr> <XOR>
void uart_send_device_umount(std::uint8_t dev_addr);

// 发送设备详细信息帧（包含 VID/PID/bInterval/itf_num/itf_protocol/instance）
// 帧：57 AB 0B 06 <dev_addr> <vid_low> <vid_high> <pid_low> <pid_high> <bInterval> <itf_num> <itf_protocol> <instance> <XOR>
void uart_send_device_info(std::uint8_t dev_addr, uint16_t vid, uint16_t pid, uint8_t bInterval, uint8_t itf_num, uint8_t itf_protocol, uint8_t instance);

// 通用帧发送接口（供 hid_encoder 等模块使用）
// 直接发送原始字节流到 UART0
void uart_send_frame(const std::uint8_t* data, std::size_t len);

// 非阻塞 UART RX 轮询：处理下行 PING/PONG 命令
// 每次调用最多读取并处理 1 字节
void uart_poll_rx();

// 是否已初始化（用于上层断言 / 调试检查）
bool uart_protocol_is_initialized();

} // namespace output

#endif // OUTPUT_UART_PROTOCOL_H
