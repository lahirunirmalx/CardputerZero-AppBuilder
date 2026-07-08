#include "WaterfallView.hpp"

#include <algorithm>

#include "Theme.hpp"

namespace nrf24 {

namespace {
Rgb ramp(double v) {
    v = v < 0 ? 0 : (v > 1 ? 1 : v);
    const Rgb& lo = theme::kLevelLow;
    const Rgb& mid = theme::kLevelMid;
    const Rgb& hi = theme::kLevelHigh;
    auto lerp = [](uint8_t a, uint8_t b, double t) {
        return static_cast<uint8_t>(a + (b - a) * t + 0.5);
    };
    if (v < 0.5) {
        double t = v / 0.5;
        return {lerp(lo.r, mid.r, t), lerp(lo.g, mid.g, t), lerp(lo.b, mid.b, t)};
    }
    double t = (v - 0.5) / 0.5;
    return {lerp(mid.r, hi.r, t), lerp(mid.g, hi.g, t), lerp(mid.b, hi.b, t)};
}

uint32_t argb(const Rgb& c) {
    return (0xFFu << 24) | (c.r << 16) | (c.g << 8) | c.b;
}
}  // namespace

WaterfallView::~WaterfallView() {
    if (tex_) SDL_DestroyTexture(tex_);
}

void WaterfallView::ensure_texture(SDL_Renderer* r) {
    if (tex_) return;
    tex_w_ = kChannels;
    tex_h_ = rect_.h > 0 ? rect_.h : 1;
    pixels_.assign(static_cast<size_t>(tex_w_) * tex_h_, argb(theme::kBackground));
    tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
                             SDL_TEXTUREACCESS_STREAMING, tex_w_, tex_h_);
}

void WaterfallView::update(const SpectrumFrame& frame) {
    if (pixels_.empty()) return;
    head_ = (head_ - 1 + tex_h_) % tex_h_;  // new row goes at the head
    uint32_t* row = &pixels_[static_cast<size_t>(head_) * tex_w_];
    for (int i = 0; i < kChannels; i++) {
        double v = scan_passes_ > 0
                       ? static_cast<double>(frame.bins[i]) / scan_passes_
                       : 0.0;
        row[i] = argb(ramp(v));
    }
    if (filled_ < tex_h_) filled_++;
}

void WaterfallView::render(SDL_Renderer* r) {
    ensure_texture(r);
    if (!tex_) return;

    // Upload the ring buffer as a top-to-bottom (newest-first) image.
    void* dst = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(tex_, nullptr, &dst, &pitch) == 0) {
        auto* out = static_cast<uint8_t*>(dst);
        for (int y = 0; y < tex_h_; y++) {
            int src = (head_ + y) % tex_h_;
            std::copy_n(reinterpret_cast<const uint8_t*>(
                            &pixels_[static_cast<size_t>(src) * tex_w_]),
                        tex_w_ * 4, out + static_cast<size_t>(y) * pitch);
        }
        SDL_UnlockTexture(tex_);
    }
    SDL_RenderCopy(r, tex_, nullptr, &rect_);
}

}  // namespace nrf24
