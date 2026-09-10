# FRANK Quest → PicoCalc port (plan)

Target: run FRANK Quest on **ClockworkPi PicoCalc** with a **Pimoroni Pico Plus 2 (RP2350B)**,
replacing HSTX HDMI with the PicoCalc's SPI TFT, I2S with PWM audio, and PS/2/USB input with the
PicoCalc's I2C keyboard. Status: **display, keyboard, SD and audio written; AGI, SCI and SCUMM all
run on real hardware, and the emulated cursor makes SCUMM playable.** Remaining: dirty rects (§3).

Confirmed working on hardware: SPI TFT, I2C keyboard, SD card and the game selector, and AGI, SCI
and SCUMM games running. SCI in particular loads noticeably faster and plays more smoothly here than
in `~/Source/freesci-archive` on the same hardware. Now checked, and it is **not** PSRAM — both use
memory-mapped QMI. freesci never overclocks at all (`set_sys_clock_khz(133000, true)`,
`src/platform/pico/pico_main.c:166`), so it runs at the RP2350 default: **3.8× less core clock**. Its
panel adds ~4.5× on top, at 25 MHz hardware SPI and `0x3A = 0x66` (3 bytes/pixel) against our 75 MHz
PIO+DMA at 16-bit. Written up for that project in
`~/Source/freesci-archive/PICO_PERFORMANCE_VS_FRANK_QUEST.md`.
PWM audio works too (checked in Space Quest 3).

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
| SPI RAM | shared with TFT | CS 20, SCK 21 |
| SD card detect | — | 22 |
| Free | — | 0,1 (UART console), 2–5, 8, 9, 28 |

Pin map confirmed against the mainboard schematic (`~/Source/PicoCalc/clockwork_Mainboard_V2.0_
Schematic.pdf`), header table: `SPI0_RX/CS/SCK/TX` = GP16/17/18/19, `RAM_CS` = GP20, `RAM_SCK` =
GP21, `SD_DET` = GP22, `PWM_L` = GP26, `PWM_R` = GP27. **GPIO 20/21/22 are stock PicoCalc, not an
add-on** — shapones' `PIN_RAM_CS = 20` was right. `SD_DET` on GP22 is a card-detect line nothing
currently uses.

HSTX/DVI is unused on PicoCalc, which frees **PIO0** and **DMA_IRQ_0**.

### Physical checks before committing
- Does the Pico Plus 2's USB-C and Qwiic/JST-SH connector clear the PicoCalc's Pico bay?
- ~~Does the case cutout allow an OTG adapter?~~ Moot — USB host is parked, see §5.

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

### 5. Mouse emulation (new — no upstream equivalent) — **done**, working on hardware
Implemented in `rp2350-minimal.cpp` (`cursor_consume_key` / `cursor_poll_motion`), feeding the same
event path as any other pointer, so nothing above the backend changes.

Arrows move, **Enter = left button, Alt = right button**. Motion runs on a 16 ms wall-clock tick
rather than per poll — ScummVM drains the queue each frame, so per-poll movement would tie cursor
speed to the event-loop rate. Acceleration ramps 1 → 8 px, +1 per 120 ms held; 1 px start keeps
clicking precise, and the ramp is what makes crossing 320 px bearable.

**Enabled per engine** from `dispatchGame()`, not globally — the right answer differs by engine:

| Engine | Emulation | Why |
|---|---|---|
| SCUMM, GOB, KYRA | on | mouse-driven, arrows mostly unused |
| AGI | off | needs arrows *and* Enter for the parser; always-on would break it |
| SCI | off | has its own keyboard cursor already |

**Pause/Break toggles at runtime** for whatever that split gets wrong; no engine in this build binds
that key.

USB HID mouse is **parked, and cannot be the answer here.** A Pico in host mode never generates
VBUS: there is no 5 V regulator or load switch on its USB connector, and the PicoCalc header ties
pin 1 (`VBUS`) to the net feeding U101 pin 37, the PMIC's charging input, rather than a host-side
supply. A bus-powered mouse plugged into the Pico's port therefore gets no power. A powered OTG hub
would work, and defeats the point on a handheld. Verified on hardware: USB mouse does nothing in
SCUMM or SCI on a `usb-hid` build with the HID stack confirmed present in the image.

SCUMM is effectively unplayable without a pointer, so this step is now load-bearing rather than a
nicety. SCI plays acceptably keyboard-only; AGI is keyboard-native.

PicoCalc has no pointing device. Build a keyboard-driven cursor that synthesizes mouse events into
the same `cabal_event_t` queue that `warpMouse()` already drives:
- arrows move; **hold-to-accelerate** (start ~1 px/tick, ramp to ~8) — without accel it is unusable
- Enter = LMB, Alt (or right-shift) = RMB
- keep the existing **USB HID mouse** path working as an optional better experience via OTG

Scope note: **AGI is fully keyboard-driven** and SCUMM v1–v4 are workable, so a keyboard-only build
already covers a real slice of the library. Point-and-click titles need the emulated cursor.

