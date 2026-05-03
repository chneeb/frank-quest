/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Game selector implementation. Renders via the OSystem backend so we
 * get palette + event routing for free; input uses the low-level event
 * pump (pollEvent) so arrow keys and Enter work during the selector
 * phase even though no engine is active.
 */

#include "frank_quest_selector.h"
#include "frank_quest_fs.h"

#include "backends/platform/rp2350/rp2350-system.h"
#include "backends/platform/rp2350/rp2350-minimal.h"

#include "common/events.h"
#include "common/system.h"
#include "graphics/surface.h"
#include "graphics/palette.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern OSystem *g_system;

namespace {

// ---- detector table -----------------------------------------------
//
// Each entry is matched against a scanned /quest/<dir> entry. The
// directory name must equal `dirName`, and `probeFile` must exist
// relative to the directory (or be NULL for "directory alone is
// enough"). First match wins, so order specific variants before
// generics (e.g. "mi1ega" before "mi1").
struct Detector {
	const char *dirName;
	const char *probeFile;       // NULL = no file probe
	const char *displayName;
	QuestEngine engine;
	uint8_t     engineSubtype;   // kyra gameId; unused for other engines
	const char *engineHint;      // scumm dirName / sci gameid
};

// Detector entries cover the same games that the old hardcoded
// cabal_main() list supported. Ordering: SCUMM first (most specific
// directory names), then SCI, then Kyra/Gob/AGI.
const Detector kDetectors[] = {
	// --- SCUMM v4 ---
	{"mi1",      "000.LFL",       "Monkey Island 1 (VGA floppy)",     QuestEngine::Scumm, 0, "mi1"},
	{"mi1ega",   "000.LFL",       "Monkey Island 1 (EGA floppy)",     QuestEngine::Scumm, 0, "mi1ega"},
	{"loom",     "000.LFL",       "Loom (VGA)",                       QuestEngine::Scumm, 0, "loom"},
	// --- SCUMM v5 ---
	{"mi1cd",    "monkey.000",    "Monkey Island 1 (CD talkie)",      QuestEngine::Scumm, 0, "mi1cd"},
	{"monkey",   "monkey.000",    "Monkey Island 1 (CD talkie)",      QuestEngine::Scumm, 0, "monkey"},
	{"mi2",      "monkey2.000",   "Monkey Island 2: LeChuck's Revenge",QuestEngine::Scumm, 0, "mi2"},
	{"monkey2",  "monkey2.000",   "Monkey Island 2: LeChuck's Revenge",QuestEngine::Scumm, 0, "monkey2"},
	{"atlantis", "atlantis.000",  "Indiana Jones and the Fate of Atlantis", QuestEngine::Scumm, 0, "atlantis"},
	{"indy4",    "atlantis.000",  "Indiana Jones and the Fate of Atlantis", QuestEngine::Scumm, 0, "indy4"},
	// --- SCUMM v6 ---
	{"dott",     "tentacle.000",  "Day of the Tentacle",              QuestEngine::Scumm, 0, "dott"},
	{"tentacle", "tentacle.000",  "Day of the Tentacle",              QuestEngine::Scumm, 0, "tentacle"},
	{"samnmax",  NULL,            "Sam & Max Hit the Road",           QuestEngine::Scumm, 0, "samnmax"},
	{"sam",      NULL,            "Sam & Max Hit the Road",           QuestEngine::Scumm, 0, "sam"},
	// --- SCUMM v7 ---
	{"ft",       "ft.la0",        "Full Throttle",                    QuestEngine::Scumm, 0, "ft"},
	{"fulltp",   "ft.la0",        "Full Throttle",                    QuestEngine::Scumm, 0, "fulltp"},

	// --- SCI (Sierra) ---
	{"kq1",      NULL, "King's Quest I",                 QuestEngine::Sci, 0, "kq1"},
	{"kq4",      NULL, "King's Quest IV",                QuestEngine::Sci, 0, "kq4"},
	{"kq5",      NULL, "King's Quest V",                 QuestEngine::Sci, 0, "kq5"},
	{"kq6",      NULL, "King's Quest VI",                QuestEngine::Sci, 0, "kq6"},
	{"lsl1",     NULL, "Leisure Suit Larry 1",           QuestEngine::Sci, 0, "lsl1"},
	{"lsl2",     NULL, "Leisure Suit Larry 2",           QuestEngine::Sci, 0, "lsl2"},
	{"lsl3",     NULL, "Leisure Suit Larry 3",           QuestEngine::Sci, 0, "lsl3"},
	{"lsl5",     NULL, "Leisure Suit Larry 5",           QuestEngine::Sci, 0, "lsl5"},
	{"lsl6",     NULL, "Leisure Suit Larry 6",           QuestEngine::Sci, 0, "lsl6"},
	{"sq1",      NULL, "Space Quest I",                  QuestEngine::Sci, 0, "sq1"},
	{"sq3",      NULL, "Space Quest III",                QuestEngine::Sci, 0, "sq3"},
	{"sq4",      NULL, "Space Quest IV",                 QuestEngine::Sci, 0, "sq4"},
	{"sq5",      NULL, "Space Quest V",                  QuestEngine::Sci, 0, "sq5"},
	{"pq1",      NULL, "Police Quest I",                 QuestEngine::Sci, 0, "pq1"},
	{"pq2",      NULL, "Police Quest II",                QuestEngine::Sci, 0, "pq2"},
	{"pq3",      NULL, "Police Quest III",               QuestEngine::Sci, 0, "pq3"},
	{"qfg1vga",  NULL, "Quest for Glory I (VGA)",        QuestEngine::Sci, 0, "qfg1vga"},
	{"qfg1",     NULL, "Quest for Glory I",              QuestEngine::Sci, 0, "qfg1"},
	{"qfg2",     NULL, "Quest for Glory II",             QuestEngine::Sci, 0, "qfg2"},
	{"qfg3",     NULL, "Quest for Glory III",            QuestEngine::Sci, 0, "qfg3"},
	{"iceman",     NULL, "Codename: ICEMAN",             QuestEngine::Sci, 0, "iceman"},
	{"laurabow2",  NULL, "Laura Bow 2: Dagger of Amon Ra",QuestEngine::Sci, 0, "laurabow2"},
	{"laurabow",   NULL, "Laura Bow: Colonel's Bequest", QuestEngine::Sci, 0, "laurabow"},
	{"longbow",    NULL, "Conquests of the Longbow",     QuestEngine::Sci, 0, "longbow"},
	{"ecoquest2",  NULL, "EcoQuest 2",                   QuestEngine::Sci, 0, "ecoquest2"},
	{"ecoquest",   NULL, "EcoQuest",                     QuestEngine::Sci, 0, "ecoquest"},
	{"freddy",     NULL, "Freddy Pharkas",               QuestEngine::Sci, 0, "freddy"},
	{"jones",      NULL, "Jones in the Fast Lane",       QuestEngine::Sci, 0, "jones"},
	{"pepper",     NULL, "Pepper's Adventures in Time",  QuestEngine::Sci, 0, "pepper"},
	{"slater",     NULL, "Slater & Charlie Go Camping",  QuestEngine::Sci, 0, "slater"},
	{"castlebrain",NULL, "Castle of Dr. Brain",          QuestEngine::Sci, 0, "castlebrain"},
	{"islandbrain",NULL, "Island of Dr. Brain",          QuestEngine::Sci, 0, "islandbrain"},
	{"mothergoose",NULL, "Mixed-Up Mother Goose",        QuestEngine::Sci, 0, "mothergoose"},
	{"camelot",    NULL, "Conquests of Camelot",         QuestEngine::Sci, 0, "camelot"},

	// --- Kyra ---
	{"kyra2",       NULL, "Legend of Kyrandia 2: Hand of Fate",   QuestEngine::Kyra, 1, ""},
	{"kyr2",        NULL, "Legend of Kyrandia 2: Hand of Fate",   QuestEngine::Kyra, 1, ""},
	{"kyrandia2",   NULL, "Legend of Kyrandia 2: Hand of Fate",   QuestEngine::Kyra, 1, ""},
	{"handoffate",  NULL, "Legend of Kyrandia 2: Hand of Fate",   QuestEngine::Kyra, 1, ""},
	{"kyra1",       NULL, "Legend of Kyrandia 1",                 QuestEngine::Kyra, 0, ""},
	{"kyr1",        NULL, "Legend of Kyrandia 1",                 QuestEngine::Kyra, 0, ""},
	{"kyrandia",    NULL, "Legend of Kyrandia 1",                 QuestEngine::Kyra, 0, ""},

	// --- Gobliiins ---
	{"gob1",  NULL, "Gobliiins",     QuestEngine::Gob, 1, ""},
	{"gob2",  NULL, "Gobliins 2",    QuestEngine::Gob, 2, ""},
	{"gob3",  NULL, "Goblins 3",     QuestEngine::Gob, 3, ""},
	{"gob",   NULL, "Gobliiins (auto-detect)", QuestEngine::Gob, 0, ""},

	// --- AGI (Sierra pre-SCI) ---
	{"agi",   NULL, "AGI fan-made / generic",  QuestEngine::Agi, 0, ""},
};

constexpr int kNumDetectors = sizeof(kDetectors) / sizeof(kDetectors[0]);

int strcasecmp_ascii(const char *a, const char *b) {
	while (*a && *b) {
		char ca = *a, cb = *b;
		if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
		if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
		if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
		++a; ++b;
	}
	return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

// ---- 5x7 glyphs ---------------------------------------------------
//
// Small inline font so the selector doesn't need a real renderer. Rows
// stored MSB-left across 5 columns. Uppercase-only plus digits and a
// handful of punctuation that we actually use in display names.

struct Glyph5x7 {
	char    ch;
	uint8_t rows[7];
};

const Glyph5x7 kGlyphs[] = {
	{' ',  {0x00,0x00,0x00,0x00,0x00,0x00,0x00}},
	{'!',  {0x04,0x04,0x04,0x04,0x04,0x00,0x04}},
	{'"',  {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}},
	{'&',  {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}},
	{'\'', {0x04,0x04,0x00,0x00,0x00,0x00,0x00}},
	{'(',  {0x02,0x04,0x08,0x08,0x08,0x04,0x02}},
	{')',  {0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
	{'*',  {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}},
	{'+',  {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}},
	{',',  {0x00,0x00,0x00,0x00,0x00,0x04,0x08}},
	{'-',  {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}},
	{'.',  {0x00,0x00,0x00,0x00,0x00,0x00,0x04}},
	{'/',  {0x01,0x02,0x04,0x08,0x10,0x00,0x00}},
	{'0',  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}},
	{'1',  {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}},
	{'2',  {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}},
	{'3',  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}},
	{'4',  {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}},
	{'5',  {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}},
	{'6',  {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}},
	{'7',  {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}},
	{'8',  {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}},
	{'9',  {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}},
	{':',  {0x00,0x04,0x00,0x00,0x00,0x04,0x00}},
	{'?',  {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}},
	{'A',  {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}},
	{'B',  {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}},
	{'C',  {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}},
	{'D',  {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}},
	{'E',  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}},
	{'F',  {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}},
	{'G',  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}},
	{'H',  {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}},
	{'I',  {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}},
	{'J',  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}},
	{'K',  {0x11,0x12,0x14,0x18,0x14,0x12,0x11}},
	{'L',  {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}},
	{'M',  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}},
	{'N',  {0x11,0x19,0x15,0x13,0x11,0x11,0x11}},
	{'O',  {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}},
	{'P',  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}},
	{'Q',  {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}},
	{'R',  {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}},
	{'S',  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}},
	{'T',  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}},
	{'U',  {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}},
	{'V',  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}},
	{'W',  {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}},
	{'X',  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}},
	{'Y',  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}},
	{'Z',  {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}},
	{'[',  {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}},
	{']',  {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}},
	{'^',  {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}},
	{'_',  {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}},
};
constexpr int kNumGlyphs = sizeof(kGlyphs) / sizeof(kGlyphs[0]);

