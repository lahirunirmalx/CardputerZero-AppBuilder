// Gpio - single output line via the Linux GPIO character device.
//
// Drives the nRF24 CE pin. The original firmware toggled AVR PORTB bit 1
// (Arduino D9) with CE_HIGH()/CE_LOW(); on the CardputerZero (RP3A0 / CM0)
// CE is an ordinary kernel GPIO line requested from /dev/gpiochipN.
//
// Uses the kernel's chardev ioctl ABI directly (<linux/gpio.h>) so there is
// no libgpiod link dependency. Prefers the v2 line API and falls back to v1
// on older kernels.
#ifndef DEMO_NRF24_GPIO_HPP
#define DEMO_NRF24_GPIO_HPP

#include <cstdint>
#include <string>

namespace nrf24 {

class Gpio {
public:
    Gpio() = default;
    ~Gpio();

    Gpio(const Gpio&) = delete;
    Gpio& operator=(const Gpio&) = delete;

    // Requests `line` on `chip` (e.g. "/dev/gpiochip0", line 26) as an
    // output initialised low. Returns false + error() on failure.
    bool open(const std::string& chip, unsigned int line);
    void close();
    bool is_open() const { return line_fd_ >= 0; }

    // Set the line high/low. Returns false on ioctl error.
    bool set(bool high);
    bool high() { return set(true); }
    bool low() { return set(false); }

    const std::string& error() const { return error_; }

private:
    int line_fd_ = -1;
    bool use_v2_ = false;
    std::string error_;
};

}  // namespace nrf24

#endif  // DEMO_NRF24_GPIO_HPP
