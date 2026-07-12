#include "dma_channel.h"

#include "hardware/dma.h"
#include "hardware/irq.h"

namespace drivers {

DmaChannel::~DmaChannel() {
    release();
}

DmaChannel::DmaChannel(DmaChannel&& other) noexcept
    : channel_(other.channel_)
    , claimed_(other.claimed_) {
    other.channel_ = -1;
    other.claimed_ = false;
}

DmaChannel& DmaChannel::operator=(DmaChannel&& other) noexcept {
    if (this != &other) {
        release();
        channel_ = other.channel_;
        claimed_ = other.claimed_;
        other.channel_ = -1;
        other.claimed_ = false;
    }
    return *this;
}

bool DmaChannel::claim(int channel) {
    if (claimed_) return true;

    if (channel >= 0) {
        channel_ = dma_claim_unused_channel(false);
        if (channel_ != channel) {
            if (channel_ >= 0) {
                dma_channel_unclaim(channel_);
            }
            return false;
        }
    } else {
        channel_ = dma_claim_unused_channel(true);
        if (channel_ < 0) {
            return false;
        }
    }

    claimed_ = true;
    return true;
}

void DmaChannel::release() {
    if (claimed_ && channel_ >= 0) {
        dma_channel_abort(channel_);
        dma_channel_unclaim(channel_);
        channel_ = -1;
        claimed_ = false;
    }
}

void DmaChannel::configure(const dma_channel_config& config,
                            volatile void* write_addr,
                            const void* read_addr,
                            std::uint32_t transfer_count) {
    if (!claimed_) return;

    dma_channel_configure(
        channel_,
        &config,
        write_addr,
        read_addr,
        transfer_count,
        false
    );
}

void DmaChannel::start_transfer(bool trigger) {
    if (!claimed_) return;

    if (trigger) {
        dma_start_channel_mask(1u << channel_);
    }
}

bool DmaChannel::is_busy() const {
    if (!claimed_) return false;
    return dma_channel_is_busy(channel_);
}

void DmaChannel::wait_for_finish() const {
    if (!claimed_) return;
    dma_channel_wait_for_finish_blocking(channel_);
}

dma_channel_config DmaChannel::get_default_config() const {
    if (!claimed_) {
        return dma_channel_get_default_config(0);
    }
    return dma_channel_get_default_config(channel_);
}

} // namespace drivers