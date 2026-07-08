#include "DemoSource.hpp"

#include <cmath>

namespace nrf24 {

// xorshift32 - tiny, deterministic, no <random> heap churn.
uint32_t DemoSource::rand_u32() {
    uint32_t x = rng_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_ = x;
    return x;
}

double DemoSource::rand01() { return (rand_u32() >> 8) / 16777216.0; }

void DemoSource::next(SpectrumFrame& frame, double dt_ms) {
    t_ms_ += dt_ms;
    const double t = t_ms_ / 1000.0;
    frame.clear();

    struct Hump {
        double center_bin;  // position in 0..kChannels-1
        double width;       // in bins
        double base;
        double wobble_hz;
    };

    // Wi-Fi: 64 bins map 2400..2528 MHz (2 MHz/bin). Centres 2412/2437/2462
    // -> bins ~6/18.5/31. Sub-GHz: a couple of narrow bursty carriers.
    const Hump wifi[] = {
        {6.0, 5.5, 0.75, 0.13},
        {18.5, 5.5, 0.90, 0.19},
        {31.0, 5.5, 0.60, 0.11},
    };
    const Hump subghz[] = {
        {20.0, 1.6, 0.85, 0.7},
        {44.0, 1.2, 0.55, 1.3},
    };

    const Hump* humps = band_ == Band::Wifi24 ? wifi : subghz;
    const int nh = band_ == Band::Wifi24 ? 3 : 2;
    const double floor = band_ == Band::Wifi24 ? 0.05 : 0.02;

    uint8_t peak = 0;
    for (int i = 0; i < kChannels; i++) {
        double level = floor + 0.05 * rand01();  // noise floor
        for (int h = 0; h < nh; h++) {
            const Hump& hp = humps[h];
            const double amp =
                hp.base * (0.7 + 0.3 * std::sin(t * hp.wobble_hz * 6.2832));
            const double d = (i - hp.center_bin) / hp.width;
            level += amp * std::exp(-d * d);
        }
        if (level > 1.0) level = 1.0;
        uint8_t v = static_cast<uint8_t>(level * scan_passes_ + 0.5);
        frame.bins[i] = v;
        if (v > peak) peak = v;
    }
    frame.peak = peak;
}

}  // namespace nrf24
