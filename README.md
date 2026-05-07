# FRANK Quest

ScummVM-compatible adventure game engine for the Raspberry Pi Pico 2 (RP2350) with HSTX HDMI output, SD card game browser, I2S / HDMI audio, PS/2 keyboard and mouse, and USB HID keyboard and mouse.

Derived from [Cabal](https://github.com/project-cabal/cabal) — a community-maintained fork of [ScummVM](https://www.scummvm.org/) by Eugene Sandulenko, Max Horn, Travis Howell and many other contributors.

## Screenshots

| | |
|---|---|
| ![Boot intro](screenshots/intro.png) | ![Game selector](screenshots/loader.png) |
| Demoscene-style boot intro | SD card game selector |
| ![Gobliiins](screenshots/gobliins.png) | ![Legend of Kyrandia](screenshots/kyra.png) |
| Gobliiins (GOB) | Legend of Kyrandia (KYRA) |
| ![Leisure Suit Larry](screenshots/larry.png) | ![Full Throttle](screenshots/throttle.png) |
| Leisure Suit Larry (SCI) | Full Throttle (SCUMM v7) |
| ![Full Throttle](screenshots/throttle2.png) | |
| Full Throttle — action sequence | |

## Supported Engines

FRANK Quest currently builds with the following ScummVM engines enabled and verified on hardware:

| Engine  | Games |
|---------|-------|
| **AGI**   | Sierra AGI titles (King's Quest I–III, Space Quest I–II, Leisure Suit Larry I, Police Quest I, Manhunter, Mixed-Up Mother Goose, …) |
| **SCI**   | Sierra SCI0 / SCI1 titles (King's Quest IV–VI, Space Quest III–V, Leisure Suit Larry II–V, Police Quest II–III, Quest for Glory I–III, …) |
| **SCUMM** | Lucasfilm SCUMM v1–v7 (Maniac Mansion, Zak McKracken, Indiana Jones and the Last Crusade, Loom, The Secret of Monkey Island, Monkey Island 2, Indiana Jones and the Fate of Atlantis, Day of the Tentacle, Sam & Max Hit the Road, Full Throttle) |
| **GOB**   | Coktel Vision games (Gobliiins, Gobliins 2, Goblins 3, Bargon Attack, Lost in Time, Ween, …) |
| **KYRA**  | Westwood Studios Kyrandia 1 (Legend of Kyrandia) and Hand of Fate |

> SCUMM v7's Full Throttle is supported (iMUSE Digital + SMUSH). The Dig and Curse of Monkey Island (SCUMM v7/v8) are not yet built.

## Hardware Platform

FRANK Quest targets the **RP2350** with two board layouts shared with the broader FRANK / Murmulator ecosystem:

| Board variant | Hardware                                                                 |
|---------------|--------------------------------------------------------------------------|
| **M2**        | [FRANK](https://rh1.tech/projects/frank?area=about) / [Murmulator 2.0](https://murmulator.ru) |
| **M1**        | Murmulator 1.x                                                           |

Select the variant at build time:

```bash
./build.sh M2          # FRANK / Murmulator 2.0
./build.sh M1          # Murmulator 1.x
```

The active layout drives all GPIO assignments (HDMI, SD, PS/2, I2S, PSRAM CS) via `src/board_config.h`.

## Features

- HSTX HDMI video, 320×200 letterboxed to 320×240, double-buffered framebuffer in SRAM
- I2S and HDMI audio output paths (TDA1387 / PCM5102 over I2S, audio frames embedded in HDMI)
- SD card game browser
- ScummVM-compatible save/load with state stored on the SD card
- PS/2 keyboard and mouse — both run on a single shared PIO program (`drivers/ps2/ps2.pio`)
- USB HID keyboard and mouse via TinyUSB host (`drivers/usbhid/`), including host-side typematic key repeat
- Full PSRAM-backed heap via a custom dlmalloc, 8 MB minimum
- 8 MB QSPI PSRAM auto-detected for both RP2350A (GPIO 8 / 19) and RP2350B (GPIO 47) packages
- Configurable CPU / PSRAM / Flash overclock at build time (504 / 133 / 66 MHz default for M2)

## Hardware Requirements

- **Raspberry Pi Pico 2 (RP2350)** or compatible RP2350A / RP2350B board
- **8 MB QSPI PSRAM** (mandatory — see below)
- **HDMI connector** wired through 270 Ω resistors (the RP2350 drives HDMI directly via HSTX)
- **SD card module** (1-bit SD over PIO, see SD pin map)
- **PS/2 keyboard and mouse** *(M2 default)* — or —
- **USB keyboard and mouse** via the native USB port (`usb-hid` build flag)
- **I2S DAC module** (e.g. TDA1387, PCM5102) for line-level audio out

> **Note:** USB HID and the USB serial console are mutually exclusive. When `usb-hid` is enabled, the native USB port is used for the host stack and debug logging is rerouted to UART.

### PSRAM is mandatory

ScummVM's runtime — heap-allocated game state, decoded resources, mixer ring buffers, font caches — does not fit in the RP2350's 520 KB of SRAM. FRANK Quest installs a custom dlmalloc backed entirely by external PSRAM (`drivers/psram_init.c`, `drivers/dlmalloc.c`). Without PSRAM the firmware will not boot.

You can obtain PSRAM-equipped hardware in several ways:

1. **Solder a PSRAM chip on top of the flash chip** of an RP2350 clone (SOP-8 flash chips are only common on clones, not the original Pico 2)
2. **Build a [Nyx 2](https://rh1.tech/projects/nyx?area=nyx2)** — a DIY RP2350 board with integrated PSRAM
3. **Buy a [Pimoroni Pico Plus 2](https://shop.pimoroni.com/products/pimoroni-pico-plus-2?variant=42092668289107)** — a ready-made Pico 2 with 8 MB PSRAM

## Pin Assignment

> Both layouts are defined in `src/board_config.h`. SD pins are also exported through `drivers/sdcard/sdcard.h`.

| Subsystem     | Signal | M1 GPIO | M2 GPIO |
|---------------|--------|--------:|--------:|
| HDMI          | CLK−   | 6       | 12      |
|               | CLK+   | 7       | 13      |
|               | D0−    | 8       | 14      |
|               | D0+    | 9       | 15      |
|               | D1−    | 10      | 16      |
|               | D1+    | 11      | 17      |
|               | D2−    | 12      | 18      |
|               | D2+    | 13      | 19      |
| SD card       | CLK    | 2       | 6       |
|               | CMD    | 3       | 7       |
|               | D0     | 4       | 4       |
|               | D3     | 5       | 5       |
| PS/2 keyboard | CLK    | 0       | 2       |
|               | DATA   | 1       | 3       |
| PS/2 mouse    | CLK    | 14      | 0       |
|               | DATA   | 15      | 1       |
| I2S audio     | DATA   | 26      | 9       |
|               | BCLK   | 27      | 10      |
|               | LRCLK  | 28      | 11      |

### PSRAM (auto-detected)

| Chip Package | CS GPIO     |
|--------------|-------------|
| RP2350B      | 47          |
| RP2350A (M1) | 19          |
| RP2350A (M2) | 8           |

## How to Use

### SD card setup

1. Format an SD card as **FAT32**.
2. Create a top-level directory called `quest`.
3. Copy each game's data files into a subdirectory of `quest/`. ScummVM-style layout works — for example `quest/monkey1/`, `quest/kq4/`, `quest/loom/`.
4. Insert the SD card and power on the device.

### Boot flow

1. **Welcome screen** — short demoscene-style intro with logo, version, ticker, and sinescroll.
2. **Game selector** — animated list of detected games on the SD card. Use cursor keys or mouse to choose a game; press **Enter** or click to launch.
3. **Game loop** — the chosen ScummVM engine takes over, just like running ScummVM on a desktop.

The selector loads game directories from `/quest/*` on the SD card lazily — no preloading, no global manifest.

### In-game

| Key                                | Action |
|------------------------------------|--------|
| Cursor keys / mouse                | Game input (engine-specific) |
| **Esc**                            | Skip dialogue / cutscene |
| **Space**                          | Pause |

(Bindings follow upstream ScummVM defaults; some engines ignore individual keys.)

## Building

### Prerequisites

1. Install the [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (version 2.0+)
2. Set the environment variable: `export PICO_SDK_PATH=/path/to/pico-sdk`
3. Install the ARM GCC toolchain (`arm-none-eabi-gcc`)
4. Install `cmake` and `picotool`

### Build

```bash
git clone https://github.com/rh1tech/frank-quest.git
cd frank-quest
./build.sh                       # M2, 504 / 133 / 66 MHz, PS/2 input
./build.sh M1                    # Murmulator 1.x layout
./build.sh M2 504 133 66 usb-hid # M2 with USB HID keyboard / mouse
./build.sh M2 252 100 50         # Conservative timings
./build.sh M1 clean              # Clean build directory first
```

Output: `build/frank-quest.uf2` and `build/frank-quest.elf`.

### Build options

`build.sh [BOARD] [CPU_MHZ] [PSRAM_MHZ] [FLASH_MHZ] [usb-hid] [clean]`

| Slot         | Values                          | Default | Notes |
|--------------|---------------------------------|---------|-------|
| `BOARD`      | `M1`, `M2`                      | `M2`    | Selects pin layout |
| `CPU_MHZ`    | `252`, `378`, `504`             | `504`   | Auto-selects core voltage (1.50 / 1.60 / 1.65 V) |
| `PSRAM_MHZ`  | `84`, `100`, `133`, `166`       | `133`   | Hard cap on QMI PSRAM bus |
| `FLASH_MHZ`  | any MHz value                   | `66`    | Hard cap on QMI flash bus |
| `usb-hid`    | flag                            | off     | USB HID keyboard / mouse, UART for console |
| `clean`      | flag                            | off     | `rm -rf ./build` before configure |

### Flashing

```bash
# With the device in BOOTSEL mode:
./flash.sh

# Or manually:
picotool load -f build/frank-quest.uf2
picotool reboot -f
```

## Troubleshooting

### Device won't boot / blank HDMI

- Confirm the board has 8 MB PSRAM and that the right CS pin is wired (GPIO 47 for RP2350B, 8 / 19 for RP2350A).
- Confirm HDMI is wired through 270 Ω resistors on the correct GPIOs for the chosen board variant.
- Drop CPU and PSRAM speeds: `./build.sh M2 252 100 50`. PSRAM at 166 MHz is corner-case sensitive to wiring.

### USB HID keyboard / mouse not detected

- Re-flash with the `usb-hid` flag — `./build.sh M2 504 133 66 usb-hid`.
- USB HID and the USB serial console cannot coexist; debug output goes to UART (default 115200 baud) when USB HID is enabled.
- Some hubs and "smart" keyboards report report descriptors that need extra parsing; the generic descriptor parser is in `drivers/usbhid/hid_app.c` if a device misbehaves.

### Slow boot or game scan

- Keep `quest/` to a reasonable number of games. The selector enumerates the SD card on each boot and probes detector heuristics for each subdirectory.

## License

Copyright © 2026 Mikhail Matveev <<xtreme@rh1.tech>>

Code authored for FRANK Quest is licensed under the **GNU General Public License, version 3 or later (GPL-3.0-or-later)**. See [LICENSE](LICENSE) for the full GPLv3 text.

The bulk of `src/` and parts of `drivers/sdcard/` are derived from upstream **Cabal** (a fork of ScummVM) and remain under their original ScummVM license terms — primarily **GPL-2.0-or-later**, with **LGPL-2.1-or-later**, **BSD**, and **GPLv3-with-font-exception** parts as noted per file. The upstream Cabal/ScummVM license texts are preserved verbatim in this repository:

| File                                  | What it covers |
|---------------------------------------|----------------|
| [COPYING](COPYING)                    | GNU GPL v2 — primary ScummVM / Cabal license |
| [COPYING.LGPL](COPYING.LGPL)          | GNU LGPL v2.1 — selected ScummVM components |
| [COPYING.BSD](COPYING.BSD)            | BSD-style — selected ScummVM components (e.g. NDS port, MPEG decoder) |
| [COPYING.FREEFONT](COPYING.FREEFONT)  | GNU GPL v3 with font embedding exception — bundled GNU FreeFont files |
| [COPYRIGHT](COPYRIGHT)                | Cabal / ScummVM contributor list |

Each source file carries an SPDX license identifier in its header. When in doubt, the file header is authoritative; the root `LICENSE` covers original FRANK Quest code only, while upstream code retains its original ScummVM-derived terms.

Selected non-Cabal third-party components carry their own headers:

| Component                            | Path                          | License |
|--------------------------------------|-------------------------------|---------|
| dlmalloc (Doug Lea)                  | `drivers/dlmalloc.c`          | Public domain (CC0) |
| pico_fatfs SD card / PIO SPI         | `drivers/sdcard/`             | BSD-2-Clause (Elehobica) |
| `Ps2Kbd_Mrmltr` (PS/2 PIO driver)    | `drivers/ps2/kbd/`            | GPL-2.0-or-later (mrmltr) |
| TinyUSB HID host glue                | `drivers/usbhid/hid_app.c`    | MIT (TinyUSB) — see file header |

## Acknowledgments

This project rests on the work of many others:

| Project | Author(s) | License | Used For |
|---------|-----------|---------|----------|
| [Cabal](https://github.com/project-cabal/cabal) | Project Cabal contributors | GPL-2.0-or-later | Engine code (AGI, SCI, SCUMM, GOB, KYRA) and ScummVM common runtime |
| [ScummVM](https://www.scummvm.org/) | The ScummVM team | GPL-2.0-or-later | Upstream of Cabal — adventure game engines, common runtime, GUI |
| [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) | Raspberry Pi Foundation | BSD-3-Clause | Hardware abstraction layer, HSTX, PIO, DMA, multicore |
| [TinyUSB](https://github.com/hathach/tinyusb) | Ha Thach | MIT | USB HID host driver |
| [pico_fatfs](https://github.com/elehobica/pico_fatfs_test) | Elehobica | BSD-2-Clause | SD card PIO-SPI driver |
| [FatFs](http://elm-chan.org/fsw/ff/) | ChaN | Custom permissive | FAT32 filesystem |
| [dlmalloc](http://gee.cs.oswego.edu/dl/html/malloc.html) | Doug Lea | Public domain | PSRAM heap allocator |

Special thanks to:

- The Cabal maintainers for keeping ScummVM's pre-1.x engines portable
- Eugene Sandulenko, Max Horn, Travis Howell, and the wider ScummVM team for two decades of adventure-game preservation
- DnCraptor and the Murmulator community for the M1/M2 hardware reference designs
- The Raspberry Pi Foundation for the RP2350 and Pico SDK
- Sierra On-Line and LucasArts for the original games

## Author

Mikhail Matveev <<xtreme@rh1.tech>>

[https://rh1.tech](https://rh1.tech) | [GitHub](https://github.com/rh1tech/frank-quest)
