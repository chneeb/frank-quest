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
 * PicoCalc SPI TFT driver.
 *
 * Implements the same graphics_* API as drivers/HDMI.c, so everything above
 * the driver is unchanged: the engine still draws 8bpp palette indices into a
 * 320x240 PSRAM framebuffer and the backend still flips buffers. This file
 * replaces HDMI.c in the build when BOARD_PICOCALC is selected.
 *
 * Two differences from HDMI matter:
 *
 * 1. HDMI scans the framebuffer continuously; SPI has to be pushed. The push
 *    happens in graphics_set_buffer(), which cabal_swap_buffers() already
 *    calls once per completed frame (src/main.c:59). That keeps the hook out
 *    of the backend entirely -- see "Push point" below.
 *
 * 2. The framebuffer lives in PSRAM and must not be DMA'd from directly, so
 *    each row is converted 8bpp -> RGB565 into a small SRAM line buffer and
 *    that is what the DMA sends. Two line buffers alternate: row N+1 is
 *    converted while row N is still in flight.
 *
 * Geometry: the framebuffer is 320x240 (CABAL_HDMI_HEIGHT) with the 320x200
 * game image already centred at y=20 by the backend. The panel is 320x320, so
 * the whole 240-row framebuffer is written at y=40 and the game image lands at
 * y=60 -- the offset PICOCALC_PORT.md specifies.
 *
 * Panel setup (register sequence, 0x3A = 0x65, PIO + DMA at 75 MHz) follows
 * ~/Source/shapones, samples/v3/picocalc.cpp, which runs this panel on real
 * hardware. Note shapones sets its clock as SYS_CLK_FREQ/4 with a 300 MHz
 * system clock; that ratio is meaningless at FRANK Quest's 504 MHz, so the
 * divisor here is derived from a target frequency instead.
 */

#include <string.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"

#include "board_config.h"
#include "HDMI.h"
#include "picocalc_lcd.pio.h"

#ifdef BOARD_PICOCALC

//=============================================================================
// Configuration
//=============================================================================

// Panel dimensions. The framebuffer is narrower/shorter than the panel; the
// difference is letterboxing, cleared once at init and never rewritten.
#define LCD_PANEL_WIDTH  320
#define LCD_PANEL_HEIGHT 320

// Target SPI bit rate. 75 MHz is what shapones runs on this panel. The SM
// needs two cycles per bit, so it is clocked at twice this.
#define LCD_SPI_HZ (75u * 1000u * 1000u)

#define LCD_PIO pio0

// ILI9488/ST7365P commands used here
#define CMD_SLPOUT   0x11
#define CMD_INVON    0x21
#define CMD_DISPON   0x29
#define CMD_CASET    0x2A
#define CMD_RASET    0x2B
#define CMD_RAMWR    0x2C
#define CMD_MADCTL   0x36
#define CMD_COLMOD   0x3A

//=============================================================================
// State
//=============================================================================

static uint8_t *graphics_buffer = NULL;
static int graphics_buffer_width = 320;
static int graphics_buffer_height = 240;
static int graphics_buffer_shift_x = 0;
static int graphics_buffer_shift_y = 0;

static uint lcd_sm = 0;
static int lcd_dma = -1;
static bool lcd_ready = false;

// Palette as the panel wants it on the wire: RGB565, high byte first. Stored
// as byte pairs rather than uint16_t so the DMA (which moves bytes) sends them
// in that order regardless of the core's endianness.
static uint8_t palette565[256][2];

// Two SRAM line buffers, 320 px * 2 B. The framebuffer is in PSRAM and cannot
// be a DMA source, so every row lands here first.
static uint8_t line_buf[2][LCD_PANEL_WIDTH * 2];

//=============================================================================
// Low-level SPI
//=============================================================================

static inline void lcd_wait_idle(void) {
    // FIFO empty is not enough: the last byte is still being shifted out.
    while (!pio_sm_is_tx_fifo_empty(LCD_PIO, lcd_sm)) {
        tight_loop_contents();
    }
    // 8 bits * 2 cycles, plus slack, at the SM clock.
    busy_wait_us(2);
}

