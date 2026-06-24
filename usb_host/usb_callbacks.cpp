// ============================================================================
// C++ 实现文件 (.cpp) 的作用
// ============================================================================
// .cpp 文件是 C++ 的"实现文件"（Implementation File）
//
// .h 文件声明了什么：
// - 有哪些函数
// - 函数签名是什么（参数类型、返回值类型）
// - 有哪些结构体/类
//
// .cpp 文件实现什么：
// - 函数的具体代码
// - 具体的算法逻辑
//
// 对比 Go：
// Go 没有 .h 和 .cpp 的分离，所有代码都在 .go 文件里
// 但是 Go 通过 interface{} 来定义契约，类似于 .h 的作用
//
// 编译过程：
// .h 文件被 #include 到需要的 .cpp 文件中
// 每个 .cpp 文件独立编译成 .o（对象文件）
// 所有 .o 文件链接成最终的可执行文件/库
// ============================================================================

// ============================================================================
// 文件开头的多行注释 - 本模块的功能说明
// ============================================================================
// 本模块负责：USB Host 回调管理层
// - 封装 TinyUSB host stack 的 tuh_* 回调
// - 提供 C++ 风格的事件分发接口到上层业务
// ============================================================================
/*
 * USB Host 回调管理层
 *
 * 本模块的功能：
 * 1. 接收 TinyUSB 的 USB 事件（设备插入/拔出，数据接收）
 * 2. 解析 HID 报告（键盘按键数据）
 * 3. 分发事件到上层业务逻辑（通过回调函数）
 */

// ============================================================================
// #ifndef / #define / #endif - 预处理指令
// ============================================================================
// #ifndef XXX = "如果未定义 XXX"
// #define XXX = "定义 XXX"
//
// 和 .h 文件一样的防重复包含机制
// ============================================================================
#ifndef ENABLE_USB
// 如果 CMake 没有传入 ENABLE_USB 宏，默认启用 USB
// 这是为了在没有 CMake 的环境下（比如直接编译）也能工作
#define ENABLE_USB 1
#endif

// ============================================================================
// #include - 包含头文件
// ============================================================================
// #include "xxx.h" - 项目自定义头文件（使用相对路径）
// #include <xxx.h> - 系统/库头文件
//
// 对比 Go 的 import：
// Go: import "fmt"
// C++: #include "path/to/file.h"
//
// 这里使用 #include "usb_callbacks.h"
// 表示包含同一目录下的 usb_callbacks.h
// ============================================================================
#include "usb_callbacks.h"

// ============================================================================
// #if ENABLE_USB - 条件编译
// ============================================================================
// 当 ENABLE_USB = 1 时，编译这段代码
// 当 ENABLE_USB = 0 时，编译器会跳过这段代码（类似于被删除）
//
// 对比 Go：
// Go 没有编译时条件编译，但可以通过 build tags 实现类似效果
// 例：// +build ignore
//
// 作用：
// 1. 开发时禁用 USB 功能，加快编译速度
// 2. 在没有 USB 硬件的平台上也能测试其他功能
// ============================================================================
#if ENABLE_USB

// ============================================================================
// #include 系统/库头文件
// ============================================================================
#include "pico/stdlib.h"          // Pico SDK: sleep_ms
#include "../hid/hid_parser.h"  // 项目内部头文件：HID 报告解析器
#include "tusb.h"               // TinyUSB 主头文件：提供 USB Host API
#include "class/hid/hid.h"      // TinyUSB HID 类头文件：HID 协议定义

#include <cstdint>  // 标准整数类型：uint8_t, uint16_t, uint32_t
#include <cstring>  // C 风格字符串函数：memcpy, memset 等

