#ifndef DRIVERS_UART_DMA_H
#define DRIVERS_UART_DMA_H

#include <cstdint>
#include <cstddef>
#include "hardware/uart.h"
#include "dma_channel.h"

namespace drivers {

class UartDmaTx {
public:
    UartDmaTx() = default;
    ~UartDmaTx() = default;

    UartDmaTx(const UartDmaTx&) = delete;
    UartDmaTx& operator=(const UartDmaTx&) = delete;

    UartDmaTx(UartDmaTx&&) = default;
    UartDmaTx& operator=(UartDmaTx&&) = default;

    bool init(uart_inst_t* uart);
    bool is_initialized() const { return initialized_; }

    void send(const std::uint8_t* data, std::size_t len);
    void send_blocking(const std::uint8_t* data, std::size_t len);

    bool is_busy() const;
    void wait_for_completion() const;

private:
    uart_inst_t* uart_ = nullptr;
    DmaChannel dma_channel_;
    bool initialized_ = false;
};

} // namespace drivers

#endif // DRIVERS_UART_DMA_H