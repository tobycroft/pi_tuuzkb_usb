#include "UsbApplication.h"
#include "blink.pio.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include <cstdio>
#include <cstring>

namespace app {

// UART 配置常量
constexpr uart_inst_t* kUartId = uart1;
constexpr uint32_t kUartBaudRate = 115200;
constexpr uint8_t kUartTxPin = 4;
constexpr uint8_t kUartRxPin = 5;

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
    // RAII 析构：释放硬件资源
    if (dma_inited_ && dma_channel_ >= 0) {
        dma_channel_cleanup_and_clear_required(dma_channel_);
    }
    if (pio_inited_) {
        pio_sm_set_enabled(pio_instance_, pio_sm_, false);
    }
    if (uart_inited_) {
        uart_deinit(kUartId);
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
    // 初始化标准 I/O
    // - 配置 UART0 (GPIO0/1) 或 USB CDC 作为 stdout
    stdio_init_all();
    return true;
}

bool UsbApplication::initPioBlink() {
    // 检查 PIO0 是否有足够空间加载 blink 程序
    if (!pio_can_add_program(pio_instance_, &blink_program)) {
        std::printf("[UsbApplication] ERROR: Cannot add PIO blink program\n");
        return false;
    }

    // 加载 blink PIO 程序到 PIO 指令内存
    pio_program_offset_ = pio_add_program(pio_instance_, &blink_program);
    std::printf("[UsbApplication] PIO blink program loaded at offset %u\n", pio_program_offset_);

    // 初始化状态机 0 控制板载 LED
    blink_program_init(pio_instance_, pio_sm_, pio_program_offset_, kLedPin);
    pio_sm_set_enabled(pio_instance_, pio_sm_, true);

    // 写入闪烁计数器值
    // PIO 程序每周期耗时: (n + 1) * 2 个循环 (两次 jmp x--)
    // 系统时钟 125 MHz, 目标频率 3 Hz (闪烁)
    uint32_t cycles = (125000000 / (2 * 3)) - 3;
    pio_instance_->txf[pio_sm_] = cycles;

    pio_inited_ = true;
    std::printf("[UsbApplication] PIO LED blink started on GPIO%u\n", kLedPin);
    return true;
}

bool UsbApplication::initDmaDemo() {
    // 申请一个空闲 DMA 通道
    dma_channel_ = dma_claim_unused_channel(true);
    if (dma_channel_ < 0) {
        std::printf("[UsbApplication] ERROR: No free DMA channel available\n");
        return false;
    }

    // 准备 DMA 源数据
    const char* demo_data = "DMA transfer OK!";
    std::strncpy(dma_src_buffer_, demo_data, kDmaBufferSize);
    std::memset(dma_dst_buffer_, 0, kDmaBufferSize);

    // 配置 DMA 通道
    // - 传输宽度: 8 位 (字节)
    // - 读地址: 自动递增
    // - 写地址: 自动递增
    dma_channel_config cfg = dma_channel_get_default_config(dma_channel_);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, true);
    channel_config_set_write_increment(&cfg, true);

    // 启动 DMA 传输
    uint32_t transfer_count = std::strlen(dma_src_buffer_) + 1;
    dma_channel_configure(
        dma_channel_,
        &cfg,
        dma_dst_buffer_,
        dma_src_buffer_,
        transfer_count,
        true
    );

    // 等待 DMA 完成（阻塞）
    dma_channel_wait_for_finish_blocking(dma_channel_);

    dma_inited_ = true;
    std::printf("[UsbApplication] DMA demo completed: %s\n", dma_dst_buffer_);
    return true;
}

bool UsbApplication::initUart() {
    // 初始化 UART1
    // - 波特率: 115200
    // - TX: GPIO4, RX: GPIO5
    // - 数据位: 8, 停止位: 1, 校验: 无
    uart_init(kUartId, kUartBaudRate);

    // 配置 GPIO 引脚功能
    gpio_set_function(kUartTxPin, GPIO_FUNC_UART);
    gpio_set_function(kUartRxPin, GPIO_FUNC_UART);

    // 设置数据流格式 (8N1)
    uart_set_format(kUartId, 8, 1, UART_PARITY_NONE);

    // 发送测试消息
    uart_puts(kUartId, "[UART1] UsbApplication initialized\n");

    uart_inited_ = true;
    std::printf("[UsbApplication] UART1 initialized on GPIO%u(GPIO%u)\n", kUartTxPin, kUartRxPin);
    return true;
}

bool UsbApplication::initUsb() {
    // 设置 USB 连接状态回调
    usb_manager_.setConnectionCallback(
        [this](bool connected) {
            this->onUsbConnectionChanged(connected);
        }
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
        int len = snprintf(uart_buf, sizeof(uart_buf),
            "[UART1] Heartbeat #%lu, USB=%s\n",
            tick_counter_,
            usb_manager_.isConnected() ? "CONNECTED" : "DISCONNECTED");
        if (uart_inited_) {
            uart_write_blocking(kUartId, reinterpret_cast<const uint8_t*>(uart_buf), len);
        }

        // 通过 USB CDC 发送心跳
        char usb_buf[64];
        int usb_len = snprintf(usb_buf, sizeof(usb_buf),
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
        uart_puts(kUartId, msg);
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
