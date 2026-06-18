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
constexpr uint8_t  kUartTXPin   = 0;
constexpr uint8_t  kUartRXPin   = 1;
constexpr uint32_t kUartBaudRate = 115200;

bool g_initialized = false;

// 静态帧缓冲区（8 字节），避免堆分配
// 帧: 0x57 0xAB LEN TYPE DATA0 DATA1 DATA2 CHECKSUM
uint8_t g_frame_buf[kKeyboardFrameLen];

} // namespace

void uart_protocol_init() {
    if (g_initialized) return;

    // 初始化 UART0: 115200/8N1
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
    g_frame_buf[2] = static_cast<uint8_t>(kKeyboardFrameLen);  // LEN = total length = 8
    g_frame_buf[3] = kTypeKeyboardEvent;                         // TYPE = 0x01

    g_initialized = true;
}

void uart_send_key_event(const usb_host::key_event& e) {
    if (!g_initialized) return;

    // ===== DATA 字段填充 =====
    // byte0: usage code
    // byte1: pressed (0x01 = press, 0x00 = release)
    // byte2: modifiers
    g_frame_buf[4] = e.usage_code;
    g_frame_buf[5] = e.pressed ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00);
    g_frame_buf[6] = e.modifiers;

    // ===== CHECKSUM 计算：XOR 所有前面字节 =====
    // 包括 [0x57, 0xAB, LEN, TYPE, DATA0, DATA1, DATA2]
    uint8_t xor_sum = 0;
    for (size_t i = 0; i < kKeyboardFrameLen - 1; i++) {
        xor_sum ^= g_frame_buf[i];
    }
    g_frame_buf[7] = xor_sum;

    // ===== 原子输出 =====
    // 单次 uart_write_blocking 调用 —— 保证 8 字节连续输出，不会被其他代码干扰
    // 注：uart_write_blocking 内部会等待 FIFO 空间；
    //     115200 baud 下 8 字节 ≈ 694 µs，可忽略阻塞时间
    uart_write_blocking(uart0, g_frame_buf, kKeyboardFrameLen);
}

bool uart_protocol_is_initialized() {
    return g_initialized;
}

} // namespace output
