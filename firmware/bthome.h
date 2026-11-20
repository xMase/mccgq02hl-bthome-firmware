// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef BTHOME_H
#define BTHOME_H

#include <stdbool.h>
#include <stdint.h>
#include "psa/crypto.h"

// Advertising data: one Service Data AD (UUID 0xFCD2), encrypted BTHome v2; the press packet adds a
// Complete Local Name AD after it. See the design spec, "Packet".
// Maximum length (legacy advertising limit): the press packet, 25 + 6 bytes.
#define BTHOME_ADV_LEN 31
#define BTHOME_LIGHT_NONE 0xFFFFu  // no light sample yet: object left out (12-bit codes stay below)
#define BTHOME_NAME "MCCG"         // HA names the device with it plus the last 4 MAC digits

// Which packet is built; each carries its own objects whatever the other fields hold.
typedef enum { BTHOME_NORMAL,
               BTHOME_BOOT,
               BTHOME_PRESS } bthome_kind_t;

typedef struct {
  bthome_kind_t kind;   // zero = normal
  uint16_t battery_mv;  // 0x0C voltage under radio load, mV; 0 = no sample yet, object left out
  uint8_t event;        // 0x3A button event of the press packet: 1 press, 2 double, 3 triple
  uint8_t door_open;    // 0x1A, 1 = open (PA3 = 1, no magnet)
  uint16_t light;       // 0x3D count, raw 12-bit IADC code; BTHOME_LIGHT_NONE = object left out
  uint8_t resets;       // 0x09 anomalous reset count; sent only together with the light object
  uint16_t cause;       // 0x54 last anomalous reset cause, raw, most significant byte first
  uint32_t version;     // 0xF2 firmware version, low 3 bytes
} bthome_state_t;

// True if both states give the same packet: same kind and same values of the objects it sends,
// except the press event: a click sequence is sent when it is decided, not when it differs.
bool bthome_payload_equal(const bthome_state_t *a, const bthome_state_t *b);

// Imports the 16-byte bindkey as a volatile AES-128 key for CCM with a 4-byte tag.
psa_status_t bthome_key_import(const uint8_t key[16], psa_key_id_t *key_id);

// mac: identity address in display order (00:11:22:33:44:55 -> {0x00, 0x11, ...}).
// *len: bytes written to out, the name AD included. More than BTHOME_ADV_LEN bytes: PSA_ERROR_BUFFER_TOO_SMALL, out untouched.
psa_status_t bthome_build(uint8_t out[BTHOME_ADV_LEN], uint8_t *len, const bthome_state_t *s,
                          uint32_t counter, const uint8_t mac[6], psa_key_id_t key_id);

#endif  // BTHOME_H
