/* display_port_st7796.c - the Freenove FNK0104S's ST7796S, a 320 x 480 IPS LCD
 * over 4-wire SPI, driven landscape (MADCTL MV) so the 480 x 320 tank goes
 * out row by row, byte-swapped, from two 20-row DMA stripes
 * (docs/board-fnk0104s.md; the spec's R#7, R#8, R#9, R#15).
 *
 * Stripe ownership is per buffer: every queued transfer gets a sequence
 * number, the done callback counts completions (SPI transfers finish in the
 * order they were queued), and a writer waits until the LAST transfer that
 * used ITS buffer is done. The buffer index carries across frames, so the
 * stripe count never matters. Brightness is LEDC PWM on the backlight; the
 * panel has no reset pin of its own (it shares CHIP_PU).
 *
 * What esp_lcd's SPI panel IO does underneath (read in IDF v5.4.1 and v5.5.5,
 * esp_lcd/spi/esp_lcd_panel_io_spi.c): tx_param, and tx_color with a command,
 * first wait out every queued colour transfer; one tx_color of a stripe is one
 * SPI transaction (the bus's max_transfer_sz is a stripe), and the done
 * callback fires once per tx_color call, after its last chunk, either way. */
#include "display_port.h"
#include "board_pins.h"
#include "tank.h"
#include "esp_lcd_panel_io.h"
#include "driver/spi_master.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "st7796";
#define LCD_HOST      SPI2_HOST
#define STRIPE_ROWS   20                                  /* 320 / 20 = 16 stripes; 19,200 B each */
#define STRIPE_PX     (TANK_W * STRIPE_ROWS)
#define STRIPE_BYTES  (STRIPE_PX * 2)
#define DMA_INTERNAL  (MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)
#define MAD_MV        0x20
#define MAD_MX        0x40
#define MAD_MY        0x80
#define MAD_BGR       0x08
#define MAD_UPRIGHT   (MAD_MV | MAD_BGR)                  /* TFT_eSPI rotation 1 with Freenove's BGR order */
#define MAD_TURNED    (MAD_MV | MAD_MX | MAD_MY | MAD_BGR)/* rotation 3: the picture on its head */
#define BL_TIMER      LEDC_TIMER_1
#define BL_CHANNEL    LEDC_CHANNEL_1

static esp_lcd_panel_io_handle_t s_io;
static i2c_master_bus_handle_t s_i2c;
static uint16_t *s_stripe[2];
static uint32_t s_seq_of[2];                 /* the sequence number of the last transfer queued from each buffer */
static uint32_t s_queued;                    /* transfers queued, ever */
static volatile uint32_t s_done;             /* transfers completed, ever (the ISR's) */
static SemaphoreHandle_t s_done_sig;
static int s_next;                           /* the buffer the next stripe fills: carried across frames */
static bool s_inverted, s_inv_sent;
static uint8_t s_brightness = 0xFF;
static bool s_light_owed;                    /* after init / sleep: the backlight comes up after one whole frame */
static int64_t s_slpout_us;                  /* SLPOUT's time: SLPIN may not follow within 120 ms (datasheet p.159) */
static int64_t s_prof_wait, s_prof_send;
static bool s_ok;                            /* init finished: the bus, the IO, the stripes and the semaphore all exist */

static bool on_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *ev, void *ctx) {
    (void)io; (void)ev; (void)ctx;            /* (ev is NULL from the SPI panel IO) */
    s_done++;
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done_sig, &hp);
    return hp == pdTRUE;
}
static void wait_done(uint32_t seq) {                     /* until transfer #seq (1-based) is off the wire */
    int64_t t = esp_timer_get_time();
    while ((int32_t)(s_done - seq) < 0) xSemaphoreTake(s_done_sig, pdMS_TO_TICKS(100));
    s_prof_wait += esp_timer_get_time() - t;
}
static void drain(void) { wait_done(s_queued); }
static void cmd(uint8_t c, const uint8_t *p, size_t n) { esp_lcd_panel_io_tx_param(s_io, c, p, n); }

