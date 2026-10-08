/* battery_port_stub.c - no battery meter, for the FNK0104S's QEMU build
 * (CONFIG_POCKET_TANK_QEMU, run_qemu.sh). Espressif QEMU does not model the
 * SAR ADC, and esp_adc's start-up self-calibration waits on it forever, so
 * the emulator build leaves battery_port_adc.c out. Everything reports "no
 * meter, no PMIC, no key": the pill hides and BOOT is the sleep key, as a
 * failed ADC calibration would leave the real board. */
#include "battery_port.h"
#include "esp_log.h"

bool battery_port_init(i2c_master_bus_handle_t bus) { (void)bus; ESP_LOGW("battery", "QEMU build: no ADC, no battery meter"); return false; }
bool battery_port_read(float *frac, bool *charging) { (void)frac; (void)charging; return false; }
int  battery_port_state(void) { return BAT_ON_BATTERY; }
bool battery_port_poweroff(void) { return false; }
bool battery_port_can_power_off(void) { return false; }
bool battery_port_has_pwr_key(void) { return false; }
void battery_port_key_init(void) { }
int  battery_port_key_poll(void) { return 0; }
void battery_port_key_trace(int seconds) { (void)seconds; }
void battery_port_dump(void) { ESP_LOGI("battery", "QEMU build: no battery meter"); }
int  battery_port_vbat_mv(void) { return 0; }
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; return false; }
void battery_port_trim_rails(void) { }
void battery_port_pin_rail(const char *name) { (void)name; }
