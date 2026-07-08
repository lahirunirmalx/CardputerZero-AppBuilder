// HardwareBackend - binds the NrfScanner's SpiBus/CeLine abstractions to the
// real Linux Spidev and Gpio drivers. Kept separate so the scanner core can
// be unit-tested with fakes and compiled without pulling in ioctl headers.
#ifndef DEMO_NRF24_HARDWARE_BACKEND_HPP
#define DEMO_NRF24_HARDWARE_BACKEND_HPP

#include "Gpio.hpp"
#include "NrfScanner.hpp"
#include "Spidev.hpp"

namespace nrf24 {

class SpidevBus : public SpiBus {
public:
    explicit SpidevBus(Spidev& dev) : dev_(dev) {}
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
        return dev_.transfer(tx, rx, len);
    }

private:
    Spidev& dev_;
};

class GpioCe : public CeLine {
public:
    explicit GpioCe(Gpio& gpio) : gpio_(gpio) {}
    bool set(bool high) override { return gpio_.set(high); }

private:
    Gpio& gpio_;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_HARDWARE_BACKEND_HPP
