/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PSRAM_ALLOCATOR_H
#define PSRAM_ALLOCATOR_H

#include <stddef.h>

// Total external PSRAM size (bytes). Keep in sync with the hardware used.
// Used by UI/status display and allocator partitioning.
#ifndef CABAL_PSRAM_SIZE_BYTES
#define CABAL_PSRAM_SIZE_BYTES (8u * 1024u * 1024u)
#endif

void *psram_malloc(size_t size);
void *psram_realloc(void *ptr, size_t size);
void psram_free(void *ptr);
void psram_reset(void);
void psram_mark_session(void);    // Mark current offset for game session
void psram_restore_session(void); // Restore to marked offset
void *psram_get_scratch_1(size_t size);
void *psram_get_scratch_2(size_t size);
void *psram_get_file_buffer(size_t size);

/* Persistent HDMI framebuffer slot. Returns a fixed PSRAM address
 * that survives psram_reset(), so HDMI DMA can keep scanning it while
 * the rest of the heap is wiped for a return-to-selector transition. */
void  *psram_get_framebuffer(void);
size_t psram_framebuffer_size(void);

void psram_set_temp_mode(int enable);
void psram_reset_temp(void);
size_t psram_get_temp_offset(void);
void psram_set_temp_offset(size_t offset);

void psram_set_sram_mode(int enable); // Force SRAM allocation for proper malloc/free
void psram_set_ready(int ready);      // Call after PSRAM hardware is initialized

// Debug: Print memory status
void psram_print_status(void);

// Debug: walk the PSRAM heap, print chunk list and used/free totals.
// Safe to call while audio/HDMI IRQs are running. Useful to snapshot
// heap state right before a suspected-corrupting operation.
void frank_quest_heap_walk(void);

#endif
