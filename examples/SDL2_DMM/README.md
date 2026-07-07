# SDL2_DMM - Bench Multimeter (Open LabBench)

A digital-multimeter app for the CardputerZero (320x170), packaging the
[Open LabBench](https://github.com/lahirunirmalx/open-lab-bench) instrument
suite (MIT). It works out of the box with a synthetic **demo** driver and
drives **real meters** over USB-serial, USB-TMC, or Prologix GPIB - selected
on-screen, no config files or environment variables.

The full Open LabBench source is vendored under [`olb/`](olb/). This example
adds two small-screen pieces:

- `dmm-compact` view ([`olb/src/views/dmm_compact.c`](olb/src/views/dmm_compact.c)):
  a 320x170 stacked layout with a mode label, a big centered VFD dot-matrix
  reading (sharp small dots) with engineering-scaled units, and AUTO / rate /
  OL badges.
- a keyboard-driven picker ([`olb/src/app/dmm_picker.c`](olb/src/app/dmm_picker.c)):
  choose the **meter** and **port** on-screen, then it launches the compact
  view for that meter.

## Use

Launch the app (or `psu_app --pick`). You get the picker:

```
Select Multimeter
 METER                 PORT
 Fluke 8846A ...       demo (no hardware)
 Keithley 2000 ...     /dev/ttyUSB0
 ...                   /dev/ttyACM0
 HP 3458A ...          usbtmc:/dev/usbtmc0
 Demo DMM (synthetic)  prologix:/dev/ttyUSB0:22
                       custom...
 arrows move  TAB col  ENTER connect  ESC quit
```

- **Up / Down** move within a column, **TAB** (or Left/Right) switches between
  METER and PORT.
- **ENTER** connects: opens the chosen meter on the chosen port and shows the
  live reading. **ESC** in the reading returns to the picker.
- Pick **custom...** and press ENTER to type any port string (a second ENTER
  accepts it), e.g. `prologix:/dev/ttyUSB0:15`.
- Defaults are **Demo DMM** + **demo** port, so ENTER works with no hardware.

Advanced: `psu_app` still accepts `--driver=<id> --view=<id> --port=<dev>
[--baud=<n>]` directly for scripting; `psu_app --list` prints every built-in
driver and view id.

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
