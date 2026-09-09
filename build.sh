#!/bin/bash
# FRANK Quest - Build script (ScummVM port for RP2350)
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-quest
#
# Derived from Cabal (https://github.com/project-cabal/cabal).
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Usage: ./build.sh [BOARD] [CPU_MHZ] [PSRAM_MHZ] [FLASH_MHZ] [usb-hid] [clean]
# Defaults: M2 504 133 66
# BOARD is M1, M2 or PICOCALC (M4 is accepted as an alias for PICOCALC).

set -e

USB_HID="1"
CLEAN=""

# Strip special (non-positional) flags so they don't pollute the positional
# slots for BOARD/CPU/PSRAM/FLASH.
POS=()
for arg in "$@"; do
    case "$arg" in
        clean) CLEAN="clean" ;;
        usb-hid|usbhid) USB_HID="1" ;;
        *) POS+=("$arg") ;;
    esac
done

BOARD="${POS[0]:-M2}"
CPU="${POS[1]:-504}"
PSRAM="${POS[2]:-133}"
FLASH="${POS[3]:-66}"

# M4 is the shorthand that grew alongside M1/M2; PICOCALC is the real name and
# the macro the sources test, so normalise before validating.
if [[ "$BOARD" == "M4" || "$BOARD" == "picocalc" ]]; then
    BOARD="PICOCALC"
fi

# Validate board variant
if [[ "$BOARD" != "M1" && "$BOARD" != "M2" && "$BOARD" != "PICOCALC" ]]; then
    echo "Invalid board variant: $BOARD"
    echo "Usage: $0 [M1|M2|PICOCALC] [CPU_MHZ] [PSRAM_MHZ] [FLASH_MHZ] [usb-hid] [clean]"
    echo "  CPU_MHZ:   252, 378, 504  (default: 504)"
    echo "  PSRAM_MHZ: 84, 100, 133, 166  (default: 133)"
    echo "  FLASH_MHZ: flash QMI cap in MHz  (default: 66)"
    echo "  usb-hid:   Enable USB keyboard/mouse (disables USB serial, uses UART)"
    exit 1
fi

echo "Building FRANK Quest:"
echo "  Board: $BOARD"
echo "  CPU:   $CPU MHz"
echo "  PSRAM: $PSRAM MHz"
echo "  Flash: $FLASH MHz"
if [[ "$USB_HID" == "1" ]]; then
    echo "  Input: USB HID keyboard/mouse (UART console)"
else
    echo "  Input: PS/2 keyboard/mouse (USB serial console)"
fi
if [[ "$BOARD" == "PICOCALC" ]]; then
    echo "  Audio: I2S (placeholder pins; PWM audio not implemented)"
    echo "  Video: none (SPI LCD driver not implemented)"
else
    echo "  Audio: I2S"
fi
echo ""

# Clean if requested
if [[ "$CLEAN" == "clean" ]]; then
    echo "Cleaning build directory..."
    rm -rf ./build
fi

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake -DPICO_PLATFORM=rp2350 \
      -DBOARD_VARIANT="$BOARD" \
      -DCPU_SPEED="$CPU" \
      -DPSRAM_SPEED="$PSRAM" \
      -DFLASH_SPEED="$FLASH" \
      -DUSB_HID_ENABLED="$USB_HID" \
      ..

# Build
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "Build complete!"
echo "  Firmware: build/frank-quest.uf2"
if [[ -f frank-quest.uf2 ]]; then
    echo "  Size: $(ls -lh frank-quest.uf2 | awk '{print $5}')"
fi