### 6. Audio — PWM on GPIO 26/27 — **done** (`drivers/picocalc_audio.c`)
Provides the same `cabal_audio_*` API as `drivers/audio/audio.c`, which is not compiled on this
board. The engine side is untouched: still 44100 Hz 16-bit stereo from the mixer, still one
`cabal_audio_process_frame()` per frame.

Schematic (V2.0) settles two things the plan had guessed at:

- **Stereo is real.** GP26 (`PWM_L`) and GP27 (`PWM_R`) each go through U501 (NC7WZ16 dual buffer)
  and an identical filter chain to `PCM_AUDIO_L`/`R`, then the SW501 headphone jack with `HP_DET`
  switching to the speaker. No mono-summing needed — shapones' mono output is its own choice.
- **One DMA channel drives both.** GP26/GP27 are channels A and B of the *same* PWM slice, and a
  slice's CC register packs both levels into one 32-bit word. Given the LCD already holds a channel,
  this matters.

The filter is a single pole at **~7.2 kHz** (220R/100nF), which suppresses a 44.1 kHz carrier by
only ~16 dB. The carrier therefore runs at **88.2 kHz** (each mixer sample emitted twice) for ~6 dB
more, which is free at this clock: 10-bit at 88.2 kHz is a clkdiv of ~5.6 at 504 MHz. That same
7.2 kHz corner also rolls off the top of the audio band, and nothing can be done about that.

Confirmed working on hardware (Space Quest 3). The 88.2 kHz carrier choice has not been A/B'd
against 44.1 kHz by ear -- if carrier whine ever shows up, that is the first knob.

### 6b. Game directory naming — AGI needs `quest/agi`
`src/frank_quest_selector.cpp` matches the **exact** directory name against `kDetectors` (case
insensitive, no prefix or content fallback), and the only row mapping to `QuestEngine::Agi` is
literally `agi`. Several AGI-era Sierra titles — `kq1`, `kq4`, `lsl1`, `sq1`, `pq1` — are hardcoded
to `QuestEngine::Sci`, since each name covers both an original AGI release and a later SCI remake.
An AGI game in a directory named after the game is therefore dispatched to the SCI launcher and
fails. Put it in `quest/agi/` — which also means only one AGI game at a time.

Worth fixing at the source: the detector table already supports a `probeFile`, so a `kq1` row could
probe for `logdir` and route to AGI, falling through to the SCI row otherwise. Nothing uses that for
these ambiguous names yet.

### 7. SD card
`drivers/sdcard/sdcard.h` pins are all `#ifndef`-overridable and the bus already defaults to `spi0`.
Point them at 16/17/18/19 in the new board block. **No driver change.**

## Suggested order

1. ~~`0x3A` spike~~ — **answered from shapones without hardware: `0x65`, 16-bit, 75 MHz.**
2. ~~Board variant block + build plumbing~~ — **done**; `./build.sh PICOCALC` configures and links,
   HDMI and PS/2 are compiled out, boots to UART console. Not yet run on hardware.
3. ~~SD pins~~ — **done**; CMake points `SDCARD_PIN_SPI0_*` at 16/17/18/19, no driver change,
   confirmed against the schematic
4. ~~LCD driver, full-frame push~~ — **done** (`drivers/LCD_picocalc.c`) and **working on hardware**
5. ~~I2C keyboard~~ — **done** (`drivers/picocalc_kbd.c`), working on hardware
6. Dirty-rect tracking → lower CPU and PSRAM load (no longer needed for frame rate)
7. ~~Mouse emulation~~ — **done**, working on hardware; SCUMM is playable
8. ~~PWM audio~~ — **done** (`drivers/picocalc_audio.c`), working on hardware

Steps 4 and 6 are separable on purpose: get it correct, then get it fast.

## Open questions

- ~~Does RGB565 work on this panel?~~ **Yes — `0x3A = 0x65`, proven in shapones.**
- ~~Max stable SPI clock?~~ **75 MHz proven in shapones** (PIO + DMA). Whether it goes higher is
  untested and not worth chasing: 75 MHz already yields ~73 fps full-frame.
- 504 MHz stability with SPI DMA + I2C live. shapones runs 300 MHz, so this is still ours to prove.
- ~~Is GPIO 20 claimed on a stock PicoCalc?~~ **Yes — `RAM_CS`, per the schematic. So are GP21
  (`RAM_SCK`) and GP22 (`SD_DET`).**
- RP2350 E9 / GPIO pull-down erratum on the I2C keyboard lines — did freesci need anything special?
- Physical fit of the Pico Plus 2 in the bay (see above).

## Cross-project note

`~/Source/frank-snes` is a candidate for the same treatment, and the LCD driver, PWM audio, SD pins
and keyboard wrapper built here are ~80% reusable there. frank-quest is the easier of the two
(native resolution, real keyboard, no 60 fps requirement); frank-snes hits a hard SPI bandwidth wall
that this port does not.
