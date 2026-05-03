/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Game selector UI.
 *
 * Scans /quest/ on the SD card for game directories, matches each against
 * a detector table, renders a scrolling alphabetical list, and returns the
 * user's choice so frank_quest_main.cpp can dispatch the right engine.
 */

#ifndef FRANK_QUEST_SELECTOR_H
#define FRANK_QUEST_SELECTOR_H

#include <stdint.h>

// Engine dispatch tags. The int values are load-bearing: stored in the
// watchdog scratch register to persist the last-selected index across the
// Ctrl+Alt+Del reboot, and matched against the main dispatcher switch.
enum class QuestEngine : uint8_t {
	Scumm = 1,
	Sci   = 2,
	Kyra  = 3,
	Gob   = 4,
	Agi   = 5,
};

// One entry in the detector table / selector list. `dirPath` is an
// absolute path on the SD card (e.g. "/quest/mi1"). `gameId` is the
// engine-specific ID string the dispatcher passes to the per-engine
// launcher — same strings the old hardcoded probe list in cabal_main()
// used. `displayName` is what the user sees in the selector.
struct QuestGame {
	char        dirPath[64];
	char        displayName[48];
	QuestEngine engine;
	uint8_t     engineSubtype;   // kyra sub-id, sci gameid slot, etc.
	char        engineHint[16];  // scumm dirName, sci gameid — small copy
};

// Scan /quest/ and populate `out` (up to `maxOut` entries). Results are
// sorted alphabetically by displayName. Returns the number of games found.
int frank_quest_scan_games(QuestGame *out, int maxOut);

// Render the game selector and block until the user picks one. Returns
// the index into `games` that was selected. If the user presses ESC and
// `allowCancel` is true, returns -1; otherwise ESC is ignored.
// `initialIndex` seeds the cursor (so we can restore the last selection
// after a Ctrl+Alt+Del reboot).
int frank_quest_run_selector(const QuestGame *games, int count,
                             int initialIndex);

// Scratch-register slot used to persist the selector cursor across warm
// reboots triggered by Ctrl+Alt+Del. Use slots 6/7 to avoid collision
// with the crash reporter (0..5) in rp2350-minimal.cpp.
#define FRANK_QUEST_SCRATCH_MAGIC_SLOT  6
#define FRANK_QUEST_SCRATCH_INDEX_SLOT  7
#define FRANK_QUEST_SCRATCH_MAGIC       0xF7A8C0DEu

#endif // FRANK_QUEST_SELECTOR_H
