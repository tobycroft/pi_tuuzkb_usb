#include "uart_protocol.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "../drivers/uart_dma.h"

namespace output {

namespace {

constexpr std::uint8_t  kUartTXPin    = 0;
constexpr std::uint8_t  kUartRXPin    = 1;
constexpr std::uint32_t kUartBaudRate = 921600;

bool g_initialized = false;
bool g_dma_enabled = false;
std::uint8_t g_kb_pkt_index = 0;
absolute_time_t g_last_uart_tx{};

drivers::UartDmaTx g_uart_dma_tx;

std::uint8_t g_frame_buf[kKeyboardFrameLen];
std::uint8_t g_device_frame_buf[kDeviceFrameLen];
std::uint8_t g_string_frame_buf[kStringFrameLen];

inline void uart_tx_send(const std::uint8_t* data, std::size_t len) {
    if (g_dma_enabled) {
        g_uart_dma_tx.send_blocking(data, len);
    } else {
        uart_write_blocking(uart0, data, len);
    }
}

} // namespace

void uart_protocol_init() {
    if (g_initialized) return;

    uart_init(uart0, kUartBaudRate);

    gpio_set_function(kUartTXPin, GPIO_FUNC_UART);
    gpio_set_function(kUartRXPin, GPIO_FUNC_UART);

    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);

    g_dma_enabled = g_uart_dma_tx.init(uart0);

    g_frame_buf[0] = kFrameHdr1;
    g_frame_buf[1] = kFrameHdr2;

    g_initialized = true;
}

void uart_send_key_event(const usb_host::key_event& e) {
    if (!g_initialized) return;

    g_frame_buf[0] = kFrameHdr1;
    g_frame_buf[1] = kFrameHdr2;
    g_frame_buf[2] = kFrameTypeKb;
    g_frame_buf[3] = e.usage_code;
    g_frame_buf[4] = e.pressed ? static_cast<std::uint8_t>(0x01) : static_cast<std::uint8_t>(0x00);
    g_frame_buf[5] = g_kb_pkt_index++;

    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < kKeyboardFrameLen - 1; i++) {
        sum += g_frame_buf[i];
    }
    g_frame_buf[6] = sum;

    g_last_uart_tx = get_absolute_time();
    uart_tx_send(g_frame_buf, kKeyboardFrameLen);
}

void uart_send_device_info(const usb_host::device_info& info, bool mounted) {
    if (!g_initialized) return;

    // 帧头：57 AB 71
    g_device_frame_buf[0] = kFrameHdr1;
    g_device_frame_buf[1] = kFrameHdr2;
    g_device_frame_buf[2] = kFrameTypeDevice;
    
    // 设备状态
    g_device_frame_buf[3] = info.dev_addr;
    g_device_frame_buf[4] = mounted ? static_cast<std::uint8_t>(0x01) : static_cast<std::uint8_t>(0x00);
    
    // 设备描述符字段
    g_device_frame_buf[5] = static_cast<std::uint8_t>((info.vid >> 8) & 0xFF);
    g_device_frame_buf[6] = static_cast<std::uint8_t>(info.vid & 0xFF);
    g_device_frame_buf[7] = static_cast<std::uint8_t>((info.pid >> 8) & 0xFF);
    g_device_frame_buf[8] = static_cast<std::uint8_t>(info.pid & 0xFF);
    g_device_frame_buf[9] = static_cast<std::uint8_t>((info.bcd_usb >> 8) & 0xFF);
    g_device_frame_buf[10] = static_cast<std::uint8_t>(info.bcd_usb & 0xFF);
    g_device_frame_buf[11] = info.b_device_class;
    g_device_frame_buf[12] = info.b_device_subclass;
    g_device_frame_buf[13] = info.b_device_protocol;
    g_device_frame_buf[14] = info.b_max_packet_size0;
    g_device_frame_buf[15] = static_cast<std::uint8_t>((info.bcd_device >> 8) & 0xFF);
    g_device_frame_buf[16] = static_cast<std::uint8_t>(info.bcd_device & 0xFF);
    
    // 配置描述符字段
    g_device_frame_buf[17] = info.b_num_interfaces;
    g_device_frame_buf[18] = info.b_configuration_value;
    g_device_frame_buf[19] = info.bm_attributes;
    g_device_frame_buf[20] = info.b_max_power;
    
    // 接口描述符字段
    g_device_frame_buf[21] = info.itf_num;
    g_device_frame_buf[22] = info.b_interface_class;
    g_device_frame_buf[23] = info.b_interface_subclass;
    g_device_frame_buf[24] = info.itf_protocol;
    g_device_frame_buf[25] = info.b_interval;
    g_device_frame_buf[26] = info.instance;

    // SUM 校验（前 27 字节）
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < kDeviceFrameLen - 1; i++) {
        sum += g_device_frame_buf[i];
    }
    g_device_frame_buf[27] = sum;

    g_last_uart_tx = get_absolute_time();
    uart_tx_send(g_device_frame_buf, kDeviceFrameLen);
}

