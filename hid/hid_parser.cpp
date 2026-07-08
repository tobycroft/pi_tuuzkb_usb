#include "hid_parser.h"

#include <cstring>

namespace hid
{

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
        : last_modifiers_(0), first_report_(true)
    {
        std::memset(last_keys_, 0, sizeof(last_keys_));
    }

    /**
     * 重置解析器内部状态，等效于重新构造
     *
     * 适用场景：USB 设备断开后重新挂载、或总线复位时调用，
     * 使解析器回到"尚未接收过任何报告"的初始状态，
     * 避免残留的旧按键状态与新一轮报告比较而产生误报
     */
    void HidBootKeyboardParser::reset()
    {
        std::memset(last_keys_, 0, sizeof(last_keys_));
        last_modifiers_ = 0;
        first_report_ = true;
    }

    /**
     * 在 keycode 数组中线性查找指定键值
     *
     * 用于 parse() 中集合差运算：判断某个 Usage Code 是否存在于
     * 当前报告或上一报告的 6 键数组中，从而区分 press / release
     *
     * @param key 待查找的 Usage Code
     * @param arr keycode 数组指针（通常为 keys[] 或 last_keys_[]）
     * @param n   数组长度（Boot Protocol 固定为 6）
     * @return true 存在，false 不存在
     */
    static bool keyInArray(uint8_t key, const uint8_t *arr, size_t n)
    {
        for (size_t i = 0; i < n; i++)
        {
            if (arr[i] == key)
                return true;
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
    void HidBootKeyboardParser::parse(const uint8_t *report, size_t len, usb_host::KeyEventCallback on_event)
    {
        if (report == nullptr || len < kBootReportSize || on_event == nullptr)
        {
            return;
        }

        const uint8_t modifiers = report[0];
        const uint8_t *keys = &report[2];

        if (!first_report_ && modifiers != last_modifiers_)
        {
            for (uint8_t bit = 0; bit < 8; bit++)
            {
                const uint8_t mask = static_cast<uint8_t>(1u << bit);
                const bool was = (last_modifiers_ & mask) != 0;
                const bool now = (modifiers & mask) != 0;
                if (was == now)
                    continue;

                usb_host::key_event ev{};
                ev.usage_code = static_cast<uint8_t>(0xE0 + bit);
                ev.pressed = now;
                ev.modifiers = modifiers;
                on_event(ev);
            }
        }

        if (!first_report_)
        {
            for (size_t i = 0; i < 6; i++)
            {
                const uint8_t k = keys[i];
                if (k <= kUsageErrorUndef)
                    continue;
                if (!keyInArray(k, last_keys_, 6))
                {
                    usb_host::key_event ev{};
                    ev.usage_code = k;
                    ev.pressed = true;
                    ev.modifiers = modifiers;
                    on_event(ev);
                }
            }
            for (size_t i = 0; i < 6; i++)
            {
                const uint8_t k = last_keys_[i];
                if (k <= kUsageErrorUndef)
                    continue;
                if (!keyInArray(k, keys, 6))
                {
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

    // -------------------------------------------------------------------------
    // HID Report Descriptor 解析 —— 检测 NKRO 报告
    // -------------------------------------------------------------------------

    /**
     * 解析 HID Report Descriptor，检测是否存在 NKRO 报告
     *
     * 原理：遍历描述符中的所有 INPUT 项，累计每个 Report ID 的总位数。
     * 若某个 Report ID 的总位数 > 64（8 字节），则判定为 NKRO 位图报告。
     *
     * HID Descriptor Item 格式（prefix byte）：
     *   Bits 0-1: data size (0=0, 1=1, 2=2, 3=4 bytes)
     *   Bits 2-3: item type (0=Main, 1=Global, 2=Local)
     *   Bits 4-7: item tag
     *
     * 关键 Global 项：
     *   REPORT_ID     (tag=8, type=1): 0x84 | size
     *   REPORT_SIZE   (tag=7, type=1): 0x74 | size
     *   REPORT_COUNT  (tag=9, type=1): 0x94 | size
     *
     * 关键 Main 项：
     *   INPUT (tag=8, type=0): 0x80 | size
     *
     * @param desc     指向 HID Report Descriptor 字节数组
     * @param desc_len 描述符长度
     * @return NkroReportInfo 检测结果
     */
    NkroReportInfo detectNkroReport(const uint8_t* desc, uint16_t desc_len) {
        NkroReportInfo result;

        if (desc == nullptr || desc_len == 0) {
            return result;
        }

        constexpr size_t kMaxReports = 4;
        struct ReportBits {
            uint8_t report_id;
            uint16_t total_bits;
        };
        ReportBits reports[kMaxReports] = {};
        size_t report_count = 0;

        uint8_t  current_report_id = 0;
        uint32_t current_report_size = 0;
        uint32_t current_report_count = 0;

        const uint8_t* p = desc;
        const uint8_t* end = desc + desc_len;

        while (p < end) {
            const uint8_t prefix = *p;
            uint8_t data_size = prefix & 0x03u;
            if (data_size == 3) data_size = 4;
            const uint8_t item_type = (prefix >> 2) & 0x03u;
            const uint8_t item_tag  = (prefix >> 4) & 0x0Fu;

            p++;
            if (p + data_size > end) break;

            uint32_t data = 0;
            if (data_size >= 1) { data |= *p; p++; }
            if (data_size >= 2) { data |= static_cast<uint32_t>(*p) << 8;  p++; }
            if (data_size >= 4) { data |= static_cast<uint32_t>(*p) << 16; p++; }

            if (item_type == 1) { // Global
                switch (item_tag) {
                    case 8: // REPORT_ID
                        current_report_id = static_cast<uint8_t>(data);
                        break;
                    case 7: // REPORT_SIZE
                        current_report_size = data;
                        break;
                    case 9: // REPORT_COUNT
                        current_report_count = data;
                        break;
                    default:
                        break;
                }
            }
            else if (item_type == 0) { // Main
                if (item_tag == 8) { // INPUT
                    const uint16_t bits = static_cast<uint16_t>(current_report_size * current_report_count);

                    bool found = false;
                    for (size_t i = 0; i < report_count; i++) {
                        if (reports[i].report_id == current_report_id) {
                            reports[i].total_bits += bits;
                            found = true;
                            break;
                        }
                    }
                    if (!found && report_count < kMaxReports) {
                        reports[report_count].report_id = current_report_id;
                        reports[report_count].total_bits = bits;
                        report_count++;
                    }
                }
            }
            // 忽略 Local (type=2) 和 Reserved (type=3)
        }

        // 选择总位数最大的报告作为 NKRO 候选
        // 条件：总位数 > 64（大于 8 字节 Boot Protocol 报告）
        for (size_t i = 0; i < report_count; i++) {
            if (reports[i].total_bits > 64 && reports[i].total_bits > result.total_bits) {
                result.report_id = reports[i].report_id;
                result.total_bits = reports[i].total_bits;
                result.detected = true;
            }
        }

        return result;
    }

    // -------------------------------------------------------------------------
    // NKRO 解析器实现
    // -------------------------------------------------------------------------

    /**
     * 构造 HidNkroKeyboardParser，初始化状态为"尚未接收过任何报告"
     *
     * 初始化策略：
     *   - 位图全部清零（表示所有按键释放）
     *   - last_modifiers_ = 0（无修饰键）
     *   - first_report_ = true（首份报告不输出事件，避免误报）
     */
    HidNkroKeyboardParser::HidNkroKeyboardParser()
        : last_modifiers_(0), first_report_(true)
    {
        std::memset(last_bitmap_.data(), 0, last_bitmap_.size());
    }

    /**
     * 重置解析器内部状态，等效于重新构造
     *
     * 适用场景：USB 设备断开后重新挂载，避免旧状态残留导致误报
     */
    void HidNkroKeyboardParser::reset()
    {
        std::memset(last_bitmap_.data(), 0, last_bitmap_.size());
        last_modifiers_ = 0;
        first_report_ = true;
    }

    /**
     * 解析 NKRO 位图报告，检测按键按下/释放事件并回调通知
     *
     * 标准 NKRO 报告布局（多数键盘采用）：
     *   [Report ID (1 byte)]  ← 可选，多数键盘有，指针和 len 包含它
     *   [modifiers (1 byte)]  ← 与 Boot Protocol 相同，bit0..7 = L/R Ctrl/Shift/Alt/GUI
     *   [bitmap ...]          ← 每个 bit 代表一个按键
     *          bit 0 of byte 0 → Usage 0x04 (A)
     *          bit 1 of byte 0 → Usage 0x05 (B)
     *          ...以此类推直到 Usage 0xE7 (Right GUI)
     *
     * 解析策略：
     *   1. 根据是否有 Report ID 跳过第一个字节，找到 modifiers 和位图起始
     *   2. 首份报告仅记录状态，不触发事件（避免上电误报）
     *   3. 对比 modifiers，逐位检测变化（与 Boot Protocol 相同）
     *   4. 位图逐 bit 对比：
     *      0→1 = press（输出事件）
     *      1→0 = release（输出事件）
     *      每个按键一个事件，保持与 Boot Protocol 下游兼容
     *   5. 更新位图和 modifiers 到 last_* 状态
     *
     * @param report          指向完整 NKRO 报告的指针，不可为 nullptr
     * @param len             完整报告长度（含 Report ID 如果存在）
     * @param on_event        按键事件回调，每个变化触发一次
     * @param report_id_present 报告是否包含 Report ID 字节（默认 true）
     */
    void HidNkroKeyboardParser::parse(const uint8_t* report, size_t len,
                                     usb_host::KeyEventCallback on_event,
                                     bool report_id_present)
    {
        if (report == nullptr || len == 0 || on_event == nullptr) {
            return;
        }

        // 定位 modifiers 和位图起始
        const size_t skip_bytes = report_id_present ? 1 : 0;
        if (len < skip_bytes + 1) {
            return;
        }

        const uint8_t modifiers = report[skip_bytes];
        const uint8_t* bitmap = report + skip_bytes + 1;
        const size_t bitmap_len = len - skip_bytes - 1;

        // 处理 modifiers 变化
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

        // 逐 bit 检测按键变化
        if (!first_report_) {
            size_t bit_idx = 0;
            for (size_t byte_idx = 0; byte_idx < kNkroMaxBitmapBytes; byte_idx++) {
                // 获取当前字节（报告实际长度可能小于 32 字节，缺省视为 0）
                const uint8_t curr_byte = (byte_idx < bitmap_len) ? bitmap[byte_idx] : 0;
                const uint8_t last_byte = last_bitmap_[byte_idx];

                if (curr_byte == last_byte) {
                    bit_idx += 8;
                    continue;
                }

                // 逐 bit 检查变化
                for (uint8_t b = 0; b < 8; b++) {
                    const uint8_t mask = static_cast<uint8_t>(1u << b);
                    const bool was = (last_byte & mask) != 0;
                    const bool now = (curr_byte & mask) != 0;
                    if (was == now) {
                        bit_idx++;
                        continue;
                    }

                    // bit index = usage_code - 0x04
                    const uint8_t usage_code = static_cast<uint8_t>(kNkroFirstUsage + bit_idx);
                    if (usage_code > kNkroLastUsage) {
                        bit_idx++;
                        continue;
                    }

                    // 跳过 0x00-0x03 特殊值
                    if (usage_code <= kUsageErrorUndef) {
                        bit_idx++;
                        continue;
                    }

                    usb_host::key_event ev{};
                    ev.usage_code = usage_code;
                    ev.pressed = now;
                    ev.modifiers = modifiers;
                    on_event(ev);

                    bit_idx++;
                }
            }
        }

        // 更新状态
        // 仅复制实际存在的位图字节，多余字节保持 0
        for (size_t i = 0; i < kNkroMaxBitmapBytes; i++) {
            if (i < bitmap_len) {
                last_bitmap_[i] = bitmap[i];
            } else {
                last_bitmap_[i] = 0;
            }
        }
        last_modifiers_ = modifiers;
        first_report_ = false;
    }

} // namespace hid