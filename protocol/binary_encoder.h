#ifndef OUTPUT_BINARY_ENCODER_H
#define OUTPUT_BINARY_ENCODER_H

// ===== 轻量级二进制编码器 =====
// 接收 HID 层产生的 key_event struct，将其编码为 57AB 风格的二进制帧，
// 然后通过 UART0 发送。与 uart/uart_protocol.h 中的 CH9350L 协议帧
// 保持一致，便于下游主控（PC / MCU）以同一套解析器消费。
//
// 57AB keyboard event 帧格式（总共 8 字节，固定长度）：
//   ┌──────┬──────┬──────┬──────┬─────────┬─────────┬───────────┬──────────┐
//   │ 0x57 │ 0xAB │ LEN  │ TYPE │ usage   │ pressed │ modifiers │ CHECKSUM │
//   │ HDR1 │ HDR2 │=0x08 │=0x01 │(HID使用)│ 0x00/01 │ (bitmask) │ XOR 校验 │
//   └──────┴──────┴──────┴──────┴─────────┴─────────┴───────────┴──────────┘
//
//   LEN      = 0x08（总帧长度，含 HDR/HDR/LEN/TYPE/DATA/CHECKSUM）
//   TYPE     = 0x01（keyboard event 类型，与 uart_protocol.h 保持一致）
//   usage    = HID usage code（普通键 0x04..0x73，修饰键 0xE0..0xE7）
//   pressed  = 0x01 = 按下，0x00 = 释放
//   modifiers= 原始 HID boot report byte0（bit0=L_CTRL ... bit7=R_GUI）
//   CHECKSUM = 前面 7 个字节的 XOR 累积
//
// 设计原则：
//   * 零堆分配：内部使用静态 8 字节缓冲区，单次原子 uart_write_blocking 输出
//   * 零 printf：编码过程完全不产生文本，不会干扰下游二进制解析器
//   * 轻量：encoder 只依赖 key_event struct，不直接操作 HID report
//   * 可插拔：如果未来需要切换到 USB CDC / SPI 等输出，只需替换 encoder
//
// 与 uart/uart_protocol.h 的关系：
//   uart_protocol 负责 UART0 硬件初始化（921600/8N1，TX=GPIO0/RX=GPIO1）
//   以及下行 PING/PONG 轮询；binary_encoder 仅负责上行 event 帧编码与发送。

#if __cplusplus < 201703L
#error "binary_encoder requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"
#include "../uart/uart_protocol.h"  // 使用 uart_protocol.h 中定义的 kFrameHdr1/kFrameHdr2

namespace output {

// BinaryEncoder 使用的常量（8 字节帧，XOR 校验）
constexpr std::size_t  kFrameSize        = 8;      // 固定 8 字节
constexpr std::uint8_t kFrameLen         = 0x08;   // LEN 字段值
constexpr std::uint8_t kFrameTypeKey     = 0x01;   // TYPE = keyboard
constexpr std::uint8_t kPressedDown      = 0x01;   // 按下
constexpr std::uint8_t kPressedUp        = 0x00;   // 释放

// ===== 二进制编码器类 =====
// 负责把 key_event 编码为 8 字节帧并原子写入 UART0。
// 单例使用即可；无多线程安全保证（RP2040 此处为阻塞单线程模型，够用）。
class BinaryEncoder {
public:
    BinaryEncoder();
    ~BinaryEncoder() = default;

    // 禁止复制（避免两份实例共享同一缓冲区造成帧撕裂）
    BinaryEncoder(const BinaryEncoder&) = delete;
    BinaryEncoder& operator=(const BinaryEncoder&) = delete;

    // 编码 key_event 到内部缓冲区并原子发送到 UART0。
    // 前置条件：UART0 必须已经由 output::uart_protocol_init() 初始化。
    void encodeAndSend(const usb_host::key_event& e);

    // 直接编码到调用方提供的缓冲区（便于单元测试 / 其他输出介质）。
    // buf 的长度必须 >= kFrameSize（=8）；返回值 = 写入的字节数。
    std::size_t encodeTo(const usb_host::key_event& e, std::uint8_t* buf,
                         std::size_t buf_size) const;

private:
    // 计算 7 字节前缀（HDR1 HDR2 LEN TYPE D0 D1 D2）的 XOR 校验和
    static std::uint8_t computeXorChecksum(const std::uint8_t* prefix7);
};

// ===== 便捷函数 =====
// 等价于 BinaryEncoder{}.encodeAndSend(e)，用于对简洁性要求高的调用点
// （例如 main.cpp 的 onKeyEvent 回调）。
void binary_encoder_send(const usb_host::key_event& e);

} // namespace output

#endif // OUTPUT_BINARY_ENCODER_H