// ============================================================================
// namespace - 命名空间
// ============================================================================
// 和 .h 文件中一样，这里也需要用 namespace 包裹代码
// .cpp 文件中的 namespace 应该和 .h 文件保持一致
// ============================================================================
namespace usb_host {

// ============================================================================
// anonymous namespace（匿名命名空间）
// ============================================================================
// namespace { ... } 是匿名命名空间
// 其中的内容只能在当前文件访问，类似于 static 全局变量
// 但比 static 更现代，推荐使用
//
// 对比 Go：
// Go 没有文件私有变量的概念，所有导出都是包级私有的（首字母大写）
// 匿名的 .go 文件同属一个 package
// ============================================================================
namespace {

// ============================================================================
// constexpr - 编译时常量
// ============================================================================
// constexpr 表示"编译时就能确定的值"
//
// 对比 Go：
// Go 的 const 就是编译时常量
// C++ 的 const 可能是运行时才确定（取决于上下文）
// constexpr 确保一定是编译时常量
//
// 例：
// constexpr int MAX = 100;    // 编译时确定
// const int MAX = 100;        // 也通常在编译时确定
// const int getMax() { return 100; }  // 运行时确定
// constexpr int getMax() { return 100; }  // 编译时确定
// ============================================================================

// ---- 常量定义 ----
// 最多同时挂载 CFG_TUH_DEVICE_MAX 个设备（CFG_TUH_DEVICE_MAX 在 tusb_config.h 中定义）
// 每个设备需要一个独立的解析器来跟踪键盘状态
constexpr size_t kMaxDevices = CFG_TUH_DEVICE_MAX;

// ============================================================================
// struct - 结构体定义
// ============================================================================
// 和 .h 中声明的结构体不同，这里是结构体定义和初始化
//
// 对比 Go：
// Go: type ParserSlot struct { Used bool; DevAddr uint8; ... }
// C++: struct ParserSlot { bool used; uint8_t dev_addr; ... };
//
// C++ 结构体可以直接初始化：
// ParserSlot slot = {};  // 所有字段初始化为零
// ParserSlot slot = {true, 1, 0, parser};  // 指定初始化
// ============================================================================

// ParserSlot：每个已挂载设备的解析器槽位
// 用于跟踪设备状态和键盘报告解析器
struct ParserSlot {
    bool used;                    // 是否被占用：true=已使用，false=空闲
    uint8_t dev_addr;             // USB 设备地址
    uint8_t instance;            // HID 实例号
    hid::HidBootKeyboardParser parser;  // HID 键盘报告解析器
    // 解析器用于将原始 HID 报告转换为 key_event
};

// ============================================================================
// 全局变量
// ============================================================================
// 全局变量在整个程序运行期间都存在
// 以 g_ 前缀命名是常见的命名约定，表示 "global"
//
// 对比 Go：
// Go 中没有真正的全局变量，但包级变量可以达到类似效果
// Go: var Parsers [maxDevices]ParserSlot
//
// 重要：C++ 中全局变量需要小心使用，因为：
// 1. 多线程访问可能需要加锁
// 2. 初始化顺序不确定（跨编译单元）
// 3. 可能导致隐藏的依赖关系
// ============================================================================

// g_parsers：所有可能的设备槽位
// 初始化为空，所有字段都是零值（used=false）
ParserSlot g_parsers[kMaxDevices] = {};

// g_mounted：当前挂载的键盘设备数量
size_t g_mounted = 0;

// g_key_cb：按键事件回调函数指针
// 初始化为 nullptr（空指针，等同于 Go 的 nil）
KeyEventCallback g_key_cb = nullptr;

// g_mount_cb：设备挂载事件回调函数指针
MountCallback g_mount_cb = nullptr;

// g_strings_cb：字符串描述符回调函数指针
StringsCallback g_strings_cb = nullptr;

// 设备待处理队列（mount 回调只做标记，所有描述符读取在主循环中执行）
struct PendingDevice {
    bool     pending;       // 是否有待处理
    uint8_t  dev_addr;      // 设备地址
    uint8_t  instance;      // HID 实例号
    uint8_t  itf_protocol;  // 接口协议
    uint16_t tick;          // 计时器（每次 poll_strings_task 递增）
};
PendingDevice g_pending[kMaxDevices] = {};

// ============================================================================
// 函数定义 - 查找或分配槽位
// ============================================================================
// 函数名前的注释解释这个函数是做什么的
// 参数和返回值在函数声明时已经说明，这里可以省略

// findOrAllocSlot - 查找或分配设备槽位
// 参数：
//   dev_addr - USB 设备地址
//   instance - HID 实例号
//   alloc - true=如果没找到就分配一个，false=只查找不分配
// 返回：找到/分配的 ParserSlot 指针，或 nullptr（未找到且不分配）
ParserSlot* findOrAllocSlot(uint8_t dev_addr, uint8_t instance, bool alloc) {
    // ============================================================================
    // for 循环
    // ============================================================================
    // 语法：for (初始化; 条件; 更新) { 循环体 }
    //
    // 对比 Go：
    // Go: for i := 0; i < kMaxDevices; i++ { ... }
    // C++: for (size_t i = 0; i < kMaxDevices; i++) { ... }
    //
    // 几乎一样，只是语法略有不同
    // ============================================================================
    for (size_t i = 0; i < kMaxDevices; i++) {
        // ============================================================================
        // if 条件语句
        // ============================================================================
        // if (条件) { ... } else { ... }
        //
        // 对比 Go：
        // Go: if g_parsers[i].used { ... }
        // C++: if (g_parsers[i].used) { ... }
        //
        // 区别：Go 的 if 条件不需要括号
        // ============================================================================
        if (g_parsers[i].used
            && g_parsers[i].dev_addr == dev_addr
            && g_parsers[i].instance == instance) {
            // 找到匹配的槽位，返回指针
            // ============================================================================
            // return 语句
            // ============================================================================
            // return 表达式;  - 返回表达式的值
            // return;         - 不返回值（用于 void 函数）
            //
            // 对比 Go：
            // Go: return &g_parsers[i]
            // C++: return &g_parsers[i];
            //
            // 几乎一样，只是 C++ 语句以分号结束
            // ============================================================================
            return &g_parsers[i];
        }
    }

    // 循环结束，未找到匹配的槽位
    if (!alloc) {
        // 如果只是查找（不分配），返回空指针
        return nullptr;
    }

    // 需要分配一个空闲槽位
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_parsers[i].used) {
            // 找到空闲槽位，分配它
            g_parsers[i].used = true;
            g_parsers[i].dev_addr = dev_addr;
            g_parsers[i].instance = instance;
            // ============================================================================
            // . 成员访问运算符
            // ============================================================================
            // 结构体变量.成员名
            //
            // 对比 Go：
            // Go: slot.Parser.Reset()
            // C++: slot.parser.reset();
            //
            // 几乎一样，只是 C++ 用 . ，Go 也用 .
            // ============================================================================
            g_parsers[i].parser.reset();
            return &g_parsers[i];
        }
    }

