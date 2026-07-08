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
#include <array>
#include "../usb_host/usb_callbacks.h"

namespace hid
{

    // ---- Modifier bit 常量（HID 1.11 规范） ----
    // Byte 0 of boot keyboard report
    constexpr uint8_t kModifierLeftCtrl = 0x01;
    constexpr uint8_t kModifierLeftShift = 0x02;
    constexpr uint8_t kModifierLeftAlt = 0x04;
    constexpr uint8_t kModifierLeftGui = 0x08;
    constexpr uint8_t kModifierRightCtrl = 0x10;
    constexpr uint8_t kModifierRightShift = 0x20;
    constexpr uint8_t kModifierRightAlt = 0x40;
    constexpr uint8_t kModifierRightGui = 0x80;

    // ---- Boot protocol keyboard report 长度 ----
    // Byte[0]: modifiers
    // Byte[1]: reserved (OEM)
    // Byte[2..7]: keycodes (6 keys)
    constexpr size_t kBootReportSize = 8;

    // ---- Usage code 特殊值 ----
    constexpr uint8_t kUsageNoEvent = 0x00; // 无按键
    constexpr uint8_t kUsageErrorRollOver = 0x01;
    constexpr uint8_t kUsagePostFail = 0x02;
    constexpr uint8_t kUsageErrorUndef = 0x03;

    // ---- NKRO 常量 ----
    // NKRO 位图最多支持 232 个按键 (0x00-0xE7)，需要 29 字节位图
    constexpr size_t kNkroMaxBitmapBits = 256;
    constexpr size_t kNkroMaxBitmapBytes = kNkroMaxBitmapBits / 8;
    // 第一个 usable keycode 是 0x04 (Keyboard A)，最后一个是 0xE7 (Right GUI)
    constexpr uint8_t kNkroFirstUsage = 0x04;
    constexpr uint8_t kNkroLastUsage = 0xE7;

    // ---- NKRO 检测结果 ----
    struct NkroReportInfo {
        bool detected = false;
        uint8_t report_id = 0;
        uint16_t total_bits = 0;
    };

    // 解析 HID Report Descriptor，检测是否存在 NKRO 报告
    // 返回 NKRO 报告的 Report ID 和位图位数
    NkroReportInfo detectNkroReport(const uint8_t* desc, uint16_t desc_len);

    // ---- 解析器状态 ----
    // 跟踪上一 report 的 keycodes，用于区分 press / release
    // 同时记录 modifiers 变化事件
    class HidBootKeyboardParser
    {
    public:
        HidBootKeyboardParser();

        // 重置内部状态（设备重新挂载时调用）
        void reset();

        // 解析一个 boot protocol report
        // 参数：report pointer，长度必须 >= kBootReportSize
        // 对每一个 press/release 变化，调用 on_event 回调
        void parse(const uint8_t *report, size_t len,
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

    // ---- NKRO (N-Key Rollover) 位图解析器 ----
    // 解析 NKRO HID 报告（位图格式，每个 bit 代表一个按键）
    // 输出与 Boot Protocol 完全相同的 key_event 结构，下游无需修改
    // 标准布局：
    //   - 若 Report ID 存在，第一个字节为 Report ID
    //   - 接下来通常是 1 字节 modifiers
    //   - 然后是位图（每个 bit = 一个按键，bit index = usage_code - 0x04）
    class HidNkroKeyboardParser
    {
    public:
        HidNkroKeyboardParser();

        // 重置内部状态（设备重新挂载时调用）
        void reset();

        // 解析一个 NKRO bitmap report
        // 参数：report pointer，完整报告长度
        // 对每一个 press/release 变化，调用 on_event 回调
        // 注意：如果报告包含 Report ID，report 指针已经包含它，len 包含它
        void parse(const uint8_t* report, size_t len,
                   usb_host::KeyEventCallback on_event,
                   bool report_id_present = true);

    private:
        // 上一次的位图状态（每个 bit 代表一个按键）
        std::array<uint8_t, kNkroMaxBitmapBytes> last_bitmap_;
        // 上一次的 modifiers（用于检测 modifiers 变化）
        uint8_t last_modifiers_;
        // 是否为第一个 report（首份报告不输出事件）
        bool first_report_;
    };

} // namespace hid

#endif // HID_HID_PARSER_H