#ifndef ENABLE_USB
#define ENABLE_USB 1
#endif

#include "usb_callbacks.h"

#if ENABLE_USB

#include "pico/stdlib.h"          // Pico SDK
#include "../hid/hid_parser.h"     // HID 报告解析器
#include "tusb.h"                 // TinyUSB 主头文件
#include "class/hid/hid.h"        // TinyUSB HID 类

#include <cstdint>
#include <cstring>

namespace usb_host {

namespace {

constexpr size_t kMaxDevices = CFG_TUH_DEVICE_MAX;

// 设备解析器槽位：跟踪设备状态和键盘报告解析器
struct ParserSlot {
    bool used;
    uint8_t dev_addr;
    uint8_t instance;
    hid::HidBootKeyboardParser parser;
};

// 字符串获取状态机（异步回调链）
enum class StrFetchState : uint8_t {
    IDLE,
    WAIT_DEV_DESC,
    WAIT_CFG_DESC,
    FETCH_MFG_STR,
    FETCH_PROD_STR,
    FETCH_SERIAL_STR,
    DONE
};

struct StringFetcher {
    bool active;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t itf_protocol;
    StrFetchState state;

    tusb_desc_device_t dev_desc;
    bool dev_desc_ok;

    uint8_t cfg_buf[512];
    bool cfg_ok;

    uint8_t str_buf[66];

    device_strings strings;

    uint8_t next_str_index;
};

// 全局状态
ParserSlot g_parsers[kMaxDevices] = {};
StringFetcher g_fetchers[kMaxDevices] = {};
size_t g_mounted = 0;
KeyEventCallback g_key_cb = nullptr;
MountCallback g_mount_cb = nullptr;
StringsCallback g_strings_cb = nullptr;

// 待处理设备队列（mount回调中标记，主循环执行）
struct PendingDevice {
    bool pending;
    uint8_t dev_addr;
    uint8_t instance;
    uint8_t itf_protocol;
    uint16_t tick;
};
PendingDevice g_pending[kMaxDevices] = {};

// 查找或分配设备槽位
ParserSlot* findOrAllocSlot(uint8_t dev_addr, uint8_t instance, bool alloc) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_parsers[i].used && g_parsers[i].dev_addr == dev_addr && g_parsers[i].instance == instance) {
            return &g_parsers[i];
        }
    }
    if (!alloc) return nullptr;
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_parsers[i].used) {
            g_parsers[i].used = true;
            g_parsers[i].dev_addr = dev_addr;
            g_parsers[i].instance = instance;
            g_parsers[i].parser.reset();
            return &g_parsers[i];
        }
    }
    return nullptr;
}

ParserSlot* findSlot(uint8_t dev_addr, uint8_t instance) {
    return findOrAllocSlot(dev_addr, instance, false);
}

void freeSlot(uint8_t dev_addr, uint8_t instance) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_parsers[i].used && g_parsers[i].dev_addr == dev_addr && g_parsers[i].instance == instance) {
            g_parsers[i].used = false;
            g_parsers[i].dev_addr = 0;
            g_parsers[i].instance = 0;
            g_parsers[i].parser.reset();
            return;
        }
    }
}

StringFetcher* findFetcher(uint8_t dev_addr) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_fetchers[i].active && g_fetchers[i].dev_addr == dev_addr) {
            return &g_fetchers[i];
        }
    }
    return nullptr;
}

StringFetcher* allocFetcher(uint8_t dev_addr, uint8_t instance, uint8_t itf_protocol) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_fetchers[i].active) {
            auto& f = g_fetchers[i];
            f.active = true;
            f.dev_addr = dev_addr;
            f.instance = instance;
            f.itf_protocol = itf_protocol;
            f.state = StrFetchState::IDLE;
            f.dev_desc_ok = false;
            f.cfg_ok = false;
            std::memset(&f.dev_desc, 0, sizeof(f.dev_desc));
            std::memset(f.cfg_buf, 0, sizeof(f.cfg_buf));
            std::memset(&f.strings, 0, sizeof(f.strings));
            f.next_str_index = 0;
            return &f;
        }
    }
    return nullptr;
}

void releaseFetcher(uint8_t dev_addr) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_fetchers[i].active && g_fetchers[i].dev_addr == dev_addr) {
            g_fetchers[i].active = false;
            return;
        }
    }
}

// 从配置描述符中提取接口信息（与原实现相同）
static void parse_cfg_info(uint8_t* buffer, uint16_t total_len, uint8_t itf_protocol, device_info& info) {
    if (total_len <= 9) return;
    const uint8_t* p = buffer + 9;
    info.b_num_interfaces = buffer[4];
    info.b_configuration_value = buffer[5];
    info.bm_attributes = buffer[7];
    info.b_max_power = buffer[8];

    while (p < buffer + total_len) {
        uint8_t desc_type = p[1];
        uint8_t desc_len = p[0];
        if (desc_type == TUSB_DESC_INTERFACE) {
            uint8_t itf_prot = p[7];
            if (itf_prot == itf_protocol) {
                info.itf_num = p[2];
                info.b_interface_class = p[5];
                info.b_interface_subclass = p[6];
                p += desc_len;
                while (p < buffer + total_len) {
                    if (p[1] == TUSB_DESC_INTERFACE) break;
                    if (p[1] == TUSB_DESC_ENDPOINT) {
                        const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)p;
                        if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                            info.b_interval = ep->bInterval;
                            break;
                        }
                    }
                    if (p[0] == 0) break;
                    p += p[0];
                }
                break;
            }
        }
        p += desc_len;
    }
}

