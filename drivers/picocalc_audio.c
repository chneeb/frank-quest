/*
 * FRANK Quest
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech>
 * https://github.com/rh1tech/frank-quest
 *
 * Derived from Cabal (https://github.com/project-cabal/cabal).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * PicoCalc PWM audio sink.
 *
 * Provides the same cabal_audio_* API as drivers/audio/audio.c, so the engine
 * side is unchanged: OSystem_RP2350 still calls cabal_audio_process_frame()
 * once per frame, the mixer still produces 44100 Hz 16-bit stereo, and only
 * what happens underneath differs. drivers/audio/audio.c is not compiled on
 * this board.
 *
 * Hardware, from the PicoCalc mainboard schematic (V2.0):
 *
 *   GP26 = PWM_L, GP27 = PWM_R -> U501 (NC7WZ16 dual buffer) -> per-channel
 *   220R + 100nF low-pass -> 100R, 47uF DC block, 1.8K -> PCM_AUDIO_L/R ->
 *   SW501 headphone jack, switching to the speaker via HP_DET.
 *
 * Two consequences drive the design:
 *
 * - GP26 and GP27 are channel A and channel B of the *same* PWM slice, and a
 *   slice's CC register packs both levels into one 32-bit word. So a single
 *   DMA channel doing 32-bit writes drives both channels. That matters: the
 *   SPI LCD already holds a DMA channel and they are not plentiful.
 *
 * - The low-pass sits at ~7.2 kHz (220R/100nF, one pole). At a 44.1 kHz
 *   carrier that is only ~16 dB of PWM residue suppression, so the carrier
 *   runs at 88.2 kHz instead -- each mixer sample emitted twice -- for ~6 dB
 *   more. It costs nothing but buffer space at this clock.
 */

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"

#include "board_config.h"
#include "audio.h"

#ifdef BOARD_PICOCALC

//=============================================================================
// Configuration
//=============================================================================

// PWM resolution. 10 bits at 88.2 kHz needs a clkdiv of ~5.6 at 504 MHz and
// ~2.8 at the 252 MHz fallback, both comfortably in range.
#define PWM_BITS       10
#define PWM_LEVELS     (1u << PWM_BITS)

// Output samples per second. Two per mixer sample (see file comment).
#define OVERSAMPLE     2
#define OUTPUT_RATE    (AUDIO_SAMPLE_RATE * OVERSAMPLE)

// Mixer frames per buffer. 512 frames is ~11.6 ms, so the two buffers give
// ~23 ms of slack -- the same order as the I2S path's.
#define FRAMES_PER_BUFFER 512
#define WORDS_PER_BUFFER  (FRAMES_PER_BUFFER * OVERSAMPLE)
#define BUFFER_COUNT      2

//=============================================================================
// State
//=============================================================================

static uint32_t __attribute__((aligned(4))) pwm_buffers[BUFFER_COUNT][WORDS_PER_BUFFER];
static volatile uint32_t buffers_free_mask = 0;

static int16_t __attribute__((aligned(4))) mixed_buffer[FRAMES_PER_BUFFER * 2];

static void (*g_mixer_callback)(uint8_t *stream, int len) = NULL;

static int dma_chan[BUFFER_COUNT] = { -1, -1 };
static uint pwm_slice = 0;
static bool stereo = true;

static bool audio_initialized = false;
static bool audio_enabled = true;
static int master_volume = 255;
static int8_t volume_shift = 0;

// Output silence briefly at startup, matching the I2S path: the amp and the
// 47uF DC blocks need a moment to settle or the first frames thump.
#define STARTUP_MUTE_FRAMES 8
static int startup_frame_counter = 0;

//=============================================================================
// DMA
//=============================================================================

static void __isr audio_dma_irq(void) {
    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (dma_chan[i] >= 0 && (dma_hw->ints0 & (1u << dma_chan[i]))) {
            dma_hw->ints0 = 1u << dma_chan[i];
            // The channel has handed over to its partner via chain_to, so this
            // buffer is now idle and can be refilled.
            buffers_free_mask |= (1u << i);
            dma_channel_set_read_addr(dma_chan[i], pwm_buffers[i], false);
        }
    }
}

//=============================================================================
// Sample conversion
//=============================================================================

// 16-bit signed -> PWM_BITS unsigned, centred at half scale.
static inline uint32_t to_level(int32_t s) {
    s >>= (16 - PWM_BITS);              // 16-bit signed -> PWM_BITS signed
    s += (int32_t)(PWM_LEVELS / 2);     // centre
    if (s < 0) s = 0;
    if (s > (int32_t)(PWM_LEVELS - 1)) s = PWM_LEVELS - 1;
    return (uint32_t)s;
}

static void fill_buffer(int idx) {
    const int16_t *src = mixed_buffer;
    uint32_t *dst = pwm_buffers[idx];

    for (int i = 0; i < FRAMES_PER_BUFFER; i++) {
        int32_t l = src[i * 2 + 0];
        int32_t r = src[i * 2 + 1];

        if (volume_shift > 0) {
            l >>= volume_shift;
            r >>= volume_shift;
        }

        if (!stereo) {
            l = (l + r) / 2;
            r = l;
        }

        // Channel A (GP26, left) in the low half, channel B (GP27, right) in
        // the high half -- one 32-bit write sets both.
        const uint32_t word = (to_level(r) << 16) | to_level(l);

        for (int o = 0; o < OVERSAMPLE; o++) {
            *dst++ = word;
        }
    }
}

