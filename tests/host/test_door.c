// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "door.h"
#include "check.h"

static void test_door_change_after_three(void)
{
  door_debounce_t d;
  door_debounce_init(&d, 0);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CHANGED);
  CHECK(d.stable == 1);
}

// Stock: a sample back at the stored state ends the sequence (a bounce).
static void test_door_bounce_ends(void)
{
  door_debounce_t d;
  door_debounce_init(&d, 0);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 0) == DOOR_BOUNCE);
  CHECK(d.stable == 0);
}

static void test_door_same_level_bounce(void)
{
  door_debounce_t d;
  door_debounce_init(&d, 1);
  CHECK(door_debounce_sample(&d, 1) == DOOR_BOUNCE);
  CHECK(d.stable == 1);
}

// Stock: every edge clears the sample count, so a change needs 3 samples after the last edge.
static void test_door_new_edge_resets_count(void)
{
  door_debounce_t d;
  door_debounce_init(&d, 0);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  door_debounce_init(&d, d.stable);  // new edge
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CHANGED);
}

static void test_door_change_after_bounce(void)
{
  door_debounce_t d;
  door_debounce_init(&d, 0);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 0) == DOOR_BOUNCE);
  door_debounce_init(&d, d.stable);  // the real opening gives a new edge
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CONTINUE);
  CHECK(door_debounce_sample(&d, 1) == DOOR_CHANGED);
  CHECK(d.stable == 1);
}

int main(void)
{
  test_door_change_after_three();
  test_door_bounce_ends();
  test_door_same_level_bounce();
  test_door_new_edge_resets_count();
  test_door_change_after_bounce();
  return 0;
}
