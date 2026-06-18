#ifndef TINY_USB_MANAGER_H
#define TINY_USB_MANAGER_H

#include "pico/stdlib.h"
#include <atomic>
#include <functional>

namespace driver {

// TinyUsbManager - 驱动层，负责管理 tinyusb 设备栈的生命周期
// 使用 RAII 模式管理 USB 设备资源
class TinyUsbManager {
public:
    // USB 连接状态回调类型
    using ConnectionCallback = std::function<void(bool)>;

    TinyUsbManager();
    ~TinyUsbManager();

    // 禁止复制，确保单一所有权
    TinyUsbManager(const TinyUsbManager&) = delete;
    TinyUsbManager& operator=(const TinyUsbManager&) = delete;

    // 初始化 tinyusb 设备栈
    // - 配置 USB 总线时钟 (48 MHz)
    // - 初始化 tinyusb 设备描述符
    // - 启用 USB D+/D- 引脚 (GPIO16/17 for Pico)
    bool initialize();

    // 任务轮询函数，需要在主循环中定期调用
    // - 处理 USB 主机请求
    // - 处理 CDC 数据收发
    void task();

    // 查询 USB 是否连接到主机
    bool isConnected() const;

    // 设置连接状态变更回调
    void setConnectionCallback(ConnectionCallback cb);

    // 发送字符串到 USB CDC 接口
    void sendCdcData(const char* data, uint32_t length);

private:
    // tinyusb 初始化标志
    bool initialized_;

    // 连接状态原子标志
    std::atomic<bool> connected_;

    // 用户回调
    ConnectionCallback connection_cb_;
};

} // namespace driver

#endif // TINY_USB_MANAGER_H