    // 没有空闲槽位
    return nullptr;
}

// findSlot - 查找设备槽位（只查找不分配）
ParserSlot* findSlot(uint8_t dev_addr, uint8_t instance) {
    // 直接调用 findOrAllocSlot，传入 alloc=false
    return findOrAllocSlot(dev_addr, instance, false);
}

// freeSlot - 释放设备槽位
void freeSlot(uint8_t dev_addr, uint8_t instance) {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (g_parsers[i].used
            && g_parsers[i].dev_addr == dev_addr
            && g_parsers[i].instance == instance) {
            // 找到匹配的槽位，释放它
            g_parsers[i].used = false;
            g_parsers[i].dev_addr = 0;
            g_parsers[i].instance = 0;
            g_parsers[i].parser.reset();
            return;
        }
    }
}

// ============================================================================
// anonymous namespace 结束
// ============================================================================
} // namespace

// ============================================================================
// 函数定义 - 注册回调
// ============================================================================
// 这些函数在 .h 文件中声明过，这里是实现

// registerKeyEventCallback - 注册按键事件回调
// 参数：cb - 回调函数指针
void registerKeyEventCallback(KeyEventCallback cb) {
    // ============================================================================
    // 赋值语句
    // ============================================================================
    // 变量 = 表达式;
    //
    // 对比 Go：
    // Go: g_key_cb = cb
    // C++: g_key_cb = cb;
    //
    // 几乎一样，只是 C++ 语句以分号结束
    // ============================================================================
    g_key_cb = cb;
}

// registerMountCallback - 注册设备挂载回调
void registerMountCallback(MountCallback cb) {
    g_mount_cb = cb;
}

// registerStringsCallback - 注册字符串描述符回调
void registerStringsCallback(StringsCallback cb) {
    g_strings_cb = cb;
}

