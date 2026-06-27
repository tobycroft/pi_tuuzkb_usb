#ifndef OUTPUT_HID_ENCODER_H
#define OUTPUT_HID_ENCODER_H

// ===== 0x71 HID Input Event Encoder =====
//
// 独立数据通道：将 USB HID keyboard event 编码为 0x71 帧并通过 UART 输出。
// 与 0x81 设备状态/连接协议完全独立，互不干扰。
//
// Frame format（9 bytes，固定长度）：
//   ┌──────┬──────┬──────┬─────┬──────┬───────┬─────────┬──────────┬───────┐
//   │ 0x57 │ 0xAB │ 0x71 │ SEQ │ TYPE │ USAGE │ PRESSED │ MODIFIER │ CRC8  │
//   │ HDR1 │ HDR2 │ TYPE │     │=0x01 │       │ 0x00/01 │ (bitmap) │       │
//   └──────┴──────┴──────┴─────┴──────┴───────┴─────────┴──────────┴───────┘
//
//   HDR1/HDR2 = 0x57 0xAB（帧头，与 0x81 共享）
//   TYPE      = 0x71（HID input event stream 标识）
//   SEQ       = uint8_t 自增序列号（0..255 循环），用于 UDP reorder / debug
//   TYPE      = 0x01（keyboard，预留 mouse/consumer 扩展）
//   USAGE     = HID usage code（1 byte）
//   PRESSED   = 0x01 = down, 0x00 = up
//   MODIFIER  = HID boot report byte0 modifier bitmap
//   CRC8      = CRC-8 校验，计算范围：byte[2..7]（0x71 到 MODIFIER，共 6 字节）
//               多项式：x^8 + x^7 + x^2 + x + 1 (0x07)，初始值 0x00
//
// 设计约束：
//   * 每个 HID event 立即生成一个 frame，不允许 batch
//   * 不允许 malloc / STL
//   * 必须 real-time safe（无动态分配，无锁，固定 9 字节栈/静态缓冲区）
//   * 与 0x81 逻辑完全独立

#if __cplusplus < 201703L
#error "hid_encoder requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace output {

// ===== 0x71 帧常量 =====
constexpr std::uint8_t kHidFrameHdr1     = 0x57;
constexpr std::uint8_t kHidFrameHdr2     = 0xAB;
constexpr std::uint8_t kHidFrameType     = 0x71;  // HID input event stream
constexpr std::uint8_t kHidSubTypeKbd    = 0x01;  // keyboard
constexpr std::size_t  kHidFrameSize     = 9;      // 固定 9 字节
constexpr std::size_t  kHidCrcStart      = 2;      // CRC 计算起始偏移（0x71）
constexpr std::size_t  kHidCrcLen        = 6;      // CRC 计算长度（6 字节）

// ===== CRC-8 =====
// 多项式：0x07 (x^8 + x^7 + x^2 + x + 1)
// 初始值：0x00
// 无 reflect，无 final XOR
// 与 Go 端 crc8() 实现严格一致
std::uint8_t crc8(const std::uint8_t* data, std::size_t len);

// ===== HidEncoder 类 =====
// 负责将 key_event 编码为 0x71 帧并原子写入 UART0。
// 单例使用；无多线程安全保证（RP2040 阻塞单线程模型）。
class HidEncoder {
public:
    HidEncoder();
    ~HidEncoder() = default;

    // 禁止复制
    HidEncoder(const HidEncoder&) = delete;
    HidEncoder& operator=(const HidEncoder&) = delete;

    // 编码 key_event 为 0x71 帧并原子发送到 UART0。
    // 前置条件：UART0 已由 uart_protocol_init() 初始化。
    void encodeAndSend(const usb_host::key_event& e);

    // 编码到调用方提供的缓冲区（便于单元测试 / 其他输出介质）。
    // buf 长度必须 >= kHidFrameSize (=9)；返回值 = 写入字节数。
    std::size_t encodeTo(const usb_host::key_event& e, std::uint8_t* buf,
                         std::size_t buf_size);

    // 获取当前 SEQ 值（用于调试）
    std::uint8_t currentSeq() const { return seq_; }

private:
    std::uint8_t seq_;  // 自增序列号，0..255 循环
};

// ===== 便捷函数 =====
void hid_encoder_send(const usb_host::key_event& e);

} // namespace output

#endif // OUTPUT_HID_ENCODER_H
