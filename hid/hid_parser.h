#ifndef HID_HID_PARSER_H
#define HID_HID_PARSER_H

// ===== HID 解析层 =====
// 解析 USB HID keyboard boot protocol report
// 不做 ASCII 转换，只输出 raw key_event

#if __cplusplus < 201703L
#error "hid_parser requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>
#include "../usb_host/usb_callbacks.h"

namespace hid {

// ---- Modifier bit 常量（HID 1.11 规范） ----
// Byte 0 of boot keyboard report
constexpr uint8_t kModifierLeftCtrl   = 0x01;
constexpr uint8_t kModifierLeftShift  = 0x02;
constexpr uint8_t kModifierLeftAlt    = 0x04;
constexpr uint8_t kModifierLeftGui    = 0x08;
constexpr uint8_t kModifierRightCtrl  = 0x10;
constexpr uint8_t kModifierRightShift = 0x20;
constexpr uint8_t kModifierRightAlt   = 0x40;
constexpr uint8_t kModifierRightGui   = 0x80;

// ---- Boot protocol keyboard report 长度 ----
// Byte[0]: modifiers
// Byte[1]: reserved (OEM)
// Byte[2..7]: keycodes (6 keys)
constexpr size_t kBootReportSize = 8;

// ---- Usage code 特殊值 ----
constexpr uint8_t kUsageNoEvent     = 0x00; // 无按键
constexpr uint8_t kUsageErrorRollOver = 0x01;
constexpr uint8_t kUsagePostFail    = 0x02;
constexpr uint8_t kUsageErrorUndef  = 0x03;

// ---- 解析器状态 ----
// 跟踪上一 report 的 keycodes，用于区分 press / release
// 同时记录 modifiers 变化事件
class HidBootKeyboardParser {
public:
    HidBootKeyboardParser();

    // 重置内部状态（设备重新挂载时调用）
    void reset();

    // 解析一个 boot protocol report
    // 参数：report pointer，长度必须 >= kBootReportSize
    // 对每一个 press/release 变化，调用 on_event 回调
    void parse(const uint8_t* report, size_t len,
               usb_host::KeyEventCallback on_event);

    // 获取最新 modifiers（便于上层查询）
    uint8_t lastModifiers() const { return last_modifiers_; }

private:
    // 上一 report 记录的 6 个 keycode
    uint8_t last_keys_[6];
    // 上一 report 的 modifiers
    uint8_t last_modifiers_;
    // 是否为第一个 report（第一个 report 不输出事件，避免上电误报 release）
    bool first_report_;
};

} // namespace hid

#endif // HID_HID_PARSER_H
