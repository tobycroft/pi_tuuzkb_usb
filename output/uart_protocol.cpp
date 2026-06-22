#include "uart_protocol.h"

// ===== UART 二进制协议实现 —— CH9350L 风格 =====
//
// 关键实现细节：
//  1. 帧组装在静态局部缓冲区，避免 malloc 开销
//  2. uart_write_blocking 一次输出整个帧 —— 原子性输出，避免与其他 UART 字符交错
//  3. checksum = XOR 所有字节（从 header[0] 到最后一个 data 字节）
//  4. 不通过 printf 打印 key 信息（违反协议层的"纯二进制"约定）
//  5. bool pressed 被规范化为 0x01 / 0x00，避免非零 true 值（如 0xFF）污染协议

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

namespace output {

namespace {

// —— UART0 硬件参数（实现细节，不对外暴露）——
// 与 output/uart_logger.h 中定义的常量一致，二者共享同一硬件 UART0
constexpr std::uint8_t  kUartTXPin    = 0;
constexpr std::uint8_t  kUartRXPin    = 1;
constexpr std::uint32_t kUartBaudRate = 9600;

bool g_initialized = false;

// 静态帧缓冲区（8 字节），避免堆分配
// 帧: 0x57 0xAB LEN TYPE DATA0 DATA1 DATA2 CHECKSUM
std::uint8_t g_frame_buf[kKeyboardFrameLen];

// PONG 帧缓冲区（6 字节）
std::uint8_t g_pong_buf[kPingPongFrameLen];

// ===== RX 状态机 =====
enum RxState {
    RX_WAIT_HDR1,    // 等待 0x57
    RX_WAIT_HDR2,    // 等待 0xAB
    RX_WAIT_LEN,     // 等待 LEN
    RX_WAIT_TYPE,    // 等待 TYPE
    RX_WAIT_DATA,    // 等待 DATA 字节
    RX_WAIT_CHECKSUM // 等待 CHECKSUM
};

RxState g_rx_state = RX_WAIT_HDR1;
std::uint8_t g_rx_len = 0;        // 收到的 LEN 字节
std::uint8_t g_rx_type = 0;       // 收到的 TYPE 字节
std::uint8_t g_rx_data[16];       // DATA 缓冲区（最大支持 16 字节）
std::uint8_t g_rx_data_idx = 0;   // 当前已收到的 DATA 字节数
std::uint8_t g_rx_checksum = 0;   // 收到的 CHECKSUM

} // namespace

void uart_protocol_init() {
    if (g_initialized) return;

    // 初始化 UART0: 9600/8N1
    uart_init(uart0, kUartBaudRate);

    // 配置 GPIO 引脚功能
    gpio_set_function(kUartTXPin, GPIO_FUNC_UART);
    gpio_set_function(kUartRXPin, GPIO_FUNC_UART);

    // 显式设置 8N1（虽然是默认值，保证与协议规范一致）
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);

    // 开启 TX FIFO，吸收短突发帧
    uart_set_fifo_enabled(uart0, true);

    // 预填充帧的静态部分（header + LEN + TYPE），运行时只需填写 DATA + CHECKSUM
    g_frame_buf[0] = kFrameHdr1;
    g_frame_buf[1] = kFrameHdr2;
    g_frame_buf[2] = static_cast<std::uint8_t>(kKeyboardFrameLen);  // LEN = total length = 8
    g_frame_buf[3] = kTypeKeyboardEvent;                              // TYPE = 0x01

    g_initialized = true;
}

void uart_send_key_event(const usb_host::key_event& e) {
    if (!g_initialized) return;

    // ===== DATA 字段填充 =====
    // byte0: usage code
    // byte1: pressed (0x01 = press, 0x00 = release)
    // byte2: modifiers
    g_frame_buf[4] = e.usage_code;
    g_frame_buf[5] = e.pressed ? static_cast<std::uint8_t>(0x01) : static_cast<std::uint8_t>(0x00);
    g_frame_buf[6] = e.modifiers;

    // ===== CHECKSUM 计算：XOR 所有前面字节 =====
    // 包括 [0x57, 0xAB, LEN, TYPE, DATA0, DATA1, DATA2]
    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kKeyboardFrameLen - 1; i++) {
        xor_sum ^= g_frame_buf[i];
    }
    g_frame_buf[7] = xor_sum;

    // ===== 原子输出 =====
    // 单次 uart_write_blocking 调用 —— 保证 8 字节连续输出，不会被其他代码干扰
    // 注：uart_write_blocking 内部会等待 FIFO 空间；
    //     9600 baud 下 8 字节 ≈ 8.3 ms
    uart_write_blocking(uart0, g_frame_buf, kKeyboardFrameLen);
}