static void lcd_write_blocking(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        pio_sm_put_blocking(LCD_PIO, lcd_sm, (uint32_t)data[i] << 24);
    }
}

static void lcd_command(uint8_t cmd, const uint8_t *data, size_t len) {
    gpio_put(LCD_PIN_DC, 0);      // command
    gpio_put(LCD_PIN_CS, 0);
    lcd_write_blocking(&cmd, 1);
    if (len) {
        lcd_wait_idle();
        gpio_put(LCD_PIN_DC, 1);  // data
        lcd_write_blocking(data, len);
    }
    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);
}

static inline void lcd_command_only(uint8_t cmd) {
    lcd_command(cmd, NULL, 0);
}

// Set the destination rectangle for the next RAMWR.
static void lcd_set_window(int x0, int y0, int x1, int y1) {
    uint8_t c[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    uint8_t p[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
    lcd_command(CMD_CASET, c, sizeof(c));
    lcd_command(CMD_RASET, p, sizeof(p));
}

//=============================================================================
// Panel init
//=============================================================================

static void lcd_panel_init(void) {
    // Hardware reset
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(1);
    gpio_put(LCD_PIN_RST, 0);
    sleep_ms(10);
    gpio_put(LCD_PIN_RST, 1);
    sleep_ms(10);

    // Unlock the manufacturer register set
    { uint8_t d[] = {0xC3}; lcd_command(0xF0, d, sizeof(d)); }
    { uint8_t d[] = {0x96}; lcd_command(0xF0, d, sizeof(d)); }

    // Memory access control: MX | MV, RGB order
    { uint8_t d[] = {0x48}; lcd_command(CMD_MADCTL, d, sizeof(d)); }

    // Pixel format. 0x65 = 18-bit RGB interface, 16-bit MCU (SPI) interface.
    // The SPI half is the one that matters: 2 bytes/pixel, not 3. The ILI9488
    // datasheet claims SPI is 18-bit only; it is not true of this panel.
    { uint8_t d[] = {0x65}; lcd_command(CMD_COLMOD, d, sizeof(d)); }

    { uint8_t d[] = {0xA0}; lcd_command(0xB1, d, sizeof(d)); }  // frame rate
    { uint8_t d[] = {0x00}; lcd_command(0xB4, d, sizeof(d)); }  // inversion ctl
    { uint8_t d[] = {0xC6}; lcd_command(0xB7, d, sizeof(d)); }  // entry mode
    { uint8_t d[] = {0x02, 0xE0}; lcd_command(0xB9, d, sizeof(d)); }

    { uint8_t d[] = {0x80, 0x06}; lcd_command(0xC0, d, sizeof(d)); }  // power 1
    { uint8_t d[] = {0x15}; lcd_command(0xC1, d, sizeof(d)); }        // power 2
    { uint8_t d[] = {0xA7}; lcd_command(0xC2, d, sizeof(d)); }        // power 3
    { uint8_t d[] = {0x04}; lcd_command(0xC5, d, sizeof(d)); }        // VCOM

    { uint8_t d[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xAA, 0x33};
      lcd_command(0xE8, d, sizeof(d)); }

    { uint8_t d[] = {0xF0, 0x06, 0x0F, 0x05, 0x04, 0x20, 0x37, 0x33,
                     0x4C, 0x37, 0x13, 0x14, 0x2B, 0x31};
      lcd_command(0xE0, d, sizeof(d)); }   // positive gamma
    { uint8_t d[] = {0xF0, 0x11, 0x1B, 0x11, 0x0F, 0x0A, 0x37, 0x43,
                     0x4C, 0x37, 0x13, 0x13, 0x2C, 0x32};
      lcd_command(0xE1, d, sizeof(d)); }   // negative gamma

    // Re-lock the manufacturer register set
    { uint8_t d[] = {0x3C}; lcd_command(0xF0, d, sizeof(d)); }
    { uint8_t d[] = {0x69}; lcd_command(0xF0, d, sizeof(d)); }

    { uint8_t d[] = {0x00}; lcd_command(0x35, d, sizeof(d)); }  // tearing off

    lcd_command_only(CMD_SLPOUT);
    sleep_ms(120);
    lcd_command_only(CMD_INVON);
    lcd_command_only(CMD_DISPON);
    sleep_ms(120);
}

// Paint the whole panel one colour. Used once at init so the letterbox bars
// above and below the framebuffer are black and stay that way.
static void lcd_fill_panel(uint16_t color565) {
    uint8_t *buf = line_buf[0];
    for (int x = 0; x < LCD_PANEL_WIDTH; x++) {
        buf[x * 2 + 0] = (uint8_t)(color565 >> 8);
        buf[x * 2 + 1] = (uint8_t)(color565 & 0xFF);
    }

    lcd_set_window(0, 0, LCD_PANEL_WIDTH - 1, LCD_PANEL_HEIGHT - 1);
    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    uint8_t cmd = CMD_RAMWR;
    lcd_write_blocking(&cmd, 1);
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 1);

    for (int y = 0; y < LCD_PANEL_HEIGHT; y++) {
        dma_channel_transfer_from_buffer_now(lcd_dma, buf, LCD_PANEL_WIDTH * 2);
        dma_channel_wait_for_finish_blocking(lcd_dma);
    }
    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);
}

