#include "Gpio.hpp"

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace nrf24 {

Gpio::~Gpio() { close(); }

bool Gpio::open(const std::string& chip, unsigned int line) {
    close();

    int chip_fd = ::open(chip.c_str(), O_RDONLY | O_CLOEXEC);
    if (chip_fd < 0) {
        error_ = "open " + chip + ": " + std::strerror(errno);
        return false;
    }

    // Try the v2 line-request API first (kernel >= 5.10).
#ifdef GPIO_V2_GET_LINE_IOCTL
    {
        struct gpio_v2_line_request req;
        std::memset(&req, 0, sizeof(req));
        req.offsets[0] = line;
        req.num_lines = 1;
        req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
        std::strncpy(req.consumer, "nrf24-ce", sizeof(req.consumer) - 1);

        if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) >= 0 && req.fd >= 0) {
            line_fd_ = req.fd;
            use_v2_ = true;
            ::close(chip_fd);
            error_.clear();
            return low();
        }
    }
#endif

    // Fall back to the v1 handle API.
    {
        struct gpiohandle_request req;
        std::memset(&req, 0, sizeof(req));
        req.lineoffsets[0] = line;
        req.lines = 1;
        req.flags = GPIOHANDLE_REQUEST_OUTPUT;
        req.default_values[0] = 0;
        std::strncpy(req.consumer_label, "nrf24-ce",
                     sizeof(req.consumer_label) - 1);

        if (ioctl(chip_fd, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0 ||
            req.fd < 0) {
            error_ = "request GPIO line " + std::to_string(line) + " on " +
                     chip + ": " + std::strerror(errno);
            ::close(chip_fd);
            return false;
        }
        line_fd_ = req.fd;
        use_v2_ = false;
    }

    ::close(chip_fd);
    error_.clear();
    return true;
}

void Gpio::close() {
    if (line_fd_ >= 0) {
        ::close(line_fd_);
        line_fd_ = -1;
    }
}

bool Gpio::set(bool high) {
    if (line_fd_ < 0) {
        error_ = "set on closed line";
        return false;
    }

#ifdef GPIO_V2_GET_LINE_IOCTL
    if (use_v2_) {
        struct gpio_v2_line_values vals;
        std::memset(&vals, 0, sizeof(vals));
        vals.mask = 1;
        vals.bits = high ? 1 : 0;
        if (ioctl(line_fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
            error_ = std::string("set GPIO value: ") + std::strerror(errno);
            return false;
        }
        return true;
    }
#endif

    struct gpiohandle_data data;
    std::memset(&data, 0, sizeof(data));
    data.values[0] = high ? 1 : 0;
    if (ioctl(line_fd_, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        error_ = std::string("set GPIO value: ") + std::strerror(errno);
        return false;
    }
    return true;
}

}  // namespace nrf24
