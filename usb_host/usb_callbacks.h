// ============================================================================
// C++ 头文件 (.h) 的作用
// ============================================================================
// 在 C++ 中，.h 文件叫做"头文件"（Header File）
//
// 为什么需要头文件？
// 1. 声明（Declaration）：告诉编译器"这个函数/类/变量存在"
// 2. 提供接口：让其他文件知道如何调用这个模块
// 3. 分离编译：加快编译速度（改动 .h 会触发重编译，改动 .cpp 可能不用）
//
// 对比 Go：
// Go 没有头文件，所有代码都在一起。但是 Go 有 interface{} 作为抽象类型。
// C++ 的 .h 类似于 Go 的 package 导出声明，但更显式。
//
// 头文件包含的内容：
// - 函数声明（告诉其他文件有哪些函数可用）
// - 类/结构体定义
// - 常量/枚举定义
// - 模板（template）
//
// 重要规则：头文件里一般不放实现代码（除非是 inline 或 template）
// ============================================================================

// ============================================================================
// #ifndef / #define / #endif - 预处理指令（防止重复包含）
// ============================================================================
// #ifndef XXX = "If Not Defined XXX"（如果没有定义 XXX）
// #define XXX    = "定义 XXX"
// #endif        = "结束 #if 块"
//
// 作用：防止同一个头文件被多次 #include，导致重复定义错误
//
// 类比 Go：Go 没有这个问题，因为 Go 不允许多次 import 同一个包
//
// 语法解释：
// #ifndef USB_HOST_USB_CALLBACKS_H
// #define USB_HOST_USB_CALLBACKS_H
//     ... 头文件内容 ...
// #endif
//
// 第一遍包含：USB_HOST_USB_CALLBACKS_H 未定义，进入 #ifndef，执行 #define 定义它
// 第二遍包含：USB_HOST_USB_CALLBACKS_H 已定义，跳过 #ifndef 块，避免重复
// ============================================================================
#ifndef USB_HOST_USB_CALLBACKS_H
#define USB_HOST_USB_CALLBACKS_H

// ============================================================================
// C++ 注释语法
// ============================================================================
// 单行注释：// 注释内容（到行尾）
// 多行注释：/* 注释内容 */（不能嵌套）
//
// 对比 Go：
// Go 的单行注释也是 //
// Go 的多行注释也是 /* */，但习惯用 // + //
// ============================================================================

// ============================================================================
// C++ 版本要求检查 - #if 预处理指令
// ============================================================================
// __cplusplus 是 C++ 编译器内置的宏，表示 C++ 标准版本
// C++11 = 201103L, C++14 = 201402L, C++17 = 201703L, C++20 = 202002L
//
// #if 条件：条件为真时编译这段代码
// #error 信息：直接编译失败并输出错误信息
//
// 作用：确保使用 C++17 或更高版本（因为我们用了某些 C++17 特性）
//
// 对比 Go：Go 没有编译时版本检查，这是 C++ 的独有特性
// ============================================================================
#if __cplusplus < 201703L
#error "usb_callbacks requires C++17 or later"
#endif

// ============================================================================
// #include - 包含其他头文件
// ============================================================================
// #include <文件名>  ：包含系统/标准库头文件
// #include "文件名"  ：包含项目自定义头文件
//
// <cstdint> vs "cstdint"：
// <> 通常用于系统库，"" 用于项目本地文件
// 但实际区别不大，编译器都会搜索
//
// 对比 Go 的 import：
// Go: import "fmt"
// C++: #include <cstdint> 或 #include "usb_callbacks.h"
// ============================================================================
#include <cstdint>  // 标准整数类型：uint8_t, uint16_t, uint32_t 等（C++11 引入）
#include <cstddef>  // 标准定义：size_t（表示内存大小/索引的无符号整数）

