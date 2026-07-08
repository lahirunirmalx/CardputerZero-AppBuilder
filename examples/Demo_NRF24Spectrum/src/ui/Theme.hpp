// Theme - the single source of truth for every colour used by the UI.
//
// Per project rule, view code never hardcodes colours; it names them here.
// Palette echoes the original WiFi-Spectrum-Analyzer's phosphor-scope look:
// a green CRT trace on near-black, amber for warnings, red for peaks.
#ifndef DEMO_NRF24_THEME_HPP
#define DEMO_NRF24_THEME_HPP

#include <cstdint>

namespace nrf24 {

struct Rgb {
    uint8_t r, g, b;
};

namespace theme {

// Background / chrome.
constexpr Rgb kBackground = {6, 10, 8};
constexpr Rgb kGrid = {18, 34, 26};
constexpr Rgb kAxis = {40, 70, 52};
constexpr Rgb kText = {150, 210, 170};
constexpr Rgb kTextDim = {80, 120, 95};

// Spectrum trace (green phosphor) and its bright leading edge.
constexpr Rgb kTrace = {40, 230, 120};
constexpr Rgb kTraceGlow = {120, 255, 180};
constexpr Rgb kEnvelope = {30, 120, 70};  // min/max hold envelope

// Signal-strength ramp for bars / waterfall (low -> high).
constexpr Rgb kLevelLow = {20, 60, 40};
constexpr Rgb kLevelMid = {230, 200, 40};   // amber
constexpr Rgb kLevelHigh = {235, 70, 55};   // red peak

// Status banner (demo / no-radio).
constexpr Rgb kBannerBg = {60, 40, 10};
constexpr Rgb kBannerText = {240, 200, 90};

// Wi-Fi channel overlay markers.
constexpr Rgb kWifiMark = {60, 90, 130};

}  // namespace theme
}  // namespace nrf24

#endif  // DEMO_NRF24_THEME_HPP
