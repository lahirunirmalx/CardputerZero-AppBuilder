#include "Cc1101Scanner.hpp"

#include <time.h>

namespace nrf24 {

namespace {
// CC1101 SPI access bits.
constexpr uint8_t WRITE_SINGLE = 0x00;
constexpr uint8_t READ_SINGLE = 0x80;
constexpr uint8_t READ_BURST = 0xC0;  // status registers need the burst bit

// Config registers.
constexpr uint8_t REG_FREQ2 = 0x0D;
constexpr uint8_t REG_FREQ1 = 0x0E;
constexpr uint8_t REG_FREQ0 = 0x0F;
constexpr uint8_t REG_MDMCFG4 = 0x10;
constexpr uint8_t REG_MDMCFG3 = 0x11;
constexpr uint8_t REG_MDMCFG2 = 0x12;
constexpr uint8_t REG_MCSM0 = 0x18;
constexpr uint8_t REG_AGCCTRL2 = 0x1B;
constexpr uint8_t REG_FSCAL3 = 0x23;
constexpr uint8_t REG_FSCAL2 = 0x24;
constexpr uint8_t REG_FSCAL1 = 0x25;
constexpr uint8_t REG_FSCAL0 = 0x26;

// Status registers (require READ_BURST).
constexpr uint8_t REG_PARTNUM = 0x30;
constexpr uint8_t REG_RSSI = 0x34;

// Command strobes.
constexpr uint8_t STROBE_SRES = 0x30;  // reset
constexpr uint8_t STROBE_SRX = 0x34;   // enable RX
constexpr uint8_t STROBE_SIDLE = 0x36;
constexpr uint8_t STROBE_SFRX = 0x3A;  // flush RX FIFO

// 26 MHz crystal: FREQ = f_carrier * 2^16 / 26e6.
constexpr double kXtalMhz = 26.0;
}  // namespace

void Cc1101Scanner::dwell(unsigned micros) {
    if (!cfg_.sleep_between) return;
    struct timespec ts;
    ts.tv_sec = micros / 1000000u;
    ts.tv_nsec = static_cast<long>(micros % 1000000u) * 1000L;
    clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, nullptr);
}

uint8_t Cc1101Scanner::read_reg(uint8_t addr) {
    uint8_t tx[2] = {static_cast<uint8_t>(addr | READ_SINGLE), 0x00};
    uint8_t rx[2] = {0, 0};
    if (!spi_.transfer(tx, rx, 2)) io_ok_ = false;
    return rx[1];
}

uint8_t Cc1101Scanner::read_status(uint8_t addr) {
    uint8_t tx[2] = {static_cast<uint8_t>(addr | READ_BURST), 0x00};
    uint8_t rx[2] = {0, 0};
    if (!spi_.transfer(tx, rx, 2)) io_ok_ = false;
    return rx[1];
}

void Cc1101Scanner::write_reg(uint8_t addr, uint8_t v) {
    uint8_t tx[2] = {static_cast<uint8_t>(addr | WRITE_SINGLE), v};
    uint8_t rx[2] = {0, 0};
    if (!spi_.transfer(tx, rx, 2)) io_ok_ = false;
}

void Cc1101Scanner::strobe(uint8_t cmd) {
    uint8_t tx[1] = {cmd};
    uint8_t rx[1] = {0};
    if (!spi_.transfer(tx, rx, 1)) io_ok_ = false;
}

void Cc1101Scanner::set_frequency(double mhz) {
    uint32_t freq =
        static_cast<uint32_t>((mhz * 65536.0) / kXtalMhz + 0.5) & 0xFFFFFF;
    // Retune requires leaving RX; hop via IDLE then re-enter RX.
    strobe(STROBE_SIDLE);
    write_reg(REG_FREQ2, static_cast<uint8_t>((freq >> 16) & 0xFF));
    write_reg(REG_FREQ1, static_cast<uint8_t>((freq >> 8) & 0xFF));
    write_reg(REG_FREQ0, static_cast<uint8_t>(freq & 0xFF));
    strobe(STROBE_SRX);
}

double Cc1101Scanner::rssi_to_dbm(uint8_t raw) {
    // Datasheet: 2's-complement, half-dB, offset ~74.
    int r = raw >= 128 ? (static_cast<int>(raw) - 256) : static_cast<int>(raw);
    return r / 2.0 - 74.0;
}

bool Cc1101Scanner::begin() {
    io_ok_ = true;
    strobe(STROBE_SRES);
    dwell(1000);

    // Minimal RX-for-RSSI config. Wide-ish RX bandwidth, GFSK, no packet
    // handling - we only ever read RSSI. Values from common CC1101 RX presets.
    write_reg(REG_MDMCFG4, 0x8C);   // RX filter BW ~ 200 kHz, data rate exp
    write_reg(REG_MDMCFG3, 0x22);   // data rate mantissa
    write_reg(REG_MDMCFG2, 0x30);   // GFSK, no sync word / carrier sense only
    write_reg(REG_MCSM0, 0x18);     // auto-calibrate on IDLE->RX
    write_reg(REG_AGCCTRL2, 0x43);  // AGC: reasonable target amplitude
    write_reg(REG_FSCAL3, 0xE9);
    write_reg(REG_FSCAL2, 0x2A);
    write_reg(REG_FSCAL1, 0x00);
    write_reg(REG_FSCAL0, 0x1F);

    // PARTNUM should read 0x00 and VERSION (0x31) nonzero on a real CC1101;
    // treat "reads back something" as present. Bus errors mark absent.
    uint8_t partnum = read_status(REG_PARTNUM);
    present_ = io_ok_;
    (void)partnum;

    set_frequency(cfg_.band.base_mhz);
    strobe(STROBE_SFRX);
    strobe(STROBE_SRX);
    return io_ok_;
}

bool Cc1101Scanner::scan_frame(SpectrumFrame& frame) {
    io_ok_ = true;
    frame.clear();

    uint8_t peak = 0;
    const double span = cfg_.ceil_dbm - cfg_.floor_dbm;
    for (int i = 0; i < kChannels; i++) {
        double mhz = cfg_.band.base_mhz + i * (cfg_.band.step_khz / 1000.0);
        set_frequency(mhz);
        dwell(cfg_.settle_us);
        double dbm = rssi_to_dbm(read_status(REG_RSSI));

        double norm = span > 0 ? (dbm - cfg_.floor_dbm) / span : 0.0;
        if (norm < 0) norm = 0;
        if (norm > 1) norm = 1;
        // Scale to the same 0..SCAN_PASSES(=32) domain the nRF24 path uses so
        // the shared views normalise identically.
        uint8_t v = static_cast<uint8_t>(norm * 32.0 + 0.5);
        frame.bins[i] = v;
        if (v > peak) peak = v;
    }
    frame.peak = peak;

    strobe(STROBE_SIDLE);
    return io_ok_;
}

}  // namespace nrf24
