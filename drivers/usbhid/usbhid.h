/*
 * FRANK Quest - USB HID Host Driver
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Provides keyboard input via USB Host.
 */

#ifndef USBHID_H
#define USBHID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// API Functions
//--------------------------------------------------------------------

/**
 * Initialize USB Host HID driver
 * Call this during system initialization
 */
void usbhid_init(void);

/**
 * Poll USB Host for events
 * Must be called periodically (e.g., in main loop)
 */
void usbhid_task(void);

/**
 * Check if a USB keyboard is connected
 * @return Non-zero if keyboard connected
 */
int usbhid_keyboard_connected(void);

/**
 * Get pending key action
 * @param keycode HID keycode of the key (0x04-0x65, 0xE0-0xE7 for modifiers)
 * @param down Non-zero if key pressed, 0 if released
 * @return Non-zero if action available, 0 if queue empty
 */
int usbhid_get_key_action(uint8_t *keycode, int *down);

/**
 * Check if a USB mouse is connected
 * @return Non-zero if mouse connected
 */
int usbhid_mouse_connected(void);

/**
 * Get accumulated mouse event
 * @param dx X movement delta (accumulated since last call)
 * @param dy Y movement delta (accumulated since last call)
 * @param dz Scroll wheel delta
 * @param buttons Button state (bit 0=left, bit 1=right, bit 2=middle)
 * @return Non-zero if event available, 0 if no mouse activity
 */
int usbhid_get_mouse_event(int16_t *dx, int16_t *dy, int8_t *dz, uint8_t *buttons);

#ifdef __cplusplus
}
#endif

#endif /* USBHID_H */
