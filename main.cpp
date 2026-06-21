// ===== RP2040 USB Host HID → CH9350L 风格二进制协议桥 =====
// 阶段控制宏：
//   ENABLE_USB=0:     UART 二进制协议已就绪，USB Host 栈暂不启动（调试优先）
//   ENABLE_USB=1:     完整启用 USB Host HID 键盘解析 + 二进制协议输出
//   ENABLE_DEBUG_TEXT=0（默认）: UART0 输出纯二进制帧，不输出文本日志
//   ENABLE_DEBUG_TEXT=1:         同时在 UART0 输出文本日志（用于 bring-up 调试）
//
// 分层架构（严格遵守）：
//   /usb_host   - TinyUSB host stack + HID 回调  → 仅产出 key_event struct
//   /hid        - HID boot protocol parser        → 仅产出 key_event struct
//   /output     - uart_protocol (二进制帧编码)     ← 唯一负责二进制编码 & UART 输出
//               - uart_logger (文本日志)           ← 保留，默认关闭
//   main.cpp    - 组装层：初始化 + 回调路由 + 主循环

// ENABLE_USB 默认值 = 0（可由 CMake target_compile_definitions 覆盖）
#ifndef ENABLE_USB
#define ENABLE_USB 0
#endif

// ENABLE_DEBUG_TEXT 默认值 = 0（纯二进制模式；设为 1 可启用文本日志）
#ifndef ENABLE_DEBUG_TEXT
#define ENABLE_DEBUG_TEXT 0
#endif

#include "pico/stdlib.h"

#include "usb_host/usb_callbacks.h"
#include "output/uart_protocol.h"
#if ENABLE_USB
#include "tusb.h"
#include "hid/hid_parser.h"
#endif

#if ENABLE_DEBUG_TEXT
#include "output/uart_logger.h"
#include <cstdio>
#endif

// ============================================================================
// key_event 路由：唯一正确路径是 output::uart_send_key_event(e)
// —— 此处不调用任何 printf / 文本日志输出 key 信息
// ============================================================================
static void onKeyEvent(const usb_host::key_event& e) {
    // 严格遵守：HID 层产生 struct → 二进制协议层编码并输出
    output::uart_send_key_event(e);

    // —— 注意：禁止在此处 printf 任何 key info ——
    // 若需要同时输出文本日志，请在 ENABLE_DEBUG_TEXT 下通过独立通道实现，
    // 且不能影响二进制帧的原子性输出。
}

#if ENABLE_USB
// 设备插拔事件：发送二进制帧通知（不依赖调试文本）
static void onMount(uint8_t dev_addr, bool mounted) {
    if (mounted) {
        output::uart_send_device_mount(dev_addr);
    } else {
        output::uart_send_device_umount(dev_addr);
    }
}
#endif  // ENABLE_USB

// ============================================================================
// main()
// ============================================================================
int main() {
    // ===== 0) 板载 LED 初始化（Pico 默认 LED = GPIO25）=====
    // 启动后立即点亮，提示固件已运行
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, 1);

#if ENABLE_DEBUG_TEXT
    // —— 调试模式：先初始化 stdio（默认 115200），再由 uart_protocol_init 覆盖为 9600 ——
    // 注意：stdio_init_all() 会调用 uart_init(uart0, PICO_DEFAULT_UART_BAUD_RATE=115200)
    //       必须在 uart_protocol_init() 之前调用，否则会被覆盖
    stdio_init_all();
    sleep_ms(300);
#endif

    // ===== 1) 初始化 UART0 二进制协议层（必须先于任何输出）=====
    // - 9600 / 8N1
    // - TX = GPIO0, RX = GPIO1
    // - 这是本项目唯一的"关键数据通道"
    // - 注意：此调用会 uart_init(uart0, 9600)，覆盖 stdio_init_all 的 115200
    output::uart_protocol_init();

#if ENABLE_DEBUG_TEXT
    std::printf("\n========================================\n");
    std::printf("  Pi Tuuzkb USB - CH9350L Bridge\n");
    std::printf("  ENABLE_USB = %d, ENABLE_DEBUG_TEXT = %d\n",
                ENABLE_USB, ENABLE_DEBUG_TEXT);
    std::printf("========================================\n");
    std::printf("BOOT OK\n");
#else
    // —— 发布模式：sleep 少许时间以避免上电瞬间抖动 ——
    sleep_ms(50);
#endif

    // ===== 2) 注册 key_event 回调（二进制协议路由）=====
    // 无论 ENABLE_USB 是否为 1，都保持注册逻辑一致 ——
    // ENABLE_USB=0 时这些是空桩，调用无副作用
    usb_host::registerKeyEventCallback(onKeyEvent);
#if ENABLE_USB
    usb_host::registerMountCallback(onMount);
#endif

    // ===== 3) USB Host 初始化（仅 ENABLE_USB=1 时启用）=====
#if ENABLE_USB
    if (!tusb_init()) {
#if ENABLE_DEBUG_TEXT
        std::printf("[MAIN] ERROR: tusb_init() failed\n");
#endif
        while (true) {
            sleep_ms(1000);
        }
    }
#if ENABLE_DEBUG_TEXT
    std::printf("[MAIN] USB Host ready, waiting for keyboard...\n");
#endif
#else
#if ENABLE_DEBUG_TEXT
    std::printf("[MAIN] USB Host disabled (ENABLE_USB=0). "
                "UART binary protocol ready.\n");
#endif
#endif

    // ===== 4) 主循环 =====
    uint32_t last_tick_ms = to_ms_since_boot(get_absolute_time());
    uint32_t last_ping_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
#if ENABLE_USB
        // TinyUSB host 事件轮询 —— 此调用会触发 tuh_hid_* 回调，
        // 回调会产生 key_event，进而路由到 output::uart_send_key_event(e)
        tuh_task();
#endif

        // ===== UART RX 轮询（PING/PONG 处理）=====
        // 非阻塞：每次调用最多读取 1 字节，逐步推进状态机
        output::uart_poll_rx();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ===== 5 秒心跳 PING =====
        if (now - last_ping_ms >= 5000) {
            last_ping_ms = now;
            output::uart_send_ping();
        }

        // —— 心跳：每秒一次，仅调试模式输出文本（确认主循环未卡死）——
#if ENABLE_DEBUG_TEXT
        if (now - last_tick_ms >= 1000) {
            last_tick_ms = now;
            std::printf("LOOP TICK\n");
        }
        (void)last_tick_ms;
#else
        // 无文本输出，适度 yield
        sleep_ms(1);
#endif
    }

    return 0;
}
