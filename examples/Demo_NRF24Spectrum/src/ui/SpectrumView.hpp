// SpectrumView - phosphor-scope + digital-bar spectrum, ported from the
// WiFi-Spectrum-Analyzer's SpectrumView for the fixed 320x170 screen.
//
// Consumes a SpectrumFrame (64 bins) and draws either an analog-style trace
// with a decaying min/max envelope (phosphor mode) or per-channel bars
// coloured by the theme signal ramp (digital mode).
#ifndef DEMO_NRF24_SPECTRUM_VIEW_HPP
#define DEMO_NRF24_SPECTRUM_VIEW_HPP

#include <SDL2/SDL.h>

#include <array>

#include "SpectrumFrame.hpp"

namespace nrf24 {

class SpectrumView {
public:
    enum class Mode { Phosphor, Digital };

    // `rect` is the drawing area inside the window. `scan_passes` normalises
    // bin counts (0..scan_passes) to 0..1.
    SpectrumView(SDL_Rect rect, int scan_passes)
        : rect_(rect), scan_passes_(scan_passes) {}

    void set_mode(Mode m) { mode_ = m; }
    Mode mode() const { return mode_; }
    void toggle_mode() {
        mode_ = (mode_ == Mode::Phosphor) ? Mode::Digital : Mode::Phosphor;
    }
    void set_peak_hold(bool on) { peak_hold_ = on; }
    bool peak_hold() const { return peak_hold_; }

    // Feed a frame; updates the smoothed trace and peak-hold envelope.
    void update(const SpectrumFrame& frame);
    // Draw the current state.
    void render(SDL_Renderer* r) const;

private:
    double norm(uint8_t bin) const;
    int bin_x(int i) const;
    int level_y(double v) const;

    SDL_Rect rect_;
    int scan_passes_;
    Mode mode_ = Mode::Phosphor;
    bool peak_hold_ = false;

    std::array<double, kChannels> smooth_{};  // decaying display trace
    std::array<double, kChannels> hold_{};     // peak-hold envelope
    bool primed_ = false;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_SPECTRUM_VIEW_HPP
