/*
 * FRANK Quest - PS/2 Keyboard Wrapper
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "board_config.h"
#include "ps2kbd_wrapper.h"
#include <new>  // for placement new in ps2kbd_init
#include "ps2kbd_mrmltr.h"

struct KeyEvent {
    int pressed;
    unsigned char key;
    uint8_t hid_code;
};

// Fixed-size ring buffer in SRAM. std::queue's backing deque lives on
// the PSRAM-backed operator-new heap, which gets wiped when
// cabal_main() tears down an engine and loops back to the selector;
// the queue's internal node pointers would then dangle. A plain
// circular buffer keeps everything in BSS and survives psram_reset().
static constexpr int EVENT_QUEUE_SIZE = 64;
static KeyEvent event_ring[EVENT_QUEUE_SIZE];
static volatile int event_ring_head = 0;  // producer
static volatile int event_ring_tail = 0;  // consumer

static inline bool event_queue_empty() {
    return event_ring_head == event_ring_tail;
}

static inline void event_queue_push(const KeyEvent &e) {
    const int next = (event_ring_head + 1) % EVENT_QUEUE_SIZE;
    if (next == event_ring_tail) {
        // Queue full — drop the oldest event by advancing tail. Input
        // is better stale than wedged.
        event_ring_tail = (event_ring_tail + 1) % EVENT_QUEUE_SIZE;
    }
    event_ring[event_ring_head] = e;
    event_ring_head = next;
}

static inline bool event_queue_pop(KeyEvent &e) {
    if (event_queue_empty()) return false;
    e = event_ring[event_ring_tail];
    event_ring_tail = (event_ring_tail + 1) % EVENT_QUEUE_SIZE;
    return true;
}

// HID to ASCII mapping
static unsigned char hid_to_ascii(uint8_t code, bool shift) {
    // Letters a-z (0x04-0x1D)
    if (code >= 0x04 && code <= 0x1D) {
        char c = 'a' + (code - 0x04);
        return shift ? (c - 32) : c;  // Uppercase if shift
    }
    
    // Numbers 1-9, 0 (0x1E-0x27)
    if (code >= 0x1E && code <= 0x27) {
        if (shift) {
            // Shifted number row symbols
            const char shifted[] = "!@#$%^&*()";
            return shifted[code - 0x1E];
        }
        if (code == 0x27) return '0';
        return '1' + (code - 0x1E);
    }
    
    // Special keys
    switch (code) {
        case 0x28: return '\r';     // Enter (CR, keycode 13)
        case 0x29: return 0x1B;     // Escape
        case 0x2A: return '\b';     // Backspace
        case 0x2B: return '\t';     // Tab
        case 0x2C: return ' ';      // Space
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        
        // Arrow keys - return special codes
        case 0x4F: return 0x80;  // Right arrow
        case 0x50: return 0x81;  // Left arrow
        case 0x51: return 0x82;  // Down arrow
        case 0x52: return 0x83;  // Up arrow
        
        // Function keys F1-F12
        case 0x3A: return 0xF1;  // F1
        case 0x3B: return 0xF2;  // F2
        case 0x3C: return 0xF3;  // F3
        case 0x3D: return 0xF4;  // F4
        case 0x3E: return 0xF5;  // F5
        case 0x3F: return 0xF6;  // F6
        case 0x40: return 0xF7;  // F7
        case 0x41: return 0xF8;  // F8
        case 0x42: return 0xF9;  // F9
        case 0x43: return 0xFA;  // F10
        case 0x44: return 0xFB;  // F11
        case 0x45: return 0xFC;  // F12
    }
    
    return 0;
}

static bool shift_held = false;

static void key_handler(hid_keyboard_report_t *curr, hid_keyboard_report_t *prev) {
    // Track shift state
    shift_held = (curr->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
    
    // Check modifiers
    uint8_t changed_mods = curr->modifier ^ prev->modifier;
    if (changed_mods) {
        // Report modifier changes
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) {
            int pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT)) != 0;
            event_queue_push({pressed, 0xE1, 0xE1});  // Shift
        }
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) {
            int pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL)) != 0;
            event_queue_push({pressed, 0xE0, 0xE0});  // Ctrl
        }
        if (changed_mods & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) {
            int pressed = (curr->modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT)) != 0;
            event_queue_push({pressed, 0xE2, 0xE2});  // Alt
        }
    }

    // Check for new key presses
    for (int i = 0; i < 6; i++) {
        if (curr->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (prev->keycode[j] == curr->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unsigned char ascii = hid_to_ascii(curr->keycode[i], shift_held);
                event_queue_push({1, ascii, curr->keycode[i]});
            }
        }
    }

    // Check for key releases
    for (int i = 0; i < 6; i++) {
        if (prev->keycode[i] != 0) {
            bool found = false;
            for (int j = 0; j < 6; j++) {
                if (curr->keycode[j] == prev->keycode[i]) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                unsigned char ascii = hid_to_ascii(prev->keycode[i], shift_held);
                event_queue_push({0, ascii, prev->keycode[i]});
            }
        }
    }
}

// The Ps2Kbd_Mrmltr object must survive a return-to-selector teardown
// that wipes the PSRAM heap (operator new routes there). Park it in an
// SRAM buffer and placement-new into it once at cold boot; subsequent
// calls are a no-op so stale PIO state-machine claims aren't leaked.
static Ps2Kbd_Mrmltr* kbd = nullptr;
alignas(Ps2Kbd_Mrmltr) static uint8_t kbd_storage[sizeof(Ps2Kbd_Mrmltr)];

extern "C" void ps2kbd_init(void) {
#ifdef BOARD_PICOCALC
    // PicoCalc has no PS/2 header; its keyboard is an I2C MCU. Leave pio0 and
    // the GPIOs alone. The rest of this file stays compiled so the event ring
    // and the ps2kbd_* entry points the backend calls remain available.
    return;
#else
    if (kbd) return;  // already initialized — PIO SM and program are retained
    // PS2 keyboard driver expects base_gpio as CLK, and base_gpio+1 as DATA
    kbd = new (kbd_storage) Ps2Kbd_Mrmltr(pio0, PS2_PIN_CLK, key_handler);
    kbd->init_gpio();
#endif
}

extern "C" void ps2kbd_tick(void) {
    if (kbd) kbd->tick();
}

// Drop any pending input events. Called on game-to-selector transitions
// so a held-Del from Ctrl+Alt+Del doesn't "leak" into the selector.
extern "C" void ps2kbd_flush(void) {
    event_ring_head = 0;
    event_ring_tail = 0;
}

extern "C" int ps2kbd_get_key(int* pressed, unsigned char* key) {
    KeyEvent e;
    if (!event_queue_pop(e)) return 0;
    *pressed = e.pressed;
    *key = e.key;
    return 1;
}

extern "C" int ps2kbd_get_key_ext(int* pressed, unsigned char* key, uint8_t* hid_code) {
    KeyEvent e;
    if (!event_queue_pop(e)) return 0;
    *pressed = e.pressed;
    *key = e.key;
    *hid_code = e.hid_code;
    return 1;
}
