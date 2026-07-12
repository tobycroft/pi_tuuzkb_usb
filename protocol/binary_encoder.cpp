// ===== 轻量级二进制编码器实现 =====
// 将 HID 层产生的 key_event 编码为 8 字节 57AB 帧，并原子写入 UART0。
//
// 帧布局（与 uart/uart_protocol.h 中定义的 keyboard event 帧完全一致）：
//   byte0: 0x57
//   byte1: 0xAB
//   byte2: 0x08            (LEN = 总帧长度)
//   byte3: 0x01            (TYPE = keyboard event)
//   byte4: usage_code      (HID usage, 0x04..0x73 / 0xE0..0xE7)
//   byte5: pressed         (0x01 按下, 0x00 释放)
//   byte6: modifiers       (原始 HID boot report byte0)
//   byte7: XOR checksum    (byte0 ^ ... ^ byte6)
//
// 为什么做"规范化 pressed"：
//   C++ bool 可能是 0/1，但也可能由编译器以非零真值构造（例如 memcpy 填充）。
//   协议下游按精确字节解析，所以必须显式归一化为 0x00 / 0x01。
//
// 为什么使用 uart_write_blocking：
//   对 UART0（921600 baud，TX FIFO 已开启）执行单次 8 字节写入：
//     * 8 bytes @ 921600 baud (10 bit/byte) ≈ 86.8 µs
//     * FIFO 缓冲足够，无需担心写失败
//     * 单次调用保证原子性，不会被 stdio printf 或其他线程插入字节

#include "binary_encoder.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"

#include <cstdint>
#include <cstddef>
#include <cstring>

namespace output {

namespace {

// 填充一个完整的 8 字节键盘帧到 buf（buf 必须 >= 8 字节）。
// 返回值：写入的字节数 = kFrameSize。
inline std::size_t fillKeyboardFrame(std::uint8_t* buf,
                                     std::uint8_t usage,
                                     std::uint8_t pressed_byte,
                                     std::uint8_t modifiers) {
    buf[0] = kFrameHdr1;
    buf[1] = kFrameHdr2;
    buf[2] = kFrameLen;
    buf[3] = kFrameTypeKey;
    buf[4] = usage;
    buf[5] = pressed_byte;
    buf[6] = modifiers;

    // CHECKSUM = XOR 累积前 7 字节
    std::uint8_t xor_sum = 0;
    for (int i = 0; i < 7; ++i) {
        xor_sum ^= buf[i];
    }
    buf[7] = xor_sum;
    return kFrameSize;
}

// pressed 字段规范化：C++ bool → 0x00 / 0x01
inline std::uint8_t normalizePressed(bool pressed) {
    return pressed ? kPressedDown : kPressedUp;
}

} // namespace

BinaryEncoder::BinaryEncoder() = default;

std::uint8_t BinaryEncoder::computeXorChecksum(const std::uint8_t* prefix7) {
    std::uint8_t xor_sum = 0;
    for (int i = 0; i < 7; ++i) {
        xor_sum ^= prefix7[i];
    }
    return xor_sum;
}

std::size_t BinaryEncoder::encodeTo(const usb_host::key_event& e,
                                    std::uint8_t* buf,
                                    std::size_t buf_size) const {
    if (buf == nullptr || buf_size < kFrameSize) {
        return 0;
    }
    const std::uint8_t pressed_byte = normalizePressed(e.pressed);
    return fillKeyboardFrame(buf, e.usage_code, pressed_byte, e.modifiers);
}

void BinaryEncoder::encodeAndSend(const usb_host::key_event& e) {
    // 线程局部静态缓冲区：避免在栈上重复声明，同时支持同一执行流的重入
    // （在 RP2040 单线程阻塞模型下，静态缓冲区是安全且高效的选择）
    static std::uint8_t frame_buf[kFrameSize];

    const std::uint8_t pressed_byte = normalizePressed(e.pressed);
    fillKeyboardFrame(frame_buf, e.usage_code, pressed_byte, e.modifiers);

    // 原子写入 UART0：一次调用 8 字节，保证帧完整性
    // 使用 uart_send_frame 以利用 DMA 加速（如果可用）
    uart_send_frame(frame_buf, kFrameSize);
}

// ---- 便捷函数：等价于 BinaryEncoder{}.encodeAndSend(e) ----
void binary_encoder_send(const usb_host::key_event& e) {
    // 使用局部实例以避免静态全局构造顺序问题；
    // BinaryEncoder 是无状态的（只有成员函数 + 内部函数局部静态 buffer），
    // 构造/析构成本为 0。
    static BinaryEncoder s_encoder;
    s_encoder.encodeAndSend(e);
}

} // namespace output