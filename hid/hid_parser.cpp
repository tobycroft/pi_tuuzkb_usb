#include "hid_parser.h"

#include <cstring>

namespace hid {

/**
 * 构造 HidBootKeyboardParser，将解析器状态初始化为"尚未接收过任何报告"
 *
 * 初始化策略：
 *   - last_modifiers_ = 0     修饰键位域清零，表示无任何 Ctrl/Shift/Alt/GUI 被按下
 *   - first_report_ = true    首份报告标志，parse() 中据此跳过事件输出，
 *                             避免上电时与全零历史状态比较而误报全量 release
 *   - last_keys_[] = 全 0     6 字节 keycode 历史清零，0x00 即 kUsageNoEvent（无按键）
 */
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

/**
 * 解析 HID Boot Protocol 键盘报告，检测按键按下/释放事件并回调通知
 *
 * Boot Protocol 报告格式（8 字节，参见 HID 1.11 规范 Section B.1）：
 *   Byte[0]    — 修饰键位域（bit0..7 对应 L-Ctrl/Shift/Alt/GUI, R-Ctrl/Shift/Alt/GUI）
 *   Byte[1]    — 保留（OEM 定义）
 *   Byte[2..7] — 当前按下的普通键 Usage Code（最多 6 键同时按下）
 *
 * 解析策略：
 *   1. 首份报告仅记录状态，不触发事件（避免上电时误报全量 release）
 *   2. 修饰键：逐位对比上次报告，每一位的 0→1 为 press，1→0 为 release
 *      bit N 映射到 Usage Code 0xE0+N（0xE0..0xE7 为 HID 修饰键 Usage 页）
 *   3. 普通键：集合差运算
 *      当前报告有、上次没有 → press
 *      上次报告有、当前没有 → release
 *   4. Usage Code 0x00..0x03 为保留/错误值，跳过不处理
 *
 * @param report   指向 8 字节 boot report 的指针，不可为 nullptr
 * @param len      report 数据长度，必须 >= kBootReportSize (8)
 * @param on_event 按键事件回调，每个 press/release 变化触发一次调用，不可为 nullptr
 */
void HidBootKeyboardParser::parse(const uint8_t* report, size_t len,
                                  usb_host::KeyEventCallback on_event) {
    if (report == nullptr || len < kBootReportSize || on_event == nullptr) {
        return;
    }

    const uint8_t modifiers = report[0];
    const uint8_t* keys = &report[2];

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

    if (!first_report_) {
        for (size_t i = 0; i < 6; i++) {
            const uint8_t k = keys[i];
            if (k <= kUsageErrorUndef) continue;
            if (!keyInArray(k, last_keys_, 6)) {
                usb_host::key_event ev{};
                ev.usage_code = k;
                ev.pressed = true;
                ev.modifiers = modifiers;
                on_event(ev);
            }
        }
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

    std::memcpy(last_keys_, keys, 6);
    last_modifiers_ = modifiers;
    first_report_ = false;
}

} // namespace hid