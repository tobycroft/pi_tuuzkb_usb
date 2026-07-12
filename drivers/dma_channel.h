#ifndef DRIVERS_DMA_CHANNEL_H
#define DRIVERS_DMA_CHANNEL_H

#include <cstdint>
#include <cstddef>
#include "hardware/dma.h"

namespace drivers {

class DmaChannel {
public:
    DmaChannel() = default;
    ~DmaChannel();

    DmaChannel(const DmaChannel&) = delete;
    DmaChannel& operator=(const DmaChannel&) = delete;

    DmaChannel(DmaChannel&& other) noexcept;
    DmaChannel& operator=(DmaChannel&& other) noexcept;

    bool claim(int channel = -1);
    void release();
    bool is_claimed() const { return claimed_; }

    int channel_num() const { return channel_; }

    void configure(const dma_channel_config& config,
                   volatile void* write_addr,
                   const void* read_addr,
                   std::uint32_t transfer_count);

    void start_transfer(bool trigger = true);

    bool is_busy() const;
    void wait_for_finish() const;

    dma_channel_config get_default_config() const;

private:
    int channel_ = -1;
    bool claimed_ = false;
};

} // namespace drivers

#endif // DRIVERS_DMA_CHANNEL_H