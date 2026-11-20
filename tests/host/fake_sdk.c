// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

// Fakes of the SDK calls and of hw.c for the host test of firmware/app.c (see sdk/fake_sdk.h).
// The advertiser fake checks the counter invariant on every packet: the counter is below the NVM3
// mark stored at that moment, and a counter never goes out with two different packets, also across
// "power cycles" (the forked children of test_app.c share the NVM3 and the counter table).
#define _GNU_SOURCE
#include <string.h>
#include <sys/mman.h>
#include "app.h"
#include "bthome.h"
#include "hw.h"
#include "fake_sdk.h"

#define NVM_KEYS 4
#define NVM_MAX 32
#define CTR_BASE 100  // counters never go below this (counter.h)
#define CTR_MAX 1024

typedef struct {
  bool present[NVM_KEYS];
  size_t len[NVM_KEYS];
  uint8_t data[NVM_KEYS][NVM_MAX];
  uint8_t packet[CTR_MAX][BTHOME_ADV_LEN + 1];  // [0] = length, 0 = counter not used yet
} shared_t;
static shared_t *shared;

struct fake_emu fake_emu_regs;
uint8_t fake_door, fake_button = 1;  // PB0 high = released
void (*fake_door_cb)(void);
void (*fake_button_cb)(void);
uint16_t fake_light_code, fake_battery_code;
bool fake_process_required, fake_led, fake_adv_running;
uint32_t fake_adv_interval, fake_last_counter;
unsigned fake_packets, fake_reboots, fake_wdog_feeds, fake_led_edges;
unsigned fake_nvm_fail_at, fake_nvm_calls, fake_bt_fail_at, fake_bt_calls;

static uint64_t now_ms;
static sl_sleeptimer_timer_handle_t *timers[16];
static sl_sleeptimer_timer_handle_t adv_timer;
static uint32_t pending_signals;
static uint16_t adv_duration;
static uint32_t counter_floor;  // the NVM3 mark when this power cycle began
static void (*battery_cb)(uint16_t code);

static void violation(const char *what, uint32_t counter)
{
  fprintf(stderr, "COUNTER INVARIANT VIOLATED: %s (counter %u)\n", what, (unsigned)counter);
  exit(1);
}

void fake_arm_replay_check(void)
{
  counter_floor = 0;
  fake_nvm_get(FAKE_NVM_MARK, &counter_floor, sizeof counter_floor);
}

