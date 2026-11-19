#ifndef HW_CONFIG_H
#define HW_CONFIG_H

#include <stdint.h>

void hw_clock_init(void);
void hw_gpio_init(void);
void hw_iwdg_init(void);
void hw_tim4_init(void);
void hw_i2c_init(void);
void hw_awu_init(void);
void hw_sleep_enter(void);
void hw_sleep_exit(void);
uint8_t hw_sleep_wake_awu(void);
void hw_reset(void);

#endif