// 将字符串结果写入 strings 结构体
static void save_string_result(StringFetcher* f, uint8_t index) {
    if (f->str_buf[0] >= 3) {
        uint16_t data_len = f->str_buf[0] - 2;
        if (data_len > 64) data_len = 64;
        switch (index) {
        case 0:
            f->strings.manufacturer_len = static_cast<uint8_t>(data_len);
            std::memcpy(f->strings.manufacturer, f->str_buf + 2, data_len);
            std::memset(f->strings.manufacturer + data_len, 0, 64 - data_len);
            break;
        case 1:
            f->strings.product_len = static_cast<uint8_t>(data_len);
            std::memcpy(f->strings.product, f->str_buf + 2, data_len);
            std::memset(f->strings.product + data_len, 0, 64 - data_len);
            break;
        case 2:
            f->strings.serial_len = static_cast<uint8_t>(data_len);
            std::memcpy(f->strings.serial, f->str_buf + 2, data_len);
            std::memset(f->strings.serial + data_len, 0, 64 - data_len);
            break;
        }
    }
}

// 异步描述符传输完成回调（从 tuh_task() 内部调用，必须轻量）
static void desc_xfer_complete_cb(tuh_xfer_t* xfer) {
    uint8_t dev_addr = static_cast<uint8_t>(xfer->daddr);
    auto* f = findFetcher(dev_addr);
    if (!f || !f->active) return;

    constexpr uint16_t langid = 0x0409;

    switch (f->state) {

    case StrFetchState::WAIT_DEV_DESC:
        if (xfer->result == XFER_RESULT_SUCCESS) {
            f->dev_desc_ok = true;
        }
        f->state = StrFetchState::WAIT_CFG_DESC;
        std::memset(f->cfg_buf, 0, sizeof(f->cfg_buf));
        tuh_descriptor_get_configuration(dev_addr, 0, f->cfg_buf, sizeof(f->cfg_buf),
                                        desc_xfer_complete_cb, dev_addr);
        break;

    case StrFetchState::WAIT_CFG_DESC:
        if (xfer->result == XFER_RESULT_SUCCESS) {
            f->cfg_ok = true;
        }

        {
            usb_host::device_info info = {};
            info.dev_addr = f->dev_addr;
            info.instance = f->instance;
            info.itf_protocol = f->itf_protocol;
            tuh_vid_pid_get(dev_addr, &info.vid, &info.pid);

            if (f->dev_desc_ok) {
                info.bcd_usb = f->dev_desc.bcdUSB;
                info.b_device_class = f->dev_desc.bDeviceClass;
                info.b_device_subclass = f->dev_desc.bDeviceSubClass;
                info.b_device_protocol = f->dev_desc.bDeviceProtocol;
                info.b_max_packet_size0 = f->dev_desc.bMaxPacketSize0;
                info.bcd_device = f->dev_desc.bcdDevice;
            }

            if (f->cfg_ok) {
                uint16_t cfg_total_len = f->cfg_buf[2] | (f->cfg_buf[3] << 8);
                parse_cfg_info(f->cfg_buf, cfg_total_len, f->itf_protocol, info);
            }

            if (info.b_interval == 0) info.b_interval = 10;

            if (g_mount_cb != nullptr) g_mount_cb(info, true);
        }

        if (f->dev_desc_ok && f->dev_desc.iManufacturer > 0) {
            f->state = StrFetchState::FETCH_MFG_STR;
            f->next_str_index = 0;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iManufacturer, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else if (f->dev_desc_ok && f->dev_desc.iProduct > 0) {
            f->state = StrFetchState::FETCH_PROD_STR;
            f->next_str_index = 1;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iProduct, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else if (f->dev_desc_ok && f->dev_desc.iSerialNumber > 0) {
            f->state = StrFetchState::FETCH_SERIAL_STR;
            f->next_str_index = 2;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iSerialNumber, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else {
            f->state = StrFetchState::DONE;
            if (g_strings_cb != nullptr) g_strings_cb(dev_addr, f->strings);
            releaseFetcher(dev_addr);
        }
        break;

    case StrFetchState::FETCH_MFG_STR:
        if (xfer->result == XFER_RESULT_SUCCESS) {
            save_string_result(f, 0);
        }
        if (f->dev_desc.iProduct > 0) {
            f->state = StrFetchState::FETCH_PROD_STR;
            f->next_str_index = 1;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iProduct, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else if (f->dev_desc.iSerialNumber > 0) {
            f->state = StrFetchState::FETCH_SERIAL_STR;
            f->next_str_index = 2;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iSerialNumber, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else {
            f->state = StrFetchState::DONE;
            if (g_strings_cb != nullptr) g_strings_cb(dev_addr, f->strings);
            releaseFetcher(dev_addr);
        }
        break;

    case StrFetchState::FETCH_PROD_STR:
        if (xfer->result == XFER_RESULT_SUCCESS) {
            save_string_result(f, 1);
        }
        if (f->dev_desc.iSerialNumber > 0) {
            f->state = StrFetchState::FETCH_SERIAL_STR;
            f->next_str_index = 2;
            std::memset(f->str_buf, 0, sizeof(f->str_buf));
            tuh_descriptor_get_string(dev_addr, f->dev_desc.iSerialNumber, langid,
                                     f->str_buf, sizeof(f->str_buf),
                                     desc_xfer_complete_cb, dev_addr);
        } else {
            f->state = StrFetchState::DONE;
            if (g_strings_cb != nullptr) g_strings_cb(dev_addr, f->strings);
            releaseFetcher(dev_addr);
        }
        break;

    case StrFetchState::FETCH_SERIAL_STR:
        if (xfer->result == XFER_RESULT_SUCCESS) {
            save_string_result(f, 2);
        }
        f->state = StrFetchState::DONE;
        if (g_strings_cb != nullptr) g_strings_cb(dev_addr, f->strings);
        releaseFetcher(dev_addr);
        break;

    default:
        break;
    }
}

} // anonymous namespace

// 注册回调实现
void registerKeyEventCallback(KeyEventCallback cb) { g_key_cb = cb; }
void registerMountCallback(MountCallback cb) { g_mount_cb = cb; }
void registerStringsCallback(StringsCallback cb) { g_strings_cb = cb; }

// 主循环任务：检查待处理设备，启动异步描述符获取链
void poll_strings_task() {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_pending[i].pending) continue;
        g_pending[i].tick++;

        if (g_pending[i].tick < 50) continue;

        uint8_t dev_addr = g_pending[i].dev_addr;
        uint8_t instance = g_pending[i].instance;
        uint8_t itf_protocol = g_pending[i].itf_protocol;
        g_pending[i].pending = false;

        if (!tuh_mounted(dev_addr)) continue;

        auto* f = allocFetcher(dev_addr, instance, itf_protocol);
        if (!f) continue;

        f->state = StrFetchState::WAIT_DEV_DESC;
        std::memset(&f->dev_desc, 0, sizeof(f->dev_desc));
        tuh_descriptor_get_device(dev_addr, &f->dev_desc, sizeof(f->dev_desc),
                                  desc_xfer_complete_cb, dev_addr);
    }
}

size_t getMountedKeyboardCount() { return g_mounted; }

} // namespace usb_host

// TinyUSB C 回调接口
extern "C" {

// 设备挂载回调（TinyUSB 调用）
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;

    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance); // TinyUSB API

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            auto* slot = usb_host::findOrAllocSlot(dev_addr, instance, true);
            if (slot != nullptr) usb_host::g_mounted++;
        }

        for (size_t i = 0; i < usb_host::kMaxDevices; i++) {
            if (!usb_host::g_pending[i].pending) {
                usb_host::g_pending[i].pending = true;
                usb_host::g_pending[i].dev_addr = dev_addr;
                usb_host::g_pending[i].instance = instance;
                usb_host::g_pending[i].itf_protocol = itf_protocol;
                usb_host::g_pending[i].tick = 0;
                break;
            }
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}