i2c_master_bus_handle_t board_i2c_bus(void) { return s_i2c; }
bool board_is_v2(void) { return false; }
bool board_is_round(void) { return false; }
bool board_is_watch(void) { return false; }
bool board_is_lcd40(void) { return true; }
bool board_has_expander(void) { return false; }
int  board_pwr_sense_pin(void) { return -1; }
void display_port_frame_origin(int *px, int *py) { *px = 0; *py = 0; }
void display_port_set_view(int view) { (void)view; }
int  display_port_view(void) { return DISPLAY_VIEW_FIT; }
void display_port_panel_to_tank(int px, int py, float *tx, float *ty) { *tx = (float)px; *ty = (float)py; }
void display_port_flush_prof(int64_t *wait_us, int64_t *send_us) { *wait_us = s_prof_wait; *send_us = s_prof_send; s_prof_wait = s_prof_send = 0; }
void display_port_set_inverted(bool inverted) { s_inverted = inverted; }   /* sent between frames (flush), after a drain */

static void backlight(uint8_t level) {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, level);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}
void display_port_set_brightness(uint8_t level) { s_brightness = level; if (!s_light_owed) backlight(level); }
uint8_t display_port_brightness(void) { return s_brightness; }

static const struct { uint8_t c; uint8_t n; uint8_t p[14]; uint16_t ms; } INIT[] = {
    { 0x01, 0, {0}, 120 },                                /* SWRESET */
    { 0x11, 0, {0}, 120 },                                /* SLPOUT */
    { 0xF0, 1, {0xC3}, 0 }, { 0xF0, 1, {0x96}, 0 },       /* command set 2, parts I and II */
    { 0x36, 1, {MAD_UPRIGHT}, 0 },                        /* MADCTL: landscape, BGR */
    { 0x3A, 1, {0x55}, 0 },                               /* 16 bpp */
    { 0xB4, 1, {0x01}, 0 },                               /* 1-dot inversion */
    { 0xB6, 3, {0x80, 0x02, 0x3B}, 0 },
    { 0xE8, 8, {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33}, 0 },
    { 0xC1, 1, {0x06}, 0 }, { 0xC2, 1, {0xA7}, 0 }, { 0xC5, 1, {0x18}, 120 },
    { 0xE0, 14, {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F, 0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B}, 0 },
    { 0xE1, 14, {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B, 0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B}, 120 },
    { 0xF0, 1, {0x3C}, 0 }, { 0xF0, 1, {0x69}, 120 },     /* command set 2 off */
    { 0x21, 0, {0}, 0 },                                  /* INVON: Freenove's TFT_INVERSION_ON */
    { 0x29, 0, {0}, 0 },                                  /* DISPON (the backlight waits for a frame) */
};                                                        /* (TFT_eSPI's ST7796_Init.h, Freenove's FNK0104S setup) */

static void send_frame(const uint16_t *fb) {
    if (s_inverted != s_inv_sent) {                       /* the flip: between frames, the wire empty */
        drain();                                          /* (tx_param waits for the wire too: this keeps the count square) */
        uint8_t m = s_inverted ? MAD_TURNED : MAD_UPRIGHT; cmd(0x36, &m, 1);
        s_inv_sent = s_inverted;
    }
    static const uint8_t caset[4] = { 0, 0, (TANK_W - 1) >> 8, (TANK_W - 1) & 0xFF };
    static const uint8_t raset[4] = { 0, 0, (TANK_H - 1) >> 8, (TANK_H - 1) & 0xFF };
    cmd(0x2A, caset, 4); cmd(0x2B, raset, 4);             /* (tx_param waits out queued colour transfers itself) */
    for (int y = 0; y < TANK_H; y += STRIPE_ROWS) {
        int b = s_next; s_next ^= 1;
        wait_done(s_seq_of[b]);                           /* this buffer's last transfer is off the wire */
        const uint16_t *src = fb + (size_t)y * TANK_W;
        uint16_t *dst = s_stripe[b];
        for (int i = 0; i < STRIPE_PX; i++) dst[i] = (uint16_t)((src[i] << 8) | (src[i] >> 8));   /* the panel wants big-endian */
        int64_t t = esp_timer_get_time();
        /* RAMWR, then RAMWR continue. A command first, so the driver waits out the
           other buffer's transfer before this one goes: the fill above overlaps it */
        esp_err_t err = esp_lcd_panel_io_tx_color(s_io, y == 0 ? 0x2C : 0x3C, dst, STRIPE_BYTES);
        s_prof_send += esp_timer_get_time() - t;
        if (err == ESP_OK) s_seq_of[b] = ++s_queued;      /* not queued: nothing of this buffer's is in flight to wait for */
    }
}
void display_port_flush(const uint16_t *fb) {
    if (!s_ok) return;                                    /* a failed init (logged once there): no panel to feed */
    send_frame(fb);
    if (s_light_owed) { drain(); backlight(s_brightness); s_light_owed = false; }   /* a whole picture first: no garbage flash */
}

