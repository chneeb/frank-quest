# FRANK Quest → PicoCalc port (plan)

Target: run FRANK Quest on **ClockworkPi PicoCalc** with a **Pimoroni Pico Plus 2 (RP2350B)**,
replacing HSTX HDMI with the PicoCalc's SPI TFT, I2S with PWM audio, and PS/2/USB input with the
PicoCalc's I2C keyboard. Status: **board variant done (`./build.sh PICOCALC` links); no PicoCalc
peripheral driver written yet.**

Two reference implementations, and they disagree in ways that matter:

- `~/Source/shapones` (`samples/v3/picocalc.{cpp,hpp,pio}`) — **the better source for the display.**
  PIO SPI (2 instructions, side-set SCK) + DMA at **75 MHz**, 16-bit colour. Proven on hardware.
- `~/Source/freesci-archive` (`src/platform/pico/hw/lcdspi/`, `.../hw/i2ckbd/`, `hw_config.c`) —
  hardware SPI at 25 MHz. Still the reference for the **I2C keyboard** and SD; for the LCD, prefer
  shapones. The keyboard wrapper is not vendorable as-is (see "Keyboard" below).

## Why this port is a good fit

| | Fits well | Because |
|---|---|---|
| Resolution | ✅ | 320×200 lands 1:1 in a 320×320 panel — no scaling, 60 px bars top/bottom |
| Frame rate | ✅ | Adventure games are fine at 15–20 fps; there is no 60 fps wall |
| Screen churn | ✅ | Mostly static rooms → dirty-rect updates are a large win |
| Keyboard | ✅ | ScummVM wants a real keyboard (parser input, save names, F5); PicoCalc has one |
| Pointing device | ❌ | PicoCalc has none, and SCUMM v5+/SCI1/KYRA/GOB are point-and-click |

The mouse is the only genuinely new design problem. Everything else is driver swaps.

## Hardware / pin map

Pimoroni Pico Plus 2 in the PicoCalc's Pico bay. PSRAM is on-package (QMI CS1, GPIO47 via
`get_psram_pin()`), so it does **not** collide with anything on the PicoCalc header.

| Function | Bus | Pins |
|---|---|---|
| TFT (ILI9488-class, 320×320) | spi1 | SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RST 15 |
| Keyboard (STM32 MCU, addr `0x1F`) | i2c1 | SDA 6, SCL 7 |
| SD card | spi0 | MISO 16, CS 17, SCK 18, MOSI 19 (exact hardware-SPI0 function pins) |
| Audio | PWM | 26 (L), 27 (R) |
| Free | — | 0,1 (UART console), 2–5, 8, 9, 21, 22, 28 |

`shapones` declares `PIN_RAM_CS = 20` (`samples/v3/picocalc.hpp:19`) — a second device sharing the
LCD's SPI bus. Unconfirmed whether that is the PicoCalc itself or a shapones-specific add-on, so
**treat GPIO 20 as taken until checked.**

HSTX/DVI is unused on PicoCalc, which frees **PIO0** and **DMA_IRQ_0**.

### Physical checks before committing
- Does the Pico Plus 2's USB-C and Qwiic/JST-SH connector clear the PicoCalc's Pico bay?
- Does the case cutout allow an OTG adapter, if USB HID mouse support is kept?

## Work items

### 1. Board variant `M4` / `PICOCALC`
`src/board_config.h` already has the M1/M2 variant machinery — add a third block with the pin map
above. `CMakeLists.txt` + `build.sh` need the variant accepted alongside M1/M2 (`build.sh` validates
the board string explicitly).

Overclock/PSRAM/flash setup is unchanged: `vreg_disable_voltage_limit()` → `vreg_set_voltage()` →
`set_flash_timings()` → `set_sys_clock_khz()` (`src/main.c:95-109`). Default stays 504 MHz / 1.65 V /
PSRAM 133 / flash 66. **Re-validate stability at 504 MHz** with SPI DMA + I2C running; step down to
378 if the panel or keyboard misbehaves.

### 2. Display — `drivers/LCD_picocalc.c`
Implement the existing `graphics_*` API from `drivers/HDMI.h` (`graphics_init`, `graphics_set_buffer`,
`graphics_set_res`, `graphics_set_palette`, `graphics_set_shift`, …) against the panel, so nothing
above the driver changes. Vendor `lcdspi.c` for init/`define_region_spi`/`hw_send_spi`.

- **Framebuffers are in PSRAM**, not SRAM (`src/main.c:169-170`, `psram_get_framebuffer()` /
  `psram_get_framebuffer_back()`). Do **not** DMA the panel directly from PSRAM. Convert
  8bpp palette → RGB into a small ping-pong **SRAM line buffer** and DMA that. `320 × 3 = 960 B`
  per line buffer.
- **Pixel format: settled — use `0x3A = 0x65`, 16-bit, 2 bytes/pixel.** shapones runs exactly this
  on hardware (`picocalc.cpp:208`, commented `0x65=16 bit colour for SPI,0x66=18bits`). `0x3A`
  splits into DPI (bits 6:4) and DBI (bits 2:0); `0x65` leaves the RGB side 18-bit and sets the
  SPI/MCU side to 16-bit, which is the half that matters. The ILI9488 datasheet's "SPI is 18-bit
  only" does not hold for this ST7365P-class panel. freesci's `0x66` (3 bytes/pixel) is simply
  leaving half the bandwidth on the table.
- **SPI clock: 75 MHz is proven**, not the 50 MHz originally assumed here and not freesci's 25.
  shapones drives the panel from a **2-instruction PIO SPI program plus DMA**, not hardware SPI
  (`picocalc.pio`, `setup_pio()`), at `SYS_CLK_FREQ / 4` with `SYS_CLK_FREQ = 300 MHz`. Note the
  divisor is relative to the system clock: at FRANK Quest's 504 MHz, `/4` would be 126 MHz, so pick
  the divisor for a *target frequency* (504 / 2 / 75 ≈ 3.36) rather than copying `/4`.
