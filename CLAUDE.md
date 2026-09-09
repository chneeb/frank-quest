# FRANK Quest — Claude Code Guide

ScummVM (via [Cabal](https://github.com/project-cabal/cabal)) for the Raspberry Pi Pico 2 / RP2350.
HSTX HDMI video, SD card game browser, I2S audio, PS/2 + USB HID input.
Engines: AGI, SCI, SCUMM v1–v7, GOB, KYRA.

## Build

```bash
export PICO_SDK_PATH=/home/chneeb/Source/pico-sdk
./build.sh [M1|M2] [CPU_MHZ] [PSRAM_MHZ] [FLASH_MHZ] [usb-hid] [clean]
# Defaults: M2 504 133 66
```

Flash with `./flash.sh` or drag-and-drop in BOOTSEL mode.

Note the CMake cache defaults (`CPU_SPEED` 252) are *not* what `build.sh` passes (504) — builds go
through `build.sh`.

## Board variants

| Variant | Hardware |
|---------|----------|
| M2 | FRANK / Murmulator 2.0 (default) |
| M1 | Murmulator 1.x |

All GPIO assignments live in `src/board_config.h`.

## Overclocking

Default is **504 MHz**. Three-part recipe in `src/main.c:95-109`, and the order matters — all of it
runs before `stdio_init_all()`, because USB must enumerate at the final clock:

1. `vreg_disable_voltage_limit()` + `vreg_set_voltage(CPU_VOLTAGE)` — CMake picks the voltage from
   `CPU_SPEED`: ≥504 → 1.65 V, ≥378 → 1.60 V, else 1.50 V.
2. `set_flash_timings()` (`src/main.c:71-92`) recomputes QMI `CLKDIV`/`RXDELAY` on `qmi_hw->m[0]` to
   hold flash at `FLASH_MAX_FREQ_MHZ` (default 66). It is `__no_inline_not_in_flash_func` so it does
   not execute from the flash it is reconfiguring.
3. `set_sys_clock_khz()`. PSRAM then re-derives its own divisor from the new `clk_sys` inside
   `psram_init()` (`drivers/psram_init.c`), so it stays at its cap independently of the core clock.

Sibling project `~/Source/frank-snes` shares this code almost verbatim — `drivers/psram_init.c` is
byte-identical there apart from the license header, so it is a mirror, not a source of fixes.

Still open here: `psram_init()` has no **QPI-exit (0xF5) preamble**, so a watchdog/panic reset
without a power cycle can leave the PSRAM stuck in QPI mode. Neither project implements one — this
is code to write, not to backport.

## Architecture

- `src/main.c` — entry, overclock/PSRAM/flash setup, framebuffer allocation, HDMI init
- `src/frank_quest_main.cpp` — engine bring-up and event loop
- `src/backends/platform/rp2350/rp2350-system.cpp` — the `OSystem` backend; talks to hardware only
  through a `cabal_*` C shim and a `cabal_event_t` queue (keyboard + absolute mouse)
- `drivers/HDMI.{c,h}` — video; `graphics_*` is the API everything above the driver uses
- `drivers/audio.c` — I2S ping-pong DMA
- `drivers/psram_init.c`, `drivers/psram_allocator.c` — QMI PSRAM (framebuffers live here, not SRAM)
- `drivers/sdcard/` — SD over SPI; pins are `#ifndef`-overridable
- `drivers/ps2/`, `drivers/usbhid/` — input

Framebuffers are **PSRAM-resident** (`src/main.c:169-170`) — relevant to any driver that reads them
per-frame.

## In-flight work

- [PicoCalc port plan](PICOCALC_PORT.md) — spec for running on ClockworkPi PicoCalc hardware
  (SPI TFT + I2C keyboard + PWM audio). Not started; read before touching drivers for that target.
