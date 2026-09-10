/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * PicoCalc keyboard driver.
 *
 * The PicoCalc's keys are scanned by an STM32 that exposes a small FIFO over
 * I2C at 0x1F. Reading register 0x09 pops one entry: two bytes, [state,
 * keycode], where state is 1 = pressed, 2 = held (auto-repeat), 3 = released.
 * Keycodes and states come from ClockworkPi's own firmware,
 * ~/Source/PicoCalc/Code/picocalc_keyboard/keyboard.h.
 *
 * Two things this does that the obvious reference implementations do not:
 *
 * - freesci-archive's i2ckbd.c sleeps 16 ms between selecting the register
 *   and reading it, which would cost a frame per poll. The register select
 *   and the read are split across two ticks instead, as ~/Source/shapones
 *   does, so neither blocks.
 *
 * - Both references collapse everything to a character and drop key-up
 *   entirely. ScummVM needs real press/release plus live modifier state:
 *   holding Shift while clicking, F5 for the menu, and Ctrl+Alt+Del for the
 *   return-to-selector gesture all depend on it. So modifiers are tracked
 *   here and emitted as their own key events, exactly as the PS/2 and USB
 *   paths do.
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"

#include "board_config.h"
#include "picocalc_kbd.h"

#ifdef BOARD_PICOCALC

//=============================================================================
// Protocol constants (ClockworkPi keyboard firmware)
//=============================================================================

#define KBD_REG_FIFO      0x09

#define KBD_STATE_PRESSED  1
#define KBD_STATE_HOLD     2
#define KBD_STATE_RELEASED 3

#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09
#define KEY_ENTER     0x0A
#define KEY_MOD_ALT   0xA1
#define KEY_MOD_SHL   0xA2
#define KEY_MOD_SHR   0xA3
#define KEY_MOD_SYM   0xA4
#define KEY_MOD_CTRL  0xA5
#define KEY_ESC       0xB1
#define KEY_LEFT      0xB4
#define KEY_UP        0xB5
#define KEY_DOWN      0xB6
#define KEY_RIGHT     0xB7
#define KEY_CAPS_LOCK 0xC1
#define KEY_BREAK     0xD0
#define KEY_INSERT    0xD1
#define KEY_HOME      0xD2
#define KEY_DEL       0xD4
#define KEY_END       0xD5
#define KEY_PAGE_UP   0xD6
#define KEY_PAGE_DOWN 0xD7
#define KEY_F1        0x81
#define KEY_F9        0x89
#define KEY_F10       0x90   // note the gap: F10 is 0x90, not 0x8A

//=============================================================================
// CABAL_* values, duplicated rather than pulled in
//=============================================================================
// rp2350-minimal.h is a C++-leaning backend header; this is a plain C driver
// that only needs a handful of its constants. Kept in sync by hand -- they are
// SDL keycodes and have not moved in twenty years.

#define CABAL_KEY_BACKSPACE  8
#define CABAL_KEY_TAB        9
#define CABAL_KEY_RETURN     13
#define CABAL_KEY_ESCAPE     27
#define CABAL_KEY_DELETE     127
#define CABAL_KEY_UP         273
#define CABAL_KEY_DOWN       274
#define CABAL_KEY_RIGHT      275
#define CABAL_KEY_LEFT       276
#define CABAL_KEY_INSERT     277
#define CABAL_KEY_HOME       278
#define CABAL_KEY_END        279
#define CABAL_KEY_PAGEUP     280
#define CABAL_KEY_PAGEDOWN   281
#define CABAL_KEY_F1         282
#define CABAL_KEY_F10        291
#define CABAL_KEY_PAUSE      19
#define CABAL_KEY_CAPSLOCK   301

// Modifier keycodes. fq_check_ctrl_alt_del() in rp2350-minimal.cpp keys off
// 305 (ctrl) and 307 (alt), so emitting these gives Ctrl+Alt+Del for free.
#define CABAL_KEY_RSHIFT     303
#define CABAL_KEY_LSHIFT     304
#define CABAL_KEY_LCTRL      305
#define CABAL_KEY_LALT       307

#define CABAL_MOD_SHIFT  0x01
#define CABAL_MOD_CTRL   0x02
#define CABAL_MOD_ALT    0x04

//=============================================================================
// Polling cadence
//=============================================================================

// The firmware scans every 16 ms and buffers 10 entries, so there is no need
// to poll hard. One phase every 4 ms gives a full read every 8 ms, comfortably
// inside the FIFO's depth even for fast typing.
#define KBD_PHASE_GAP_US   4000

