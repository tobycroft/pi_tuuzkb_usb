#ifndef TINY_USB_MANAGER_H
#define TINY_USB_MANAGER_H

// 确保使用 C++17 标准
#if __cplusplus < 201703L
#error "TinyUsbManager requires C++17 or later"
#endif

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

// 前置声明
typedef void (*ConnectionCallback)(bool connected, void* user_data);

namespace driver {

// TinyUsbManager - 驱动层，负责管理 tinyusb 设备栈的生命周期
// 使用 RAII 模式管理 USB 设备资源
class TinyUsbManager {
public:
    TinyUsbManager();
    ~TinyUsbManager();

    // 禁止复制，确保单一所有权
    TinyUsbManager(const TinyUsbManager&) = delete;
    TinyUsbManager& operator=(const TinyUsbManager&) = delete;

    // 初始化 tinyusb 设备栈
    bool initialize();

    // 任务轮询函数，需要在主循环中定期调用
    void task();

    // 查询 USB 是否连接到主机
    bool isConnected() const;

    // 设置连接状态变更回调
    void setConnectionCallback(ConnectionCallback cb, void* user_data);

    // 发送字符串到 USB CDC 接口
    void sendCdcData(const char* data, uint32_t length);

private:
    bool initialized_;
    bool connected_;
    ConnectionCallback connection_cb_;
    void* user_data_;
};

} // namespace driver

#endif // TINY_USB_MANAGER_H
