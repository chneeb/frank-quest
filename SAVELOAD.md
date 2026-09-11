# Save / restore — why it does nothing, and what to build

Status: **investigated, not implemented.** Nothing below is written yet.

## Symptom

In SCI, F5 and F7 appear dead, and picking "save game" from a game's own menu does nothing visible.
In SCUMM there is no reachable save or restore menu at all.

## What is actually happening

Two different causes, both downstream of `DISABLE_GUI=1` (`CMakeLists.txt`).

### SCI: the save works, silently, always into slot 0

`src/gui/saveload.h` is a Cabal stub, and its own comment says so:

```cpp
int runModalWithCurrentTarget() {
    // On embedded, just return slot 0 for save, or first available for load
    // A real implementation would show a menu
    return _saveMode ? 0 : 0;
}
```

SCI's `kSaveGame` / `kRestoreGame` (`src/engines/sci/engine/kfile.cpp:759`, `:858`) construct a
`GUI::SaveLoadChooser` and use whatever slot it returns. The stub returns 0 without drawing
anything, so the game saves successfully into slot 0 and gives no feedback. The `quest-sci.000`
already sitting in `/quest/saves` is exactly that: a real, working slot-0 save.

So SCI save/load is not broken. It has no UI, no slot choice, and no confirmation.

Note this also means `originalsaveload` is **not** the fix. Setting it true (`sci.cpp:512`) would
hand save/restore back to the game's own Sierra dialogs, which is a different behaviour and
unnecessary once the chooser is real.

### SCUMM: no dialog is reached at all

SCUMM saves through the main menu, and `Engine::openMainMenuDialog()`
(`src/engines/engine.cpp:370`) is an early `return;` under `DISABLE_GUI`. Nothing is called, so F5
does nothing. Unlike SCI, SCUMM has no in-game save UI of its own to fall back on.

## What to build

Implement what the Cabal stub invites: a real chooser, once, wired at two points.

The UI can follow `src/frank_quest_selector.cpp`, which already draws a scrolling list against the
framebuffer and takes keyboard input — same look as the game selector, no ScummVM GUI involved, so
`DISABLE_GUI` stays as it is. Re-enabling the real GUI would drag in the theme engine, fonts and the
widget set, which is presumably why it was switched off on a 520 KB part.

1. **`GUI::SaveLoadChooser::runModalWithCurrentTarget()`** (`src/gui/saveload.h`) — return a slot the
   user actually picked, and a description via `getResultString()`. Fixes SCI through its existing
   code path, with no per-engine hooks.
2. **`Engine::openMainMenuDialog()` under `DISABLE_GUI`** (`src/engines/engine.cpp:370`) — point it
   at the same chooser and call `saveGameState()` / `loadGameState()`. That is what gives SCUMM F5.

`Engine::saveGameState(int slot, const String &desc)`, `loadGameState(int slot)` and the
`can*GameStateCurrently()` gates are virtual on the base class (`src/engines/engine.h:221-248`), and
SCI, SCUMM, AGI and KYRA all override them — so one path serves every engine in the build. GOB does
not override them; check what the base class does there before assuming GOB can save.

Gate the chooser on `canSaveGameStateCurrently()` so it refuses mid-cutscene rather than writing a
corrupt save.

## Must be fixed at the same time: save domains collide

`frank_quest_main.cpp` sets one ConfMan domain per *engine*, not per game:

| Launcher | Domain | Line |
|---|---|---|
| GOB | `quest-gob` | 277 |
| AGI | `quest-agi` | 396 |
| KYRA | `quest-kyra` | 596 |
| SCI | `quest-sci` | 727 |
| SCUMM | `quest-scumm` | 886 |

The domain is the save target, so every SCI game shares one namespace: Space Quest 3 and King's
Quest would overwrite each other's slots, and a slot list would show another game's saves. Harmless
while the only slot is 0 and nothing lists them; actively wrong the moment the chooser exists.

The selector already knows which game it dispatched (`QuestGame::dirPath`, and `engineHint` for
SCUMM/SCI), so a per-game domain is available at the call site.

## Open questions

- Does AGI route through `SaveLoadChooser`, or does it use its own dialogs? Unchecked.
- Text entry for save descriptions: the PicoCalc has a real keyboard, so typing a name is possible.
  Auto-generating ("Slot 3") is less work and may be enough.
- Does the chooser need to run while the engine holds the framebuffer? The selector runs between
  games; a mid-game chooser has to draw over a live screen and restore it afterwards.
