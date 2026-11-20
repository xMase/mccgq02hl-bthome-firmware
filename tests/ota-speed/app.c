// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

// Throwaway spike: counts the bytes written without response to `sink` and exposes the total in `count`
// (little-endian uint32, reset at every connection). No NVM3 writes, no bonding.
#include <stdint.h>
#include "sl_bt_api.h"
#include "gatt_db.h"
#include "app_assert.h"

static uint8_t adv = 0xff;
static uint32_t received;

static void start_advertising(void)
{
  sl_status_t sc = sl_bt_legacy_advertiser_generate_data(adv, sl_bt_advertiser_general_discoverable);
  app_assert_status(sc);
  sc = sl_bt_legacy_advertiser_start(adv, sl_bt_legacy_advertiser_connectable);
  app_assert_status(sc);
}

void app_init(void)
{
}
void app_process_action(void)
{
}

void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      sc = sl_bt_advertiser_create_set(&adv);
      app_assert_status(sc);
      sc = sl_bt_advertiser_set_timing(adv, 160, 160, 0, 0);  // 100 ms
      app_assert_status(sc);
      start_advertising();
      break;
    case sl_bt_evt_connection_opened_id:
      received = 0;
      sc = sl_bt_gatt_server_write_attribute_value(gattdb_count, 0, sizeof received, (uint8_t *)&received);
      app_assert_status(sc);
      break;
    case sl_bt_evt_gatt_server_user_write_request_id:
      if (evt->data.evt_gatt_server_user_write_request.characteristic == gattdb_sink) {
        received += evt->data.evt_gatt_server_user_write_request.value.len;
        // Updated per write: a RAM copy of 4 bytes, no flash access.
        sc = sl_bt_gatt_server_write_attribute_value(gattdb_count, 0, sizeof received, (uint8_t *)&received);
        app_assert_status(sc);
        // A write request needs an answer; a write command (without response) gets none.
        if (evt->data.evt_gatt_server_user_write_request.att_opcode == sl_bt_gatt_write_request) {
          sc = sl_bt_gatt_server_send_user_write_response(
              evt->data.evt_gatt_server_user_write_request.connection, gattdb_sink, 0);
          app_assert_status(sc);
        }
      }
      break;
    case sl_bt_evt_connection_closed_id:
      start_advertising();
      break;
  }
}
