// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "diag.h"
#include "check.h"

static void test_diag_wdog0_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 0x8, false, 0));
  CHECK(d.count == 1);
  CHECK(d.cause == 0x0008);
}

static void test_diag_por_ignored(void)
{
  diag_t d = { 0, 0 };
  CHECK(!diag_boot(&d, 0x1, false, 0));
  CHECK(d.count == 0 && d.cause == 0);
}

static void test_diag_bod_ignored(void)
{
  diag_t d = { 0, 0 };
  CHECK(!diag_boot(&d, 1u << 7, false, 0));
  CHECK(d.count == 0 && d.cause == 0);
}

static void test_diag_sysreq_valid_word_ignored(void)
{
  diag_t d = { 0, 0 };
  CHECK(!diag_boot(&d, 0x40, true, DIAG_BTL_REASON_GO));
  CHECK(d.count == 0 && d.cause == 0);
}

static void test_diag_sysreq_valid_badimage_ignored(void)
{
  diag_t d = { 0, 0 };
  CHECK(!diag_boot(&d, 0x40, true, DIAG_BTL_REASON_BADIMAGE));
  CHECK(d.count == 0 && d.cause == 0);
}

// The bootloader's HardFault handler resets with reason FATAL and a valid signature.
static void test_diag_sysreq_valid_fatal_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 0x40, true, 0x0204));
  CHECK(d.count == 1 && d.cause == 0x0040);
}

static void test_diag_sysreq_invalid_word_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 0x40, false, DIAG_BTL_REASON_GO));
  CHECK(d.count == 1 && d.cause == 0x0040);
}

static void test_diag_selockup_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 0x8000, false, 0));
  CHECK(d.count == 1 && d.cause == 0x8000);
}

static void test_diag_wdog1_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 1u << 4, false, 0));
  CHECK(d.count == 1 && d.cause == 0x0010);
}

static void test_diag_lockup_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 1u << 5, false, 0));
  CHECK(d.count == 1 && d.cause == 0x0020);
}

// WDOG0 fires while the bootloader waits after an intended reboot: SYSREQ with WDOG0, valid word.
static void test_diag_sysreq_wdog0_valid_word_counts(void)
{
  diag_t d = { 0, 0 };
  CHECK(diag_boot(&d, 0x48, true, DIAG_BTL_REASON_GO));
  CHECK(d.count == 1 && d.cause == 0x0048);
}

static void test_diag_count_wraps(void)
{
  diag_t d = { 255, 0x0008 };
  CHECK(diag_boot(&d, 0x10, false, 0));
  CHECK(d.count == 0 && d.cause == 0x0010);
}

int main(void)
{
  test_diag_wdog0_counts();
  test_diag_por_ignored();
  test_diag_bod_ignored();
  test_diag_sysreq_valid_word_ignored();
  test_diag_sysreq_valid_badimage_ignored();
  test_diag_sysreq_valid_fatal_counts();
  test_diag_sysreq_invalid_word_counts();
  test_diag_selockup_counts();
  test_diag_wdog1_counts();
  test_diag_lockup_counts();
  test_diag_sysreq_wdog0_valid_word_counts();
  test_diag_count_wraps();
  return 0;
}
