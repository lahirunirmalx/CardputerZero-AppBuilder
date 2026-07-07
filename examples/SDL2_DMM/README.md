# SDL2_DMM - Bench Multimeter (Open LabBench)

A digital-multimeter app for the CardputerZero (320x170), packaging the
[Open LabBench](https://github.com/lahirunirmalx/open-lab-bench) instrument
suite (MIT). It works out of the box with a synthetic **demo** driver and
drives **real meters** over USB-serial, USB-TMC, or Prologix GPIB - no code
changes, just environment variables.

The full Open LabBench source is vendored under [`olb/`](olb/). This example
adds one small-screen view, `dmm-compact` (see
[`olb/src/views/dmm_compact.c`](olb/src/views/dmm_compact.c)): a 320x170
stacked layout with a mode label, a big centered VFD dot-matrix reading with
engineering-scaled units, and AUTO / rate / OL badges.

## Run (demo, no hardware)

In the desktop emulator or on-device the launcher defaults to the demo driver:

```
sdl2-dmm            # == psu_app --driver=dmm-demo --view=dmm-compact --port=-
```

## Run against a real meter

The launcher reads these environment variables (all optional):

| Var          | Meaning                       | Example                        |
|--------------|-------------------------------|--------------------------------|
| `DMM_DRIVER` | driver id                     | `owon-xdm`, `keysight-34461a`, `hp-3458a` |
| `DMM_PORT`   | transport / device            | `/dev/ttyUSB0`, `usbtmc:/dev/usbtmc0`, `prologix:/dev/ttyUSB0:22` |
| `DMM_BAUD`   | serial baud (serial drivers)  | `115200`                       |
| `DMM_VIEW`   | view id                       | `dmm-compact` (default), `dmm-toolbar`, `dmm-full` |

Examples:

```
# OWON XDM over USB-serial
DMM_DRIVER=owon-xdm DMM_PORT=/dev/ttyUSB0 sdl2-dmm

# Keysight 34461A over USB-TMC (kernel usbtmc driver)
DMM_DRIVER=keysight-34461a DMM_PORT=usbtmc:/dev/usbtmc0 sdl2-dmm

# HP 3458A over a Prologix GPIB-USB adapter at GPIB address 22
DMM_DRIVER=hp-3458a DMM_PORT=prologix:/dev/ttyUSB0:22 sdl2-dmm
```

Run `psu_app --list` to see every built-in driver and view id.

## Transports (built in)

- **Serial** (USB-serial) - OWON XDM, SCPI meters
- **USB-TMC** - `usbtmc:/dev/usbtmc0` (Linux kernel driver) or the userspace
  libusb backend (compiled in when `libusb-1.0` is present)
- **Prologix GPIB** - `prologix:<serial-dev>:<gpib-addr>` for classic HP-IB
  meters (HP 3458A / 3457A / 3478A) and any SCPI meter behind a GPIB adapter
- **VXI-11 / HiSLIP** - LAN meters (see upstream docs)

## Build

`packaging/build.sh` runs `make -C olb app`. Requires `libsdl2-dev`,
`libsdl2-ttf-dev`, and (for USB-TMC) `libusb-1.0-0-dev`; CI installs these via
`packaging/ci-deps.sh`. Keys: `ESC` / `Q` to quit.
