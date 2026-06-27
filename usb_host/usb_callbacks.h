#ifndef USB_HOST_USB_CALLBACKS_H
#define USB_HOST_USB_CALLBACKS_H

#if __cplusplus < 201703L
#error "usb_callbacks requires C++17 or later"
#endif

#include <cstdint>
#include <cstddef>

namespace usb_host {

// 按键事件：从 USB HID 键盘接收到的原始按键数据
struct key_event {
    uint8_t usage_code;   // HID Usage Code（键盘键唯一标识）
    bool pressed;         // true=按下, false=释放
    uint8_t modifiers;    // 修饰键位图（Ctrl/Shift/Alt/Win）
};

// 设备信息：USB HID 设备插入/拔出时传递的完整描述符信息
struct device_info {
    uint8_t  dev_addr;           // USB 设备地址（1-127）
    uint8_t  mounted;            // 0=拔出, 1=插入
    
    uint16_t vid;                // Vendor ID
    uint16_t pid;                // Product ID
    uint16_t bcd_usb;            // USB 版本号
    uint8_t  b_device_class;     // 设备类代码（HID=0x03）
    uint8_t  b_device_subclass;  // 设备子类代码
    uint8_t  b_device_protocol;  // 设备协议代码
    uint8_t  b_max_packet_size0; // 端点0最大包大小
    uint16_t bcd_device;         // 设备版本号
    
    uint8_t  b_num_interfaces;       // 接口数量
    uint8_t  b_configuration_value;  // 配置值
    uint8_t  bm_attributes;          // 配置属性
    uint8_t  b_max_power;            // 最大功耗（2mA单位）
    
    uint8_t  itf_num;              // 接口编号
    uint8_t  b_interface_class;    // 接口类代码
    uint8_t  b_interface_subclass; // 接口子类代码
    uint8_t  itf_protocol;         // 接口协议（KEYBOARD=1, MOUSE=2）
    uint8_t  b_interval;           // 中断端点轮询间隔（毫秒）
    uint8_t  instance;             // HID 实例号
};

// 设备字符串描述符（UTF-16LE 编码，枚举完成后单独获取）
struct device_strings {
    uint8_t manufacturer[64];  // 制造商字符串
    uint8_t product[64];       // 产品名称字符串
    uint8_t serial[64];        // 序列号字符串
    uint8_t manufacturer_len;  // 实际长度
    uint8_t product_len;       // 实际长度
    uint8_t serial_len;        // 实际长度
};

// 回调函数类型定义
using KeyEventCallback = void(*)(const key_event&);
using MountCallback = void(*)(const device_info& info, bool mounted);
using StringsCallback = void(*)(uint8_t dev_addr, const device_strings& strings);

// 注册回调 API
void registerKeyEventCallback(KeyEventCallback cb);
void registerMountCallback(MountCallback cb);
void registerStringsCallback(StringsCallback cb);

// 主循环调用：处理待获取的字符串描述符（必须在 tuh_task() 之后调用）
void poll_strings_task();

// 获取当前挂载的键盘设备数量
size_t getMountedKeyboardCount();

// 设置所有已挂载键盘的 LED 状态（NUM_LOCK/CAPS_LOCK/SCROLL_LOCK）
// led_byte: Bit0=NumLock, Bit1=CapsLock, Bit2=ScrollLock
void setKeyboardLed(uint8_t led_byte);

} // namespace usb_host

#endif // USB_HOST_USB_CALLBACKS_H