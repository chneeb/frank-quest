# FRANK Quest I2S Audio Driver
#
# Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
# https://github.com/rh1tech/frank-quest
#
# Derived from Cabal (https://github.com/project-cabal/cabal).
# SPDX-License-Identifier: GPL-2.0-or-later

# Generate PIO header from assembly
pico_generate_pio_header(frank-quest ${CMAKE_CURRENT_LIST_DIR}/audio_i2s.pio)

# Add audio source to main executable (no separate library needed)
target_sources(frank-quest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/audio.c
)

target_include_directories(frank-quest PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(frank-quest
    hardware_pio
    hardware_dma
)
