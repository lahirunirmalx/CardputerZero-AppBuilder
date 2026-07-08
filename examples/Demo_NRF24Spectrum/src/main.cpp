// Demo_NRF24Spectrum - 2.4 GHz spectrum analyzer for the CardputerZero.
//
// Combines two of the author's projects into one on-device app:
//   * nrf24-scanner          - Arduino firmware that sweeps 2.4 GHz with an
//                              nRF24L01's RPD. Ported here to Linux spidev +
//                              GPIO (see src/nrf/), so the CardputerZero
//                              (RP3A0 / CM0) drives the radio directly - no
//                              Arduino, no USB serial.
//   * WiFi-Spectrum-Analyzer - SDL2 phosphor-scope + waterfall UI. Ported to
//                              the 320x170 screen (see src/ui/).
//
// The scanner feeds SpectrumFrames straight into the UI in-process. With no
// radio (or --demo) it falls back to a synthetic DemoSource so it runs in the
// czdev emulator too.
//
// A CC1101 sub-GHz radio on the same caps is driven by src/nrf/Cc1101Scanner;
// the B key toggles the active band between 2.4 GHz (nRF24) and sub-GHz
// (CC1101), producing the same SpectrumFrame either way.
//
// Keys:  B = band (2.4GHz/subGHz)  V = spectrum mode   W = waterfall on/off
//        M = peak hold             Esc/Q = quit
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "nrf/Cc1101Scanner.hpp"
#include "nrf/HardwareBackend.hpp"
#include "nrf/NrfScanner.hpp"
#include "sim/DemoSource.hpp"
#include "ui/SpectrumFrame.hpp"
#include "ui/SpectrumView.hpp"
#include "ui/Theme.hpp"
#include "ui/WaterfallView.hpp"

using namespace nrf24;

namespace {

constexpr int kScreenW = 320;
constexpr int kScreenH = 170;
constexpr char kFontPath[] = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";

struct Options {
    bool demo = false;
    bool selftest = false;
    std::string spi = "/dev/spidev0.0";      // nRF24 chip-select
    std::string cc_spi = "/dev/spidev0.1";   // CC1101 chip-select
    std::string ce_chip = "/dev/gpiochip0";
    unsigned ce_line = 26;  // HAT_P0 / GPIO26 on the CardputerZero EXT header
    uint32_t spi_hz = 4000000;
    int scan_passes = 32;
    bool no_cc1101 = false;  // skip the sub-GHz radio entirely
};

enum class Band { Wifi24, SubGhz };

void print_usage() {
    std::printf(
        "Demo_NRF24Spectrum - 2.4 GHz nRF24 spectrum analyzer\n"
        "  --demo               synthetic data, no radio needed\n"
        "  --selftest           run scan-math self-test and exit\n"
        "  --spi PATH           nRF24 spidev device (default /dev/spidev0.0)\n"
        "  --cc-spi PATH        CC1101 spidev device (default /dev/spidev0.1)\n"
        "  --no-cc1101          disable the sub-GHz CC1101 radio\n"
        "  --ce-chip PATH       GPIO chip for nRF24 CE (default /dev/gpiochip0)\n"
        "  --ce-line N          GPIO line for nRF24 CE (default 26)\n"
        "  --spi-hz N           SPI clock in Hz (default 4000000)\n"
        "  --passes N           SCAN_PASSES depth (default 32)\n");
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto need = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s requires a value\n", name);
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--demo") {
            o.demo = true;
        } else if (a == "--selftest") {
            o.selftest = true;
        } else if (a == "--spi") {
            const char* v = need("--spi");
            if (!v) return false;
            o.spi = v;
        } else if (a == "--cc-spi") {
            const char* v = need("--cc-spi");
            if (!v) return false;
            o.cc_spi = v;
        } else if (a == "--no-cc1101") {
            o.no_cc1101 = true;
        } else if (a == "--ce-chip") {
            const char* v = need("--ce-chip");
            if (!v) return false;
            o.ce_chip = v;
        } else if (a == "--ce-line") {
            const char* v = need("--ce-line");
            if (!v) return false;
            o.ce_line = static_cast<unsigned>(std::atoi(v));
        } else if (a == "--spi-hz") {
            const char* v = need("--spi-hz");
            if (!v) return false;
            o.spi_hz = static_cast<uint32_t>(std::atol(v));
        } else if (a == "--passes") {
            const char* v = need("--passes");
            if (!v) return false;
            o.scan_passes = std::atoi(v);
        } else if (a == "-h" || a == "--help") {
            print_usage();
            return false;
        } else {
            std::fprintf(stderr, "unknown option: %s\n", a.c_str());
            print_usage();
            return false;
        }
    }
    return true;
}

