// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef DIAG_H
#define DIAG_H

#include <stdbool.h>
#include <stdint.h>

// EMU->RSTCAUSE bits (efr32bg21_emu.h).
#define DIAG_RST_WDOG0 (1u << 3)
#define DIAG_RST_WDOG1 (1u << 4)
#define DIAG_RST_LOCKUP (1u << 5)
#define DIAG_RST_SYSREQ (1u << 6)
#define DIAG_RST_SELOCKUP (1u << 15)

// Bootloader reset reasons of an intended reset (api/btl_reset_info.h). Any other reason, FATAL
// (0x0204) included, is a bootloader fault.
#define DIAG_BTL_REASON_GO 0x0201u
#define DIAG_BTL_REASON_BADIMAGE 0x0203u

// Anomalous reset record. The caller stores it in NVM3 key 0x00002, zero-initialized before use.
typedef struct {
  uint8_t count;   // anomalous resets, wraps 255 -> 0
  uint16_t cause;  // RSTCAUSE of the last anomalous reset
} diag_t;

// WDOG0, WDOG1, LOCKUP or SELOCKUP set, or SYSREQ set unless the bootloader reset word is valid
// and its reason is GO or BADIMAGE. POR, pin, brown-out and an intended software reset are not
// anomalous.
bool diag_anomalous(uint32_t rstcause, bool reset_word_valid, uint16_t reset_reason);

// Call once per boot. On an anomalous reset: count++, cause = rstcause. Returns true when d
// changed and the caller must store it.
bool diag_boot(diag_t *d, uint32_t rstcause, bool reset_word_valid, uint16_t reset_reason);

#endif  // DIAG_H