//=============================================================================
// Frame push
//=============================================================================

// Convert one framebuffer row of palette indices into an SRAM line buffer.
// Reads straight from PSRAM, which is fine for the CPU; only the DMA is
// barred from doing it.
static inline void convert_row(const uint8_t *src, uint8_t *dst, int width) {
    for (int x = 0; x < width; x++) {
        const uint8_t *c = palette565[src[x]];
        dst[x * 2 + 0] = c[0];
        dst[x * 2 + 1] = c[1];
    }
}

static void lcd_push_frame(const uint8_t *fb) {
    const int w = graphics_buffer_width;
    const int h = graphics_buffer_height;
    if (w <= 0 || h <= 0 || w > LCD_PANEL_WIDTH || h > LCD_PANEL_HEIGHT) return;

    // Centre the framebuffer in the panel: 320x240 -> y=40..279, which puts
    // the game's 320x200 (already centred at y=20 within the framebuffer) at
    // y=60 on the panel.
    const int x0 = (LCD_PANEL_WIDTH - w) / 2;
    const int y0 = (LCD_PANEL_HEIGHT - h) / 2;

    lcd_set_window(x0, y0, x0 + w - 1, y0 + h - 1);

    gpio_put(LCD_PIN_DC, 0);
    gpio_put(LCD_PIN_CS, 0);
    uint8_t cmd = CMD_RAMWR;
    lcd_write_blocking(&cmd, 1);
    lcd_wait_idle();
    gpio_put(LCD_PIN_DC, 1);

    // Ping-pong: convert row N+1 while row N is still going out.
    int slot = 0;
    convert_row(fb, line_buf[slot], w);

    for (int y = 0; y < h; y++) {
        dma_channel_transfer_from_buffer_now(lcd_dma, line_buf[slot], (uint32_t)w * 2);

        if (y + 1 < h) {
            int next = slot ^ 1;
            convert_row(fb + (size_t)(y + 1) * w, line_buf[next], w);
            dma_channel_wait_for_finish_blocking(lcd_dma);
            slot = next;
        } else {
            dma_channel_wait_for_finish_blocking(lcd_dma);
        }
    }

    lcd_wait_idle();
    gpio_put(LCD_PIN_CS, 1);
}

//=============================================================================
// graphics_* API (mirrors drivers/HDMI.c)
//=============================================================================

void graphics_set_buffer(uint8_t *buffer) {
    graphics_buffer = buffer;

    // Push point. cabal_swap_buffers() calls this once per finished frame
    // (src/main.c:59), so hooking here means the backend needs no PicoCalc
    // special case. Before graphics_init() this is just a store -- main.c
    // sets the buffer before bringing the display up.
    if (lcd_ready && buffer) {
        lcd_push_frame(buffer);
    }
}

uint8_t *graphics_get_buffer(void) {
    return graphics_buffer;
}

uint32_t graphics_get_width(void) {
    return (uint32_t)graphics_buffer_width;
}

uint32_t graphics_get_height(void) {
    return (uint32_t)graphics_buffer_height;
}

