// NrfScanner - direct port of the Arduino nRF24 spectrum firmware to the
// CardputerZero's Linux SPI/GPIO. Same registers, same two-stage adaptive
// sweep, same per-channel RPD accumulation - the only thing that changes is
// the transport (spidev + GPIO chardev instead of AVR SPI + PORTB).
//
// One call to scan_frame() performs a full sweep and fills a SpectrumFrame,
// exactly the data the firmware used to print as "DATA,<peak>,c0..c63".
#ifndef DEMO_NRF24_SCANNER_HPP
#define DEMO_NRF24_SCANNER_HPP

#include <cstdint>
#include <string>

#include "../ui/SpectrumFrame.hpp"

namespace nrf24 {

// SPI byte transport abstraction. The real backend is SpidevBus; tests use a
// scripted fake to verify the accumulation math without hardware.
class SpiBus {
public:
    virtual ~SpiBus() = default;
    // Full-duplex: send `tx`, receive into `rx` (len bytes, may alias).
    virtual bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) = 0;
};

// CE line abstraction (real backend is the Gpio chardev; tests use a no-op).
class CeLine {
public:
    virtual ~CeLine() = default;
    virtual bool set(bool high) = 0;
};

// Tunables mirror the firmware #defines. Defaults match main.cpp/main.h.
struct ScanConfig {
    int channels = kChannels;      // CHANNELS (64)
    int scan_passes = 32;          // SCAN_PASSES
    int probe_passes = 4;          // PROBE_PASSES (min(SCAN_PASSES, 4))
    unsigned rx_settle_us = 130;   // RX_SETTLE_US
    unsigned ch_switch_us = 80;    // CH_SWITCH_SETTLE_US
    bool sleep_between = true;      // false in --selftest (no real timing)
};

class NrfScanner {
public:
    NrfScanner(SpiBus& spi, CeLine& ce, ScanConfig cfg = {})
        : spi_(spi), ce_(ce), cfg_(cfg) {
        if (cfg_.probe_passes > cfg_.scan_passes)
            cfg_.probe_passes = cfg_.scan_passes;
    }

    // One-time radio bring-up (EN_AA off, RF_SETUP for max gain, power up).
    // Returns false if any SPI transfer fails.
    bool begin();

    // Perform a full adaptive sweep and fill `frame`. Returns false on a
    // transport error (frame left cleared).
    bool scan_frame(SpectrumFrame& frame);

    const ScanConfig& config() const { return cfg_; }

private:
    // Register access - byte-for-byte the firmware's getRegister/setRegister.
    uint8_t get_register(uint8_t r);
    void set_register(uint8_t r, uint8_t v);
    void sample_channel(int i);
    void dwell(unsigned micros);

    SpiBus& spi_;
    CeLine& ce_;
    ScanConfig cfg_;
    uint8_t channel_[kChannels] = {0};
    bool io_ok_ = true;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_SCANNER_HPP
