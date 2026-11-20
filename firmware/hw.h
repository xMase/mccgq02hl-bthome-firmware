// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef HW_H
#define HW_H

#include <stdbool.h>
#include <stdint.h>

// PA3 and PB0 inputs with glitch filter and no pull, as stock; EXTI on both edges (ports
// A and B wake from EM2).
// PC0 (LED), PD4 (light divider supply) and PC1 (light input) off. Starts WDOG0.
void hw_init(void);

uint8_t hw_door_level(void);  // PA3: 1 = no magnet = open
void hw_door_irq_register(void (*cb)(void));
uint8_t hw_button_level(void);  // PB0: 0 = pressed
void hw_button_irq_register(void (*cb)(void));
void hw_light_on(void);        // PD4 high; the divider needs ~10 s to settle (README)
uint16_t hw_light_read(void);  // one PC1 conversion, then PD4 off; raw 12-bit code, ratiometric to VDD
// Arms one AVDD/4 conversion that the next radio TX start triggers (PRS RAC_TX -> IADC single
// trigger), so it samples the battery under load. A new arm replaces a pending one. cb runs in
// the IADC interrupt with the 12-bit code; the IADC is off again after it.
void hw_battery_arm(void (*cb)(uint16_t code));
void hw_led(bool on);  // PC0, active high (from the stock code, not verified)
void hw_wdog_feed(void);

#endif  // HW_H
