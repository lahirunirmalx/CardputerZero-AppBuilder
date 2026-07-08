// Cc1101Scanner - sub-GHz (300-928 MHz) RSSI spectrum sweep for the CC1101
// radio that sits alongside the nRF24 on the MTools / Hydra-RF caps.
//
// The nRF24 gives us 2.4 GHz; the CC1101 covers the Sub-GHz ISM bands. This
// scanner tunes the CC1101 across a band, reads its RSSI status register at
// each step, and produces the same 64-bin SpectrumFrame the UI already knows
// how to draw - so a single "band" toggle switches the whole app between the
// two radios with no UI changes.
//
// Uses the shared SpiBus abstraction (its own chip-select). No IDLE<->RX
// thrash per channel: we stay in RX and let the AGC settle between hops.
#ifndef DEMO_NRF24_CC1101_SCANNER_HPP
#define DEMO_NRF24_CC1101_SCANNER_HPP

#include <cstdint>

#include "NrfScanner.hpp"  // for SpiBus
#include "../ui/SpectrumFrame.hpp"

namespace nrf24 {

// A sub-GHz band to sweep. base_mhz is the frequency of bin 0; each of the
// kChannels bins steps by step_khz. Defaults cover the 433 MHz ISM band.
struct Cc1101Band {
    double base_mhz = 433.0;
    double step_khz = 200.0;  // 64 bins * 200 kHz ~= 12.8 MHz span
    const char* label = "433MHz";
};

struct Cc1101Config {
    Cc1101Band band{};
    // RSSI (dBm) range mapped onto the 0..1 display scale.
    double floor_dbm = -110.0;
    double ceil_dbm = -20.0;
    unsigned settle_us = 500;  // AGC/RSSI settle after a frequency hop
    bool sleep_between = true;
};

class Cc1101Scanner {
public:
    Cc1101Scanner(SpiBus& spi, Cc1101Config cfg = {}) : spi_(spi), cfg_(cfg) {}

    // Reset + base configuration (GFSK, RX, RSSI usable). Returns false if a
    // transfer fails or the part number register does not look like a CC1101.
    bool begin();

    // Sweep the band and fill `frame` (bins scaled floor..ceil dBm -> 0..255
    // over SCAN_PASSES-equivalent range). Returns false on transport error.
    bool scan_frame(SpectrumFrame& frame);

    void set_band(const Cc1101Band& b) { cfg_.band = b; }
    const Cc1101Config& config() const { return cfg_; }
    bool present() const { return present_; }

private:
    uint8_t read_reg(uint8_t addr);       // status/config single read
    uint8_t read_status(uint8_t addr);    // status register (burst bit)
    void write_reg(uint8_t addr, uint8_t v);
    void strobe(uint8_t cmd);
    void set_frequency(double mhz);
    void dwell(unsigned micros);
    static double rssi_to_dbm(uint8_t raw);

    SpiBus& spi_;
    Cc1101Config cfg_;
    bool present_ = false;
    bool io_ok_ = true;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_CC1101_SCANNER_HPP