// ============================================================================
// namespace - 命名空间（避免名字冲突）
// ============================================================================
// namespace 关键字用于创建一个命名空间
// 命名空间的作用：防止不同模块定义的函数/变量名冲突
//
// 对比 Go：
// Go 用 package 来组织代码，但 package 名不提供命名空间隔离
// Go 的做法是通过目录结构和 import path 来区分
//
// C++ 命名空间示例：
// namespace usb_host { ... }          // 定义命名空间
// usb_host::registerCallback();       // 使用 :: 访问命名空间内的内容
// using namespace usb_host;            // 一次性引入整个命名空间（慎用）
//
// 命名空间可以嵌套：
// namespace outer {
//     namespace inner {
//         void func();
//     }
// }
// outer::inner::func();  // 访问嵌套命名空间的函数
// ============================================================================
namespace usb_host {

// ============================================================================
// struct - 结构体（类似 Go 的 struct）
// ============================================================================
// C++ 的 struct 和 class 几乎一样（默认访问权限不同）
// - struct 默认 public（成员可以直接访问）
// - class 默认 private（成员需要通过方法访问）
//
// 对比 Go：
// Go: type Person struct { Name string; Age int }
// C++: struct Person { std::string name; int age; };
//
// C++ 结构体可以包含：
// - 成员变量
// - 成员函数（但通常放在 .cpp 文件）
// - 构造函数/析构函数
// ============================================================================

// ---- key_event 结构定义 ----
// 代表一个按键事件：从 USB HID 键盘接收到的原始按键数据
struct key_event {
    uint8_t usage_code;   // HID Usage Code：键盘上每个键的唯一标识
                          // 例如：0x04 = 'a', 0x28 = Enter, 0x29 = Escape
    bool pressed;         // true = 按键按下, false = 按键释放
    uint8_t modifiers;    // 修饰键位图：Ctrl/Shift/Alt/Win 的组合状态
                          // 位0=Left Ctrl, 位1=Left Shift, 位2=Left Alt, 位3=Left Win
                          // 位4=Right Ctrl, 位5=Right Shift, 位6=Right Alt, 位7=Right Win
};

// ---- 设备信息结构 ----
// 当 USB HID 设备（键盘/鼠标）插入/拔出时，传递的完整设备描述符信息
struct device_info {
    // --- 设备状态 ---
    uint8_t dev_addr;         // USB 设备地址（1-127），由 USB 主机分配
    uint8_t mounted;          // 0=设备拔出，1=设备插入
    
    // --- 设备描述符字段 ---
    uint16_t vid;             // Vendor ID：厂商标识（USB-IF 分配）
    uint16_t pid;             // Product ID：产品标识（厂商自定义）
    uint16_t bcd_usb;         // USB 版本号（例如 0x0200 = USB 2.0）
    uint8_t  b_device_class;  // 设备类代码（HID=0x03，或 0x00=接口定义）
    uint8_t  b_device_subclass; // 设备子类代码
    uint8_t  b_device_protocol; // 设备协议代码
    uint8_t  b_max_packet_size0; // 端点0最大包大小（8/16/32/64）
    uint16_t bcd_device;      // 设备版本号（厂商自定义）
    
    // --- 配置描述符字段 ---
    uint8_t  b_num_interfaces;  // 配置支持的接口数量
    uint8_t  b_configuration_value; // 配置值（用于 SetConfiguration）
    uint8_t  bm_attributes;     // 配置属性（供电模式、远程唤醒等）
    uint8_t  b_max_power;       // 最大功耗（单位：2mA，例如 50=100mA）
    
