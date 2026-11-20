// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "battery.h"
#include "check.h"

// BTHome voltage 0x0C in mV: the same V = code * 4 * 1.21 / 4095, rounded to the nearest mV.
static void test_battery_mv(void)
{
  CHECK(battery_mv(0) == 0);
  CHECK(battery_mv(2352) == 2780);  // 2779.9, measured on the chip 2026-11-16
  CHECK(battery_mv(2232) == 2638);  // 2638.1, measured just after a burst 2026-11-16
  CHECK(battery_mv(4095) == 4840);
}

int main(void)
{
  test_battery_mv();
  return 0;
}
