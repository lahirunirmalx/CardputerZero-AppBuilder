// Spidev - thin RAII wrapper over a Linux /dev/spidevX.Y device.
//
// Replaces the Arduino SPI library used by the original firmware. A single
// full-duplex transfer maps to one SPI_IOC_MESSAGE ioctl. The nRF24 talks
// SPI mode 0, MSB first; CSN is driven automatically by the kernel per
// transfer, so - unlike the AVR build - there is no separate CS GPIO.
#ifndef DEMO_NRF24_SPIDEV_HPP
#define DEMO_NRF24_SPIDEV_HPP

#include <cstdint>
#include <string>

namespace nrf24 {

class Spidev {
public:
    Spidev() = default;
    ~Spidev();

    Spidev(const Spidev&) = delete;
    Spidev& operator=(const Spidev&) = delete;

    // Opens the device and configures mode 0 / MSB-first / speed_hz.
    // Returns false and sets error() on failure (missing device, no perms,
    // SPI overlay disabled). Never throws.
    bool open(const std::string& path, uint32_t speed_hz = 4000000);
    void close();
    bool is_open() const { return fd_ >= 0; }

    // Full-duplex transfer of len bytes: tx -> MOSI, MISO -> rx.
    // tx and rx may alias (same buffer). Returns false on ioctl error.
    bool transfer(const uint8_t* tx, uint8_t* rx, size_t len);

    const std::string& error() const { return error_; }

private:
    int fd_ = -1;
    uint32_t speed_hz_ = 4000000;
    std::string error_;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_SPIDEV_HPP