// ---- Self-test: verify the ported scan math against scripted RPD bytes ----

// A fake SPI bus that returns a preset RPD value for reads of REG_RPD (0x09)
// and 0 for everything else. Lets us check the two-stage accumulation without
// hardware, mirroring the Arduino algorithm's expected counts.
class FakeSpi : public SpiBus {
public:
    // rpd_hot: channels (bin index) that should report RPD>0 every sample.
    explicit FakeSpi(const bool* hot) : hot_(hot) {}
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        rx[0] = 0;
        if (len >= 2) rx[1] = 0;
        uint8_t reg = tx[0] & 0x1F;
        bool is_write = (tx[0] & 0x20) != 0;
        if (is_write && reg == 0x05) {         // RF_CH write records channel
            cur_ch_ = tx[1] / 2;               // step = 2 for 64 channels
        } else if (!is_write && reg == 0x09) {  // RPD read
            if (len >= 2)
                rx[1] = (cur_ch_ >= 0 && cur_ch_ < kChannels && hot_[cur_ch_])
                            ? 1
                            : 0;
        }
        return true;
    }

private:
    const bool* hot_;
    int cur_ch_ = -1;
};

class NoopCe : public CeLine {
public:
    bool set(bool) override { return true; }
};

// Fake CC1101 bus: any read of the RSSI status register (0x34 | burst) returns
// a fixed raw byte; everything else returns 0. Lets us check the RSSI->dBm->
// bin scaling deterministically.
class FakeCc1101 : public SpiBus {
public:
    explicit FakeCc1101(uint8_t rssi_raw) : rssi_raw_(rssi_raw) {}
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        rx[0] = 0;
        if (len >= 2) rx[1] = 0;
        // Status-register read = addr | 0xC0. RSSI addr is 0x34.
        if (len >= 2 && tx[0] == (0x34 | 0xC0)) rx[1] = rssi_raw_;
        return true;
    }

private:
    uint8_t rssi_raw_;
};

int run_cc1101_selftest() {
    // raw 0x50 = 80 -> 80/2 - 74 = -34 dBm. With floor -110, ceil -20 (span
    // 90), norm = (-34 - -110)/90 = 0.844 -> round(0.844*32) = 27.
    FakeCc1101 spi(0x50);
    Cc1101Config cfg;
    cfg.sleep_between = false;
    Cc1101Scanner cc(spi, cfg);
    cc.begin();
    SpectrumFrame frame;
    cc.scan_frame(frame);

    const uint8_t want = 27;
    int failures = 0;
    for (int i = 0; i < kChannels; i++) {
        if (frame.bins[i] != want) {
            std::fprintf(stderr, "cc1101 selftest FAIL bin %d: got %u want %u\n",
                         i, frame.bins[i], want);
            failures++;
            break;
        }
    }
    if (frame.peak != want) {
        std::fprintf(stderr, "cc1101 selftest FAIL peak: got %u want %u\n",
                     frame.peak, want);
        failures++;
    }
    if (failures == 0)
        std::printf("cc1101 selftest PASS (rssi 0x50 -> -34dBm -> bin %u)\n",
                    want);
    return failures;
}

