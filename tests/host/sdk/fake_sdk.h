// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

#ifndef FAKE_SDK_H
#define FAKE_SDK_H

// Fake Silicon Labs SDK for the host test of firmware/app.c: just the types, constants and calls
// app.c uses, plus the controls of tests/host/fake_sdk.c. The one-line headers next to this file
// carry the names of the SDK headers app.c includes. Calls with more than four parameters are
// macros that drop the arguments the fake does not need.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint32_t sl_status_t;
#define SL_STATUS_OK 0u
#define SL_STATUS_INVALID_STATE 2u
// A failed assertion ends the process with this code (exit, so coverage data is written).
#define FAKE_ASSERT_EXIT 42
#define app_assert_status(sc)                                       \
  do {                                                              \
    if ((sc) != SL_STATUS_OK) {                                     \
      fprintf(stderr, "app_assert_status: 0x%x\n", (unsigned)(sc)); \
      exit(FAKE_ASSERT_EXIT);                                       \
    }                                                               \
  } while (0)
#define app_assert(expr, ...)                     \
  do {                                            \
    if (!(expr)) {                                \
      fprintf(stderr, "app_assert: %s\n", #expr); \
      exit(FAKE_ASSERT_EXIT);                     \
    }                                             \
  } while (0)
#define SL_APPLICATION_VERSION 0x010203u

// Bluetooth
typedef struct {
  uint8_t addr[6];
} bd_addr;
typedef struct {
  uint32_t header;
  union {
    struct {
      uint32_t extsignals;
    } evt_system_external_signal;
  } data;
} sl_bt_msg_t;
#define SL_BT_MSG_ID(header) (header)
#define sl_bt_evt_system_boot_id 0x000100a0u
#define sl_bt_evt_system_external_signal_id 0x030100a0u
#define sl_bt_evt_advertiser_timeout_id 0x010400a0u
#define SL_BT_INVALID_ADVERTISING_SET_HANDLE 0xffu
#define sl_bt_advertiser_advertising_data_packet 0
#define sl_bt_legacy_advertiser_non_connectable 0
void sl_bt_on_event(sl_bt_msg_t *evt);  // app.c
sl_status_t sl_bt_external_signal(uint32_t extsignals);
sl_status_t sl_bt_gap_get_identity_address(bd_addr *address, uint8_t *type);
sl_status_t sl_bt_advertiser_create_set(uint8_t *handle);
sl_status_t sl_bt_advertiser_set_tx_power(uint8_t handle, int16_t power, int16_t *set_power);
sl_status_t sl_bt_advertiser_stop(uint8_t handle);
sl_status_t sl_bt_legacy_advertiser_set_data(uint8_t handle, uint8_t type, size_t len, const uint8_t *data);
sl_status_t sl_bt_legacy_advertiser_start(uint8_t handle, uint8_t connectable);
sl_status_t fake_adv_timing(uint8_t handle, uint32_t interval, uint16_t duration);
#define sl_bt_advertiser_set_timing(handle, min, max, duration, max_events) fake_adv_timing(handle, min, duration)

// Sleeptimer: virtual clock, see fake_run().
typedef struct sl_sleeptimer_timer_handle sl_sleeptimer_timer_handle_t;
typedef void (*sl_sleeptimer_timer_callback_t)(sl_sleeptimer_timer_handle_t *handle, void *data);
struct sl_sleeptimer_timer_handle {
  bool running;
  uint64_t due;
  uint32_t period;  // 0 = one shot
  sl_sleeptimer_timer_callback_t cb;
  void *data;
};
sl_status_t fake_timer_once(sl_sleeptimer_timer_handle_t *h, uint32_t ms, sl_sleeptimer_timer_callback_t cb, void *data);
sl_status_t fake_timer_periodic(sl_sleeptimer_timer_handle_t *h, uint32_t ms, sl_sleeptimer_timer_callback_t cb, void *data);
bool fake_timer_running(const sl_sleeptimer_timer_handle_t *h);  // start_* fail on a running timer
#define sl_sleeptimer_restart_timer_ms(h, ms, cb, data, prio, flags) fake_timer_once(h, ms, cb, data)
#define sl_sleeptimer_start_timer_ms(h, ms, cb, data, prio, flags) \
  (fake_timer_running(h) ? SL_STATUS_INVALID_STATE : fake_timer_once(h, ms, cb, data))
#define sl_sleeptimer_restart_periodic_timer_ms(h, ms, cb, data, prio, flags) fake_timer_periodic(h, ms, cb, data)
#define sl_sleeptimer_start_periodic_timer_ms(h, ms, cb, data, prio, flags) \
  (fake_timer_running(h) ? SL_STATUS_INVALID_STATE : fake_timer_periodic(h, ms, cb, data))
sl_status_t sl_sleeptimer_stop_timer(sl_sleeptimer_timer_handle_t *h);
sl_status_t sl_sleeptimer_is_timer_running(const sl_sleeptimer_timer_handle_t *h, bool *running);

// NVM3: three keys, kept in memory shared between the forked "power cycles".
typedef uint32_t Ecode_t;
typedef struct {
  int unused;
} nvm3_Handle_t;
#define ECODE_NVM3_OK 0u
#define ECODE_NVM3_ERR_KEY_NOT_FOUND 1u
#define nvm3_defaultHandle ((nvm3_Handle_t *)0)
Ecode_t nvm3_readData(nvm3_Handle_t *h, uint32_t key, void *value, size_t len);
Ecode_t nvm3_writeData(nvm3_Handle_t *h, uint32_t key, const void *value, size_t len);
Ecode_t nvm3_deleteObject(nvm3_Handle_t *h, uint32_t key);

// Reset cause registers and the bootloader
struct fake_emu {
  uint32_t RSTCAUSE;
};
extern struct fake_emu fake_emu_regs;
#define EMU (&fake_emu_regs)
void RMU_ResetCauseClear(void);
typedef struct {
  uint16_t reason;
  uint16_t signature;
} BootloaderResetCause_t;
#define BOOTLOADER_RESET_SIGNATURE_VALID 0xF00Fu
#define BOOTLOADER_RESET_SIGNATURE_INVALID 0xC33Cu
void bootloader_rebootAndInstall(void);  // the real one never returns; the fake counts calls

// SDK application hooks
void app_init(void);
void app_process_action(void);

// Controls of fake_sdk.c
#define FAKE_NVM_MARK 1u
#define FAKE_NVM_DIAG 2u
#define FAKE_NVM_BINDKEY 3u
#define FAKE_RESET_WORD ((volatile BootloaderResetCause_t *)0x20000000u)
void fake_arm_replay_check(void);  // before each power cycle: its first counter must reach the stored mark
void fake_init(void);              // once, before the first fork
void fake_nvm_put(uint32_t key, const void *value, size_t len);
bool fake_nvm_get(uint32_t key, void *value, size_t len);  // false when absent
bool fake_nvm_erase(uint32_t key);                         // false when absent
void fake_run(uint32_t ms);                                // advances the virtual clock, running timers and events
void fake_dispatch(void);                                  // delivers pending external signals and ADC samples
void fake_send(uint32_t event_id);                         // an event without data, as the stack sends it
extern uint8_t fake_door, fake_button;                     // pin levels
extern void (*fake_door_cb)(void);
extern void (*fake_button_cb)(void);
extern uint16_t fake_light_code, fake_battery_code;
extern bool fake_process_required;
extern bool fake_led;
extern bool fake_adv_running;
extern uint32_t fake_adv_interval;
extern unsigned fake_packets, fake_reboots, fake_wdog_feeds, fake_led_edges;
extern uint32_t fake_last_counter;
// Fault injection: the call with this number (1 = first, 0 = never) returns an error. Counted are
// the nvm3_* calls, and the Bluetooth calls whose status app.c checks.
extern unsigned fake_nvm_fail_at, fake_nvm_calls, fake_bt_fail_at, fake_bt_calls;

#endif  // FAKE_SDK_H
