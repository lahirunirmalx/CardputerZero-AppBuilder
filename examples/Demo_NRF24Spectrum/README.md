# Demo_NRF24Spectrum

## Purpose / 目的

A 2.4 GHz (and sub-GHz) RF **spectrum analyzer** that runs entirely on the
CardputerZero. It combines two projects:

- [**nrf24-scanner**](https://github.com/lahirunirmalx/nrf24-scanner) - the
  2.4 GHz sweep logic (nRF24L01 RPD sampling, two-stage adaptive scan),
  originally Arduino firmware, **ported here to Linux SPI + GPIO**.
- [**WiFi-Spectrum-Analyzer**](https://github.com/lahirunirmalx/WiFi-Spectrum-Analyzer) -
  the SDL2 phosphor-scope + waterfall UI, **ported to the 320x170 screen**.

The original setup used an Arduino to run the radio and a PC to draw the UI,
linked by USB serial. On the CardputerZero (a Raspberry Pi CM0 / Linux board)
we drop the Arduino: the radio is driven **directly over the kernel's `spidev`
+ GPIO character device**, and the scanner feeds the UI in-process - no serial
link. A CC1101 (sub-GHz) on the same RF cap is also supported; the `B` key
switches bands.

With no radio wired (or `--demo`) it renders synthetic data, so it also runs in
the `czdev` emulator.

## Controls

| Key | Action |
|-----|--------|
| `B` | Toggle band: 2.4 GHz (nRF24) <-> sub-GHz (CC1101) |
| `V` | Spectrum mode: phosphor trace <-> digital bars |
| `W` | Waterfall on/off |
| `M` | Peak-hold envelope on/off |
| `Esc` / `Q` | Quit |

## Build & run

```bash
make                       # -> ./demo-nrf24spectrum
make selftest              # verify the ported scan math (no hardware)
./demo-nrf24spectrum --demo    # synthetic data, any machine / emulator
./demo-nrf24spectrum           # live radio (see wiring below)
```

Options:

```
--demo             synthetic data, no radio needed
--selftest         run the scan-math self-test and exit
--spi PATH         nRF24 spidev device   (default /dev/spidev0.0)
--cc-spi PATH      CC1101 spidev device  (default /dev/spidev0.1)
--no-cc1101        disable the sub-GHz radio
--ce-chip PATH     GPIO chip for nRF24 CE (default /dev/gpiochip0)
--ce-line N        GPIO line for nRF24 CE (default 26 = HAT_P0)
--spi-hz N         SPI clock in Hz        (default 4000000)
--passes N         SCAN_PASSES sweep depth (default 32)
```

## Hardware wiring

> **Important compatibility note.** The
> [MTools Tec C1101+nRF24 Cap](https://shop.mtoolstec.com/product/c1101-nrf24-rf-cap-for-cardputer-adv)
> and the
> [PINGEQUA Hydra RF 424 Cap](https://www.tindie.com/products/pingequalab/hydra-rf-424-cap-for-cardputer-adv-nrf24-cc1101-2/)
> are built for the **M5Stack Cardputer ADV (ESP32-S3)**, not the
> **CardputerZero (Raspberry Pi CM0)**. Their published pin numbers
> ([Hydra-RF ref](https://github.com/pingequalab/cardputer-adv-hydra-rf):
> nRF24 CS=GPIO6 / CE=GPIO4, CC1101 CS=GPIO13 / GDO0=GPIO5) are **ESP32 GPIOs
> driven by Bruce firmware** and do not apply to the CM0's Linux SPI. The ADV
> also uses an 18-pin header vs. the CardputerZero's 14-pin EXT header.
>
> To use one of these caps here, wire its nRF24 (and CC1101) SPI signals to the
> CardputerZero's EXT SPI0 pins with jumpers/an adapter as below. The radio
> chips themselves are standard SPI parts; only the carrier board's pin
> assignment differs.

### CardputerZero EXT 2.54-14P header (RP3A0 / CM0)

SPI0 lines exposed on the EXT header:

| Signal | CM0 GPIO | Linux |
|--------|----------|-------|
| SCLK   | GPIO11   | spidev0 clock |
| MOSI   | GPIO10   | spidev0 MOSI |
| MISO   | GPIO9    | spidev0 MISO |
| CE0    | GPIO8    | `/dev/spidev0.0` chip-select |
| CS1    | GPIO7    | `/dev/spidev0.1` chip-select |
| HAT_P0 | GPIO26   | free GPIO (nRF24 CE) |
| HAT_P1 | GPIO23   | free GPIO (spare) |

### Connect the cap's radios to those pins

nRF24L01 (2.4 GHz), on `/dev/spidev0.0`:

| nRF24 pin | CardputerZero |
|-----------|---------------|
| VCC       | 3V3 |
| GND       | GND |
| SCK       | GPIO11 (SCLK) |
| MOSI      | GPIO10 |
| MISO      | GPIO9 |
| CSN       | GPIO8 (CE0 / spidev0.0) |
| CE        | GPIO26 (HAT_P0) -> `--ce-line 26` |
| IRQ       | not used |

CC1101 (sub-GHz), on `/dev/spidev0.1` (shares SCLK/MOSI/MISO):

| CC1101 pin | CardputerZero |
|------------|---------------|
| VCC        | 3V3 |
| GND        | GND |
| SCLK/SI/SO | GPIO11 / GPIO10 / GPIO9 (shared) |
| CSN        | GPIO7 (CS1 / spidev0.1) |
| GDO0       | not used (RSSI-only sweep) |

> nRF24L01 must run at **3.3 V**. If you use a bare module (not a cap with its
> own regulator/caps), add a 10 uF + 100 nF cap across its VCC/GND for stable
> RX - the same requirement the original Arduino build had.

### Enable SPI on the device

The two radios share SCLK/MOSI/MISO but need **separate chip-selects** so they
never talk over each other - the nRF24 on `spidev0.0` and the CC1101 on
`spidev0.1`. Enable both CS lines in the boot config:

```bash
# add to /boot/firmware/config.txt (or /boot/config.txt), then reboot:
dtparam=spi=on
dtoverlay=spi0-2cs   # exposes both spidev0.0 (CE0) and spidev0.1 (CE1)
```

```bash
# after reboot, confirm both nodes exist:
ls -l /dev/spidev0.*   # -> /dev/spidev0.0  /dev/spidev0.1
```

The app already targets these by default (`--spi /dev/spidev0.0` for the nRF24,
`--cc-spi /dev/spidev0.1` for the CC1101); override the flags if your overlay
maps them differently.

The `.deb`'s `postinst` adds the `pi` user to the `spi` and `gpio` groups so
the app can open these without root. Verify the CE line number matches how you
wired it (`--ce-line`).

## How the port maps to the original firmware

| Arduino firmware | Port here |
|------------------|-----------|
| `SPI.transfer()` + `PORTB` CS | `src/nrf/Spidev.cpp` (`SPI_IOC_MESSAGE` ioctl) |
| CE on PB1 (D9) via `CE_HIGH/LOW` | `src/nrf/Gpio.cpp` (GPIO chardev line) |
| `getRegister`/`setRegister`, two-stage `scanChannels` | `src/nrf/NrfScanner.cpp` (byte-for-byte semantics) |
| CSV `DATA,<peak>,c0..c63` over serial | `SpectrumFrame` passed in-process |
| host SDL2 `SpectrumView`/`WaterfallView` | `src/ui/` at fixed 320x170 |

The registers, config bits, dwell times (`RX_SETTLE_US=130`,
`CH_SWITCH_SETTLE_US=80`), `PROBE_PASSES`/`SCAN_PASSES`, and the probe-then-deep
adaptive strategy are unchanged. `make selftest` checks the accumulation math
against scripted RPD/RSSI bytes.

## Verify it's working

- **Emulator / no radio:** `./demo-nrf24spectrum --demo` shows animated
  spectrum + waterfall with a `DEMO` banner.
- **Live 2.4 GHz:** run without `--demo`; toggling a nearby 2.4 GHz Wi-Fi AP or
  moving a Bluetooth device should light up the corresponding channel bins.
- **Live sub-GHz:** press `B`; keying a 433 MHz remote near the antenna should
  raise bins in the CC1101 band.

## TODO / future

- CC1101 band presets beyond 433 MHz (868 / 915 MHz) as selectable bands.
- Record/replay of captured frames (the original analyzer had CSV replay).
