#include "Spidev.hpp"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace nrf24 {

Spidev::~Spidev() { close(); }

bool Spidev::open(const std::string& path, uint32_t speed_hz) {
    close();
    speed_hz_ = speed_hz;

    fd_ = ::open(path.c_str(), O_RDWR);
    if (fd_ < 0) {
        error_ = "open " + path + ": " + std::strerror(errno);
        return false;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speed_hz_) < 0) {
        error_ = "configure " + path + ": " + std::strerror(errno);
        close();
        return false;
    }

    error_.clear();
    return true;
}

void Spidev::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool Spidev::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    if (fd_ < 0) {
        error_ = "transfer on closed device";
        return false;
    }

    struct spi_ioc_transfer xfer;
    std::memset(&xfer, 0, sizeof(xfer));
    xfer.tx_buf = reinterpret_cast<uintptr_t>(tx);
    xfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
    xfer.len = static_cast<uint32_t>(len);
    xfer.speed_hz = speed_hz_;
    xfer.bits_per_word = 8;

    if (ioctl(fd_, SPI_IOC_MESSAGE(1), &xfer) < 0) {
        error_ = std::string("SPI transfer: ") + std::strerror(errno);
        return false;
    }
    return true;
}

}  // namespace nrf24
