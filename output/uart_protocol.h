#ifndef OUTPUT_UART_PROTOCOL_H
#define OUTPUT_UART_PROTOCOL_H

// ===== UART 二进制协议输出层 =====
//
// Frame format (键盘帧，带 index):
//   ┌─────────┬─────────┬──────┬──────────────┬───────┬──────────┐
//   │  HEADER │  HEADER │ TYPE │    DATA      │ Index │ CHECKSUM │
//   │  0x57   │  0xAB   │      │              │       │   SUM    │
//   └─────────┴─────────┴──────┴──────────────┴───────┴──────────┘
//
// Frame format (其他帧，不带 index):
//   ┌─────────┬─────────┬──────┬──────────────┬──────────┐
//   │  HEADER │  HEADER │ TYPE │    DATA      │ CHECKSUM │
//   │  0x57   │  0xAB   │      │              │   SUM    │
//   └─────────┴─────────┴──────┴──────────────┴──────────┘
//
// CHECKSUM = SUM of [0x57] + [0xAB] + [TYPE] + [DATA0] + ... + [DATAN-1] (+ [Index] if exists) (低8位)
//
// 设计原则：
//   - USB HID 层只产生 key_event struct（不关心 UART 编码）
//   - 本模块负责 frame 编码 & 输出
//   - 不使用 printf 输出 key 信息（仅调试信息可走 printf）
//   - 输出到 UART0：TX=GPIO0, RX=GPIO1, 921600/8N1

#if __cplusplus < 201703L
#error "uart_protocol requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"
#include "pico/time.h"

namespace output {

// ===== 帧常量 =====
constexpr std::uint8_t kFrameHdr1 = 0x57;
constexpr std::uint8_t kFrameHdr2 = 0xAB;

// 帧类型
constexpr std::uint8_t kFrameTypeKb      = 0x77;
constexpr std::uint8_t kFrameTypeDevice  = 0x71;
constexpr std::uint8_t kFrameTypeString  = 0x72;

// 键盘帧: 57 AB 77 <usage> <pressed> <index> <checksum> = 7 字节
constexpr std::size_t kKeyboardFrameLen   = 7;   // 2+1+2+1+1
// 设备帧: 57 AB 71 <dev_addr><mounted><vid><pid><bcd_usb><dev_class><dev_subclass><dev_protocol>
//         <max_pkt0><bcd_dev><num_itf><cfg_val><attr><power><itf_num><itf_class><itf_subclass>
//         <itf_protocol><interval><instance><checksum> = 28 字节
constexpr std::size_t kDeviceFrameLen     = 28;  // 2+1+24+1
// 字符串帧: 57 AB 72 <dev_addr><mfg_len><mfg(64)><prod_len><prod(64)><serial_len><serial(64)><checksum> = 200 字节
constexpr std::size_t kStringFrameLen     = 200; // 2+1+196+1

// ===== 公共 API =====

// 初始化 UART0 协议层（需在发送任何帧之前调用一次）
void uart_protocol_init();

// 编码并发送一个 keyboard event 二进制帧
// 帧内容：57 AB 77 <usage> <pressed> <index> <SUM>
// 仅发送普通按键事件，修饰键（usage 0xE0..0xE7）由调用方过滤，不进入此函数
void uart_send_key_event(const usb_host::key_event& e);

// 编码并发送一个设备事件二进制帧
// 帧内容：57 AB 71 <dev_addr><mounted><vid><pid>...<checksum>
void uart_send_device_info(const usb_host::device_info& info, bool mounted);

// 编码并发送一个字符串描述符帧
// 帧内容：57 AB 72 <dev_addr><mfg_len><mfg(16)><prod_len><prod(16)><serial_len><serial(16)><checksum>
void uart_send_device_strings(uint8_t dev_addr, const usb_host::device_strings& strings);

// 通用帧发送接口
void uart_send_frame(const std::uint8_t* data, std::size_t len);

// 是否已初始化
bool uart_protocol_is_initialized();

// 获取最后一次 UART TX 的时间戳（用于 LED 闪烁指示）
absolute_time_t get_last_uart_tx_time();

} // namespace output

#endif // OUTPUT_UART_PROTOCOL_H