bool display_port_init(void) {
    static const gpio_num_t held[] = { F_PIN_LCD_CS, F_PIN_LCD_BL, F_PIN_TP_RST, F_PIN_LCD_SCLK, F_PIN_LCD_MOSI, F_PIN_LCD_DC };
    for (size_t i = 0; i < sizeof held / sizeof held[0]; i++) gpio_hold_dis(held[i]);   /* a deep sleep's holds end here */
    i2c_master_bus_config_t bus = { .i2c_port = I2C_NUM_0, .sda_io_num = F_PIN_I2C_SDA, .scl_io_num = F_PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true };
    if (i2c_new_master_bus(&bus, &s_i2c) != ESP_OK) { ESP_LOGE(TAG, "no I2C bus"); return false; }
    ledc_timer_config_t tc = { .speed_mode = LEDC_LOW_SPEED_MODE, .duty_resolution = LEDC_TIMER_8_BIT, .timer_num = BL_TIMER,
                               .freq_hz = 20000, .clk_cfg = LEDC_AUTO_CLK };
    ledc_channel_config_t cc = { .gpio_num = F_PIN_LCD_BL, .speed_mode = LEDC_LOW_SPEED_MODE, .channel = BL_CHANNEL,
                                 .timer_sel = BL_TIMER, .duty = 0, .hpoint = 0 };
    if (ledc_timer_config(&tc) != ESP_OK || ledc_channel_config(&cc) != ESP_OK) { ESP_LOGE(TAG, "no backlight PWM"); return false; }
    spi_bus_config_t sb = { .sclk_io_num = F_PIN_LCD_SCLK, .mosi_io_num = F_PIN_LCD_MOSI, .miso_io_num = -1,
                            .quadwp_io_num = -1, .quadhd_io_num = -1, .max_transfer_sz = STRIPE_BYTES };
    if (spi_bus_initialize(LCD_HOST, &sb, SPI_DMA_CH_AUTO) != ESP_OK) { ESP_LOGE(TAG, "no SPI bus"); return false; }
    s_done_sig = xSemaphoreCreateBinary();
    if (!s_done_sig) { ESP_LOGE(TAG, "no semaphore"); return false; }
    esp_lcd_panel_io_spi_config_t io = { .cs_gpio_num = F_PIN_LCD_CS, .dc_gpio_num = F_PIN_LCD_DC, .spi_mode = 0,
        .pclk_hz = CONFIG_POCKET_TANK_LCD40_SPI_MHZ * 1000 * 1000, .trans_queue_depth = 2, .on_color_trans_done = on_done,
        .lcd_cmd_bits = 8, .lcd_param_bits = 8 };
    if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io) != ESP_OK) { ESP_LOGE(TAG, "no panel IO"); return false; }
    size_t dma0 = heap_caps_get_free_size(DMA_INTERNAL), big0 = heap_caps_get_largest_free_block(DMA_INTERNAL);
    for (int i = 0; i < 2; i++) {
        s_stripe[i] = heap_caps_malloc(STRIPE_BYTES, DMA_INTERNAL);
        if (!s_stripe[i]) { ESP_LOGE(TAG, "no DMA stripe %d (%u B; DMA free %u, largest %u)", i, (unsigned)STRIPE_BYTES, (unsigned)dma0, (unsigned)big0); return false; }
    }
    ESP_LOGI(TAG, "stripes 2 x %u B | internal DMA free %u -> %u, largest block %u -> %u (spec R#8)", (unsigned)STRIPE_BYTES,
             (unsigned)dma0, (unsigned)heap_caps_get_free_size(DMA_INTERNAL),
             (unsigned)big0, (unsigned)heap_caps_get_largest_free_block(DMA_INTERNAL));
    for (size_t i = 0; i < sizeof INIT / sizeof INIT[0]; i++) {
        cmd(INIT[i].c, INIT[i].n ? INIT[i].p : NULL, INIT[i].n);
        if (INIT[i].ms) vTaskDelay(pdMS_TO_TICKS(INIT[i].ms));
        if (INIT[i].c == 0x11) s_slpout_us = esp_timer_get_time();
    }
    uint16_t *black = heap_caps_calloc(TANK_W * TANK_H, 2, MALLOC_CAP_SPIRAM);
    if (black) { send_frame(black); heap_caps_free(black); }   /* the glass black before the light (the stripes hold their copies) */
    s_light_owed = true;
    s_ok = true;
    ESP_LOGI(TAG, "board: the FNK0104S (ST7796S 480x320 over SPI at %d MHz)", CONFIG_POCKET_TANK_LCD40_SPI_MHZ);
    return true;
}