// Short by design. A missing or wedged keyboard must not stall a frame; the
// bus runs at 400 kHz, where these transfers take tens of microseconds.
#define KBD_I2C_TIMEOUT_US 5000

// Consecutive failures before the I2C block is torn down and re-initialised.
#define KBD_FAIL_LIMIT     8

#define KBD_QUEUE_SIZE 16

typedef struct {
    int pressed;
    int keycode;
    int ascii;
    int flags;
} kbd_event_t;

static kbd_event_t queue[KBD_QUEUE_SIZE];
static volatile uint8_t q_head = 0, q_tail = 0;

static bool kbd_inited = false;
static bool phase_fetch = false;     // false: select register, true: read it
static absolute_time_t next_phase;
static int fail_count = 0;
static int mods = 0;

//=============================================================================
// Queue
//=============================================================================

static void queue_push(int pressed, int keycode, int ascii, int flags) {
    uint8_t next = (uint8_t)((q_head + 1) % KBD_QUEUE_SIZE);
    if (next == q_tail) return;  // full: drop the oldest input rather than block
    queue[q_head].pressed = pressed;
    queue[q_head].keycode = keycode;
    queue[q_head].ascii   = ascii;
    queue[q_head].flags   = flags;
    q_head = next;
}

bool picocalc_kbd_get_event(int *pressed, int *keycode, int *ascii, int *flags) {
    if (q_tail == q_head) return false;
    const kbd_event_t *e = &queue[q_tail];
    if (pressed) *pressed = e->pressed;
    if (keycode) *keycode = e->keycode;
    if (ascii)   *ascii   = e->ascii;
    if (flags)   *flags   = e->flags;
    q_tail = (uint8_t)((q_tail + 1) % KBD_QUEUE_SIZE);
    return true;
}

//=============================================================================
// Key translation
//=============================================================================

// Map a firmware keycode to the CABAL_KEY_* space. Returns 0 for keys with no
// equivalent, which are dropped.
static int translate_key(uint8_t raw) {
    // Printable ASCII passes straight through; the firmware has already
    // applied Shift and the Sym layer, so 'A' arrives as 'A'.
    if (raw >= 0x20 && raw < 0x7F) return raw;

    if (raw >= KEY_F1 && raw <= KEY_F9) return CABAL_KEY_F1 + (raw - KEY_F1);

    switch (raw) {
    case KEY_F10:       return CABAL_KEY_F10;
    case KEY_BACKSPACE: return CABAL_KEY_BACKSPACE;
    case KEY_TAB:       return CABAL_KEY_TAB;
    case KEY_ENTER:     return CABAL_KEY_RETURN;   // firmware sends 0x0A
    case KEY_ESC:       return CABAL_KEY_ESCAPE;
    case KEY_UP:        return CABAL_KEY_UP;
    case KEY_DOWN:      return CABAL_KEY_DOWN;
    case KEY_LEFT:      return CABAL_KEY_LEFT;
    case KEY_RIGHT:     return CABAL_KEY_RIGHT;
    case KEY_INSERT:    return CABAL_KEY_INSERT;
    case KEY_HOME:      return CABAL_KEY_HOME;
    case KEY_END:       return CABAL_KEY_END;
    case KEY_PAGE_UP:   return CABAL_KEY_PAGEUP;
    case KEY_PAGE_DOWN: return CABAL_KEY_PAGEDOWN;
    case KEY_DEL:       return CABAL_KEY_DELETE;
    case KEY_BREAK:     return CABAL_KEY_PAUSE;
    case KEY_CAPS_LOCK: return CABAL_KEY_CAPSLOCK;
    default:            return 0;
    }
}

// Modifiers are reported as ordinary keys with their own codes. Track the live
// mask and translate to a CABAL_KEY_* so they reach the backend as real key
// events. Returns 0 if `raw` is not a modifier.
static int translate_modifier(uint8_t raw, int down) {
    int bit = 0, key = 0;

    switch (raw) {
    case KEY_MOD_SHL:  bit = CABAL_MOD_SHIFT; key = CABAL_KEY_LSHIFT; break;
    case KEY_MOD_SHR:  bit = CABAL_MOD_SHIFT; key = CABAL_KEY_RSHIFT; break;
    case KEY_MOD_CTRL: bit = CABAL_MOD_CTRL;  key = CABAL_KEY_LCTRL;  break;
    case KEY_MOD_ALT:  bit = CABAL_MOD_ALT;   key = CABAL_KEY_LALT;   break;
    // KEY_MOD_SYM is handled inside the keyboard firmware, which sends the
    // resulting symbol directly. Nothing to track here.
    default: return 0;
    }

    if (down) {
        mods |= bit;
    } else {
        // Shift has two physical keys; only clear the bit when neither is
        // down. The firmware does not tell us about the other one, so treat
        // either release as releasing shift -- wrong only while both are held.
        mods &= ~bit;
    }
    return key;
}

