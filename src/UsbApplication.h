#ifndef USB_APPLICATION_H
#define USB_APPLICATION_H

#include "TinyUsbManager.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include <cstdint>

namespace app {

// UsbApplication - 业务逻辑层
// 整合 USB 通信、LED 闪烁、DMA、UART 等功能
// 负责业务逻辑编排，不直接操作硬件寄存器
class UsbApplication {
public:
    UsbApplication();
    ~UsbApplication();

    // 禁止复制
    UsbApplication(const UsbApplication&) = delete;
    UsbApplication& operator=(const UsbApplication&) = delete;

    // 初始化所有子系统
    bool initialize();

    // 主循环执行一步任务
    void runOnce();

    // 主循环（阻塞）
    void runLoop();

private:
    // ==== USB 子系统 ====
    driver::TinyUsbManager usb_manager_;

    // ==== PIO LED 闪烁子系统 ====
    // PIO 实例和状态机编号
    PIO pio_instance_;
    uint8_t pio_sm_;
    uint32_t pio_program_offset_;
    bool pio_inited_;

    // ==== DMA 子系统 ====
    int dma_channel_;
    static constexpr uint32_t kDmaBufferSize = 32;
    char dma_src_buffer_[kDmaBufferSize];
    char dma_dst_buffer_[kDmaBufferSize];
    bool dma_inited_;

    // ==== UART 子系统 ====
    bool uart_inited_;

    // ==== 状态计数 ====
    uint32_t tick_counter_;

    // ==== 初始化函数 ====
    bool initStdio();
    bool initPioBlink();
    bool initDmaDemo();
    bool initUart();
    bool initUsb();

    // ==== 任务函数 ====
    void processUsbTask();
    void processBlinkTask();
    void processHeartbeat();

    // ==== USB 连接状态回调 ====
    void onUsbConnectionChanged(bool connected);

    // ==== LED 闪烁控制 ====
    // 使用 PIO 精确控制 LED 闪烁频率
    // pin: LED 引脚 (GPIO25 for Pico on-board LED)
    // freq_hz: 闪烁频率 (Hz)
    void blinkPin(uint8_t pin, uint32_t freq_hz);
};

} // namespace app

#endif // USB_APPLICATION_H
