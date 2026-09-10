/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PICOCALC_KBD_H
#define PICOCALC_KBD_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up i2c1 and the keyboard MCU. Safe to call when no keyboard
// answers: every later call then simply produces no events.
void picocalc_kbd_init(void);

// Advance the non-blocking poll state machine. Cheap, and rate-limited
// internally, so calling it from a tight event loop is fine.
void picocalc_kbd_tick(void);

// Dequeue one key event. Returns true if *pressed / *keycode / *ascii /
// *flags were filled in. keycode is in the CABAL_KEY_* space, flags is a
// mask of CABAL_MOD_*.
bool picocalc_kbd_get_event(int *pressed, int *keycode, int *ascii, int *flags);

#ifdef __cplusplus
}
#endif

#endif // PICOCALC_KBD_H