void uart_send_pong(std::uint8_t payload) {
    if (!g_initialized) return;

    // 预填充帧头
    g_pong_buf[0] = kFrameHdr1;                                       // 0x57
    g_pong_buf[1] = kFrameHdr2;                                       // 0xAB
    g_pong_buf[2] = static_cast<std::uint8_t>(kPingPongFrameLen);     // LEN = 6
    g_pong_buf[3] = kTypePong;                                        // TYPE = 0x03
    g_pong_buf[4] = payload;                                          // DATA = payload

    // CHECKSUM: XOR 所有前面字节
    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kPingPongFrameLen - 1; i++) {
        xor_sum ^= g_pong_buf[i];
    }
    g_pong_buf[5] = xor_sum;

    // 原子输出
    uart_write_blocking(uart0, g_pong_buf, kPingPongFrameLen);
}

void uart_send_ping() {
    if (!g_initialized) return;

    // 4 字节原始帧：57 AB 10 03
    // 无 LEN / TYPE / DATA / CHECKSUM，直接发送
    const std::uint8_t raw_ping[] = {0x57, 0xAB, 0x10, 0x03};
    uart_write_blocking(uart0, raw_ping, sizeof(raw_ping));
}

void uart_send_device_mount(std::uint8_t dev_addr) {
    if (!g_initialized) return;

    std::uint8_t buf[kDeviceEventFrameLen];
    buf[0] = kFrameHdr1;                                              // 0x57
    buf[1] = kFrameHdr2;                                              // 0xAB
    buf[2] = static_cast<std::uint8_t>(kDeviceEventFrameLen);         // LEN = 6
    buf[3] = kTypeDeviceMount;                                        // TYPE = 0x04
    buf[4] = dev_addr;                                                // DATA = dev_addr

    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kDeviceEventFrameLen - 1; i++) {
        xor_sum ^= buf[i];
    }
    buf[5] = xor_sum;

    uart_write_blocking(uart0, buf, kDeviceEventFrameLen);
}

void uart_send_device_umount(std::uint8_t dev_addr) {
    if (!g_initialized) return;

    std::uint8_t buf[kDeviceEventFrameLen];
    buf[0] = kFrameHdr1;                                              // 0x57
    buf[1] = kFrameHdr2;                                              // 0xAB
    buf[2] = static_cast<std::uint8_t>(kDeviceEventFrameLen);         // LEN = 6
    buf[3] = kTypeDeviceUmount;                                       // TYPE = 0x05
    buf[4] = dev_addr;                                                // DATA = dev_addr

    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kDeviceEventFrameLen - 1; i++) {
        xor_sum ^= buf[i];
    }
    buf[5] = xor_sum;

    uart_write_blocking(uart0, buf, kDeviceEventFrameLen);
}

