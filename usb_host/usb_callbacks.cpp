// ===== USB Host 回调管理层
// 通过 CMake target_compile_definitions 传入的 ENABLE_USB 宏控制：
//   ENABLE_USB=0: 仅编译空桩实现，不链接 tinyusb host stack
//   ENABLE_USB=1:  完整 USB Host HID 解析
// —— 注意：此处不再硬编码 ENABLE_USB，由 CMakeLists.txt 统一控制

#ifndef ENABLE_USB
// CMake 默认会通过 target_compile_definitions 传入 ENABLE_USB。
// 若头文件被直接引入且未定义此宏，则默认回退为 1（启用），
// 以确保用户在 HID host 就绪时能正常收到 raw keyboard event。
#define ENABLE_USB 1
#endif

#include "usb_callbacks.h"

#if ENABLE_USB

#include "../hid/hid_parser.h"
#include "tusb.h"
#include "class/hid/hid.h"

#include <cstdint>
#include <cstring>

namespace usb_host {

namespace {

// 最多同时挂载 CFG_TUH_DEVICE_MAX 个设备
// 每个设备一个独立的解析器
constexpr size_t kMaxDevices = CFG_TUH_DEVICE_MAX;

struct ParserSlot {
    bool used;
    uint8_t dev_addr;
    uint8_t instance;
    hid::HidBootKeyboardParser parser;
};

ParserSlot g_parsers[kMaxDevices] = {};
size_t g_mounted = 0;
KeyEventCallback g_key_cb = nullptr;
MountCallback g_mount_cb = nullptr;

ParserSlot* findOrAllocSlot(uint8_t dev_addr, uint8_t instance, bool alloc) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_parsers[i].used
            && g_parsers[i].dev_addr == dev_addr
            && g_parsers[i].instance == instance) {
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
        if (g_parsers[i].used
            && g_parsers[i].dev_addr == dev_addr
            && g_parsers[i].instance == instance) {
            g_parsers[i].used = false;
            g_parsers[i].dev_addr = 0;
            g_parsers[i].instance = 0;
            g_parsers[i].parser.reset();
            return;
        }
    }
}

} // namespace

void registerKeyEventCallback(KeyEventCallback cb) {
    g_key_cb = cb;
}

void registerMountCallback(MountCallback cb) {
    g_mount_cb = cb;
}

size_t getMountedKeyboardCount() {
    return g_mounted;
}

} // namespace usb_host

// ===========================
// TinyUSB Host Stack Callbacks
// ===========================
extern "C" {

// 设备挂载回调：只处理 HID keyboard 接口。
// 非 keyboard 接口也通过 receive_report 启动轮询以保持枚举健康，
// 但不分配 parser slot、不计数 g_mounted。
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;

    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        auto* slot = usb_host::findOrAllocSlot(dev_addr, instance, true);
        if (slot != nullptr) {
            usb_host::g_mounted++;
            
            if (usb_host::g_mount_cb != nullptr) {
                usb_host::device_info info = {};
                info.dev_addr = dev_addr;
                info.instance = instance;
                info.itf_protocol = itf_protocol;
                
                tuh_vid_pid_get(dev_addr, &info.vid, &info.pid);
                
                uint8_t buffer[512];
                uint16_t len = tuh_descriptor_get_configuration_sync(dev_addr, 0, buffer, sizeof(buffer));
                if (len > 9) {
                    const uint8_t* p = buffer + 9;
                    while (p < buffer + len) {
                        uint8_t desc_type = p[1];
                        uint8_t desc_len = p[0];
                        
                        if (desc_type == TUSB_DESC_INTERFACE) {
                            uint8_t itf_num = p[2];
                            uint8_t itf_prot = p[5];
                            if (itf_prot == HID_ITF_PROTOCOL_KEYBOARD) {
                                info.itf_num = itf_num;
                                p += desc_len;
                                while (p < buffer + len) {
                                    if (p[1] == TUSB_DESC_ENDPOINT) {
                                        const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)p;
                                        if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                                            info.bInterval = ep->bInterval;
                                            break;
                                        }
                                    }
                                    p += p[0];
                                }
                                break;
                            }
                        }
                        p += desc_len;
                    }
                }
                
                if (info.bInterval == 0) {
                    info.bInterval = 10;
                }
                
                usb_host::g_mount_cb(info, true);
            }
        }
    }

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        usb_host::freeSlot(dev_addr, instance);
        if (usb_host::g_mounted > 0) {
            usb_host::g_mounted--;
        }
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

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
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

#else // ENABLE_USB == 0: 空桩实现，不链接 tinyusb

#include <cstddef>

namespace usb_host {

void registerKeyEventCallback(KeyEventCallback) {
    // 空桩：ENABLE_USB=0 时不做任何事
}

void registerMountCallback(MountCallback) {
    // 空桩
}

size_t getMountedKeyboardCount() {
    return 0;
}

} // namespace usb_host

// 注意：ENABLE_USB=0 时不提供 tuh_* 回调符号
// tinyusb host 栈不被链接，不会被调用。

#endif // ENABLE_USB
