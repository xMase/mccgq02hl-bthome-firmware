// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>

// code: 12-bit IADC result of AVDD/4 against the internal 1.21 V reference.
// Returns the voltage in mV, rounded (BTHome object 0x0C).
uint16_t battery_mv(uint32_t code);

#endif  // BATTERY_H
