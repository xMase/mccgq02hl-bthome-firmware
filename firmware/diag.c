// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include "diag.h"

bool diag_anomalous(uint32_t rstcause, bool reset_word_valid, uint16_t reset_reason)
{
  if (rstcause & (DIAG_RST_WDOG0 | DIAG_RST_WDOG1 | DIAG_RST_LOCKUP | DIAG_RST_SELOCKUP)) {
    return true;
  }
  bool intended = reset_word_valid &&
                  (reset_reason == DIAG_BTL_REASON_GO || reset_reason == DIAG_BTL_REASON_BADIMAGE);
  return (rstcause & DIAG_RST_SYSREQ) && !intended;
}

bool diag_boot(diag_t *d, uint32_t rstcause, bool reset_word_valid, uint16_t reset_reason)
{
  if (!diag_anomalous(rstcause, reset_word_valid, reset_reason)) {
    return false;
  }
  d->count++;
  d->cause = (uint16_t)rstcause;
  return true;
}