static void handle_report(uint8_t state, uint8_t raw) {
    if (state != KBD_STATE_PRESSED && state != KBD_STATE_HOLD &&
        state != KBD_STATE_RELEASED) {
        return;
    }

    const int down = (state != KBD_STATE_RELEASED);

    int key = translate_modifier(raw, down);
    if (key) {
        // Modifier: the mask has already been updated, and the event carries
        // the new state so a listener sees Shift go down before the key does.
        queue_push(down, key, 0, mods);
        return;
    }

    key = translate_key(raw);
    if (!key) return;

    // HOLD is the firmware's auto-repeat. ScummVM expects repeats as further
    // key-downs, which is exactly what this produces.
    int ascii = 0;
    if (key >= 32 && key < 127) {
        ascii = key;
    } else if (key == CABAL_KEY_BACKSPACE || key == CABAL_KEY_TAB ||
               key == CABAL_KEY_RETURN || key == CABAL_KEY_ESCAPE) {
        ascii = key;
    }

    queue_push(down, key, ascii, mods);
}

//=============================================================================
// I2C plumbing
//=============================================================================

static void kbd_i2c_bringup(void) {
    gpio_set_function(PICOCALC_KBD_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICOCALC_KBD_SCL_PIN, GPIO_FUNC_I2C);
    i2c_init(PICOCALC_KBD_I2C_PORT, 400 * 1000);
    gpio_pull_up(PICOCALC_KBD_SDA_PIN);
    gpio_pull_up(PICOCALC_KBD_SCL_PIN);
}

void picocalc_kbd_init(void) {
    if (kbd_inited) return;

    kbd_i2c_bringup();

    q_head = q_tail = 0;
    mods = 0;
    fail_count = 0;
    phase_fetch = false;
    next_phase = get_absolute_time();
    kbd_inited = true;
}

static void note_failure(void) {
    if (++fail_count < KBD_FAIL_LIMIT) return;

    // The MCU can be left mid-transaction by a reset on our side. Tear the
    // block down and bring it back rather than spinning on a dead bus.
    printf("PicoCalc keyboard: I2C unresponsive, reinitialising\n");
    i2c_deinit(PICOCALC_KBD_I2C_PORT);
    kbd_i2c_bringup();
    fail_count = 0;
    phase_fetch = false;
}

void picocalc_kbd_tick(void) {
    if (!kbd_inited) return;
    if (absolute_time_diff_us(get_absolute_time(), next_phase) > 0) return;

    if (!phase_fetch) {
        // Phase 1: select the FIFO register.
        const uint8_t reg = KBD_REG_FIFO;
        int r = i2c_write_timeout_us(PICOCALC_KBD_I2C_PORT, PICOCALC_KBD_ADDR,
                                     &reg, 1, false, KBD_I2C_TIMEOUT_US);
        if (r < 0) {
            note_failure();
            next_phase = make_timeout_time_us(KBD_PHASE_GAP_US);
            return;
        }
        phase_fetch = true;
        next_phase = make_timeout_time_us(KBD_PHASE_GAP_US);
        return;
    }

    // Phase 2: read the entry. raw[0] is the state, raw[1] the keycode.
    uint8_t raw[2] = { 0, 0 };
    int r = i2c_read_timeout_us(PICOCALC_KBD_I2C_PORT, PICOCALC_KBD_ADDR,
                                raw, sizeof(raw), false, KBD_I2C_TIMEOUT_US);
    phase_fetch = false;

    if (r < 0) {
        note_failure();
        next_phase = make_timeout_time_us(KBD_PHASE_GAP_US);
        return;
    }
    fail_count = 0;

    if (raw[0] == 0 && raw[1] == 0) {
        // FIFO empty; nothing waiting, so back off for a full interval.
        next_phase = make_timeout_time_us(KBD_PHASE_GAP_US);
        return;
    }

    handle_report(raw[0], raw[1]);

    // Something was queued, so there may be more. Come back immediately to
    // drain the FIFO instead of trickling one key per 8 ms.
    next_phase = get_absolute_time();
}

#endif // BOARD_PICOCALC