//=============================================================================
// cabal_audio_* API
//=============================================================================

void cabal_audio_set_mixer_callback(void (*callback)(uint8_t *stream, int len)) {
    g_mixer_callback = callback;
}

bool cabal_audio_init(void) {
    audio_initialized = false;
    startup_frame_counter = 0;
    buffers_free_mask = 0;
    memset(pwm_buffers, 0, sizeof(pwm_buffers));

    // Both pins must sit on one slice for the packed-CC trick. They do on a
    // stock PicoCalc (GP26/GP27 are 5A/5B); fall back to mono if a board ever
    // wires them apart, rather than driving one channel with the other's data.
    pwm_slice = pwm_gpio_to_slice_num(PICOCALC_AUDIO_PIN_L);
    stereo = (pwm_gpio_to_slice_num(PICOCALC_AUDIO_PIN_R) == pwm_slice);

    gpio_set_function(PICOCALC_AUDIO_PIN_L, GPIO_FUNC_PWM);
    if (stereo) {
        gpio_set_function(PICOCALC_AUDIO_PIN_R, GPIO_FUNC_PWM);
    }

    // One PWM period per output sample.
    float div = (float)clock_get_hz(clk_sys) / ((float)OUTPUT_RATE * (float)PWM_LEVELS);
    if (div < 1.0f) div = 1.0f;

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, PWM_LEVELS - 1);
    pwm_init(pwm_slice, &cfg, true);

    // Park at mid-scale so the output sits at the DC blocks' centre.
    pwm_set_gpio_level(PICOCALC_AUDIO_PIN_L, PWM_LEVELS / 2);
    if (stereo) {
        pwm_set_gpio_level(PICOCALC_AUDIO_PIN_R, PWM_LEVELS / 2);
    }

    for (int i = 0; i < BUFFER_COUNT; i++) {
        dma_chan[i] = dma_claim_unused_channel(true);
    }

    // Two channels chained to each other: playback never stops, and the IRQ
    // only has to refill behind the playhead.
    for (int i = 0; i < BUFFER_COUNT; i++) {
        dma_channel_config dc = dma_channel_get_default_config(dma_chan[i]);
        channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
        channel_config_set_read_increment(&dc, true);
        channel_config_set_write_increment(&dc, false);
        channel_config_set_dreq(&dc, pwm_get_dreq(pwm_slice));
        channel_config_set_chain_to(&dc, dma_chan[(i + 1) % BUFFER_COUNT]);
        dma_channel_configure(dma_chan[i], &dc,
                              &pwm_hw->slice[pwm_slice].cc,
                              pwm_buffers[i],
                              WORDS_PER_BUFFER,
                              false);
        dma_channel_set_irq0_enabled(dma_chan[i], true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, audio_dma_irq);
    irq_set_enabled(DMA_IRQ_0, true);

    cabal_audio_set_volume(master_volume);

    audio_initialized = true;
    audio_enabled = true;

    dma_channel_start(dma_chan[0]);
    return true;
}

void cabal_audio_shutdown(void) {
    if (!audio_initialized) return;

    for (int i = 0; i < BUFFER_COUNT; i++) {
        if (dma_chan[i] < 0) continue;
        dma_channel_set_irq0_enabled(dma_chan[i], false);
        dma_channel_abort(dma_chan[i]);
        dma_channel_unclaim(dma_chan[i]);
        dma_chan[i] = -1;
    }
    irq_set_enabled(DMA_IRQ_0, false);
    irq_remove_handler(DMA_IRQ_0, audio_dma_irq);

    // Leave the pins parked at mid-scale rather than pulled to a rail.
    pwm_set_gpio_level(PICOCALC_AUDIO_PIN_L, PWM_LEVELS / 2);
    if (stereo) {
        pwm_set_gpio_level(PICOCALC_AUDIO_PIN_R, PWM_LEVELS / 2);
    }

    audio_initialized = false;
}

bool cabal_audio_is_initialized(void) {
    return audio_initialized;
}

void cabal_audio_process_frame(void) {
    if (!audio_initialized || !audio_enabled) return;

    while (buffers_free_mask != 0) {
        int idx = -1;
        for (int i = 0; i < BUFFER_COUNT; i++) {
            if (buffers_free_mask & (1u << i)) { idx = i; break; }
        }
        if (idx < 0) break;

        if (startup_frame_counter < STARTUP_MUTE_FRAMES) {
            startup_frame_counter++;
            memset(mixed_buffer, 0, sizeof(mixed_buffer));
        } else {
            memset(mixed_buffer, 0, sizeof(mixed_buffer));
            if (g_mixer_callback) {
                g_mixer_callback((uint8_t *)mixed_buffer, FRAMES_PER_BUFFER * 4);
            }
        }

        fill_buffer(idx);
        buffers_free_mask &= ~(1u << idx);
    }
}

void cabal_audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 255) volume = 255;
    master_volume = volume;

    // Same coarse shift-based attenuation the I2S path uses.
    if (volume >= 224)      volume_shift = 0;
    else if (volume >= 160) volume_shift = 1;
    else if (volume >= 96)  volume_shift = 2;
    else if (volume >= 32)  volume_shift = 3;
    else                    volume_shift = 4;
}

int cabal_audio_get_volume(void) {
    return master_volume;
}

void cabal_audio_set_enabled(bool enabled) {
    audio_enabled = enabled;
}

bool cabal_audio_is_enabled(void) {
    return audio_enabled;
}

#endif // BOARD_PICOCALC
