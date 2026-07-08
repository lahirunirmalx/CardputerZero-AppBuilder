#include "NrfScanner.hpp"

#include <time.h>

namespace nrf24 {

namespace {
// nRF24L01 register addresses (identical to the firmware's _NRF24_* defines).
constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_EN_AA = 0x01;
constexpr uint8_t REG_RF_SETUP = 0x06;
constexpr uint8_t REG_RF_CH = 0x05;
constexpr uint8_t REG_RPD = 0x09;

constexpr uint8_t CFG_PWR_UP = 0x02;
constexpr uint8_t CFG_PRIM_RX = 0x01;

constexpr uint8_t CMD_WRITE = 0x20;  // OR'd with reg to write (firmware: | 0x20)
}  // namespace

void NrfScanner::dwell(unsigned micros) {
    if (!cfg_.sleep_between) return;  // selftest / no-timing mode
    struct timespec ts;
    ts.tv_sec = micros / 1000000u;
    ts.tv_nsec = static_cast<long>(micros % 1000000u) * 1000L;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
}

uint8_t NrfScanner::get_register(uint8_t r) {
    // Firmware: transfer(r & 0x1F), then read one byte.
    uint8_t tx[2] = {static_cast<uint8_t>(r & 0x1F), 0x00};
    uint8_t rx[2] = {0, 0};
    if (!spi_.transfer(tx, rx, 2)) io_ok_ = false;
    return rx[1];
}

void NrfScanner::set_register(uint8_t r, uint8_t v) {
    // Firmware: transfer((r & 0x1F) | 0x20), then transfer(v).
    uint8_t tx[2] = {static_cast<uint8_t>((r & 0x1F) | CMD_WRITE), v};
    uint8_t rx[2] = {0, 0};
    if (!spi_.transfer(tx, rx, 2)) io_ok_ = false;
}

bool NrfScanner::begin() {
    io_ok_ = true;
    // powerUp(): CONFIG |= PWR_UP, then settle.
    set_register(REG_CONFIG,
                 static_cast<uint8_t>(get_register(REG_CONFIG) | CFG_PWR_UP));
    dwell(cfg_.rx_settle_us);
    // Disable auto-ack and set RF_SETUP to 0x0F (max gain, as in firmware).
    set_register(REG_EN_AA, 0x00);
    set_register(REG_RF_SETUP, 0x0F);
    for (int i = 0; i < cfg_.channels; i++) channel_[i] = 0;
    return io_ok_;
}

void NrfScanner::sample_channel(int i) {
    // 128 / CHANNELS; with 64 channels this is the firmware's i * 2.
    const uint8_t step = static_cast<uint8_t>(128 / cfg_.channels);
    set_register(REG_RF_CH, static_cast<uint8_t>(i * step));
    dwell(cfg_.ch_switch_us);
    if (get_register(REG_RPD) > 0) {
        channel_[i]++;
    }
}

bool NrfScanner::scan_frame(SpectrumFrame& frame) {
    io_ok_ = true;
    for (int i = 0; i < cfg_.channels; i++) channel_[i] = 0;

    // Enter RX once for the whole sweep (CONFIG = PWR_UP | PRIM_RX, CE high).
    set_register(REG_CONFIG, CFG_PWR_UP | CFG_PRIM_RX);
    ce_.set(true);
    dwell(cfg_.rx_settle_us);

    // Stage 1 - uniform probe across every channel.
    for (int pass = 0; pass < cfg_.probe_passes; pass++) {
        for (int i = 0; i < cfg_.channels; i++) sample_channel(i);
    }

    // Stage 2 - deep sample only on channels the probe lit up.
    for (int pass = cfg_.probe_passes; pass < cfg_.scan_passes; pass++) {
        for (int i = 0; i < cfg_.channels; i++) {
            if (channel_[i] == 0) continue;
            sample_channel(i);
        }
    }

    ce_.set(false);

    // Build the frame (firmware's outputChannels: norm = max, then bins).
    frame.clear();
    uint8_t norm = 0;
    for (int i = 0; i < cfg_.channels; i++) {
        frame.bins[i] = channel_[i];
        if (channel_[i] > norm) norm = channel_[i];
    }
    frame.peak = norm;

    return io_ok_;
}

}  // namespace nrf24