void uart_send_device_info(std::uint8_t dev_addr, uint16_t vid, uint16_t pid, uint8_t bInterval, uint8_t itf_num, uint8_t itf_protocol, uint8_t instance) {
    if (!g_initialized) return;

    std::uint8_t buf[kDeviceInfoFrameLen];
    buf[0] = kFrameHdr1;                                              // 0x57
    buf[1] = kFrameHdr2;                                              // 0xAB
    buf[2] = static_cast<std::uint8_t>(kDeviceInfoFrameLen);          // LEN = 0xB (11)
    buf[3] = kTypeDeviceInfo;                                         // TYPE = 0x06
    buf[4] = dev_addr;                                                // DATA: dev_addr
    buf[5] = static_cast<uint8_t>(vid & 0xFF);                        // DATA: vid_low
    buf[6] = static_cast<uint8_t>((vid >> 8) & 0xFF);                 // DATA: vid_high
    buf[7] = static_cast<uint8_t>(pid & 0xFF);                        // DATA: pid_low
    buf[8] = static_cast<uint8_t>((pid >> 8) & 0xFF);                 // DATA: pid_high
    buf[9] = bInterval;                                               // DATA: bInterval
    buf[10] = itf_num;                                                // DATA: itf_num
    buf[11] = itf_protocol;                                           // DATA: itf_protocol
    buf[12] = instance;                                               // DATA: instance

    std::uint8_t xor_sum = 0;
    for (std::size_t i = 0; i < kDeviceInfoFrameLen - 1; i++) {
        xor_sum ^= buf[i];
    }
    buf[13] = xor_sum;

    uart_write_blocking(uart0, buf, kDeviceInfoFrameLen);
}

void uart_send_frame(const std::uint8_t* data, std::size_t len) {
    if (!g_initialized || data == nullptr || len == 0) return;
    uart_write_blocking(uart0, data, len);
}

void uart_poll_rx() {
    if (!g_initialized) return;

    // 检查 RX FIFO 是否有数据
    if (!uart_is_readable(uart0)) return;

    // 读取 1 字节
    std::uint8_t byte = uart_getc(uart0);

    switch (g_rx_state) {
    case RX_WAIT_HDR1:
        if (byte == kFrameHdr1) {
            g_rx_state = RX_WAIT_HDR2;
        }
        break;

    case RX_WAIT_HDR2:
        if (byte == kFrameHdr2) {
            g_rx_state = RX_WAIT_LEN;
        } else {
            // 不是 0xAB，重置
            g_rx_state = RX_WAIT_HDR1;
        }
        break;

    case RX_WAIT_LEN:
        g_rx_len = byte;
        // LEN 必须 >= 5 (header + len + type + checksum 最小 5 字节)
        // 且 DATA 长度 = LEN - 5，不能超过缓冲区
        if (g_rx_len < 5 || (g_rx_len - 5) > sizeof(g_rx_data)) {
            g_rx_state = RX_WAIT_HDR1;
        } else {
            g_rx_state = RX_WAIT_TYPE;
        }
        break;

    case RX_WAIT_TYPE: {
        g_rx_type = byte;
        g_rx_data_idx = 0;
        // 计算 DATA 字节数 = LEN - 5
        std::uint8_t data_len = g_rx_len - 5;
        if (data_len == 0) {
            // 无 DATA，直接等待 CHECKSUM
            g_rx_state = RX_WAIT_CHECKSUM;
        } else {
            g_rx_state = RX_WAIT_DATA;
        }
        break;
    }

    case RX_WAIT_DATA: {
        g_rx_data[g_rx_data_idx++] = byte;
        // 判断是否收完所有 DATA
        std::uint8_t expected_data_len = g_rx_len - 5;
        if (g_rx_data_idx >= expected_data_len) {
            g_rx_state = RX_WAIT_CHECKSUM;
        }
        break;
    }

    case RX_WAIT_CHECKSUM: {
        g_rx_checksum = byte;
        // 校验 XOR
        std::uint8_t xor_sum = kFrameHdr1 ^ kFrameHdr2 ^ g_rx_len ^ g_rx_type;
        for (std::uint8_t i = 0; i < g_rx_data_idx; i++) {
            xor_sum ^= g_rx_data[i];
        }

        if (xor_sum == g_rx_checksum) {
            // 校验通过，根据 TYPE 分派
            if (g_rx_type == kTypePing && g_rx_data_idx == 1) {
                // PING: 回传 PONG，payload = g_rx_data[0]
                uart_send_pong(g_rx_data[0]);
            }
            // 其他 TYPE 暂不处理（TYPE=0x01 是 TX 方向，不应出现在 RX）
        }
        // 无论校验成功或失败，都重置状态机
        g_rx_state = RX_WAIT_HDR1;
        g_rx_len = 0;
        g_rx_type = 0;
        g_rx_data_idx = 0;
        break;
    }
    }
}

bool uart_protocol_is_initialized() {
    return g_initialized;
}

} // namespace output
