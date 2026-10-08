/* battery_port_adc.c — the FNK0104S has no PMIC: its battery is a voltage on an
 * ADC pin (board_pins.h F_PIN_BAT_ADC). This is the stub that links the build
 * (no battery: the meter hidden, no power-off, no key); Task 10 replaces it. */
#include "battery_port.h"

bool battery_port_init(i2c_master_bus_handle_t bus) { (void)bus; return false; }
bool battery_port_read(float *frac, bool *charging) { (void)frac; (void)charging; return false; }
int  battery_port_state(void) { return BAT_ON_BATTERY; }
bool battery_port_poweroff(void) { return false; }
bool battery_port_can_power_off(void) { return false; }
bool battery_port_has_pwr_key(void) { return false; }
void battery_port_key_init(void) {}
int  battery_port_key_poll(void) { return 0; }
void battery_port_key_trace(int seconds) { (void)seconds; }
void battery_port_dump(void) {}
int  battery_port_vbat_mv(void) { return 0; }
bool battery_port_set_rail(const char *name, bool on) { (void)name; (void)on; return false; }
void battery_port_trim_rails(void) {}
void battery_port_pin_rail(const char *name) { (void)name; }
