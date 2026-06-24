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

// 全局状态
ParserSlot g_parsers[kMaxDevices] = {};
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

} // anonymous namespace

// 注册回调实现
void registerKeyEventCallback(KeyEventCallback cb) { g_key_cb = cb; }
void registerMountCallback(MountCallback cb) { g_mount_cb = cb; }
void registerStringsCallback(StringsCallback cb) { g_strings_cb = cb; }

// 主循环任务：处理待获取的设备描述符和字符串描述符
void poll_strings_task() {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_pending[i].pending) continue;
        g_pending[i].tick++;
        
        // 等待约 500ms 确保设备枚举完成
        if (g_pending[i].tick < 50) continue;
        
        uint8_t dev_addr = g_pending[i].dev_addr;
        uint8_t instance = g_pending[i].instance;
        uint8_t itf_protocol = g_pending[i].itf_protocol;
        g_pending[i].pending = false;
        
        if (!tuh_mounted(dev_addr)) continue; // TinyUSB API
        
        // 构造设备信息
        usb_host::device_info info = {};
        info.dev_addr = dev_addr;
        info.instance = instance;
        info.itf_protocol = itf_protocol;
        
        // 获取 VID/PID（TinyUSB 缓存）
        tuh_vid_pid_get(dev_addr, &info.vid, &info.pid); // TinyUSB API
        
        // 获取设备描述符（同步 API）
        tusb_desc_device_t dev_desc;
        std::memset(&dev_desc, 0, sizeof(dev_desc));
        uint8_t dev_desc_result = tuh_descriptor_get_device_sync(dev_addr, &dev_desc, sizeof(dev_desc)); // TinyUSB API
        
        if (dev_desc_result == XFER_RESULT_SUCCESS) {
            info.bcd_usb = dev_desc.bcdUSB;
            info.b_device_class = dev_desc.bDeviceClass;
            info.b_device_subclass = dev_desc.bDeviceSubClass;
            info.b_device_protocol = dev_desc.bDeviceProtocol;
            info.b_max_packet_size0 = dev_desc.bMaxPacketSize0;
            info.bcd_device = dev_desc.bcdDevice;
        }
        
        // 获取配置描述符
        uint8_t buffer[512];
        std::memset(buffer, 0, sizeof(buffer));
        uint8_t cfg_result = tuh_descriptor_get_configuration_sync(dev_addr, 0, buffer, sizeof(buffer)); // TinyUSB API
        
        if (cfg_result == XFER_RESULT_SUCCESS) {
            uint16_t cfg_total_len = buffer[2] | (buffer[3] << 8);
            if (cfg_total_len > 9) {
                const uint8_t* p = buffer + 9;
                info.b_num_interfaces = buffer[4];
                info.b_configuration_value = buffer[5];
                info.bm_attributes = buffer[7];
                info.b_max_power = buffer[8];
                
                while (p < buffer + cfg_total_len) {
                    uint8_t desc_type = p[1];
                    uint8_t desc_len = p[0];
                    if (desc_type == TUSB_DESC_INTERFACE) {
                        // USB Interface Descriptor: p[2]=bInterfaceNumber, p[3]=bAlternateSetting,
                        // p[4]=bNumEndpoints, p[5]=bInterfaceClass, p[6]=bInterfaceSubClass,
                        // p[7]=bInterfaceProtocol, p[8]=iInterface
                        uint8_t itf_num = p[2];
                        uint8_t itf_class = p[5];
                        uint8_t itf_subclass = p[6];
                        uint8_t itf_prot = p[7];
                        if (itf_prot == itf_protocol) {
                            info.itf_num = itf_num;
                            info.b_interface_class = itf_class;
                            info.b_interface_subclass = itf_subclass;
                            p += desc_len;
                            while (p < buffer + cfg_total_len) {
                                if (p[1] == TUSB_DESC_INTERFACE) break; // 进入下一个接口，停止
                                if (p[1] == TUSB_DESC_ENDPOINT) {
                                    const tusb_desc_endpoint_t* ep = (const tusb_desc_endpoint_t*)p;
                                    if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                                        info.b_interval = ep->bInterval;
                                        break;
                                    }
                                }
                                if (p[0] == 0) break; // 防止死循环
                                p += p[0];
                            }
                            break;
                        }
                    }
                    p += desc_len;
                }
            }
        }
        
        if (info.b_interval == 0) info.b_interval = 10;
        
        // 发送设备挂载事件
        if (g_mount_cb != nullptr) g_mount_cb(info, true);
        
        // 获取字符串描述符
        usb_host::device_strings strings = {};
        constexpr uint16_t langid = 0x0409;
        
        if (dev_desc_result == XFER_RESULT_SUCCESS) {
            // 制造商字符串
            if (dev_desc.iManufacturer > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(dev_addr, dev_desc.iManufacturer, langid, str_buf, sizeof(str_buf)); // TinyUSB API
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.manufacturer_len = data_len;
                    std::memcpy(strings.manufacturer, str_buf + 2, data_len);
                    std::memset(strings.manufacturer + data_len, 0, 64 - data_len);
                }
            }
            // 产品名称字符串
            if (dev_desc.iProduct > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(dev_addr, dev_desc.iProduct, langid, str_buf, sizeof(str_buf)); // TinyUSB API
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.product_len = data_len;
                    std::memcpy(strings.product, str_buf + 2, data_len);
                    std::memset(strings.product + data_len, 0, 64 - data_len);
                }
            }
            // 序列号字符串
            if (dev_desc.iSerialNumber > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(dev_addr, dev_desc.iSerialNumber, langid, str_buf, sizeof(str_buf)); // TinyUSB API
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.serial_len = data_len;
                    std::memcpy(strings.serial, str_buf + 2, data_len);
                    std::memset(strings.serial + data_len, 0, 64 - data_len);
                }
            }
        }
        
        // 发送字符串描述符事件
        if (g_strings_cb != nullptr) g_strings_cb(dev_addr, strings);
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
        
        // 标记待处理，延迟到主循环执行
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
    
    tuh_hid_receive_report(dev_addr, instance); // TinyUSB API：发起第一次 HID IN transfer
}

// 设备卸载回调（TinyUSB 调用）
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance); // TinyUSB API
    
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            usb_host::freeSlot(dev_addr, instance);
            if (usb_host::g_mounted > 0) usb_host::g_mounted--;
        }
        
        // 清除待处理标记
        for (size_t i = 0; i < usb_host::kMaxDevices; i++) {
            if (usb_host::g_pending[i].pending && usb_host::g_pending[i].dev_addr == dev_addr) {
                usb_host::g_pending[i].pending = false;
                break;
            }
        }
        
        // 通知上层设备已拔出
        if (usb_host::g_mount_cb != nullptr) {
            usb_host::device_info info = {};
            info.dev_addr = dev_addr;
            info.instance = instance;
            info.itf_protocol = itf_protocol;
            tuh_vid_pid_get(dev_addr, &info.vid, &info.pid); // TinyUSB API
            usb_host::g_mount_cb(info, false);
        }
    }
}

// HID 报告接收回调（TinyUSB 调用）
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance); // TinyUSB API
    
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        auto* slot = usb_host::findSlot(dev_addr, instance);
        if (slot != nullptr && usb_host::g_key_cb != nullptr) {
            slot->parser.parse(report, static_cast<size_t>(len), usb_host::g_key_cb);
        }
    }
    
    tuh_hid_receive_report(dev_addr, instance); // TinyUSB API：继续 polling
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