const Glyph5x7 *findGlyph(char ch) {
	if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
	for (int i = 0; i < kNumGlyphs; ++i) {
		if (kGlyphs[i].ch == ch) return &kGlyphs[i];
	}
	return &kGlyphs[0]; // space fallback
}

void drawGlyph(Graphics::Surface *surf, int x, int y, char ch,
               uint8_t color) {
	const Glyph5x7 *g = findGlyph(ch);
	const int w = surf->getWidth();
	const int h = surf->getHeight();
	for (int row = 0; row < 7; ++row) {
		const int py = y + row;
		if (py < 0 || py >= h) continue;
		const uint8_t bits = g->rows[row];
		for (int col = 0; col < 5; ++col) {
			if (!(bits & (1 << (4 - col)))) continue;
			const int px = x + col;
			if (px < 0 || px >= w) continue;
			*(uint8_t *)surf->getBasePtr(px, py) = color;
		}
	}
}

void drawText(Graphics::Surface *surf, int x, int y, const char *s,
              uint8_t color) {
	for (; *s; ++s) {
		drawGlyph(surf, x, y, *s, color);
		x += 6;  // 5 px glyph + 1 px gap
	}
}

void fillRect(Graphics::Surface *surf, int x, int y, int w, int h,
              uint8_t color) {
	const int sw = surf->getWidth();
	const int sh = surf->getHeight();
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > sw) w = sw - x;
	if (y + h > sh) h = sh - y;
	if (w <= 0 || h <= 0) return;
	for (int row = 0; row < h; ++row) {
		uint8_t *p = (uint8_t *)surf->getBasePtr(x, y + row);
		memset(p, color, w);
	}
}