int run_selftest() {
    bool hot[kChannels] = {false};
    hot[6] = hot[7] = hot[40] = true;  // three "active" channels

    FakeSpi spi(hot);
    NoopCe ce;
    ScanConfig cfg;
    cfg.scan_passes = 32;
    cfg.probe_passes = 4;
    cfg.sleep_between = false;  // no real timing in the test

    NrfScanner scanner(spi, ce, cfg);
    scanner.begin();
    SpectrumFrame frame;
    scanner.scan_frame(frame);

    // Hot channels get all SCAN_PASSES samples (4 probe + 28 deep = 32).
    // Cold channels get only probe passes, all misses -> 0.
    int failures = 0;
    for (int i = 0; i < kChannels; i++) {
        uint8_t want = hot[i] ? cfg.scan_passes : 0;
        if (frame.bins[i] != want) {
            std::fprintf(stderr,
                         "selftest FAIL ch %d: got %u want %u\n", i,
                         frame.bins[i], want);
            failures++;
        }
    }
    if (frame.peak != cfg.scan_passes) {
        std::fprintf(stderr, "selftest FAIL peak: got %u want %d\n",
                     frame.peak, cfg.scan_passes);
        failures++;
    }
    if (failures == 0)
        std::printf("selftest PASS (peak=%u, hot channels=%d)\n",
                    frame.peak, 3);

    failures += run_cc1101_selftest();
    return failures == 0 ? 0 : 1;
}

// ---- Small text helper ----------------------------------------------------

