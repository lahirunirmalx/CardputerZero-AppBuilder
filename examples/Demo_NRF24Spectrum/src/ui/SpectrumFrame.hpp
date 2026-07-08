// SpectrumFrame - one 2.4 GHz sweep from the nRF24 scanner.
//
// This is the single data structure shared by every frame source
// (NrfScanner over SPI, DemoSource synthetic data) and consumed by the
// UI views. It is the exact information the original Arduino firmware
// streamed as the CSV line "DATA,<peak>,c0..c63" - just kept in-process.
#ifndef DEMO_NRF24_SPECTRUM_FRAME_HPP
#define DEMO_NRF24_SPECTRUM_FRAME_HPP

#include <array>
#include <cstdint>

namespace nrf24 {

// Number of channel bins swept per frame. Matches CHANNELS in the firmware.
constexpr int kChannels = 64;

struct SpectrumFrame {
    // Per-channel RPD hit count, 0..SCAN_PASSES.
    std::array<uint8_t, kChannels> bins{};
    // Peak hit count across all bins (firmware's "norm" / <peak> field).
    uint8_t peak = 0;

    void clear() {
        bins.fill(0);
        peak = 0;
    }
};

}  // namespace nrf24

#endif  // DEMO_NRF24_SPECTRUM_FRAME_HPP
