#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// ===== TinyUSB Host Stack 配置 =====
// 本项目 RP2040 作为 USB HOST，用于连接键盘接收器
//
// 当 ENABLE_USB=0 时，TinyUSB 库不参与链接，
// tusb_option.h 等内部头文件不可访问，因此整个配置体用宏守卫包裹

#if ENABLE_USB

#ifdef __cplusplus
extern "C" {
#endif

#include "tusb_option.h"

// ----- Device 模式：禁用（本项目仅使用 HOST 模式） -----
#ifndef CFG_TUD_ENABLED
#define CFG_TUD_ENABLED              0
#endif

// ----- Host 模式：启用 -----
#ifndef CFG_TUH_ENABLED
#define CFG_TUH_ENABLED              1
#endif

// RP2040 使用硬件 USB host 控制器（hwcmsys/hwctrl）
// pico-sdk 中 tinyusb 会自动使用 rhport = 0

// 最大同时挂载的 USB 设备数
#ifndef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX           2
#endif

// Host 端点缓冲区大小（足够 HID keyboard 使用）
#ifndef CFG_TUH_ENDPOINT_MAX
#define CFG_TUH_ENDPOINT_MAX         8
#endif

// ----- HID Class 配置 -----
// 启用 HID host 类，支持 keyboard boot protocol
#ifndef CFG_TUH_HID
#define CFG_TUH_HID                  1
#endif

// 同时处理的 HID 接口数
#ifndef CFG_TUH_HID_EPIN_BUFSIZE
#define CFG_TUH_HID_EPIN_BUFSIZE     64
#endif

#ifndef CFG_TUH_HID_EPOUT_BUFSIZE
#define CFG_TUH_HID_EPOUT_BUFSIZE    64
#endif

// 禁用非必需类以节省内存
#ifndef CFG_TUH_MSC
#define CFG_TUH_MSC                  0
#endif

#ifndef CFG_TUH_CDC
#define CFG_TUH_CDC                  0
#endif

#ifndef CFG_TUH_VENDOR
#define CFG_TUH_VENDOR               0
#endif

// ----- Debug / Log -----
// CFG_TUSB_DEBUG 可选：0=关, 1=错误, 2=警告, 3=信息
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG               0
#endif

// MCU 特定（RP2040）
#ifndef CFG_TUSB_RHPORT0_MODE
#define CFG_TUSB_RHPORT0_MODE        (OPT_MODE_HOST)
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                  OPT_OS_NONE
#endif

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN           __attribute__ ((aligned(4)))
#endif

#ifdef __cplusplus
}
#endif

#endif // ENABLE_USB

#endif // TUSB_CONFIG_H