void fake_init(void)
{
  shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  const void *ram = mmap((void *)0x20000000u, 4096, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (shared == MAP_FAILED || ram != (void *)0x20000000u) {
    fprintf(stderr, "fake_init: mmap failed\n");
    exit(1);
  }
}

// --- NVM3 ---
void fake_nvm_put(uint32_t key, const void *value, size_t len)
{
  if (key >= NVM_KEYS || len > NVM_MAX) {
    violation("bad NVM3 key or length", key);
  }
  memcpy(shared->data[key], value, len);
  shared->len[key] = len;
  shared->present[key] = true;
}

bool fake_nvm_get(uint32_t key, void *value, size_t len)
{
  if (key >= NVM_KEYS || !shared->present[key]) {
    return false;
  }
  memcpy(value, shared->data[key], len < shared->len[key] ? len : shared->len[key]);
  return true;
}

bool fake_nvm_erase(uint32_t key)
{
  bool had = key < NVM_KEYS && shared->present[key];
  if (had) {
    shared->present[key] = false;
  }
  return had;
}

#define ECODE_FAKE_ERROR 2u
static bool nvm_fault(void)
{
  return ++fake_nvm_calls == fake_nvm_fail_at;
}

Ecode_t nvm3_readData(nvm3_Handle_t *h, uint32_t key, void *value, size_t len)
{
  (void)h;
  if (nvm_fault()) {
    return ECODE_FAKE_ERROR;
  }
  if (key < NVM_KEYS && shared->present[key] && shared->len[key] != len) {
    return ECODE_FAKE_ERROR;  // the SDK refuses a read of another size than stored
  }
  return fake_nvm_get(key, value, len) ? ECODE_NVM3_OK : ECODE_NVM3_ERR_KEY_NOT_FOUND;
}

Ecode_t nvm3_writeData(nvm3_Handle_t *h, uint32_t key, const void *value, size_t len)
{
  (void)h;
  if (nvm_fault()) {
    return ECODE_FAKE_ERROR;
  }
  fake_nvm_put(key, value, len);
  return ECODE_NVM3_OK;
}

Ecode_t nvm3_deleteObject(nvm3_Handle_t *h, uint32_t key)
{
  (void)h;
  if (nvm_fault()) {
    return ECODE_FAKE_ERROR;
  }
  return fake_nvm_erase(key) ? ECODE_NVM3_OK : ECODE_NVM3_ERR_KEY_NOT_FOUND;
}

// --- sleeptimer on a virtual clock ---
static sl_status_t timer_arm(sl_sleeptimer_timer_handle_t *h, uint32_t ms, sl_sleeptimer_timer_callback_t cb, void *data)
{
  size_t i = 0;
  while (i < sizeof timers / sizeof *timers && timers[i] && timers[i] != h) {
    i++;
  }
  if (i == sizeof timers / sizeof *timers) {
    violation("too many timers", (uint32_t)i);
  }
  timers[i] = h;
  *h = (sl_sleeptimer_timer_handle_t){ .running = true, .due = now_ms + ms, .cb = cb, .data = data };
  return SL_STATUS_OK;
}

// cppcheck-suppress staticFunction ; also called through the sleeptimer macros of sdk/fake_sdk.h
sl_status_t fake_timer_once(sl_sleeptimer_timer_handle_t *h, uint32_t ms, sl_sleeptimer_timer_callback_t cb, void *data)
{
  return timer_arm(h, ms, cb, data);
}

// cppcheck-suppress unusedFunction ; called through a macro of sdk/fake_sdk.h
sl_status_t fake_timer_periodic(sl_sleeptimer_timer_handle_t *h, uint32_t ms, sl_sleeptimer_timer_callback_t cb, void *data)
{
  sl_status_t sc = timer_arm(h, ms, cb, data);
  h->period = ms;
  return sc;
}

// cppcheck-suppress unusedFunction ; called through a macro of sdk/fake_sdk.h
bool fake_timer_running(const sl_sleeptimer_timer_handle_t *h)
{
  return h->running;
}

sl_status_t sl_sleeptimer_stop_timer(sl_sleeptimer_timer_handle_t *h)
{
  h->running = false;
  return SL_STATUS_OK;
}

sl_status_t sl_sleeptimer_is_timer_running(const sl_sleeptimer_timer_handle_t *h, bool *running)
{
  *running = h->running;
  return SL_STATUS_OK;
}

static sl_sleeptimer_timer_handle_t *next_due(uint64_t limit)
{
  sl_sleeptimer_timer_handle_t *best = NULL;
  for (size_t i = 0; i < sizeof timers / sizeof *timers && timers[i]; i++) {
    if (timers[i]->running && timers[i]->due <= limit && (!best || timers[i]->due < best->due)) {
      best = timers[i];
    }
  }
  return best;
}

void fake_send(uint32_t event_id)
{
  sl_bt_msg_t evt = { .header = event_id };
  sl_bt_on_event(&evt);
}

// The stack delivers the signals raised since the last event as one event. A battery sample armed
// by the app runs at the next TX, which here is any dispatch while the advertiser runs.
void fake_dispatch(void)
{
  for (;;) {
    if (battery_cb && fake_adv_running) {
      void (*cb)(uint16_t) = battery_cb;
      battery_cb = NULL;
      cb(fake_battery_code);
    }
    if (!pending_signals) {
      return;
    }
    sl_bt_msg_t evt = { .header = sl_bt_evt_system_external_signal_id };
    evt.data.evt_system_external_signal.extsignals = pending_signals;
    pending_signals = 0;
    sl_bt_on_event(&evt);
  }
}

void fake_run(uint32_t ms)
{
  uint64_t end = now_ms + ms;
  sl_sleeptimer_timer_handle_t *t;
  while ((t = next_due(end)) != NULL) {
    now_ms = t->due;
    if (t->period) {
      t->due += t->period;
    } else {
      t->running = false;
    }
    t->cb(t, t->data);
    if (!next_due(now_ms)) {  // timers that expire together raise their signals in one event
      fake_dispatch();
    }
  }
  now_ms = end;
}

// --- Bluetooth stack ---
static sl_status_t bt_status(void)
{
  return ++fake_bt_calls == fake_bt_fail_at ? 1u : SL_STATUS_OK;
}

sl_status_t sl_bt_external_signal(uint32_t extsignals)
{
  pending_signals |= extsignals;
  return SL_STATUS_OK;
}

sl_status_t sl_bt_gap_get_identity_address(bd_addr *address, uint8_t *type)
{
  static const uint8_t le[6] = { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
  memcpy(address->addr, le, sizeof le);
  *type = 0;
  return bt_status();
}

sl_status_t sl_bt_advertiser_create_set(uint8_t *handle)
{
  *handle = 1;
  return bt_status();
}

sl_status_t sl_bt_advertiser_set_tx_power(uint8_t handle, int16_t power, int16_t *set_power)
{
  (void)handle;
  *set_power = power;
  return bt_status();
}

sl_status_t sl_bt_advertiser_stop(uint8_t handle)
{
  (void)handle;
  fake_adv_running = false;
  adv_timer.running = false;
  return SL_STATUS_OK;
}

// cppcheck-suppress unusedFunction ; called through a macro of sdk/fake_sdk.h
sl_status_t fake_adv_timing(uint8_t handle, uint32_t interval, uint16_t duration)
{
  (void)handle;
  fake_adv_interval = interval;
  adv_duration = duration;
  return bt_status();
}

static void adv_timeout(sl_sleeptimer_timer_handle_t *h, void *data)
{
  (void)h;
  (void)data;
  fake_adv_running = false;
  fake_send(sl_bt_evt_advertiser_timeout_id);
}

sl_status_t sl_bt_legacy_advertiser_start(uint8_t handle, uint8_t connectable)
{
  (void)handle;
  (void)connectable;
  fake_adv_running = true;
  if (adv_duration) {
    fake_timer_once(&adv_timer, adv_duration * 10u, adv_timeout, NULL);  // duration unit: 10 ms
  }
  return bt_status();
}

sl_status_t sl_bt_legacy_advertiser_set_data(uint8_t handle, uint8_t type, size_t len, const uint8_t *data)
{
  (void)handle;
  (void)type;
  size_t end = (size_t)data[0] + 1u;  // end of the service data AD; the counter LE sits before the 4-byte MIC
  uint32_t mark = 0, counter = 0;
  for (int i = 3; i >= 0; i--) {
    counter = counter << 8 | data[end - 8 + (size_t)i];
  }
  if (!fake_nvm_get(FAKE_NVM_MARK, &mark, sizeof mark) || counter >= mark) {
    violation("counter not below the stored NVM3 mark: nonce reuse after a reset", counter);
  }
  if (counter < CTR_BASE || counter - CTR_BASE >= CTR_MAX) {
    violation("counter out of the tracked range", counter);
  }
  if (fake_packets == 0 && counter < counter_floor) {
    violation("first counter after a reset is below the mark stored before it: replay", counter);
  }
  uint8_t *seen = shared->packet[counter - CTR_BASE];
  if (seen[0] && (seen[0] != len || memcmp(seen + 1, data, len) != 0)) {
    violation("same counter, different packet: nonce reuse", counter);
  }
  seen[0] = (uint8_t)len;
  memcpy(seen + 1, data, len);
  fake_last_counter = counter;
  fake_packets++;
  return bt_status();
}

// --- bootloader, reset cause, SDK hooks ---
void RMU_ResetCauseClear(void)
{
  fake_emu_regs.RSTCAUSE = 0;
}

void bootloader_rebootAndInstall(void)
{
  fake_reboots++;
}

bool app_is_process_required(void)
{
  return fake_process_required;
}

// --- hw.c ---
void hw_init(void)
{
}

uint8_t hw_door_level(void)
{
  return fake_door;
}

void hw_door_irq_register(void (*cb)(void))
{
  fake_door_cb = cb;
}

uint8_t hw_button_level(void)
{
  return fake_button;
}

void hw_button_irq_register(void (*cb)(void))
{
  fake_button_cb = cb;
}

void hw_light_on(void)
{
}

uint16_t hw_light_read(void)
{
  return fake_light_code;
}

void hw_battery_arm(void (*cb)(uint16_t code))
{
  battery_cb = cb;
}

void hw_led(bool on)
{
  fake_led = on;
  fake_led_edges++;
}

void hw_wdog_feed(void)
{
  fake_wdog_feeds++;
}
