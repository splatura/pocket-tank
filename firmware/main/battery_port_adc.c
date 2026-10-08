/* battery_port_adc.c - the FNK0104S's battery (2026-10-08): no PMIC, no fuel
 * gauge, no charge signal to the chip. The cell reaches GPIO9 (ADC1_CH8)
 * through R14 = R15 = 100 k; a 1 s timer takes one calibrated sample into a
 * ring of 8 and the reads return the cached median at once (spec R#5, R#6).
 * Three facts, kept apart:
 *   level    - the median on a LiPo's resting curve (an ESTIMATE)
 *   power    - CONFIRMED only when the USB-Serial-JTAG port sees a host:
 *              BAT_PLUGGED, never CHARGING / FULL (nothing tells us that)
 *   charging - unknown on this board
 * A wall charger is no USB host: it reads as battery, so update mode's
 * low-battery rule applies - an update that asks for a cable, never one that
 * starts on a flat cell. */
#include "battery_port.h"
#include "board_pins.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/usb_serial_jtag.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "battery";
#define RING 8
static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static SemaphoreHandle_t s_mx;
static int s_ring[RING], s_n, s_at;
static int s_shown = -1;
static esp_timer_handle_t s_tick;

static int sample_mv(void) {                              /* one calibrated reading at the cell, mV; 0 = failed */
    int raw = 0, pin_mv = 0;
    xSemaphoreTake(s_mx, portMAX_DELAY);
    bool ok = adc_oneshot_read(s_adc, ADC_CHANNEL_8, &raw) == ESP_OK && adc_cali_raw_to_voltage(s_cali, raw, &pin_mv) == ESP_OK;
    xSemaphoreGive(s_mx);
    return ok ? (int)(pin_mv * F_BAT_DIVIDER + 0.5f) : 0;
}
static void put(int mv) { if (mv <= 0) return; s_ring[s_at] = mv; s_at = (s_at + 1) % RING; if (s_n < RING) s_n++; }
static void tick(void *arg) { (void)arg; put(sample_mv()); }

bool battery_port_init(i2c_master_bus_handle_t bus) {
    (void)bus;
    s_mx = xSemaphoreCreateMutex();
    adc_oneshot_unit_init_cfg_t uc = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_chan_cfg_t ch = { .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };   /* the pin sees ~2.1 V at full */
    adc_cali_curve_fitting_config_t cf = { .unit_id = ADC_UNIT_1, .chan = ADC_CHANNEL_8, .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_new_unit(&uc, &s_adc) != ESP_OK || adc_oneshot_config_channel(s_adc, ADC_CHANNEL_8, &ch) != ESP_OK
        || adc_cali_create_scheme_curve_fitting(&cf, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "no calibrated ADC on GPIO%d: the battery pill stays hidden", F_PIN_BAT_ADC);
        return false;
    }
    for (int i = 0; i < 4; i++) put(sample_mv());         /* seeded NOW: update mode runs a second into the boot (Review Focus 1) */
    const esp_timer_create_args_t ta = { .callback = tick, .name = "battery" };
    esp_timer_create(&ta, &s_tick); esp_timer_start_periodic(s_tick, 1000000);
    ESP_LOGI(TAG, "battery from GPIO%d x %.2f: %d mV (an estimate - docs/board-fnk0104s.md)", F_PIN_BAT_ADC, F_BAT_DIVIDER, battery_port_vbat_mv());
    return s_n >= 4;
}
int battery_port_vbat_mv(void) { return s_n ? battery_median_mv(s_ring, s_n) : 0; }
bool battery_port_read(float *frac, bool *charging) {
    if (s_n < 4) return false;
    s_shown = battery_hyst_pct(s_shown, battery_lipo_frac(battery_port_vbat_mv()));
    *frac = s_shown / 100.0f;
    *charging = false;                                    /* unknown on this board: never claimed */
    return true;
}
int battery_port_state(void) { return usb_serial_jtag_is_connected() ? BAT_PLUGGED : BAT_ON_BATTERY; }
bool battery_port_poweroff(void) { return false; }
bool battery_port_can_power_off(void) { return false; }
bool battery_port_has_pwr_key(void) { return false; }
void battery_port_key_init(void) { }
int  battery_port_key_poll(void) { return 0; }            /* BOOT is the key: main.c's sleep_button_poll owns it */
void battery_port_key_trace(int seconds) { (void)seconds; ESP_LOGI(TAG, "no PWR key on this board (BOOT sleeps)"); }
void battery_port_dump(void) { ESP_LOGI(TAG, "ADC battery: %d mV median of %d, %s, divider %.2f", battery_port_vbat_mv(), s_n,
                                        usb_serial_jtag_is_connected() ? "USB host seen" : "no USB host", F_BAT_DIVIDER); }
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; return false; }
void battery_port_trim_rails(void) { }
void battery_port_pin_rail(const char *name) { (void)name; }