void display_port_sleep(void) {                           /* backlight off, DISPOFF, SLPIN (+5 ms), the wire empty first */
    if (!s_ok) return;
    drain();
    backlight(0);
    s_light_owed = true;                                  /* a brightness change while asleep only records the level */
    int64_t since = esp_timer_get_time() - s_slpout_us;
    if (since < 120000) vTaskDelay(pdMS_TO_TICKS((120000 - since) / 1000 + 1));
    cmd(0x28, NULL, 0); cmd(0x10, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
}
void display_port_wake(void) {                            /* SLPOUT (+120 ms), DISPON; the light after one whole frame */
    if (!s_ok) return;
    cmd(0x11, NULL, 0); s_slpout_us = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(120));
    cmd(0x29, NULL, 0);
    s_light_owed = true;
}
void display_port_deep_standby(void) { }                 /* SLPIN is the ST7796S's lowest state: display_port_sleep sent it */
void display_port_deep_sleep_bus(void) {                  /* a powered panel's inputs never float: clock, data and D/C low */
    static const gpio_num_t lo[] = { F_PIN_LCD_SCLK, F_PIN_LCD_MOSI, F_PIN_LCD_DC };
    for (size_t i = 0; i < sizeof lo / sizeof lo[0]; i++) { gpio_reset_pin(lo[i]); gpio_set_direction(lo[i], GPIO_MODE_OUTPUT); gpio_set_level(lo[i], 0); gpio_hold_en(lo[i]); }
}
void display_port_deep_sleep_pins(bool tp_awake_high) {   /* CS high (deselected), backlight low, the touch chip in reset */
    gpio_reset_pin(F_PIN_LCD_CS); gpio_set_direction(F_PIN_LCD_CS, GPIO_MODE_OUTPUT); gpio_set_level(F_PIN_LCD_CS, 1); gpio_hold_en(F_PIN_LCD_CS);
    gpio_reset_pin(F_PIN_LCD_BL); gpio_set_direction(F_PIN_LCD_BL, GPIO_MODE_OUTPUT); gpio_set_level(F_PIN_LCD_BL, 0); gpio_hold_en(F_PIN_LCD_BL);
    gpio_set_level(F_PIN_TP_RST, tp_awake_high ? 1 : 0); gpio_hold_en(F_PIN_TP_RST);
}