// poll_strings_task - 在主循环中调用，处理待获取的设备描述符和字符串描述符
// 必须在 tuh_task() 之后调用，因为同步获取函数依赖 tuh_task() 驱动 USB 传输
// 注意：mount 回调中无法使用同步 API（_ctrl_xfer.stage 不是 IDLE）
void poll_strings_task() {
    for (size_t i = 0; i < kMaxDevices; i++) {
        if (!g_pending[i].pending) continue;

        g_pending[i].tick++;

        // 等待约 50 次调用（约 500ms），确保设备枚举完全完成
        if (g_pending[i].tick < 50) continue;

        uint8_t dev_addr = g_pending[i].dev_addr;
        uint8_t instance = g_pending[i].instance;
        uint8_t itf_protocol = g_pending[i].itf_protocol;
        g_pending[i].pending = false;

        // 设备可能被拔出了，检查是否还连接
        if (!tuh_mounted(dev_addr)) continue;

        // 构造设备信息结构体
        usb_host::device_info info = {};
        info.dev_addr = dev_addr;
        info.instance = instance;
        info.itf_protocol = itf_protocol;

        // 获取 VID/PID（TinyUSB 缓存）
        tuh_vid_pid_get(dev_addr, &info.vid, &info.pid);

        // 获取设备描述符（同步 API，在主循环中可正常工作）
        tusb_desc_device_t dev_desc;
        std::memset(&dev_desc, 0, sizeof(dev_desc));
        uint8_t dev_desc_result = tuh_descriptor_get_device_sync(dev_addr, &dev_desc, sizeof(dev_desc));
        if (dev_desc_result == XFER_RESULT_SUCCESS) {
            info.bcd_usb = dev_desc.bcdUSB;
            info.b_device_class = dev_desc.bDeviceClass;
            info.b_device_subclass = dev_desc.bDeviceSubClass;
            info.b_device_protocol = dev_desc.bDeviceProtocol;
            info.b_max_packet_size0 = dev_desc.bMaxPacketSize0;
            info.bcd_device = dev_desc.bcdDevice;
        }

        // 获取配置描述符，解析接口和端点信息
        uint8_t buffer[512];
        std::memset(buffer, 0, sizeof(buffer));
        uint8_t cfg_result = tuh_descriptor_get_configuration_sync(
            dev_addr, 0, buffer, sizeof(buffer));

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
                        uint8_t itf_num = p[2];
                        uint8_t itf_prot = p[5];

                        if (itf_prot == itf_protocol) {
                            info.itf_num = itf_num;
                            info.b_interface_class = p[4];
                            info.b_interface_subclass = p[5];

                            p += desc_len;
                            while (p < buffer + cfg_total_len) {
                                if (p[1] == TUSB_DESC_ENDPOINT) {
                                    const tusb_desc_endpoint_t* ep =
                                        (const tusb_desc_endpoint_t*)p;
                                    if (ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
                                        info.b_interval = ep->bInterval;
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
        }

        if (info.b_interval == 0) {
            info.b_interval = 10;
        }

        // 发送 0x71 设备帧
        if (usb_host::g_mount_cb != nullptr) {
            usb_host::g_mount_cb(info, true);
        }

        // 获取字符串描述符并发送 0x72
        usb_host::device_strings strings = {};
        constexpr uint16_t langid = 0x0409;

        if (dev_desc_result == XFER_RESULT_SUCCESS) {
            if (dev_desc.iManufacturer > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(
                    dev_addr, dev_desc.iManufacturer, (uint16_t)langid,
                    str_buf, (uint16_t)sizeof(str_buf));
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.manufacturer_len = data_len;
                    std::memcpy(strings.manufacturer, str_buf + 2, data_len);
                    std::memset(strings.manufacturer + data_len, 0, 64 - data_len);
                }
            }

            if (dev_desc.iProduct > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(
                    dev_addr, dev_desc.iProduct, (uint16_t)langid,
                    str_buf, (uint16_t)sizeof(str_buf));
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.product_len = data_len;
                    std::memcpy(strings.product, str_buf + 2, data_len);
                    std::memset(strings.product + data_len, 0, 64 - data_len);
                }
            }

            if (dev_desc.iSerialNumber > 0) {
                uint8_t str_buf[66];
                std::memset(str_buf, 0, sizeof(str_buf));
                uint8_t str_result = tuh_descriptor_get_string_sync(
                    dev_addr, dev_desc.iSerialNumber, (uint16_t)langid,
                    str_buf, (uint16_t)sizeof(str_buf));
                if (str_result == XFER_RESULT_SUCCESS && str_buf[0] >= 3) {
                    uint16_t data_len = str_buf[0] - 2;
                    if (data_len > 64) data_len = 64;
                    strings.serial_len = data_len;
                    std::memcpy(strings.serial, str_buf + 2, data_len);
                    std::memset(strings.serial + data_len, 0, 64 - data_len);
                }
            }
        }

        if (usb_host::g_strings_cb != nullptr) {
            usb_host::g_strings_cb(dev_addr, strings);
        }
    }
}

// getMountedKeyboardCount - 获取挂载的键盘数量
size_t getMountedKeyboardCount() {
    return g_mounted;
}

// ============================================================================
// namespace 结束
// ============================================================================
} // namespace usb_host

