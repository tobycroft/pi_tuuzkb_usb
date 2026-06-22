#include "TinyUsbManager.h"

// ===== 说明 =====
// 本文件历史用途为 USB Device 栈管理器（CDC 心跳）。
// 当前项目目标为 USB Host HID keyboard → CH9350L 风格二进制协议桥，
// 严格禁用 USB Device stack（tud_* API）。
//
// 因此：
//   - ENABLE_USB=1 时保留原始 TinyUSB 相关代码（虽然仍使用 device API，
//     但仅作为遗留类存在；主流程 main.cpp 使用 tuh_* host API）
//   - ENABLE_USB=0 时整个实现为空桩，不 include tusb.h，确保编译通过

#ifndef ENABLE_USB
#define ENABLE_USB 0
#endif

#if ENABLE_USB
#include "tusb.h"
#include <cstdio>
#endif

namespace driver {

TinyUsbManager::TinyUsbManager()
    : initialized_(false)
    , connected_(false)
    , connection_cb_(nullptr)
    , user_data_(nullptr) {
}

TinyUsbManager::~TinyUsbManager() {
    initialized_ = false;
}

bool TinyUsbManager::initialize() {
    if (initialized_) {
        return true;
    }

#if ENABLE_USB
#if CFG_TUD_ENABLED
    // tinyusb 由 pico_stdio_usb 自动初始化（Device 模式）
    initialized_ = true;
    std::printf("[TinyUsbManager] USB device stack ready (managed by pico_stdio_usb)\n");
#else
    // Host 模式（当前项目架构）：tusb_init() 已由 main 调用，
    // 此处仅将 manager 标记为就绪，后续可扩展为 Host 侧连接管理。
    initialized_ = true;
    std::printf("[TinyUsbManager] USB host stack ready (Host-mode stub)\n");
#endif
#else
    // ENABLE_USB=0：不依赖 tusb.h，直接标记为就绪
    initialized_ = true;
#endif
    return true;
}

void TinyUsbManager::task() {
    if (!initialized_) {
        return;
    }

#if ENABLE_USB
#if CFG_TUD_ENABLED
    // Device 模式：通过 tud_task 的挂载状态判断 USB 连接
    tud_task();
    bool current_state = tud_mounted();
    if (current_state != connected_) {
        connected_ = current_state;
        if (connection_cb_) {
            connection_cb_(current_state, user_data_);
        }
    }
#else
    // Host 模式：任务轮询由上层调用 tuh_task() 处理，此处不重复
    (void)connection_cb_;
    (void)user_data_;
#endif
#else
    (void)connection_cb_;
    (void)user_data_;
#endif
}

bool TinyUsbManager::isConnected() const {
    return connected_;
}

void TinyUsbManager::setConnectionCallback(ConnectionCallback cb, void* user_data) {
    connection_cb_ = cb;
    user_data_ = user_data;
}

void TinyUsbManager::sendCdcData(const char* data, uint32_t length) {
#if ENABLE_USB && CFG_TUD_ENABLED
    if (!initialized_ || !connected_) {
        return;
    }

    uint32_t written = tud_cdc_write(data, length);
    if (written > 0) {
        tud_cdc_write_flush();
    }
#else
    // Host 模式或 ENABLE_USB=0：无 Device 侧 CDC 通道，直接忽略发送请求
    (void)data;
    (void)length;
    (void)initialized_;
    (void)connected_;
#endif
}

} // namespace driver
