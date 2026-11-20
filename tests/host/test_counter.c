// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "counter.h"
#include "check.h"

static void test_counter_first_boot(void)
{
  counter_t c;
  counter_boot(&c, false, 0);
  CHECK(c.value == 100);
  CHECK(c.mark == 132);
}

static void test_counter_low_mark_clamped(void)
{
  counter_t c;
  counter_boot(&c, true, 7);
  CHECK(c.value == 100);
  CHECK(c.mark == 132);
}

static void test_counter_advance_crosses_mark(void)
{
  counter_t c;
  counter_boot(&c, true, 1000);
  CHECK(c.value == 1000 && c.mark == 1032);
  for (int i = 0; i < 31; i++) {
    CHECK(!counter_advance(&c));
  }
  CHECK(counter_advance(&c));
  CHECK(c.value == 1032);
  CHECK(c.mark == 1064);
}

static void test_counter_reboot_never_repeats(void)
{
  counter_t c, c2;
  uint32_t stored, max_sent;
  counter_boot(&c, true, 500);
  stored = c.mark;  // the caller stores the mark before the first packet
  max_sent = c.value;
  for (int i = 0; i < 300; i++) {
    if (counter_advance(&c)) {
      stored = c.mark;
    }
    CHECK(c.value < stored);  // the invariant holds for every value sent
    max_sent = c.value;
  }
  // Battery pulled here: the next boot reads the last stored mark.
  counter_boot(&c2, true, stored);
  CHECK(c2.value > max_sent);
}

static void test_counter_boot_jump_bounded(void)
{
  counter_t c, c2;
  uint32_t stored;
  counter_boot(&c, true, 500);
  stored = c.mark;
  for (int i = 0; i < 200; i++) {
    if (counter_advance(&c)) {
      stored = c.mark;
    }
    // A reset right after this value went out: the boot value is above it, by at most COUNTER_STEP (32).
    counter_boot(&c2, true, stored);
    CHECK(c2.value > c.value);
    CHECK(c2.value - c.value <= 32);
  }
}

static void test_counter_high_mark_rejected(void)
{
  counter_t c;
  // Above 2^31 the mark is not a value this firmware wrote (~3 900 years away); refuse it.
  CHECK(!counter_boot(&c, true, 0x80000001u));
  CHECK(!counter_boot(&c, true, UINT32_MAX - 100u));
  CHECK(counter_boot(&c, true, 0x80000000u));
  CHECK(counter_boot(&c, false, 0));
}

static void test_counter_unchanged_payload(void)
{
  counter_t c;
  counter_boot(&c, true, 4242);
  // No payload change: no counter_advance call, the value on air stays.
  CHECK(c.value == 4242);
}

int main(void)
{
  test_counter_first_boot();
  test_counter_low_mark_clamped();
  test_counter_advance_crosses_mark();
  test_counter_reboot_never_repeats();
  test_counter_boot_jump_bounded();
  test_counter_high_mark_rejected();
  test_counter_unchanged_payload();
  return 0;
}
