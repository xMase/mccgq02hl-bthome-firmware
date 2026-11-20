// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "battery.h"

uint16_t battery_mv(uint32_t code)
{
  return (uint16_t)((code * 4840u + 2047u) / 4095u);  // 4 * 1.21 V = 4840 mV full scale
}