void graphics_set_res(int w, int h) {
    graphics_buffer_width = w;
    graphics_buffer_height = h;
}

void graphics_set_shift(int x, int y) {
    graphics_buffer_shift_x = x;
    graphics_buffer_shift_y = y;
}

void graphics_set_palette(uint8_t i, uint32_t color888) {
    const uint8_t r = (color888 >> 16) & 0xFF;
    const uint8_t g = (color888 >> 8) & 0xFF;
    const uint8_t b = color888 & 0xFF;
    const uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    palette565[i][0] = (uint8_t)(c >> 8);
    palette565[i][1] = (uint8_t)(c & 0xFF);
}

void graphics_init(g_out out) {
    (void)out;  // there is only one output on this board

    // Control lines
    gpio_init(LCD_PIN_CS);
    gpio_init(LCD_PIN_DC);
    gpio_init(LCD_PIN_RST);
    gpio_set_dir(LCD_PIN_CS, GPIO_OUT);
    gpio_set_dir(LCD_PIN_DC, GPIO_OUT);
    gpio_set_dir(LCD_PIN_RST, GPIO_OUT);
    gpio_put(LCD_PIN_CS, 1);
    gpio_put(LCD_PIN_DC, 1);
    gpio_put(LCD_PIN_RST, 1);

    // PIO SPI
    uint offset = pio_add_program(LCD_PIO, &picocalc_lcd_program);
    lcd_sm = pio_claim_unused_sm(LCD_PIO, true);

    pio_gpio_init(LCD_PIO, LCD_PIN_SCK);
    pio_gpio_init(LCD_PIO, LCD_PIN_MOSI);
    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_SCK, 1, true);
    pio_sm_set_consecutive_pindirs(LCD_PIO, lcd_sm, LCD_PIN_MOSI, 1, true);

    pio_sm_config c = picocalc_lcd_program_get_default_config(offset);
    sm_config_set_out_pins(&c, LCD_PIN_MOSI, 1);
    sm_config_set_sideset_pins(&c, LCD_PIN_SCK);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);
    // Shift out MSB first, autopull at 8 bits.
    sm_config_set_out_shift(&c, false, true, 8);

    // Two SM cycles per bit, so the SM runs at twice the wire rate. Derived
    // from the live system clock rather than a fixed ratio: this board boots
    // at 504 MHz and falls back to 252 MHz, and both must land on 75 MHz.
    float div = (float)clock_get_hz(clk_sys) / (float)(LCD_SPI_HZ * 2u);
    if (div < 1.0f) div = 1.0f;
    sm_config_set_clkdiv(&c, div);

    pio_sm_init(LCD_PIO, lcd_sm, offset, &c);
    pio_sm_set_enabled(LCD_PIO, lcd_sm, true);

    // DMA: bytes into the PIO TX FIFO, paced by the SM.
    lcd_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(lcd_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(LCD_PIO, lcd_sm, true));
    dma_channel_configure(lcd_dma, &dc, &LCD_PIO->txf[lcd_sm], NULL, 0, false);

    lcd_panel_init();

    // Black the whole panel once, so the letterbox bars are correct and the
    // per-frame push only ever has to touch the framebuffer's rows.
    lcd_fill_panel(0x0000);

    lcd_ready = true;

    // main.c sets the buffer before calling us; show it now.
    if (graphics_buffer) {
        lcd_push_frame(graphics_buffer);
    }
}

//=============================================================================
// HDMI-only entry points, kept so the shared header stays honest
//=============================================================================

void graphics_restore_sync_colors(void) {
    // HDMI reserves palette entries 240-243 for sync levels. SPI has no such
    // constraint, so there is nothing to restore.
}

void graphics_set_bgcolor(uint32_t color888) {
    (void)color888;  // letterbox bars are painted once at init
}

void set_palette(uint8_t n) {
    (void)n;
}

void startVIDEO(uint8_t vol) {
    (void)vol;
}

struct video_mode_t graphics_get_video_mode(int mode) {
    (void)mode;
    struct video_mode_t m = { LCD_PANEL_WIDTH, LCD_PANEL_WIDTH, 60, 0 };
    return m;
}

#endif // BOARD_PICOCALC
