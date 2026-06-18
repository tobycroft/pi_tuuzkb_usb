#include "src/UsbApplication.h"
#include "pico/stdlib.h"
#include <cstdio>

int main() {
    // 初始化 stdio（UART + USB CDC）
    stdio_init_all();

    // 短暂延时，让串口监控工具就绪
    sleep_ms(500);

    std::printf("\n========================================\n");
    std::printf("  Pi Tuuzkb USB - TinyUSB Demo v0.2\n");
    std::printf("========================================\n\n");

    // 创建应用实例
    // RAII 自动管理所有硬件资源生命周期
    app::UsbApplication app;

    // 初始化所有子系统
    if (!app.initialize()) {
        std::printf("[MAIN] ERROR: Application initialization failed\n");

        // 错误指示：快速闪烁 LED
        while (true) {
            gpio_put(PICO_DEFAULT_LED_PIN, 1);
            sleep_ms(100);
            gpio_put(PICO_DEFAULT_LED_PIN, 0);
            sleep_ms(100);
        }
    }

    std::printf("[MAIN] System ready, starting main loop...\n\n");

    // 进入主应用循环
    app.runLoop();

    // 理论上不会到达此处
    return 0;
}
