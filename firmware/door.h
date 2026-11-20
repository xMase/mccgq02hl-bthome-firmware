// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef DOOR_H
#define DOOR_H

#include <stdint.h>

// Stock debounce: every PA3 edge restarts it; samples
// 10 ms apart; 3 samples away from the stored state accept the change; a sample back at the
// stored state ends the sequence as a bounce.
typedef struct {
  uint8_t stable;  // accepted level: 1 = no magnet = open
  uint8_t count;   // samples away from stable since the last edge
} door_debounce_t;

typedef enum {
  DOOR_CONTINUE,  // take another sample in 10 ms
  DOOR_CHANGED,   // d->stable holds the new level; stop sampling
  DOOR_BOUNCE,    // back at the stored state; stop sampling
} door_result_t;

// Call at boot with the PA3 level, and on every edge with d->stable (restarts the count).
void door_debounce_init(door_debounce_t *d, uint8_t level);

door_result_t door_debounce_sample(door_debounce_t *d, uint8_t level);

#endif  // DOOR_H
