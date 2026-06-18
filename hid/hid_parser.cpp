#include "hid_parser.h"

#include <cstring>

namespace hid {

HidBootKeyboardParser::HidBootKeyboardParser()
    : last_modifiers_(0)
    , first_report_(true) {
    std::memset(last_keys_, 0, sizeof(last_keys_));
}

void HidBootKeyboardParser::reset() {
    std::memset(last_keys_, 0, sizeof(last_keys_));
    last_modifiers_ = 0;
    first_report_ = true;
}

static bool keyInArray(uint8_t key, const uint8_t* arr, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (arr[i] == key) return true;
    }
    return false;
}

void HidBootKeyboardParser::parse(const uint8_t* report, size_t len,
                                  usb_host::KeyEventCallback on_event) {
    if (report == nullptr || len < kBootReportSize || on_event == nullptr) {
        return;
    }

    const uint8_t modifiers = report[0];
    const uint8_t* keys = &report[2]; // Byte 2..7

    // ----- modifiers 变化：触发 modifier key press / release -----
    // 把 modifiers 中的每一位看成独立按键：
    //   bit0..bit3 -> usage codes 0xE0..0xE3 (Left Ctrl/Shift/Alt/GUI)
    //   bit4..bit7 -> usage codes 0xE4..0xE7 (Right Ctrl/Shift/Alt/GUI)
    if (!first_report_ && modifiers != last_modifiers_) {
        for (uint8_t bit = 0; bit < 8; bit++) {
            const uint8_t mask = static_cast<uint8_t>(1u << bit);
            const bool was = (last_modifiers_ & mask) != 0;
            const bool now = (modifiers & mask) != 0;
            if (was == now) continue;

            usb_host::key_event ev{};
            ev.usage_code = static_cast<uint8_t>(0xE0 + bit);
            ev.pressed = now;
            ev.modifiers = modifiers;
            on_event(ev);
        }
    }

    // ----- regular keycodes: press / release -----
    if (!first_report_) {
        // 在当前 keys 中但不在 last_keys_ 中 → press
        for (size_t i = 0; i < 6; i++) {
            const uint8_t k = keys[i];
            if (k <= kUsageErrorUndef) continue; // 跳过保留值
            if (!keyInArray(k, last_keys_, 6)) {
                usb_host::key_event ev{};
                ev.usage_code = k;
                ev.pressed = true;
                ev.modifiers = modifiers;
                on_event(ev);
            }
        }
        // 在 last_keys_ 中但不在当前 keys 中 → release
        for (size_t i = 0; i < 6; i++) {
            const uint8_t k = last_keys_[i];
            if (k <= kUsageErrorUndef) continue;
            if (!keyInArray(k, keys, 6)) {
                usb_host::key_event ev{};
                ev.usage_code = k;
                ev.pressed = false;
                ev.modifiers = modifiers;
                on_event(ev);
            }
        }
    }

    // 保存状态
    std::memcpy(last_keys_, keys, 6);
    last_modifiers_ = modifiers;
    first_report_ = false;
}

} // namespace hid
