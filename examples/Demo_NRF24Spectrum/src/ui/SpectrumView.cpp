#include "SpectrumView.hpp"

#include "Theme.hpp"

namespace nrf24 {

namespace {
void set_color(SDL_Renderer* r, const Rgb& c, uint8_t a = 255) {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, a);
}

// Linear blend across the low->mid->high signal ramp.
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
}  // namespace

double SpectrumView::norm(uint8_t bin) const {
    if (scan_passes_ <= 0) return 0.0;
    double v = static_cast<double>(bin) / scan_passes_;
    return v > 1.0 ? 1.0 : v;
}

int SpectrumView::bin_x(int i) const {
    return rect_.x + (i * (rect_.w - 1)) / (kChannels - 1);
}

int SpectrumView::level_y(double v) const {
    if (v < 0) v = 0;
    if (v > 1) v = 1;
    return rect_.y + rect_.h - 1 - static_cast<int>(v * (rect_.h - 1) + 0.5);
}

void SpectrumView::update(const SpectrumFrame& frame) {
    // Phosphor decay: rise fast to new peaks, fall slowly - the CRT look.
    const double attack = 0.6, decay = 0.12;
    for (int i = 0; i < kChannels; i++) {
        double target = norm(frame.bins[i]);
        if (!primed_) {
            smooth_[i] = target;
            hold_[i] = target;
            continue;
        }
        double alpha = target > smooth_[i] ? attack : decay;
        smooth_[i] += (target - smooth_[i]) * alpha;
        if (target > hold_[i])
            hold_[i] = target;                 // instant rise
        else
            hold_[i] += (0.0 - hold_[i]) * 0.02;  // slow bleed-down
    }
    primed_ = true;
}

void SpectrumView::render(SDL_Renderer* r) const {
    // Background + grid.
    set_color(r, theme::kBackground);
    SDL_RenderFillRect(r, &rect_);
    set_color(r, theme::kGrid);
    for (int gy = 1; gy < 4; gy++) {
        int y = rect_.y + gy * rect_.h / 4;
        SDL_RenderDrawLine(r, rect_.x, y, rect_.x + rect_.w - 1, y);
    }
    for (int gx = 1; gx < 8; gx++) {
        int x = rect_.x + gx * rect_.w / 8;
        SDL_RenderDrawLine(r, x, rect_.y, x, rect_.y + rect_.h - 1);
    }

    if (mode_ == Mode::Digital) {
        int bw = (rect_.w / kChannels);
        if (bw < 1) bw = 1;
        for (int i = 0; i < kChannels; i++) {
            int x = bin_x(i);
            int y = level_y(smooth_[i]);
            Rgb c = ramp(smooth_[i]);
            set_color(r, c);
            SDL_Rect bar = {x, y, bw, rect_.y + rect_.h - y};
            SDL_RenderFillRect(r, &bar);
        }
    } else {
        // Peak-hold envelope first (dim), then the bright trace on top.
        if (peak_hold_) {
            set_color(r, theme::kEnvelope);
            for (int i = 1; i < kChannels; i++) {
                SDL_RenderDrawLine(r, bin_x(i - 1), level_y(hold_[i - 1]),
                                   bin_x(i), level_y(hold_[i]));
            }
        }
        set_color(r, theme::kTrace);
        for (int i = 1; i < kChannels; i++) {
            SDL_RenderDrawLine(r, bin_x(i - 1), level_y(smooth_[i - 1]),
                               bin_x(i), level_y(smooth_[i]));
        }
        // Bright leading dots at each bin for the phosphor glow.
        set_color(r, theme::kTraceGlow);
        for (int i = 0; i < kChannels; i++) {
            int x = bin_x(i), y = level_y(smooth_[i]);
            SDL_RenderDrawPoint(r, x, y);
        }
    }

    // Baseline axis.
    set_color(r, theme::kAxis);
    SDL_RenderDrawLine(r, rect_.x, rect_.y + rect_.h - 1,
                       rect_.x + rect_.w - 1, rect_.y + rect_.h - 1);
}

}  // namespace nrf24
