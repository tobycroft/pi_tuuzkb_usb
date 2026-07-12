#include "uart_dma.h"

#include "hardware/dma.h"
#include "hardware/uart.h"
#include "hardware/structs/uart.h"

namespace drivers {

namespace {

constexpr int get_uart_tx_dreq(uart_inst_t* uart) {
    if (uart == uart0) {
        return DREQ_UART0_TX;
    } else {
        return DREQ_UART1_TX;
    }
}

} // namespace

bool UartDmaTx::init(uart_inst_t* uart) {
    if (initialized_ || uart == nullptr) return false;

    if (!dma_channel_.claim()) {
        return false;
    }

    uart_ = uart;
    initialized_ = true;
    return true;
}

void UartDmaTx::send(const std::uint8_t* data, std::size_t len) {
    if (!initialized_ || data == nullptr || len == 0) return;

    dma_channel_.wait_for_finish();

    auto config = dma_channel_.get_default_config();
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_read_increment(&config, true);
    channel_config_set_write_increment(&config, false);
    channel_config_set_dreq(&config, get_uart_tx_dreq(uart_));

    volatile void* uart_dr = &uart_get_hw(uart_)->dr;

    dma_channel_.configure(config, uart_dr, data, static_cast<std::uint32_t>(len));
    dma_channel_.start_transfer(true);
}

void UartDmaTx::send_blocking(const std::uint8_t* data, std::size_t len) {
    send(data, len);
    wait_for_completion();
}

bool UartDmaTx::is_busy() const {
    if (!initialized_) return false;
    return dma_channel_.is_busy();
}

void UartDmaTx::wait_for_completion() const {
    if (!initialized_) return;
    dma_channel_.wait_for_finish();
}

} // namespace drivers