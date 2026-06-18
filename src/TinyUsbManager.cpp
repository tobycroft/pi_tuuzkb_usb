#include "TinyUsbManager.h"

// 仅在 .cpp 文件中包含 tinyusb 头文件
#include "tusb.h"
#include <stdio.h>

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

    // tinyusb 由 pico_stdio_usb 自动初始化
    initialized_ = true;
    printf("[TinyUsbManager] USB device stack ready (managed by pico_stdio_usb)\n");
    return true;
}

void TinyUsbManager::task() {
    if (!initialized_) {
        return;
    }

    bool current_state = tud_mounted();
    if (current_state != connected_) {
        connected_ = current_state;
        if (connection_cb_) {
            connection_cb_(current_state, user_data_);
        }
    }
}

bool TinyUsbManager::isConnected() const {
    return connected_;
}

void TinyUsbManager::setConnectionCallback(ConnectionCallback cb, void* user_data) {
    connection_cb_ = cb;
    user_data_ = user_data;
}

void TinyUsbManager::sendCdcData(const char* data, uint32_t length) {
    if (!initialized_ || !connected_) {
        return;
    }

    uint32_t written = tud_cdc_write(data, length);
    if (written > 0) {
        tud_cdc_write_flush();
    }
}

} // namespace driver