// ============================================================================
// extern "C" - C++ 和 C 的混合编程
// ============================================================================
// extern "C" { ... } 告诉 C++ 编译器：
// "这里面的是 C 语言代码，不要用 C++ 的名字修饰（name mangling）"
//
// 什么是 name mangling？
// C++ 支持函数重载（同名函数，参数不同）
// 所以 C++ 编译器会对函数名进行"修饰"，加上参数类型信息
// 例如：void foo(int) 变成 _Z3fooi
//
// 但 C 语言不支持函数重载，所以不会进行这种修饰
//
// 为什么要 extern "C"？
// TinyUSB 库是 C 语言写的，它提供的回调函数是 C 风格
// 但我们的代码是 C++，需要用 extern "C" 来正确链接
//
// 对比 Go：
// Go 没有这个问题，因为 Go 和 C 的互操作是通过 cgo 明确声明的
// ============================================================================
extern "C" {

// ============================================================================
// TinyUSB Host Stack 回调函数
// ============================================================================
// 这些函数名是 TinyUSB 库规定的，不能改变
// TinyUSB 会在适当的时机调用这些函数

// ---- tuh_hid_mount_cb - 设备挂载回调 ----
// 这是 TinyUSB 定义的回调函数，由 TinyUSB 在检测到 HID 设备时调用
// 参数：
//   dev_addr - USB 设备地址（1-127）
//   instance - HID 实例号
//   desc_report - HID 报告描述符（通常为空）
//   desc_len - 报告描述符长度（通常为 0）
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;

    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    // 处理键盘和鼠标设备（HID boot protocol 设备）
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || 
        itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        
        // 键盘设备需要分配解析器槽位，鼠标设备不需要（暂不解析鼠标数据）
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            auto* slot = usb_host::findOrAllocSlot(dev_addr, instance, true);
            if (slot != nullptr) {
                usb_host::g_mounted++;
            }
        }

        // 标记设备待处理，所有描述符读取和 0x71/0x72 发送延迟到主循环中执行
        // 原因：tuh_hid_mount_cb 是从 tuh_task() 内部调用的，此时 _ctrl_xfer.stage
        // 不是 IDLE，无法启动新的同步控制传输
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

    // 重要：发起第一次 HID IN transfer
    tuh_hid_receive_report(dev_addr, instance);
}

// ---- tuh_hid_umount_cb - 设备卸载回调 ----
// 当 HID 设备从总线移除时，TinyUSB 调用这个函数
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    // 处理键盘和鼠标设备的卸载
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD || 
        itf_protocol == HID_ITF_PROTOCOL_MOUSE) {
        
        // 键盘设备需要释放槽位
        if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
            usb_host::freeSlot(dev_addr, instance);
            if (usb_host::g_mounted > 0) {
                usb_host::g_mounted--;
            }
        }

        // 清除该设备的待处理标记（防止 poll_strings_task 对已拔出设备操作）
        for (size_t i = 0; i < usb_host::kMaxDevices; i++) {
            if (usb_host::g_pending[i].pending && usb_host::g_pending[i].dev_addr == dev_addr) {
                usb_host::g_pending[i].pending = false;
                break;
            }
        }

        // 调用卸载回调
        if (usb_host::g_mount_cb != nullptr) {
            usb_host::device_info info = {};
            info.dev_addr = dev_addr;
            info.instance = instance;
            info.itf_protocol = itf_protocol;

            // 获取设备信息（可能还能获取到）
            tuh_vid_pid_get(dev_addr, &info.vid, &info.pid);

            // 通知上层设备已拔出
            usb_host::g_mount_cb(info, false);
        }
    }
}

