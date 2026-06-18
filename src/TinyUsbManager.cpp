#include "TinyUsbManager.h"

#include "bsp/board.h"
#include "tusb.h"
#include "tusb_cdc.h"
#include <cstdio>

namespace driver {

TinyUsbManager::TinyUsbManager()
    : initialized_(false)
    , connected_(false)
    , connection_cb_(nullptr) {
}

TinyUsbManager::~TinyUsbManager() {
    // RAII 析构：当前 tinyusb 没有官方的反初始化函数
    // 此处标记为未初始化状态
    initialized_ = false;
}

bool TinyUsbManager::initialize() {
    if (initialized_) {
        return true;
    }

    // 初始化板级支持包 (BSP)
    // - 配置 USB 引脚 D+/D- (GPIO16/17)
    // - 配置 USB 时钟为 48 MHz
    // - 初始化 USB PHY
    board_init();

    // 初始化 tinyusb 设备栈
    // - 解析设备描述符
    // - 配置控制端点 (EP0)
    // - 初始化 CDC ACM 接口
    tud_init(BOARD_TUD_RHPORT);

    initialized_ = true;

    std::printf("[TinyUsbManager] USB device stack initialized\n");
    return true;
}

void TinyUsbManager::task() {
    if (!initialized_) {
        return;
    }

    // tinyusb 核心任务处理
    // 1. 处理来自主机的 SETUP 包
    // 2. 处理端点 IN/OUT 事务
    // 3. 调用用户回调 ( tud_cdc_*)
    tud_task();

    // 检查连接状态变化
    bool current_state = tud_mounted();
    if (current_state != connected_.load()) {
        connected_.store(current_state);
        if (connection_cb_) {
            connection_cb_(current_state);
        }
    }
}

bool TinyUsbManager::isConnected() const {
    return connected_.load();
}

void TinyUsbManager::setConnectionCallback(ConnectionCallback cb) {
    connection_cb_ = std::move(cb);
}

void TinyUsbManager::sendCdcData(const char* data, uint32_t length) {
    if (!initialized_ || !connected_.load()) {
        return;
    }

    // 通过 CDC 接口发送数据
    // tud_cdc_write 写入发送缓冲区
    // tud_cdc_write_flush 触发 IN 事务
    uint32_t written = tud_cdc_write(data, length);
    if (written > 0) {
        tud_cdc_write_flush();
    }
}

// ==== tinyusb 回调函数 (弱符号覆盖) ====

// USB 挂载回调 - 设备枚举完成
extern "C" void tud_mount_cb(void) {
    std::printf("[TinyUsbManager] USB mounted, host connected\n");
}

// USB 卸载回调 - 主机断开
extern "C" void tud_umount_cb(void) {
    std::printf("[TinyUsbManager] USB unmounted, host disconnected\n");
}

// 总线挂起回调 - 主机挂起
extern "C" void tud_suspend_cb(bool remote_wakeup_en) {
    (void)remote_wakeup_en;
    std::printf("[TinyUsbManager] USB bus suspended\n");
}

// 总线恢复回调 - 从挂起恢复
extern "C" void tud_resume_cb(void) {
    std::printf("[TinyUsbManager] USB bus resumed\n");
}

// CDC 接口控制请求回调
extern "C" void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    if (dtr && rts) {
        std::printf("[TinyUsbManager] CDC terminal opened (DTR+RTS)\n");
    }
}

// CDC 接收回调 - 主机发送数据到设备
extern "C" void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    // 回显接收到的数据
    uint8_t buf[64];
    uint32_t count = tud_cdc_read(buf, sizeof(buf));
    if (count > 0) {
        // 回显
        tud_cdc_write(buf, count);
        tud_cdc_write_flush();
    }
}

} // namespace driver