// 设备卸载回调（TinyUSB 调用）
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            usb_host::freeSlot(dev_addr, instance);
            if (usb_host::g_mounted > 0) usb_host::g_mounted--;
        }

        for (size_t i = 0; i < usb_host::kMaxDevices; i++) {
            if (usb_host::g_pending[i].pending && usb_host::g_pending[i].dev_addr == dev_addr) {
                usb_host::g_pending[i].pending = false;
                break;
            }
        }

        usb_host::releaseFetcher(dev_addr);

        if (usb_host::g_mount_cb != nullptr) {
            usb_host::device_info info = {};
            info.dev_addr = dev_addr;
            info.instance = instance;
            info.itf_protocol = itf_protocol;
            tuh_vid_pid_get(dev_addr, &info.vid, &info.pid);
            usb_host::g_mount_cb(info, false);
        }
    }
}

// HID 报告接收回调（TinyUSB 调用）
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        auto* slot = usb_host::findSlot(dev_addr, instance);
        if (slot != nullptr && usb_host::g_key_cb != nullptr) {
            slot->parser.parse(report, static_cast<size_t>(len), usb_host::g_key_cb);
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}

} // extern "C"

#else // ENABLE_USB == 0

#include <cstddef>

namespace usb_host {

void registerKeyEventCallback(KeyEventCallback) {}
void registerMountCallback(MountCallback) {}
void registerStringsCallback(StringsCallback) {}
void poll_strings_task() {}
size_t getMountedKeyboardCount() { return 0; }

} // namespace usb_host

#endif // ENABLE_USB