#include "uart_protocol.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

namespace output {

namespace {

constexpr std::uint8_t  kUartTXPin    = 0;
constexpr std::uint8_t  kUartRXPin    = 1;
constexpr std::uint32_t kUartBaudRate = 9600;

bool g_initialized = false;

std::uint8_t g_frame_buf[kKeyboardFrameLen];
std::uint8_t g_device_frame_buf[kDeviceFrameLen];

} // namespace

void uart_protocol_init() {
    if (g_initialized) return;

    uart_init(uart0, kUartBaudRate);

    gpio_set_function(kUartTXPin, GPIO_FUNC_UART);
    gpio_set_function(kUartRXPin, GPIO_FUNC_UART);

    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);

    g_frame_buf[0] = kFrameHdr1;
    g_frame_buf[1] = kFrameHdr2;
    g_frame_buf[2] = kFrameHdr3Kb;

    g_initialized = true;
}

void uart_send_key_event(const usb_host::key_event& e) {
    if (!g_initialized) return;

    g_frame_buf[3] = e.usage_code;
    g_frame_buf[4] = e.pressed ? static_cast<std::uint8_t>(0x01) : static_cast<std::uint8_t>(0x00);
    g_frame_buf[5] = e.modifiers;

    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kKeyboardFrameLen - 1; i++) {
        xor_sum ^= g_frame_buf[i];
    }
    g_frame_buf[6] = xor_sum;

    uart_write_blocking(uart0, g_frame_buf, kKeyboardFrameLen);
}

void uart_send_device_info(const usb_host::device_info& info, bool mounted) {
    if (!g_initialized) return;

    // 帧头：57 AB 71
    g_device_frame_buf[0] = kFrameHdr1;
    g_device_frame_buf[1] = kFrameHdr2;
    g_device_frame_buf[2] = kFrameHdr3Dev;
    
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

    // 字符串描述符字段
    // 制造商字符串
    g_device_frame_buf[27] = info.manufacturer_len;
    for (size_t i = 0; i < 16; i++) {
        g_device_frame_buf[28 + i] = (i < info.manufacturer_len) ? info.manufacturer[i] : 0x00;
    }
    
    // 产品名称字符串
    g_device_frame_buf[44] = info.product_len;
    for (size_t i = 0; i < 16; i++) {
        g_device_frame_buf[45 + i] = (i < info.product_len) ? info.product[i] : 0x00;
    }
    
    // 序列号字符串
    g_device_frame_buf[61] = info.serial_len;
    for (size_t i = 0; i < 16; i++) {
        g_device_frame_buf[62 + i] = (i < info.serial_len) ? info.serial[i] : 0x00;
    }

    // XOR 校验（前 78 字节）
    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kDeviceFrameLen - 1; i++) {
        xor_sum ^= g_device_frame_buf[i];
    }
    g_device_frame_buf[78] = xor_sum;

    uart_write_blocking(uart0, g_device_frame_buf, kDeviceFrameLen);
}

void uart_send_frame(const std::uint8_t* data, std::size_t len) {
    if (!g_initialized || data == nullptr || len == 0) return;
    uart_write_blocking(uart0, data, len);
}

bool uart_protocol_is_initialized() {
    return g_initialized;
}

} // namespace output
