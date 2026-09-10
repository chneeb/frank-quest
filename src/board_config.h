/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/structs/sysinfo.h"
#include "hardware/vreg.h"

/*
 * Board Configuration Variants for FRANK Quest (ScummVM port for RP2350)
 *
 * BOARD_M1       - M1 GPIO layout
 * BOARD_M2       - M2 GPIO layout (default)
 * BOARD_PICOCALC - ClockworkPi PicoCalc (build.sh also accepts M4 as an alias)
 *
 * PSRAM pin is auto-detected based on chip package:
 *   RP2350B: GPIO47 (for M1, M2 and PicoCalc)
 *   RP2350A: GPIO19 (M1) or GPIO8 (M2); N/A on PicoCalc (Pico Plus 2 is a B)
 *
 * M1 GPIO Layout:
 *   HDMI: CLKN=6, CLKP=7, D0N=8, D0P=9, D1N=10, D1P=11, D2N=12, D2P=13
 *   SD:   CLK=2, CMD=3, DAT0=4, DAT3=5
 *   PS/2 Keyboard: CLK=0, DATA=1
 *   PS/2 Mouse: CLK=14, DATA=15
 *   I2S:  DATA=26, CLK=27, LRCK=28
 *
 * M2 GPIO Layout:
 *   HDMI: CLKN=12, CLKP=13, D0N=14, D0P=15, D1N=16, D1P=17, D2N=18, D2P=19
 *   SD:   CLK=6, CMD=7, DAT0=4, DAT3=5
 *   PS/2 Keyboard: CLK=2, DATA=3
 *   PS/2 Mouse: CLK=0, DATA=1
 *   I2S:  DATA=9, CLK=10, LRCK=11
 *
 * PicoCalc GPIO Layout (Pimoroni Pico Plus 2 in the PicoCalc's Pico bay):
 *   TFT:  spi1 SCK=10, MOSI=11, MISO=12, CS=13, DC=14, RST=15
 *   KBD:  i2c1 SDA=6, SCL=7 (STM32 keyboard MCU at 0x1F)
 *   SD:   spi0 MISO=16, CS=17, SCK=18, MOSI=19
 *   Audio: PWM L=26, R=27
 *   No HDMI, no PS/2. See PICOCALC_PORT.md.
 *
 * CPU/PSRAM Speed (set via CMake -DCPU_SPEED=xxx -DPSRAM_SPEED=xxx):
 *   252 MHz - no overclock (default for stable operation)
 *   378 MHz - medium overclock
 *   504 MHz - high overclock
 */

// Default to M2 if no config specified
#if !defined(BOARD_M1) && !defined(BOARD_M2) && !defined(BOARD_PICOCALC)
#define BOARD_M2
#endif

//=============================================================================
// CPU/PSRAM Speed Defaults (can be overridden via CMake)
//=============================================================================
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 252
#endif

#ifndef CPU_VOLTAGE
#define CPU_VOLTAGE VREG_VOLTAGE_1_50
#endif

#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 133
#endif

//=============================================================================
// PSRAM Pin Auto-Detection
//=============================================================================

// PSRAM pin for RP2350A variants
#if defined(BOARD_M1)
#define PSRAM_PIN_RP2350A 19
#elif defined(BOARD_PICOCALC)
// The Pico Plus 2 is always an RP2350B with on-package PSRAM on QMI CS1, so
// this branch is unreachable there; defined only to keep the macro total.
#define PSRAM_PIN_RP2350A 47
#else
#define PSRAM_PIN_RP2350A 8
#endif

// PSRAM pin for RP2350B (always GPIO47)
#define PSRAM_PIN_RP2350B 47

// Runtime function to get PSRAM pin based on chip package
static inline uint get_psram_pin(void) {
    // Check if RP2350A (bit 0 set) or RP2350B (bit 0 clear)
    uint32_t package_sel = *((io_ro_32*)(SYSINFO_BASE + SYSINFO_PACKAGE_SEL_OFFSET));
    if (package_sel & 1) {
        // RP2350A - use board-specific pin
        return PSRAM_PIN_RP2350A;
    } else {
        // RP2350B - always GPIO47
        return PSRAM_PIN_RP2350B;
    }
}

//=============================================================================
// M1 Layout Configuration
//=============================================================================
#ifdef BOARD_M1

