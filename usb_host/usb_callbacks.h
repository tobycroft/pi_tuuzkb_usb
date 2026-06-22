#ifndef USB_HOST_USB_CALLBACKS_H
#define USB_HOST_USB_CALLBACKS_H

// ===== USB Host 回调管理层 =====
// 负责封装 TinyUSB host stack 的 tuh_* 回调
// 提供 C++ 风格的事件分发接口到上层业务

#if __cplusplus < 201703L
#error "usb_callbacks requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>

namespace usb_host {

// ---- key_event 结构定义 ----
// 原始按键事件：仅携带 HID usage code / 按下状态 / modifiers
struct key_event {
    uint8_t usage_code;
    bool pressed;
    uint8_t modifiers;
};

// ---- 设备信息结构 ----
struct device_info {
    uint8_t dev_addr;
    uint16_t vid;
    uint16_t pid;
    uint8_t bInterval;
    uint8_t itf_num;
    uint8_t itf_protocol;
    uint8_t instance;
};

// ---- 事件回调函数类型 ----
// key_event: 按键变化事件
// mount / umount: 设备插拔事件（包含设备详细信息）
using KeyEventCallback = void(*)(const key_event&);
using MountCallback = void(*)(const device_info& info, bool mounted);

// ---- 注册回调的 API ----
// 由 main 或业务逻辑层在初始化时调用
void registerKeyEventCallback(KeyEventCallback cb);
void registerMountCallback(MountCallback cb);

// ---- 打印调试信息的辅助接口 ----
// 返回当前挂载的键盘设备数量
size_t getMountedKeyboardCount();

} // namespace usb_host

#endif // USB_HOST_USB_CALLBACKS_H
