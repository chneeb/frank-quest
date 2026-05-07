#!/bin/bash
#
# release.sh - Build release firmware for FRANK Quest (M1 + M2)
#
# Usage: ./release.sh [VERSION]
#   VERSION  - version string (e.g. "1.01"), prompted interactively if omitted
#
# Output format: <prefix>frank-quest_<MAJOR>_<MINOR>.uf2
#
# Build matrix (2 variants):
#   m1p2_frank-quest_*.uf2   — Murmulator 1.x layout
#   m2p2_frank-quest_*.uf2   — FRANK / Murmulator 2.0 layout
#
# Both variants ship with USB HID keyboard/mouse enabled (UART console).
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# Release-wide clock defaults — match build.sh / README "tested" speeds.
# Override with env vars for one-off rebuilds, e.g.
#   RELEASE_CPU_SPEED=252 RELEASE_PSRAM_SPEED=100 ./release.sh
RELEASE_CPU_SPEED="${RELEASE_CPU_SPEED:-504}"
RELEASE_PSRAM_SPEED="${RELEASE_PSRAM_SPEED:-133}"
RELEASE_FLASH_SPEED="${RELEASE_FLASH_SPEED:-66}"

# Build matrix: "board:prefix"
BUILD_MATRIX=(
    "M1:m1p2_"
    "M2:m2p2_"
)

VERSION_FILE="version.txt"

# Read last version or initialize
if [[ -f "$VERSION_FILE" ]]; then
    read -r LAST_MAJOR LAST_MINOR < "$VERSION_FILE"
else
    LAST_MAJOR=1
    LAST_MINOR=0
fi

# Calculate next version (for default suggestion)
NEXT_MINOR=$((LAST_MINOR + 1))
NEXT_MAJOR=$LAST_MAJOR
if [[ $NEXT_MINOR -ge 100 ]]; then
    NEXT_MAJOR=$((NEXT_MAJOR + 1))
    NEXT_MINOR=0
fi

echo ""
echo -e "${CYAN}┌─────────────────────────────────────────────────────────────────┐${NC}"
echo -e "${CYAN}│                    FRANK Quest Release Builder                  │${NC}"
echo -e "${CYAN}└─────────────────────────────────────────────────────────────────┘${NC}"
echo ""
echo -e "Last version: ${YELLOW}${LAST_MAJOR}.$(printf '%02d' $LAST_MINOR)${NC}"
echo -e "Variants: ${CYAN}${#BUILD_MATRIX[@]}${NC} (M1, M2)"
echo -e "Clocks:   CPU=${RELEASE_CPU_SPEED} PSRAM=${RELEASE_PSRAM_SPEED} FLASH=${RELEASE_FLASH_SPEED}"
echo ""

DEFAULT_VERSION="${NEXT_MAJOR}.$(printf '%02d' $NEXT_MINOR)"

# Accept version from command line or prompt interactively
if [[ -n "$1" ]]; then
    INPUT_VERSION="$1"
    echo -e "Version (from command line): ${CYAN}${INPUT_VERSION}${NC}"
else
    read -p "Enter version [default: $DEFAULT_VERSION]: " INPUT_VERSION
    INPUT_VERSION=${INPUT_VERSION:-$DEFAULT_VERSION}
fi

# Parse version (handle both "1.00" and "1 00" formats)
if [[ "$INPUT_VERSION" == *"."* ]]; then
    MAJOR="${INPUT_VERSION%%.*}"
    MINOR="${INPUT_VERSION##*.}"
else
    read -r MAJOR MINOR <<< "$INPUT_VERSION"
fi

# Strip leading zeros for arithmetic, then re-pad
MINOR=$((10#$MINOR))
MAJOR=$((10#$MAJOR))

# Validate
if [[ $MAJOR -lt 0 ]]; then
    echo -e "${RED}Error: Major version must be >= 1${NC}"
    exit 1
fi
if [[ $MINOR -lt 0 || $MINOR -ge 100 ]]; then
    echo -e "${RED}Error: Minor version must be 0-99${NC}"
    exit 1
fi

VERSION="${MAJOR}_$(printf '%02d' $MINOR)"
VERSION_DOT="${MAJOR}.$(printf '%02d' $MINOR)"
echo ""
echo -e "${GREEN}Building release version: ${VERSION_DOT}${NC}"

# Persist new version
echo "$MAJOR $MINOR" > "$VERSION_FILE"

RELEASE_DIR="$SCRIPT_DIR/releases"
mkdir -p "$RELEASE_DIR"

SUCCEEDED=()
FAILED=()

for ENTRY in "${BUILD_MATRIX[@]}"; do
    IFS=':' read -r BOARD PREFIX <<< "$ENTRY"
    LABEL="${PREFIX}${BOARD}"
    OUTPUT_NAME="${PREFIX}frank-quest_${VERSION}.uf2"

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${CYAN}Building: $OUTPUT_NAME (BOARD=$BOARD)${NC}"
    echo ""

    rm -rf build
    mkdir build
    cd build

    if cmake .. \
        -DPICO_PLATFORM=rp2350 \
        -DBOARD_VARIANT="$BOARD" \
        -DCPU_SPEED="$RELEASE_CPU_SPEED" \
        -DPSRAM_SPEED="$RELEASE_PSRAM_SPEED" \
        -DFLASH_SPEED="$RELEASE_FLASH_SPEED" \
        -DUSB_HID_ENABLED=ON > /dev/null 2>&1; then

        if make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4) > /dev/null 2>&1; then
            if [[ -f "frank-quest.uf2" ]]; then
                cp "frank-quest.uf2" "$RELEASE_DIR/$OUTPUT_NAME"
                echo -e "  ${GREEN}✓ $LABEL${NC} → releases/$OUTPUT_NAME"
                SUCCEEDED+=("$OUTPUT_NAME")
            else
                echo -e "  ${RED}✗ $LABEL: UF2 not found${NC}"
                FAILED+=("$LABEL")
            fi
        else
            echo -e "  ${RED}✗ $LABEL: Build failed${NC}"
            FAILED+=("$LABEL")
        fi
    else
        echo -e "  ${RED}✗ $LABEL: CMake configure failed${NC}"
        FAILED+=("$LABEL")
    fi

    cd "$SCRIPT_DIR"
done

# Tidy up the working build directory so the next plain ./build.sh starts fresh.
rm -rf build

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ ${#SUCCEEDED[@]} -gt 0 ]]; then
    echo -e "${GREEN}Succeeded: ${SUCCEEDED[*]}${NC}"
fi
if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo -e "${RED}Failed: ${FAILED[*]}${NC}"
fi

echo ""
echo "Release files:"
for FNAME in "${SUCCEEDED[@]}"; do
    ls -la "$RELEASE_DIR/$FNAME" 2>/dev/null | awk '{printf "  %-55s (%s bytes)\n", $9, $5}'
done
echo ""
echo -e "Version: ${CYAN}${VERSION_DOT}${NC}"

if [[ ${#FAILED[@]} -gt 0 ]]; then
    echo -e "${YELLOW}Warning: ${#FAILED[@]} variant(s) failed to build${NC}"
    exit 1
fi