    // --- 接口描述符字段 ---
    uint8_t  itf_num;           // 接口编号：设备内接口的序号
    uint8_t  b_interface_class; // 接口类代码（HID=0x03）
    uint8_t  b_interface_subclass; // 接口子类代码（HID boot=0x01）
    uint8_t  itf_protocol;      // 接口协议：HID_ITF_PROTOCOL_KEYBOARD=1, MOUSE=2
    uint8_t  b_interval;        // 中断端点轮询间隔（毫秒）
    uint8_t  instance;          // HID 实例号：支持同一个设备多个 HID 接口
};

// ---- 设备字符串描述符结构 ----
// USB 字符串描述符（UTF-16LE 编码，枚举完成后单独获取）
struct device_strings {
    uint8_t  manufacturer[16];  // 制造商字符串（UTF-16LE）
    uint8_t  product[16];       // 产品名称字符串（UTF-16LE）
    uint8_t  serial[16];        // 序列号字符串（UTF-16LE）
    uint8_t  manufacturer_len;  // 实际制造商字符串长度
    uint8_t  product_len;       // 实际产品名称字符串长度
    uint8_t  serial_len;        // 实际序列号字符串长度
};

// ============================================================================
// typedef / using - 类型别名
// ============================================================================
// C++98: typedef 原类型 别名;
// C++11: using 别名 = 原类型;  // 更清晰，推荐使用
//
// 对比 Go：
// Go: type HandlerFunc func(int) error
// C++: using HandlerFunc = std::function<void(int)>;  // 需要 <functional>
//
// 这里的用法是定义"函数指针类型"：
// using KeyEventCallback = void(*)(const key_event&);
//
// 解释：KeyEventCallback 是一个"指向函数的指针"的类型
// 它指向的函数签名是：void 函数名(const key_event&)
// ============================================================================

// ---- 事件回调函数类型 ----
// KeyEventCallback：按键变化事件的回调函数类型
// 回调函数签名：void callback(const key_event& event)
// - 参数：key_event 类型（按引用传递）
// - 返回：void（无返回值）
//
// 对比 Go 的函数类型：
// Go: type KeyEventCallback func(key_event)  // 值传递
// C++: using KeyEventCallback = void(*)(const key_event&);  // 引用传递
using KeyEventCallback = void(*)(const key_event&);

// MountCallback：设备插拔事件的回调函数类型
// 回调函数签名：void callback(const device_info& info, bool mounted)
// - 参数1：device_info 设备信息
// - 参数2：bool mounted，true=插入，false=拔出
using MountCallback = void(*)(const device_info& info, bool mounted);

// StringsCallback：字符串描述符获取完成回调
// 回调函数签名：void callback(uint8_t dev_addr, const device_strings& strings)
// - 参数1：dev_addr 设备地址
// - 参数2：device_strings 字符串描述符
using StringsCallback = void(*)(uint8_t dev_addr, const device_strings& strings);

// ============================================================================
// 函数声明（不包含实现）
// ============================================================================
// 头文件中只放函数声明，实现放在 .cpp 文件中
// 这叫做"声明与定义分离"
//
// 对比 Go：
// Go 所有代码都在同一个 .go 文件，不需要分离
// C++ 需要分离是为了：
// 1. 加快编译（改实现不影响依赖它的文件）
// 2. 隐藏实现细节（只暴露接口）
// ============================================================================

// ---- 注册回调的 API ----
// 由 main 或业务逻辑层在初始化时调用
// 这些函数会在后续被调用，用于设置回调函数

// 注册按键事件回调：当收到按键事件时调用这个函数
// 参数 cb：回调函数指针
void registerKeyEventCallback(KeyEventCallback cb);

// 注册设备挂载回调：当设备插入/拔出时调用这个函数
// 参数 cb：回调函数指针
// 参数 info：设备信息
// 参数 mounted：true=设备插入，false=设备拔出
void registerMountCallback(MountCallback cb);

// 注册字符串描述符回调：当字符串描述符获取完成时调用这个函数
// 参数 cb：回调函数指针
// 参数 dev_addr：设备地址
// 参数 strings：字符串描述符
void registerStringsCallback(StringsCallback cb);

// ---- 查询状态的辅助接口 ----
// 返回当前挂载的键盘设备数量
// 用于调试和状态显示
size_t getMountedKeyboardCount();

// ============================================================================
// namespace 结束标记
// ============================================================================
// 闭合上面的 namespace usb_host { ... }
// 这行注释只是标记，方便阅读，不影响代码
// ============================================================================
} // namespace usb_host

// ============================================================================
// #endif - 结束 #ifndef 块
// ============================================================================
// 对应开头的 #ifndef USB_HOST_USB_CALLBACKS_H
// 表示头文件内容到此结束
// ============================================================================
#endif // USB_HOST_USB_CALLBACKS_H
