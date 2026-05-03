/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Filesystem factory for RP2350.
 */

#if defined(__RP2350__) || defined(PICO_ON_DEVICE)

#include "backends/fs/rp2350/rp2350-fs-factory.h"
#include "backends/fs/rp2350/rp2350-fs.h"

namespace Common {
DECLARE_SINGLETON(RP2350FilesystemFactory);
}

AbstractFSNode *RP2350FilesystemFactory::makeRootFileNode() const {
	return new RP2350::RP2350FileSystemNode();
}

AbstractFSNode *RP2350FilesystemFactory::makeCurrentDirectoryFileNode() const {
	// On RP2350 we use /quest as the current directory. All game data
	// and saves live under /quest/<game>/ on the SD card.
	return new RP2350::RP2350FileSystemNode("/quest");
}

AbstractFSNode *RP2350FilesystemFactory::makeFileNodePath(const Common::String &path) const {
	return new RP2350::RP2350FileSystemNode(path);
}

#endif // __RP2350__ || PICO_ON_DEVICE
