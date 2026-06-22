#include "hid_encoder.h"
#include "uart_protocol.h"
#include <cstring>

namespace output {

// ===== CRC-8 实现 =====
// 多项式：0x07 (x^8 + x^7 + x^2 + x + 1)
// 初始值：0x00
// 无 reflect，无 final XOR
std::uint8_t crc8(const std::uint8_t* data, std::size_t len) {
    std::uint8_t crc = 0x00;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// ===== HidEncoder 实现 =====

HidEncoder::HidEncoder() : seq_(0) {}

std::size_t HidEncoder::encodeTo(const usb_host::key_event& e, std::uint8_t* buf,
                                  std::size_t buf_size) {
    if (buf_size < kHidFrameSize) {
        return 0;
    }

    // 帧头
    buf[0] = kHidFrameHdr1;  // 0x57
    buf[1] = kHidFrameHdr2;  // 0xAB
    buf[2] = kHidFrameType;  // 0x71

    // Payload
    buf[3] = seq_;              // SEQ
    buf[4] = kHidSubTypeKbd;    // TYPE = 0x01 (keyboard)
    buf[5] = e.usage_code;      // USAGE
    buf[6] = e.pressed ? 0x01 : 0x00;  // PRESSED
    buf[7] = e.modifiers;       // MODIFIER

    // CRC8：计算范围 byte[2..7]（0x71 到 MODIFIER，共 6 字节）
    buf[8] = crc8(&buf[kHidCrcStart], kHidCrcLen);

    // 序列号自增（0..255 循环）
    ++seq_;

    return kHidFrameSize;
}

void HidEncoder::encodeAndSend(const usb_host::key_event& e) {
    std::uint8_t buf[kHidFrameSize];
    std::size_t len = encodeTo(e, buf, kHidFrameSize);
    if (len == kHidFrameSize) {
        uart_send_frame(buf, len);
    }
}

// ===== 便捷函数 =====
void hid_encoder_send(const usb_host::key_event& e) {
    static HidEncoder encoder;
    encoder.encodeAndSend(e);
}

} // namespace output