void draw_text(SDL_Renderer* r, TTF_Font* font, const char* s, int x, int y,
               const Rgb& c) {
    if (!font) return;
    SDL_Color col = {c.r, c.g, c.b, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, s, col);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    SDL_Rect dst = {x, y, surf->w, surf->h};
    SDL_FreeSurface(surf);
    if (tex) {
        SDL_RenderCopy(r, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opt;
    if (!parse_args(argc, argv, opt)) return 0;
    if (opt.selftest) return run_selftest();

    // ---- Bring up the radios (or fall back to demo) -----------------------
    // nRF24 (2.4 GHz) on its own chip-select + CE GPIO.
    Spidev nrf_spidev;
    Gpio ce_gpio;
    SpidevBus nrf_bus(nrf_spidev);
    GpioCe ce_line(ce_gpio);

    ScanConfig cfg;
    cfg.scan_passes = opt.scan_passes;
    cfg.probe_passes = opt.scan_passes < 4 ? opt.scan_passes : 4;
    NrfScanner nrf(nrf_bus, ce_line, cfg);

    // CC1101 (sub-GHz) on a second chip-select. No CE line needed.
    Spidev cc_spidev;
    SpidevBus cc_bus(cc_spidev);
    Cc1101Scanner cc(cc_bus);

    bool nrf_live = false;
    bool cc_live = false;
    std::string radio_err;
    if (!opt.demo) {
        if (!nrf_spidev.open(opt.spi, opt.spi_hz)) {
            radio_err = nrf_spidev.error();
        } else if (!ce_gpio.open(opt.ce_chip, opt.ce_line)) {
            radio_err = ce_gpio.error();
        } else if (!nrf.begin()) {
            radio_err = "nRF24 begin() SPI transfer failed";
        } else {
            nrf_live = true;
        }
        if (!opt.no_cc1101) {
            if (cc_spidev.open(opt.cc_spi, opt.spi_hz) && cc.begin())
                cc_live = true;
        }
        if (!nrf_live)
            std::fprintf(stderr, "nRF24 unavailable (%s) - demo mode\n",
                         radio_err.c_str());
        if (!opt.no_cc1101 && !cc_live)
            std::fprintf(stderr, "CC1101 unavailable on %s - sub-GHz demo\n",
                         opt.cc_spi.c_str());
    }

    Band band = Band::Wifi24;
    DemoSource demo(0x1234abcdu, opt.scan_passes);

    // ---- SDL setup --------------------------------------------------------
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    bool have_ttf = TTF_Init() == 0;
    TTF_Font* font = have_ttf ? TTF_OpenFont(kFontPath, 11) : nullptr;

    SDL_Window* win = SDL_CreateWindow(
        "nRF24 Spectrum", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        kScreenW, kScreenH, SDL_WINDOW_BORDERLESS);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* ren =
        SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    // Layout: title bar, spectrum (top ~55%), waterfall (bottom), footer.
    const int header_h = 14;
    const int footer_h = 12;
    const int plot_h = kScreenH - header_h - footer_h;
    SDL_Rect spec_rect = {0, header_h, kScreenW, plot_h * 55 / 100};
    SDL_Rect wf_rect = {0, spec_rect.y + spec_rect.h, kScreenW,
                        plot_h - spec_rect.h};

    SpectrumView spectrum(spec_rect, opt.scan_passes);
    spectrum.set_peak_hold(true);
    WaterfallView waterfall(wf_rect, opt.scan_passes);
    bool show_waterfall = true;

    SpectrumFrame frame;
    bool running = true;
    uint32_t last_ticks = SDL_GetTicks();

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        running = false;
                        break;
                    case SDLK_b:
                        // Switch band between 2.4 GHz (nRF24) and sub-GHz
                        // (CC1101). Resets the waterfall history look.
                        band = band == Band::Wifi24 ? Band::SubGhz
                                                     : Band::Wifi24;
                        break;
                    case SDLK_v:
                        spectrum.toggle_mode();
                        break;
                    case SDLK_w:
                        show_waterfall = !show_waterfall;
                        break;
                    case SDLK_m:
                        spectrum.set_peak_hold(!spectrum.peak_hold());
                        break;
                    default:
                        break;
                }
            }
        }

        uint32_t now = SDL_GetTicks();
        double dt = static_cast<double>(now - last_ticks);
        last_ticks = now;

        // Acquire a frame from the active band's radio, else synth demo.
        bool live = false;
        if (band == Band::Wifi24) {
            if (nrf_live && nrf.scan_frame(frame)) {
                live = true;
            } else if (nrf_live) {
                nrf_live = false;  // SPI error mid-scan
                radio_err = "nRF24 SPI error during scan";
            }
        } else {  // SubGhz
            if (cc_live && cc.scan_frame(frame)) {
                live = true;
            } else if (cc_live) {
                cc_live = false;
            }
        }
        if (!live) {
            demo.set_band(band == Band::Wifi24 ? DemoSource::Band::Wifi24
                                               : DemoSource::Band::SubGhz);
            demo.next(frame, dt <= 0 ? 16.0 : dt);
        }

        spectrum.update(frame);
        if (show_waterfall) waterfall.update(frame);

        // ---- Render -------------------------------------------------------
        SDL_SetRenderDrawColor(ren, theme::kBackground.r, theme::kBackground.g,
                               theme::kBackground.b, 255);
        SDL_RenderClear(ren);

        spectrum.render(ren);
        if (show_waterfall) waterfall.render(ren);

        // Title bar - names the active radio/band.
        char title[64];
        const char* band_label =
            band == Band::Wifi24 ? "nRF24 2.4GHz" : "CC1101 433MHz";
        std::snprintf(title, sizeof(title), "%s  peak=%u", band_label,
                      frame.peak);
        draw_text(ren, font, title, 4, 1, theme::kText);

        // Live/demo banner.
        if (live) {
            draw_text(ren, font, "LIVE", kScreenW - 34, 1, theme::kTrace);
        } else {
            SDL_Rect b = {kScreenW - 46, 0, 46, header_h};
            SDL_SetRenderDrawColor(ren, theme::kBannerBg.r, theme::kBannerBg.g,
                                   theme::kBannerBg.b, 255);
            SDL_RenderFillRect(ren, &b);
            draw_text(ren, font, "DEMO", kScreenW - 40, 1, theme::kBannerText);
        }

        // Footer key hints.
        draw_text(ren, font, "B band  V mode  W wfall  M hold  Esc quit", 4,
                  kScreenH - footer_h, theme::kTextDim);

        SDL_RenderPresent(ren);
    }

    if (font) TTF_CloseFont(font);
    if (have_ttf) TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