// Scan /quest/ for directories and match against the detector table.
int scanGames(QuestGame *out, int maxOut) {
	const char *root = "/quest";
	if (!cabal_path_exists(root)) {
		printf("Selector: /quest does not exist on SD card\n");
		return 0;
	}

	CabalDir *dir = cabal_dir_open(root);
	if (!dir) {
		printf("Selector: cannot open /quest\n");
		return 0;
	}

	int count = 0;
	CabalDirEntry entry;
	while (cabal_dir_read(dir, &entry) && count < maxOut) {
		if (!entry.isDirectory) continue;

		// Match against detector table.
		const Detector *d = NULL;
		for (int i = 0; i < kNumDetectors; ++i) {
			if (strcasecmp_ascii(entry.name, kDetectors[i].dirName) != 0)
				continue;
			if (kDetectors[i].probeFile) {
				char probe[128];
				snprintf(probe, sizeof(probe), "%s/%s/%s",
				         root, entry.name, kDetectors[i].probeFile);
				if (!cabal_path_exists(probe)) continue;
			}
			d = &kDetectors[i];
			break;
		}
		if (!d) {
			printf("Selector: /quest/%s — no detector match, skipping\n",
			       entry.name);
			continue;
		}

		QuestGame *g = &out[count++];
		snprintf(g->dirPath, sizeof(g->dirPath), "%s/%s", root, entry.name);
		strncpy(g->displayName, d->displayName, sizeof(g->displayName) - 1);
		g->displayName[sizeof(g->displayName) - 1] = 0;
		g->engine = d->engine;
		g->engineSubtype = d->engineSubtype;
		strncpy(g->engineHint, d->engineHint, sizeof(g->engineHint) - 1);
		g->engineHint[sizeof(g->engineHint) - 1] = 0;

		printf("Selector: found %s -> %s\n", g->dirPath, g->displayName);
	}
	cabal_dir_close(dir);

	// Sort alphabetically by displayName. N is small so insertion sort
	// is fine and avoids pulling in qsort.
	for (int i = 1; i < count; ++i) {
		QuestGame tmp = out[i];
		int j = i;
		while (j > 0 && strcasecmp_ascii(out[j - 1].displayName, tmp.displayName) > 0) {
			out[j] = out[j - 1];
			--j;
		}
		out[j] = tmp;
	}

	return count;
}

