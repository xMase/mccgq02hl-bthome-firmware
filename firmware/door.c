// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "door.h"

#define DOOR_SAMPLES 3

void door_debounce_init(door_debounce_t *d, uint8_t level)
{
  d->stable = level;
  d->count = 0;
}

door_result_t door_debounce_sample(door_debounce_t *d, uint8_t level)
{
  if (level == d->stable) {
    return DOOR_BOUNCE;
  }
  if (++d->count < DOOR_SAMPLES) {
    return DOOR_CONTINUE;
  }
  d->stable = level;
  d->count = 0;
  return DOOR_CHANGED;
}
