// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "counter.h"

#define COUNTER_MIN 100u
#define COUNTER_STEP 32u  // values sent per NVM3 write

// Any accepted boot leaves at least 2^31 values before UINT32_MAX: about 3 900 years at 1 500
// payload changes a day, so counter_advance needs no wrap check.
#define COUNTER_MAX_BOOT_MARK 0x80000000u

bool counter_boot(counter_t *c, bool has_mark, uint32_t stored_mark)
{
  if (has_mark && stored_mark > COUNTER_MAX_BOOT_MARK) {
    return false;
  }
  c->value = (has_mark && stored_mark > COUNTER_MIN) ? stored_mark : COUNTER_MIN;
  c->mark = c->value + COUNTER_STEP;
  return true;
}

bool counter_advance(counter_t *c)
{
  c->value++;
  if (c->value < c->mark) {
    return false;
  }
  c->mark += COUNTER_STEP;
  return true;
}