- Full frame at that config = 320×200×2 = **128 KB ≈ 14 ms @ 75 MHz → ~73 fps**, before any
  dirty-rect work. **The panel is not the bottleneck.** Dirty rects (§3) remain worth doing for
  the engine-side and PSRAM-read savings, but they are no longer load-bearing for playability.
- Blit 1:1 at `y+60` (320×200 centred in 320×320). No scaling — it costs bandwidth and looks worse.
- Run the SPI push from **core 1** so it overlaps the engine on core 0.

### 3. Dirty-rect tracking (the main perf lever)
`OSystem_RP2350::updateScreen()` (`src/backends/platform/rp2350/rp2350-system.cpp:268`) currently
blits the **whole** surface every frame. `copyRectToScreen()` (`:240`) already receives the rects —
accumulate a union dirty rect there, consume and reset it in `updateScreen()`, and push only that
region over SPI.

**Cursor gotcha:** the cursor is composited into the framebuffer by the `cabal_*` layer
(`cabal_set_mouse_cursor` / `cabal_set_mouse_pos`, `rp2350-system.cpp:365-402`). A cursor move must
dirty **old rect ∪ new rect**, or you leave trails. This is the classic bug in this kind of port.

Worst cases that will still hurt regardless: room fades and Full Throttle's SMUSH cutscenes.

### 4. Keyboard — `drivers/picocalc_kbd.c`
I2C1 @ `0x1F`. The backend already consumes a `cabal_event_t` queue carrying `kbd.keycode`,
`kbd.ascii` and `CABAL_MOD_SHIFT|CTRL|ALT` (`rp2350-system.cpp:461-476`), so the new driver just
feeds that queue — no changes above the shim.

**Do not reuse freesci's `kbd_input.c` as-is.** It deliberately collapses everything to characters,
drops key-up entirely, and relies on the keyboard firmware pre-combining Shift+F1→F6. ScummVM needs
real **key-down / key-up plus live modifier state**. The PicoCalc keyboard MCU does report
press/hold/release over I2C — write a richer wrapper against that.

Keys that must work: Esc, F1–F10 (esp. **F5** = ScummVM menu), Enter, Backspace, arrows, full ASCII.

### 5. Mouse emulation (new — no upstream equivalent)
PicoCalc has no pointing device. Build a keyboard-driven cursor that synthesizes mouse events into
the same `cabal_event_t` queue that `warpMouse()` already drives:
- arrows move; **hold-to-accelerate** (start ~1 px/tick, ramp to ~8) — without accel it is unusable
- Enter = LMB, Alt (or right-shift) = RMB
- keep the existing **USB HID mouse** path working as an optional better experience via OTG

Scope note: **AGI is fully keyboard-driven** and SCUMM v1–v4 are workable, so a keyboard-only build
already covers a real slice of the library. Point-and-click titles need the emulated cursor.

### 6. Audio — PWM on GPIO 26/27
Keep the whole ping-pong DMA / pre-roll / IRQ-rearm scaffold in `drivers/audio.c`; replace only the
sink: PIO I2S → two PWM slices fed by a **DMA-timer-paced** channel at the mixer rate, 8→10-bit
conversion. Mono-summing to one channel is acceptable if DMA channels get tight.

Easier than the equivalent frank-snes swap (a ScummVM mixer callback tolerates far more latency than
an emulated APU), and core 1 gets its HDMI-encoding budget back, which pays for iMUSE/SMUSH/MIDI.

### 7. SD card
`drivers/sdcard/sdcard.h` pins are all `#ifndef`-overridable and the bus already defaults to `spi0`.
Point them at 16/17/18/19 in the new board block. **No driver change.**

## Suggested order

1. ~~`0x3A` spike~~ — **answered from shapones without hardware: `0x65`, 16-bit, 75 MHz.**
2. ~~Board variant block + build plumbing~~ — **done**; `./build.sh PICOCALC` configures and links,
   HDMI and PS/2 are compiled out, boots to UART console. Not yet run on hardware.
3. SD pins → game selector reachable
4. LCD driver, full-frame push (correct first) → picture on screen
5. I2C keyboard → menus and AGI/parser games playable
6. Dirty-rect tracking → lower CPU and PSRAM load (no longer needed for frame rate)
7. Mouse emulation → point-and-click games playable
8. PWM audio

Steps 4 and 6 are separable on purpose: get it correct, then get it fast.

## Open questions

- ~~Does RGB565 work on this panel?~~ **Yes — `0x3A = 0x65`, proven in shapones.**
- ~~Max stable SPI clock?~~ **75 MHz proven in shapones** (PIO + DMA). Whether it goes higher is
  untested and not worth chasing: 75 MHz already yields ~73 fps full-frame.
- 504 MHz stability with SPI DMA + I2C live. shapones runs 300 MHz, so this is still ours to prove.
- Is GPIO 20 (`PIN_RAM_CS` in shapones) claimed on a stock PicoCalc, or is that an add-on?
- RP2350 E9 / GPIO pull-down erratum on the I2C keyboard lines — did freesci need anything special?
- Physical fit of the Pico Plus 2 in the bay (see above).

## Cross-project note

`~/Source/frank-snes` is a candidate for the same treatment, and the LCD driver, PWM audio, SD pins
and keyboard wrapper built here are ~80% reusable there. frank-quest is the easier of the two
(native resolution, real keyboard, no 60 fps requirement); frank-snes hits a hard SPI bandwidth wall
that this port does not.
