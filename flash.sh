#!/bin/bash
# FRANK Quest - Flash firmware to connected Pico device
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-quest
#
# Derived from Cabal (https://github.com/project-cabal/cabal).
# SPDX-License-Identifier: GPL-2.0-or-later

# Default to ELF file from build directory
FIRMWARE="${1:-./build/frank-quest.elf}"

# Check if firmware file exists
if [ ! -f "$FIRMWARE" ]; then
    # Try .uf2 if .elf not found
    FIRMWARE="${FIRMWARE%.elf}.uf2"
    if [ ! -f "$FIRMWARE" ]; then
        echo "Error: Firmware file not found"
        echo "Usage: $0 [firmware.elf|firmware.uf2]"
        echo "Default: ./build/frank-quest.elf"
        exit 1
    fi
fi

echo "Flashing: $FIRMWARE"
picotool load -f "$FIRMWARE" && picotool reboot -f
