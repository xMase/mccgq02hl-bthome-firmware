// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef COUNTER_H
#define COUNTER_H

#include <stdbool.h>
#include <stdint.h>

// BTHome encryption counter. Invariant: every value ever sent is lower than the stored mark.
// bthome-ble accepts any counter below 100, so the counter never goes below 100.
typedef struct {
  uint32_t value;  // counter of the payload on air
  uint32_t mark;   // the caller stores this in NVM3
} counter_t;

// has_mark = false on first boot. Sets value = max(stored_mark, 100), mark = value + 32.
// The caller must store c->mark before the first packet. Returns false, and the caller must not
// send, when the stored mark is above 2^31: this firmware cannot have written it (wrong data in
// flash), and starting there could wrap the counter into reused nonces.
bool counter_boot(counter_t *c, bool has_mark, uint32_t stored_mark);

// Call when the payload changed. Increments value. Returns true when value reached mark:
// then mark += 32, and the caller must store c->mark before it sends value.
bool counter_advance(counter_t *c);

#endif  // COUNTER_H