// ---- palette ------------------------------------------------------
//
// The selector owns palette slots 0..5 during its lifetime. Engines
// rewrite the palette on init() so there's no need to save/restore.
enum : uint8_t {
	kColBg       = 0,  // black
	kColText     = 1,  // white
	kColDim      = 2,  // grey (status lines)
	kColAccent   = 3,  // accent (title / scrollbar thumb)
	kColTrack    = 4,  // scrollbar track
	kColHighlight= 5,  // highlight fill
};

void installPalette() {
	byte pal[6 * 3];
	// bg     — black
	pal[0]=0x00; pal[1]=0x00; pal[2]=0x00;
	// text   — white
	pal[3]=0xF0; pal[4]=0xF0; pal[5]=0xF0;
	// dim    — mid-grey
	pal[6]=0x70; pal[7]=0x70; pal[8]=0x78;
	// accent — warm amber
	pal[9]=0xE0; pal[10]=0xB8; pal[11]=0x30;
	// track  — dim blue
	pal[12]=0x28; pal[13]=0x30; pal[14]=0x48;
	// highlight — bright blue
	pal[15]=0x50; pal[16]=0x70; pal[17]=0xE0;
	g_system->getPaletteManager()->setPalette(pal, 0, 6);
}

void renderFrame(const QuestGame *games, int count,
                 int selected, int scroll, int visibleLines,
                 int listX, int listY, int lineH, int listW) {
	Graphics::Surface *surf = g_system->lockScreen();
	if (!surf || !surf->getPixels()) {
		if (surf) g_system->unlockScreen();
		return;
	}

	fillRect(surf, 0, 0, surf->getWidth(), surf->getHeight(), kColBg);

	// Title bar.
	drawText(surf, 8,  6, "FRANK QUEST", kColAccent);
	drawText(surf, 8, 16, "Select a game and press ENTER.", kColDim);

	// Footer.
	const int sh = surf->getHeight();
	drawText(surf, 8, sh - 22,
	         "UP/DOWN: navigate   ENTER: launch   CTRL+ALT+DEL: reset",
	         kColDim);
	char countLine[48];
	snprintf(countLine, sizeof(countLine), "%d game%s on SD card",
	         count, count == 1 ? "" : "s");
	drawText(surf, 8, sh - 12, countLine, kColDim);

	if (count <= 0) {
		drawText(surf, listX, listY,
		         "NO GAMES FOUND IN /QUEST", kColText);
		drawText(surf, listX, listY + lineH,
		         "Copy games to /quest/<dir>/ on the SD card.", kColDim);
		g_system->unlockScreen();
		g_system->updateScreen();
		return;
	}

	int end = scroll + visibleLines;
	if (end > count) end = count;

	for (int i = scroll; i < end; ++i) {
		const int y = listY + (i - scroll) * lineH;
		if (i == selected) {
			fillRect(surf, listX - 2, y - 1, listW + 4, lineH,
			         kColHighlight);
			drawText(surf, listX, y, games[i].displayName, kColText);
		} else {
			drawText(surf, listX, y, games[i].displayName, kColText);
		}
	}

	// Scrollbar: always drawn so the UI layout doesn't jitter; when the
	// list fits entirely in view the thumb fills the whole track.
	const int sbX = listX + listW + 6;
	const int sbY = listY - 1;
	const int sbH = visibleLines * lineH;
	const int sbW = 3;
	fillRect(surf, sbX, sbY, sbW, sbH, kColTrack);

	int thumbH;
	int thumbY;
	if (count <= visibleLines) {
		thumbH = sbH;
		thumbY = sbY;
	} else {
		// Thumb size proportional to visible fraction, minimum 6px.
		thumbH = (sbH * visibleLines) / count;
		if (thumbH < 6) thumbH = 6;
		if (thumbH > sbH) thumbH = sbH;
		const int maxScroll = count - visibleLines;
		const int maxThumbTravel = sbH - thumbH;
		thumbY = sbY + (maxThumbTravel * scroll) / (maxScroll > 0 ? maxScroll : 1);
	}
	fillRect(surf, sbX, thumbY, sbW, thumbH, kColAccent);

	g_system->unlockScreen();
	g_system->updateScreen();
}

} // namespace

