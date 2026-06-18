#include "UsbApplication.h"
#include "blink.pio.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

namespace app {

// LED 引脚 - Pico 板载 LED (GPIO25)
constexpr uint8_t kLedPin = PICO_DEFAULT_LED_PIN;

// 心跳间隔 (毫秒)
constexpr uint32_t kHeartbeatIntervalMs = 1000;

UsbApplication::UsbApplication()
    : pio_instance_(pio0)
    , pio_sm_(0)
    , pio_program_offset_(0)
    , pio_inited_(false)
    , dma_channel_(-1)
    , dma_inited_(false)
    , uart_inited_(false)
    , tick_counter_(0) {
}

UsbApplication::~UsbApplication() {
    if (dma_inited_ && dma_channel_ >= 0) {
        dma_channel_cleanup(dma_channel_);
        dma_channel_unclaim(dma_channel_);
    }
    if (pio_inited_) {
        pio_sm_set_enabled(pio_instance_, pio_sm_, false);
    }
    if (uart_inited_) {
        uart_deinit(uart1);
    }
}

bool UsbApplication::initialize() {
    std::printf("[UsbApplication] Starting initialization...\n");

    if (!initStdio()) return false;
    if (!initPioBlink()) return false;
    if (!initDmaDemo()) return false;
    if (!initUart()) return false;
    if (!initUsb()) return false;

    std::printf("[UsbApplication] All subsystems initialized successfully\n");
    return true;
}

bool UsbApplication::initStdio() {
    std::printf("[UsbApplication] Stdio already initialized by pico_stdio_init\n");
    return true;
}

bool UsbApplication::initPioBlink() {
    if (!pio_can_add_program(pio_instance_, &blink_program)) {
        std::printf("[UsbApplication] ERROR: Cannot add PIO blink program\n");
        return false;
    }

    pio_program_offset_ = pio_add_program(pio_instance_, &blink_program);
    std::printf("[UsbApplication] PIO blink program loaded at offset %lu\n", pio_program_offset_);

    blinkPin(kLedPin, 3);
    pio_inited_ = true;
    return true;
}

bool UsbApplication::initDmaDemo() {
    dma_channel_ = dma_claim_unused_channel(true);
    if (dma_channel_ < 0) {
        std::printf("[UsbApplication] ERROR: Cannot claim DMA channel\n");
        return false;
    }

    std::snprintf(dma_src_buffer_, kDmaBufferSize, "DMA-Demo-Pico-USB-v0.2");
    memset(dma_dst_buffer_, 0, kDmaBufferSize);

    dma_channel_config cfg = dma_channel_get_default_config(dma_channel_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, true);

    dma_channel_configure(
        dma_channel_,
        &cfg,
        dma_dst_buffer_,
        dma_src_buffer_,
        kDmaBufferSize,
        false
    );

    dma_channel_start(dma_channel_);
    dma_channel_wait_for_finish_blocking(dma_channel_);

    std::printf("[UsbApplication] DMA transfer complete: %s\n", dma_dst_buffer_);

    dma_inited_ = true;
    return true;
}

bool UsbApplication::initUart() {
    // 初始化 UART1
    // - 波特率: 115200
    // - TX: GPIO4, RX: GPIO5
    // - 数据位: 8, 停止位: 1, 校验: 无
    uart_init(uart1, 115200);

    // 配置 GPIO 引脚功能
    gpio_set_function(4, GPIO_FUNC_UART);
    gpio_set_function(5, GPIO_FUNC_UART);

    // 设置数据流格式 (8N1)
    uart_set_format(uart1, 8, 1, UART_PARITY_NONE);

    // 发送测试消息
    uart_puts(uart1, "[UART1] UsbApplication initialized\n");

    uart_inited_ = true;
    std::printf("[UsbApplication] UART1 initialized on GPIO4(GPIO5)\n");
    return true;
}

bool UsbApplication::initUsb() {
    // 设置 USB 连接状态回调
    usb_manager_.setConnectionCallback(
        [](bool connected, void* user_data) {
            static_cast<UsbApplication*>(user_data)->onUsbConnectionChanged(connected);
        },
        this
    );

    // 初始化 tinyusb 设备栈
    if (!usb_manager_.initialize()) {
        std::printf("[UsbApplication] ERROR: USB initialization failed\n");
        return false;
    }

    std::printf("[UsbApplication] USB subsystem ready\n");
    return true;
}

void UsbApplication::runOnce() {
    processUsbTask();
    processBlinkTask();
    processHeartbeat();
}

void UsbApplication::runLoop() {
    std::printf("[UsbApplication] Entering main loop...\n");
    while (true) {
        runOnce();
    }
}

void UsbApplication::processUsbTask() {
    // 调用 USB 任务处理器
    // 必须在主循环中频繁调用以处理 USB 协议事务
    usb_manager_.task();
}

void UsbApplication::processBlinkTask() {
    // PIO 状态机自动处理 LED 闪烁，无需额外操作
}

void UsbApplication::processHeartbeat() {
    // 每 1 秒输出一次心跳消息
    // 使用计数器 + sleep_ms 实现粗略计时
    // （精确计时应使用 hardware_timer）
    static uint32_t last_heartbeat_ms = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (now - last_heartbeat_ms >= kHeartbeatIntervalMs) {
        last_heartbeat_ms = now;
        tick_counter_++;

        // 通过 UART 发送心跳
        char uart_buf[64];
        int len = std::snprintf(uart_buf, sizeof(uart_buf),
            "[UART1] Heartbeat #%lu, USB=%s\n",
            tick_counter_,
            usb_manager_.isConnected() ? "CONNECTED" : "DISCONNECTED");
        if (uart_inited_) {
            uart_write_blocking(uart1, reinterpret_cast<const uint8_t*>(uart_buf), len);
        }

        // 通过 USB CDC 发送心跳
        char usb_buf[64];
        int usb_len = std::snprintf(usb_buf, sizeof(usb_buf),
            "[USB-CDC] Heartbeat #%lu\r\n", tick_counter_);
        usb_manager_.sendCdcData(usb_buf, static_cast<uint32_t>(usb_len));

        std::printf("[UsbApplication] Heartbeat #%lu\n", tick_counter_);
    }
}

void UsbApplication::onUsbConnectionChanged(bool connected) {
    std::printf("[UsbApplication] USB host connection: %s\n",
        connected ? "CONNECTED" : "DISCONNECTED");

    // 发送连接状态变化到 UART
    const char* msg = connected
        ? "[UART1] USB host connected\n"
        : "[UART1] USB host disconnected\n";
    if (uart_inited_) {
        uart_puts(uart1, msg);
    }
}

void UsbApplication::blinkPin(uint8_t pin, uint32_t freq_hz) {
    blink_program_init(pio_instance_, pio_sm_, pio_program_offset_, pin);
    pio_sm_set_enabled(pio_instance_, pio_sm_, true);

    uint32_t cycles = (125000000 / (2 * freq_hz)) - 3;
    pio_instance_->txf[pio_sm_] = cycles;

    std::printf("[UsbApplication] Blinking GPIO%u at %u Hz\n", pin, freq_hz);
}

} // namespace app