void uart_send_device_strings(uint8_t dev_addr, const usb_host::device_strings& strings) {
    if (!g_initialized) return;

    // 帧头：57 AB 72
    g_string_frame_buf[0] = kFrameHdr1;
    g_string_frame_buf[1] = kFrameHdr2;
    g_string_frame_buf[2] = kFrameTypeString;

    // 设备地址
    g_string_frame_buf[3] = dev_addr;

    // 制造商字符串（偏移 4，长度 1 + 64）
    g_string_frame_buf[4] = strings.manufacturer_len;
    for (std::size_t i = 0; i < 64; i++) {
        g_string_frame_buf[5 + i] = (i < strings.manufacturer_len) ? strings.manufacturer[i] : 0x00;
    }

    // 产品名称字符串（偏移 69 = 4 + 1 + 64，长度 1 + 64）
    g_string_frame_buf[69] = strings.product_len;
    for (std::size_t i = 0; i < 64; i++) {
        g_string_frame_buf[70 + i] = (i < strings.product_len) ? strings.product[i] : 0x00;
    }

    // 序列号字符串（偏移 134 = 69 + 1 + 64，长度 1 + 64）
    g_string_frame_buf[134] = strings.serial_len;
    for (std::size_t i = 0; i < 64; i++) {
        g_string_frame_buf[135 + i] = (i < strings.serial_len) ? strings.serial[i] : 0x00;
    }

    // SUM 校验（前 199 字节）
    std::uint8_t sum = 0;
    for (std::size_t i = 0; i < kStringFrameLen - 1; i++) {
        sum += g_string_frame_buf[i];
    }
    g_string_frame_buf[199] = sum;

    g_last_uart_tx = get_absolute_time();
    uart_tx_send(g_string_frame_buf, kStringFrameLen);
}

void uart_send_frame(const std::uint8_t* data, std::size_t len) {
    if (!g_initialized || data == nullptr || len == 0) return;
    g_last_uart_tx = get_absolute_time();
    uart_tx_send(data, len);
}

bool uart_protocol_is_initialized() {
    return g_initialized;
}

absolute_time_t get_last_uart_tx_time() {
    return g_last_uart_tx;
}

bool uart_rx_process() {
    if (!g_initialized) return false;

    enum class RxState {
        WaitHdr1,
        WaitHdr2,
        WaitCmd,
        WaitLedByte,
        WaitChecksum
    };

    static RxState rx_state = RxState::WaitHdr1;
    static std::uint8_t checksum_acc = 0;
    static std::uint8_t led_byte = 0;

    bool handled = false;

    while (uart_is_readable(uart0)) {
        std::uint8_t byte = uart_getc(uart0);

        switch (rx_state) {
            case RxState::WaitHdr1:
                if (byte == kFrameHdr1) {
                    checksum_acc = byte;
                    rx_state = RxState::WaitHdr2;
                }
                break;

            case RxState::WaitHdr2:
                if (byte == kFrameHdr2) {
                    checksum_acc += byte;
                    rx_state = RxState::WaitCmd;
                } else {
                    rx_state = RxState::WaitHdr1;
                }
                break;

            case RxState::WaitCmd:
                if (byte == kFrameTypeLed) {
                    checksum_acc += byte;
                    rx_state = RxState::WaitLedByte;
                } else {
                    rx_state = RxState::WaitHdr1;
                }
                break;

            case RxState::WaitLedByte:
                led_byte = byte;
                checksum_acc += byte;
                rx_state = RxState::WaitChecksum;
                break;

            case RxState::WaitChecksum: {
                std::uint8_t expected = static_cast<std::uint8_t>(checksum_acc & 0xFF);
                rx_state = RxState::WaitHdr1;
                if (byte == expected) {
                    usb_host::setKeyboardLed(led_byte);
                    handled = true;
                }
                break;
            }
        }
    }

    return handled;
}

} // namespace output