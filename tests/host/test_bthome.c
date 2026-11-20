// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#include <string.h>
#include "bthome.h"
#include "check.h"

static const uint8_t key[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };
static const uint8_t mac[6] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };

static void print_hex(const uint8_t *b, size_t n)
{
  for (size_t i = 0; i < n; i++) {
    printf("%02x", b[i]);
  }
  printf("\n");
}

// Builds s, checks the length and the counter after the encrypted objects, prints the packet.
static void build(psa_key_id_t id, const bthome_state_t *s, uint8_t want_len)
{
  uint8_t out[BTHOME_ADV_LEN], len;
  CHECK(bthome_build(out, &len, s, 0x12345678, mac, id) == PSA_SUCCESS);
  CHECK(len == want_len && out[0] < len);               // press: the name AD follows the service data AD
  size_t end = out[0] + 1;                              // end of the service data AD; the press name AD follows it
  CHECK(out[end - 8] == 0x78 && out[end - 5] == 0x12);  // counter LE before the 4-byte MIC
  print_hex(out, len);
}

// Every field set: the largest packet of each kind, none above the 31-byte limit.
static const bthome_state_t full = { .battery_mv = 2780, .door_open = 1, .light = 1234, .resets = 3, .cause = 0x0008, .version = 0x010203 };

static void test_bthome_max_len(psa_key_id_t id)
{
  const bthome_kind_t kinds[] = { BTHOME_NORMAL, BTHOME_BOOT, BTHOME_PRESS };
  for (size_t i = 0; i < 3; i++) {
    bthome_state_t s = full;
    uint8_t out[BTHOME_ADV_LEN], len = 0;
    s.kind = kinds[i];
    CHECK(bthome_build(out, &len, &s, 0x12345678, mac, id) == PSA_SUCCESS);
    CHECK(len <= 31);
  }
}

static void test_payload_equal_press(void)
{
  bthome_state_t a = full, b;
  a.kind = BTHOME_PRESS;
  // Light, battery, door and resets are not in the press packet.
  b = a;
  b.light++;
  CHECK(bthome_payload_equal(&a, &b));
  b = a;
  b.battery_mv++;
  CHECK(bthome_payload_equal(&a, &b));
  b = a;
  b.door_open ^= 1;
  CHECK(bthome_payload_equal(&a, &b));
  b = a;
  b.resets++;
  CHECK(bthome_payload_equal(&a, &b));
  b = a;
  b.cause++;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.version++;
  CHECK(!bthome_payload_equal(&a, &b));
  // The event is not compared: a click sequence is sent by its own decision, not by a state change.
  b = a;
  b.event = 3;
  CHECK(bthome_payload_equal(&a, &b));
}

static void test_payload_equal_boot(void)
{
  bthome_state_t a = full, b;
  a.kind = BTHOME_BOOT;
  b = a;
  b.door_open ^= 1;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.cause++;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.version++;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.light++;
  CHECK(bthome_payload_equal(&a, &b));
  b = a;
  b.battery_mv++;
  CHECK(bthome_payload_equal(&a, &b));
}

static void test_payload_equal_normal(void)
{
  bthome_state_t a = full, b;
  CHECK(bthome_payload_equal(&a, &a));
  b = a;
  b.door_open ^= 1;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.battery_mv++;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.light++;
  CHECK(!bthome_payload_equal(&a, &b));
  b = a;
  b.resets++;
  CHECK(!bthome_payload_equal(&a, &b));
  // The cause and the version are not in the normal packet.
  b = a;
  b.cause++;
  b.version++;
  CHECK(bthome_payload_equal(&a, &b));
  // A different kind is never equal, even with the same values.
  b = a;
  b.kind = BTHOME_BOOT;
  CHECK(!bthome_payload_equal(&a, &b));
  b.kind = BTHOME_PRESS;
  CHECK(!bthome_payload_equal(&a, &b));
}

int main(void)
{
  psa_key_id_t id;
  uint8_t out[BTHOME_ADV_LEN], again[BTHOME_ADV_LEN], next[BTHOME_ADV_LEN];
  uint8_t len, len2;
  const bthome_state_t s = { .battery_mv = 2780, .door_open = 1, .light = 1234 };

  CHECK(psa_crypto_init() == PSA_SUCCESS);
  CHECK(bthome_key_import(key, &id) == PSA_SUCCESS);
  memset(out, 0xEE, sizeof out);
  CHECK(bthome_build(out, &len, &s, 0x12345678, mac, id) == PSA_SUCCESS);
  CHECK(len == 25);

  CHECK(sizeof out == 31);
  CHECK(out[0] == 24);                                                              // AD length
  CHECK(out[1] == 0x16);                                                            // Service Data, 16-bit UUID
  CHECK(out[2] == 0xD2 && out[3] == 0xFC);                                          // UUID 0xFCD2, little-endian
  CHECK(out[4] == 0x41);                                                            // version 2, encrypted, not trigger-based
  CHECK(out[17] == 0x78 && out[18] == 0x56 && out[19] == 0x34 && out[20] == 0x12);  // counter LE

  // A repeat of an unchanged payload gives the same bytes (same nonce, same data).
  CHECK(bthome_build(again, &len2, &s, 0x12345678, mac, id) == PSA_SUCCESS);
  CHECK(memcmp(out, again, len) == 0);
  // A new counter gives a new ciphertext and tag.
  CHECK(bthome_build(next, &len2, &s, 0x12345679, mac, id) == PSA_SUCCESS);
  CHECK(memcmp(out + 5, next + 5, 12) != 0 && memcmp(out + 21, next + 21, 4) != 0);
  print_hex(out, len);

  // Each object without a sample yet is left out (voltage 3 bytes, light 5 with the reset count);
  // the packet id is always there.
  build(id, &(bthome_state_t){ .battery_mv = 0, .door_open = 1, .light = 1234 }, 22);
  build(id, &(bthome_state_t){ .battery_mv = 2780, .door_open = 1, .light = BTHOME_LIGHT_NONE }, 20);
  build(id, &(bthome_state_t){ .battery_mv = 0, .door_open = 1, .light = BTHOME_LIGHT_NONE }, 17);
  // Boot packet: door, cause and version only. Press packet: cause and version, then the name AD.
  uint8_t pk[BTHOME_ADV_LEN], pklen;
  bthome_state_t bs = full;
  bs.kind = BTHOME_BOOT;
  build(id, &bs, 25);
  CHECK(bthome_build(pk, &pklen, &bs, 0x12345678, mac, id) == PSA_SUCCESS && pk[0] == 24);
  bs.kind = BTHOME_PRESS;
  bs.event = 1;
  build(id, &bs, 31);
  CHECK(bthome_build(pk, &pklen, &bs, 0x12345678, mac, id) == PSA_SUCCESS && pk[0] == 24);
  CHECK(pk[25] == 0x05 && pk[26] == 0x09 && pk[27] == 'M' && pk[28] == 'C' && pk[29] == 'C' &&
        pk[30] == 'G');
  // Double and triple press: same packet, only the 3A value differs (decoded by check_bthome.py).
  bs.event = 2;
  build(id, &bs, 31);
  bs.event = 3;
  build(id, &bs, 31);
  test_bthome_max_len(id);
  test_payload_equal_press();
  test_payload_equal_boot();
  test_payload_equal_normal();
  return 0;
}
