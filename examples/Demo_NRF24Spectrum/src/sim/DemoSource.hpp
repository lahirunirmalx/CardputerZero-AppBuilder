// DemoSource - synthetic spectrum frames for when no radio is wired.
//
// Ports the WiFi-Spectrum-Analyzer's demo mode: a few drifting Wi-Fi-like
// humps (channels 1/6/11) plus broadband noise, so the UI can be exercised
// in the czdev emulator or on a device without the nRF24 module attached.
#ifndef DEMO_NRF24_DEMO_SOURCE_HPP
#define DEMO_NRF24_DEMO_SOURCE_HPP

#include <cstdint>

#include "../ui/SpectrumFrame.hpp"

namespace nrf24 {

class DemoSource {
public:
    // Which radio's spectrum to emulate. Wifi24 = three Wi-Fi humps; SubGhz =
    // a couple of narrow bursty carriers on a lower noise floor.
    enum class Band { Wifi24, SubGhz };

    explicit DemoSource(uint32_t seed = 0x1234abcdu, int scan_passes = 32)
        : rng_(seed), scan_passes_(scan_passes) {}

    void set_band(Band b) { band_ = b; }

    // Advance the animation by `dt_ms` and fill a fresh frame.
    void next(SpectrumFrame& frame, double dt_ms);

private:
    uint32_t rand_u32();
    double rand01();

    uint32_t rng_;
    int scan_passes_;
    Band band_ = Band::Wifi24;
    double t_ms_ = 0.0;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_DEMO_SOURCE_HPP
