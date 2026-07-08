// WaterfallView - scrolling spectrogram, ported from the analyzer's
// WaterfallView. Each incoming frame becomes one new row at the top; older
// rows scroll down. Colour follows the theme signal ramp.
#ifndef DEMO_NRF24_WATERFALL_VIEW_HPP
#define DEMO_NRF24_WATERFALL_VIEW_HPP

#include <SDL2/SDL.h>

#include <vector>

#include "SpectrumFrame.hpp"

namespace nrf24 {

class WaterfallView {
public:
    WaterfallView(SDL_Rect rect, int scan_passes)
        : rect_(rect), scan_passes_(scan_passes) {}
    ~WaterfallView();

    // Lazily builds the streaming texture. Safe to call every frame.
    void ensure_texture(SDL_Renderer* r);
    void update(const SpectrumFrame& frame);
    void render(SDL_Renderer* r);

private:
    SDL_Rect rect_;
    int scan_passes_;
    SDL_Texture* tex_ = nullptr;
    int tex_w_ = 0, tex_h_ = 0;
    // Ring buffer of rows, each kChannels ARGB pixels; newest at head_.
    std::vector<uint32_t> pixels_;  // tex_h_ * kChannels
    int head_ = 0;
    int filled_ = 0;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_WATERFALL_VIEW_HPP
