/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Enable PSRAM heap after hardware initialization.
 */

#include "psram_allocator.h"

// Called after PSRAM hardware is initialized
void cabal_enable_psram_heap(void) {
    psram_set_ready(1);  // Enable PSRAM allocations in new/malloc
}