// HDMI Pins
#define HDMI_PIN_CLKN 6
#define HDMI_PIN_CLKP 7
#define HDMI_PIN_D0N  8
#define HDMI_PIN_D0P  9
#define HDMI_PIN_D1N  10
#define HDMI_PIN_D1P  11
#define HDMI_PIN_D2N  12
#define HDMI_PIN_D2P  13

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    2
#define SDCARD_PIN_CMD    3
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  0
#define PS2_PIN_DATA 1

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  14
#define PS2_MOUSE_DATA 15

// I2S Audio Pins
#define I2S_DATA_PIN       26
#define I2S_CLOCK_PIN_BASE 27

#endif // BOARD_M1

//=============================================================================
// M2 Layout Configuration
//=============================================================================
#ifdef BOARD_M2

// HDMI Pins
#define HDMI_PIN_CLKN 12
#define HDMI_PIN_CLKP 13
#define HDMI_PIN_D0N  14
#define HDMI_PIN_D0P  15
#define HDMI_PIN_D1N  16
#define HDMI_PIN_D1P  17
#define HDMI_PIN_D2N  18
#define HDMI_PIN_D2P  19

#define HDMI_BASE_PIN HDMI_PIN_CLKN

// SD Card Pins
#define SDCARD_PIN_CLK    6
#define SDCARD_PIN_CMD    7
#define SDCARD_PIN_D0     4
#define SDCARD_PIN_D3     5

// PS/2 Keyboard Pins
#define PS2_PIN_CLK  2
#define PS2_PIN_DATA 3

// PS/2 Mouse Pins
#define PS2_MOUSE_CLK  0
#define PS2_MOUSE_DATA 1

// I2S Audio Pins
#define I2S_DATA_PIN       9
#define I2S_CLOCK_PIN_BASE 10

#endif // BOARD_M2

//=============================================================================
// PicoCalc Layout Configuration
//=============================================================================
#ifdef BOARD_PICOCALC

// No HDMI on PicoCalc: the panel is SPI, driven by drivers/LCD_picocalc.c,
// which CMake substitutes for drivers/HDMI.c on this board. HDMI_PIN_* are
// left undefined; HDMI.c is not compiled here, and 6/7 are the keyboard's
// I2C lines rather than an HDMI pair.

// TFT panel (ILI9488-class 320x320) on spi1
#define LCD_SPI_PORT   spi1
#define LCD_PIN_SCK    10
#define LCD_PIN_MOSI   11
#define LCD_PIN_MISO   12
#define LCD_PIN_CS     13
#define LCD_PIN_DC     14
#define LCD_PIN_RST    15

// Keyboard MCU on i2c1
#define PICOCALC_KBD_I2C_PORT i2c1
#define PICOCALC_KBD_SDA_PIN  6
#define PICOCALC_KBD_SCL_PIN  7
#define PICOCALC_KBD_ADDR     0x1F

// SD Card (spi0; hardware-SPI0 function pins)
#define SDCARD_PIN_CLK    18
#define SDCARD_PIN_CMD    19
#define SDCARD_PIN_D0     16
#define SDCARD_PIN_D3     17

// PWM Audio
#define PICOCALC_AUDIO_PIN_L 26
#define PICOCALC_AUDIO_PIN_R 27

// No PS/2 on PicoCalc. PS2_PIN_* / PS2_MOUSE_* stay undefined; the PS/2 init
// calls are compiled out instead (see rp2350-minimal.cpp, ps2kbd_wrapper.cpp),
// which also leaves PIO0 free for the LCD.

#endif // BOARD_PICOCALC

//=============================================================================
// Display Configuration
//=============================================================================

// Screen resolution for ScummVM games
// AGI: 160x200 (doubled to 320x200)
// GOB: 320x200
// SCI: 320x200 (SCI0/SCI1), 640x480 (SCI1.1+)
// We use 320x200 as base with letterboxing to 320x240 for HDMI
#define CABAL_SCREEN_WIDTH  320
#define CABAL_SCREEN_HEIGHT 200
#define CABAL_HDMI_HEIGHT   240  // With letterboxing

// Double buffer in SRAM (critical for HDMI timing)
// Each buffer: 320 * 240 = 76,800 bytes
// Total: ~154KB for double buffering
#define CABAL_FRAMEBUFFER_SIZE (CABAL_SCREEN_WIDTH * CABAL_HDMI_HEIGHT)

//=============================================================================
// Memory Configuration
//=============================================================================

// PSRAM total size (8MB)
#ifndef CABAL_PSRAM_SIZE_BYTES
#define CABAL_PSRAM_SIZE_BYTES (8u * 1024u * 1024u)
#endif

// Game data directory on SD card
#define CABAL_GAME_DIR "quest"

#endif // BOARD_CONFIG_H