// ---- tuh_hid_report_received_cb - HID 报告接收回调 ----
// 当 HID 设备发送数据时，TinyUSB 调用这个函数
// 这是 HID polling 的核心回调
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    const uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        // 查找设备的解析器槽位
        auto* slot = usb_host::findSlot(dev_addr, instance);

        // 检查槽位和回调都有效
        if (slot != nullptr && usb_host::g_key_cb != nullptr) {
            // ============================================================================
            // static_cast - 类型转换
            // ============================================================================
            // static_cast<目标类型>(表达式)
            // 用于相关的类型之间的转换
            //
            // 这里的用法：
            // static_cast<size_t>(len) 将 uint16_t 转换为 size_t
            //
            // 对比 Go：
            // Go 的类型转换语法：目标类型(表达式)
            // Go: size_t(len)
            // C++: static_cast<size_t>(len)
            //
            // C++ 有多种类型转换：
            // - static_cast: 编译时类型转换
            // - const_cast: 添加/移除 const
            // - reinterpret_cast: 位级别的重新解释
            // - dynamic_cast: 运行时多态类型转换
            // ============================================================================

            // 调用解析器解析 HID 报告
            // 解析器会将原始报告转换为 key_event，然后调用 g_key_cb
            slot->parser.parse(report, static_cast<size_t>(len), usb_host::g_key_cb);
        }
    }

    // ============================================================================
    // 重要：再次发起 HID IN transfer
    // ============================================================================
    // 这是实现 continuous polling 的关键
    //
    // 工作原理：
    // 1. 设备插入 -> 调用 tuh_hid_receive_report -> 发起 IN transaction
    // 2. 设备返回数据 -> TinyUSB 调用 tuh_hid_report_received_cb
    // 3. 在回调中再次调用 tuh_hid_receive_report -> 发起下一个 IN transaction
    // 4. 循环往复...
    //
    // 这样就形成了持续的 polling 循环
    // 每次 polling 的间隔由设备的 bInterval 决定
    tuh_hid_receive_report(dev_addr, instance);
}

// ============================================================================
// extern "C" 结束
// ============================================================================
} // extern "C"

// ============================================================================
// #else - 条件编译的另一个分支
// ============================================================================
// 如果 ENABLE_USB = 0，编译这段代码
// 这提供了在没有 TinyUSB 的情况下也能编译通过的能力
// ============================================================================
#else // ENABLE_USB == 0: 空桩实现，不链接 tinyusb

// ============================================================================
// #include <cstddef> - 当 ENABLE_USB=0 时只需要这个头文件
// ============================================================================
// 因为这个分支不需要 USB 相关的功能
// 所以只需要标准库头文件
// ============================================================================
#include <cstddef>

// ============================================================================
// namespace - 和上面的 usb_host { } 配对
// ============================================================================
namespace usb_host {

// ============================================================================
// 空桩实现（Stub Implementation）
// ============================================================================
// 当 ENABLE_USB = 0 时，这些函数不做任何事
// 这样其他代码调用这些函数时不会编译错误

void registerKeyEventCallback(KeyEventCallback) {
    // 空实现：直接返回，不保存回调
    // 参数被忽略（Go 中也经常这样忽略参数）
}

void registerMountCallback(MountCallback) {
    // 空实现
}

void registerStringsCallback(StringsCallback) {
    // 空实现
}

void poll_strings_task() {
    // 空实现
}

size_t getMountedKeyboardCount() {
    // 总是返回 0，表示没有设备挂载
    return 0;
}

// ============================================================================
// namespace 结束
// ============================================================================
} // namespace usb_host

// ============================================================================
// #endif - 结束 #if ENABLE_USB 块
// ============================================================================
// 对应 #if ENABLE_USB
// ============================================================================
#endif // ENABLE_USB
