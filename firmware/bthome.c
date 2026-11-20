// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include <string.h>
#include "bthome.h"

#define BTHOME_TAG_LEN 4
// Largest object set of any kind: packet id, door, cause, version (2+2+4+4) or normal (2+2+3+2+3).
#define BTHOME_PLAIN_MAX 12
// BTHome device info: version 2 (bits 5-7), encryption (bit 0), trigger-based (bit 2) clear.
#define BTHOME_DEVICE_INFO 0x41
#define BTHOME_ALG PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, BTHOME_TAG_LEN)

psa_status_t bthome_key_import(const uint8_t key[16], psa_key_id_t *key_id)
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attr, 128);
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT);
  psa_set_key_algorithm(&attr, BTHOME_ALG);
  psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_VOLATILE);
  return psa_import_key(&attr, key, 16, key_id);
}

bool bthome_payload_equal(const bthome_state_t *a, const bthome_state_t *b)
{
  if (a->kind != b->kind) {
    return false;
  }
  switch (a->kind) {
    case BTHOME_BOOT:
      return a->door_open == b->door_open && a->cause == b->cause && a->version == b->version;
    case BTHOME_PRESS:
      return a->cause == b->cause && a->version == b->version;
    default:
      return a->door_open == b->door_open && a->battery_mv == b->battery_mv &&
             a->light == b->light && a->resets == b->resets;
  }
}

psa_status_t bthome_build(uint8_t out[BTHOME_ADV_LEN], uint8_t *len, const bthome_state_t *s,
                          uint32_t counter, const uint8_t mac[6], psa_key_id_t key_id)
{
  const uint8_t ctr[4] = { (uint8_t)counter, (uint8_t)(counter >> 8),
                           (uint8_t)(counter >> 16), (uint8_t)(counter >> 24) };
  // Objects of the kind in ascending id order; the spec's packet table says which. Normal: voltage and
  // light (with the reset count) only once sampled. Packet id = counter low byte: bthome-ble drops
  // packets that repeat the last id within 4 s, so repeats of a press packet fire one HA event.
  uint8_t plain[BTHOME_PLAIN_MAX];
  size_t plain_len = 0;
  plain[plain_len++] = 0x00;
  plain[plain_len++] = (uint8_t)counter;
  if (s->kind == BTHOME_NORMAL && s->light != BTHOME_LIGHT_NONE) {
    plain[plain_len++] = 0x09;
    plain[plain_len++] = s->resets;
  }
  if (s->kind == BTHOME_NORMAL && s->battery_mv) {
    plain[plain_len++] = 0x0C;
    plain[plain_len++] = (uint8_t)s->battery_mv;
    plain[plain_len++] = (uint8_t)(s->battery_mv >> 8);
  }
  if (s->kind != BTHOME_PRESS) {
    plain[plain_len++] = 0x1A;
    plain[plain_len++] = s->door_open;
  }
  if (s->kind == BTHOME_PRESS) {
    plain[plain_len++] = 0x3A;  // button event: 1 press, 2 double, 3 triple
    plain[plain_len++] = s->event;
  }
  if (s->kind == BTHOME_NORMAL && s->light != BTHOME_LIGHT_NONE) {
    plain[plain_len++] = 0x3D;
    plain[plain_len++] = (uint8_t)s->light;
    plain[plain_len++] = (uint8_t)(s->light >> 8);
  }
  if (s->kind != BTHOME_NORMAL) {
    plain[plain_len++] = 0x54;  // raw object: id, length, data
    plain[plain_len++] = 2;
    plain[plain_len++] = (uint8_t)(s->cause >> 8);
    plain[plain_len++] = (uint8_t)s->cause;
    plain[plain_len++] = 0xF2;
    plain[plain_len++] = (uint8_t)s->version;
    plain[plain_len++] = (uint8_t)(s->version >> 8);
    plain[plain_len++] = (uint8_t)(s->version >> 16);
  }
  const size_t name_len = s->kind == BTHOME_PRESS ? 2 + sizeof BTHOME_NAME - 1 : 0;
  const size_t sd_len = 9 + plain_len + BTHOME_TAG_LEN;  // service data AD, length byte included
  if (sd_len + name_len > BTHOME_ADV_LEN) {
    return PSA_ERROR_BUFFER_TOO_SMALL;
  }
  // bthome.io/encryption: nonce = MAC (display order) + UUID LE + device info + counter LE.
  uint8_t nonce[13];
  memcpy(nonce, mac, 6);
  nonce[6] = 0xD2;
  nonce[7] = 0xFC;
  nonce[8] = BTHOME_DEVICE_INFO;
  memcpy(nonce + 9, ctr, 4);

  uint8_t sealed[BTHOME_PLAIN_MAX + BTHOME_TAG_LEN];  // ciphertext followed by the tag
  size_t sealed_len;
  psa_status_t st = psa_aead_encrypt(key_id, BTHOME_ALG, nonce, sizeof nonce, NULL, 0,
                                     plain, plain_len, sealed, sizeof sealed, &sealed_len);
  if (st != PSA_SUCCESS) {
    return st;
  }
  out[0] = (uint8_t)(sd_len - 1);
  out[1] = 0x16;  // Service Data, 16-bit UUID
  out[2] = 0xD2;
  out[3] = 0xFC;
  out[4] = BTHOME_DEVICE_INFO;
  memcpy(out + 5, sealed, plain_len);
  memcpy(out + 5 + plain_len, ctr, 4);
  memcpy(out + 9 + plain_len, sealed + plain_len, BTHOME_TAG_LEN);
  if (name_len) {
    out[sd_len] = (uint8_t)(name_len - 1);
    out[sd_len + 1] = 0x09;  // Complete Local Name
    memcpy(out + sd_len + 2, BTHOME_NAME, name_len - 2);
  }
  *len = (uint8_t)(sd_len + name_len);
  return PSA_SUCCESS;
}
