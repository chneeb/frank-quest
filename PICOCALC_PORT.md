# FRANK Quest → PicoCalc port (plan)

Target: run FRANK Quest on **ClockworkPi PicoCalc** with a **Pimoroni Pico Plus 2 (RP2350B)**,
replacing HSTX HDMI with the PicoCalc's SPI TFT, I2S with PWM audio, and PS/2/USB input with the
PicoCalc's I2C keyboard. Status: **not started — this is the spec, nothing below is implemented.**

Reference implementation for all three PicoCalc peripherals: `~/Source/freesci-archive`
(`src/platform/pico/hw/lcdspi/`, `src/platform/pico/hw/i2ckbd/`, `src/platform/pico/hw_config.c`).
Those files are vendorable more or less as-is for the LCD and SD; the keyboard wrapper is not
(see "Keyboard" below).

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
| Free | — | 0,1 (UART console), 2–5, 8, 9, 20–22, 28 |

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
- Panel is `0x3A = 0x66` → **18-bit, 3 bytes/pixel** over SPI (`lcdspi.c:592`).
  Full frame = 320×200×3 = **192 KB ≈ 31 ms @ 50 MHz SPI**.
  → **Spike first: try `0x3A = 0x55` (16-bit).** The ILI9488 datasheet says SPI is 18-bit only, but
  this panel is ST7365P-class and some PicoCalc firmwares push RGB565. If it takes, bandwidth halves.
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

1. `0x3A = 0x55` spike (30 min) — decides the display budget
2. Board variant block + build plumbing, boot to UART console at 504 MHz
3. SD pins → game selector reachable
4. LCD driver, full-frame push (slow but correct) → picture on screen
5. I2C keyboard → menus and AGI/parser games playable
6. Dirty-rect tracking → usable frame rate
7. Mouse emulation → point-and-click games playable
8. PWM audio

Steps 4 and 6 are separable on purpose: get it correct, then get it fast.

## Open questions

- Does `0x3A = 0x55` (RGB565) work on this panel? Decides everything about display feel.
- Max stable SPI clock for the panel (freesci runs 25 MHz; 50 MHz is commented out in `lcdspi.h`).
- 504 MHz stability with SPI DMA + I2C live.
- RP2350 E9 / GPIO pull-down erratum on the I2C keyboard lines — did freesci need anything special?
- Physical fit of the Pico Plus 2 in the bay (see above).

## Cross-project note

`~/Source/frank-snes` is a candidate for the same treatment, and the LCD driver, PWM audio, SD pins
and keyboard wrapper built here are ~80% reusable there. frank-quest is the easier of the two
(native resolution, real keyboard, no 60 fps requirement); frank-snes hits a hard SPI bandwidth wall
that this port does not.
