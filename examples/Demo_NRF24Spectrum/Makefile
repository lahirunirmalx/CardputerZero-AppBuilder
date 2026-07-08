CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
PKGS     := sdl2 SDL2_ttf
PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
PKG_LIBS   := $(shell pkg-config --libs $(PKGS))

TARGET := demo-nrf24spectrum
SRC := \
	src/main.cpp \
	src/nrf/Spidev.cpp \
	src/nrf/Gpio.cpp \
	src/nrf/NrfScanner.cpp \
	src/nrf/Cc1101Scanner.cpp \
	src/sim/DemoSource.cpp \
	src/ui/SpectrumView.cpp \
	src/ui/WaterfallView.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(PKG_CFLAGS) $(SRC) -o $(TARGET) $(PKG_LIBS)

# Build and run the scan-math self-test (no SDL window, no hardware).
selftest: $(TARGET)
	./$(TARGET) --selftest

clean:
	rm -f $(TARGET)

.PHONY: all selftest clean