int frank_quest_scan_games(QuestGame *out, int maxOut) {
	return scanGames(out, maxOut);
}

int frank_quest_run_selector(const QuestGame *games, int count,
                             int initialIndex) {
	installPalette();

	// Layout. The backend initializes screen at 320x200.
	const int listX     = 16;
	const int listY     = 36;
	const int listW     = 260;
	const int lineH     = 10;
	const int sh        = g_system->getHeight();
	const int footerY   = sh - 28;
	int visibleLines    = (footerY - listY) / lineH;
	if (visibleLines < 1) visibleLines = 1;

	int selected = initialIndex;
	if (selected < 0) selected = 0;
	if (selected >= count) selected = count > 0 ? count - 1 : 0;
	int scroll = 0;
	if (selected >= visibleLines) scroll = selected - visibleLines + 1;

	renderFrame(games, count, selected, scroll, visibleLines,
	            listX, listY, lineH, listW);

	// Poll events directly from the rp2350 OSystem — EventManager
	// isn't set up this early and we only need raw key navigation.
	OSystem_RP2350 *sys = static_cast<OSystem_RP2350 *>(g_system);

	while (true) {
		Common::Event ev;
		bool dirty = false;
		while (sys->pollEvent(ev)) {
			if (ev.type != Common::EVENT_KEYDOWN) continue;
			if (count <= 0) continue;

			int prevSel = selected;
			int prevScroll = scroll;

			switch (ev.kbd.keycode) {
			case Common::KEYCODE_UP:
				if (selected > 0) --selected;
				break;
			case Common::KEYCODE_DOWN:
				if (selected < count - 1) ++selected;
				break;
			case Common::KEYCODE_PAGEUP:
				selected -= visibleLines;
				if (selected < 0) selected = 0;
				break;
			case Common::KEYCODE_PAGEDOWN:
				selected += visibleLines;
				if (selected >= count) selected = count - 1;
				break;
			case Common::KEYCODE_HOME:
				selected = 0;
				break;
			case Common::KEYCODE_END:
				selected = count - 1;
				break;
			case Common::KEYCODE_RETURN:
			case Common::KEYCODE_KP_ENTER:
				return selected;
			default:
				break;
			}

			// Keep selection visible.
			if (selected < scroll) scroll = selected;
			if (selected >= scroll + visibleLines)
				scroll = selected - visibleLines + 1;

			if (selected != prevSel || scroll != prevScroll) dirty = true;
		}

		if (dirty) {
			renderFrame(games, count, selected, scroll, visibleLines,
			            listX, listY, lineH, listW);
		}
		g_system->delayMillis(16);
	}
}
