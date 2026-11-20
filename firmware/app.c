// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

// BTHome v2 broadcaster: boot, normal and press packets (design spec, "Packet" and "Firmware structure").
// Interrupt and timer callbacks only raise signals; all work runs in the Bluetooth event loop.
#include <string.h>
#include "sl_bt_api.h"
#include "sl_sleeptimer.h"
#include "nvm3_default.h"
#include "em_rmu.h"
#include "btl_interface.h"
#include "btl_reset_info.h"
#include "app_properties_config.h"
#include "app_assert.h"
#include "mbedtls/platform_util.h"
#include "app.h"
#include "bthome.h"
#include "counter.h"
#include "diag.h"
#include "battery.h"
#include "door.h"
#include "hw.h"

#define SIG_DOOR_EDGE (1u << 0)
#define SIG_DEBOUNCE (1u << 1)
#define SIG_LIGHT (1u << 2)
#define SIG_BATTERY (1u << 3)
#define SIG_LED_STEP (1u << 4)
#define SIG_BATTERY_DONE (1u << 5)
#define SIG_LIGHT_READ (1u << 6)
#define SIG_BUTTON_EDGE (1u << 7)
#define SIG_BUTTON_DEBOUNCE (1u << 8)
#define SIG_HOLD (1u << 9)
#define SIG_REBOOT (1u << 10)
#define SIG_KIND_HOLD (1u << 11)
#define SIG_CLICK (1u << 12)

#define NVM3_KEY_COUNTER_MARK 0x00001u
#define NVM3_KEY_DIAG 0x00002u
#define NVM3_KEY_BINDKEY 0x00003u
#define BINDKEY_HANDOFF_MAGIC 0x4B455931u  // "KEY1"

// The bootloader leaves its reset reason in the first RAM word (linker region BOOTLOADER_RESET_REGION).
#define RESET_WORD ((volatile BootloaderResetCause_t *)0x20000000u)

// Advertising timing, in the stack's units: interval 0.625 ms, duration 10 ms.
#define BURST_INTERVAL 80    // 50 ms
#define BURST_DURATION 300   // 3 s
#define IDLE_INTERVAL 16000  // 10 s
#define TX_POWER_DBM_X10 0   // 0 dBm for idle and burst (spec, "Proposed timing")

#define DEBOUNCE_MS 10
#define LIGHT_PERIOD_MS 60000u
#define LIGHT_SETTLE_MS 10000u  // PD4 high to the read, in EM2 (owner, 2026-11-17)
#define BATTERY_PERIOD_MS 3600000u
#define BATTERY_FIRST_MS 10000u
#define LED_BLINK_MS 100   // on and off time; one blink at boot and per door change
#define HOLD_MS 3000u      // PB0 held this long reboots into the bootloader (owner, 2026-11-17)
#define BLINK_END_MS 600u  // 3 blinks, as blink(3) lasts
// PB0 hold enters the AppLoader only above this; provisional until the PPK2/supply test (owner, 2026-11-17)
#define OTA_MIN_MV 2000u
#define OTA_REFUSED_BLINKS 5
#define KIND_HOLD_MS 1000u  // proxies scan 10 ms per 100 ms: a packet needs ~1 s on air to be caught
#define BUTTON_BLINKS 2     // a press has its own pattern (owner, 2026-11-17)
#define CLICK_GAP_MS 400u   // releases closer than this form one click sequence (designer, 2026-11-18)
#define CLICKS_MAX 3

static uint8_t adv = SL_BT_INVALID_ADVERTISING_SET_HANDLE;  // stays invalid while NVM3 has no key
static psa_key_id_t key_id;
static uint8_t mac[6];  // identity address, display order
static counter_t ctr;
static bthome_state_t now;   // latest readings
static bthome_state_t sent;  // payload on air
static uint8_t event_due;    // decided click sequence (1-3) waiting for its turn, 0 = none
static uint8_t clicks;       // short presses in the running sequence
static door_debounce_t deb;
static door_debounce_t btn;               // same debounce as the door, as stock
static uint8_t led_steps;                 // LED toggles left in the current pattern
static volatile uint16_t battery_loaded;  // code sampled during a TX, set in the IADC interrupt
static sl_sleeptimer_timer_handle_t t_debounce, t_btn_debounce, t_light, t_light_read, t_battery, t_battery_first, t_led;
static sl_sleeptimer_timer_handle_t t_hold, t_reboot, t_kind_hold, t_click;
static diag_t diag;
#ifndef BUILD_RELEASE
volatile uint8_t g_debug_fault;  // set to 1 over SWD or by a triple click: the next light tick faults the chip
#endif
// swd/provision-bindkey.tcl fills this in .bss at `main` (magic last); boot() moves it to NVM3 and wipes it.
volatile struct {
  uint32_t magic;
  uint8_t key[16];
} g_bindkey_handoff;

static void raise(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  (void)handle;
  sl_bt_external_signal((uint32_t)(uintptr_t)data);
}

static void door_edge(void)
{
  sl_bt_external_signal(SIG_DOOR_EDGE);
}

static void button_edge(void)
{
  sl_bt_external_signal(SIG_BUTTON_EDGE);
}

static void battery_done(uint16_t code)
{
  battery_loaded = code;
  sl_bt_external_signal(SIG_BATTERY_DONE);
}

static void store_mark(void)
{
  // A failed write must stop the firmware: sending above an unstored mark could repeat nonces.
  Ecode_t e = nvm3_writeData(nvm3_defaultHandle, NVM3_KEY_COUNTER_MARK, &ctr.mark, sizeof ctr.mark);
  app_assert(e == ECODE_NVM3_OK, "nvm3 write 0x%lx", (unsigned long)e);
}

static void publish(void)
{
  uint8_t buf[BTHOME_ADV_LEN], len;
  psa_status_t ps = bthome_build(buf, &len, &now, ctr.value, mac, key_id);
  app_assert(ps == PSA_SUCCESS, "bthome_build %ld", (long)ps);
  sl_status_t sc = sl_bt_legacy_advertiser_set_data(adv, sl_bt_advertiser_advertising_data_packet,
                                                    len, buf);
  app_assert_status(sc);
  sent = now;
  sl_sleeptimer_restart_timer_ms(&t_kind_hold, KIND_HOLD_MS, raise,
                                 (void *)(uintptr_t)SIG_KIND_HOLD, 0, 0);
}

// Sends the latest readings if they differ from the payload on air, then a decided click event;
// a repeat keeps its counter. A packet of another kind waits until the payload on air has had
// KIND_HOLD_MS (SIG_KIND_HOLD calls update again): the state first, then the event.
static void update(void)
{
  if (adv == SL_BT_INVALID_ADVERTISING_SET_HANDLE) {
    return;  // no key: nothing is sent, so the counter must not advance
  }
  bool holding = false;
  sl_sleeptimer_is_timer_running(&t_kind_hold, &holding);
  if (!bthome_payload_equal(&now, &sent)) {
    if (holding && now.kind != sent.kind) {
      return;
    }
  } else if (event_due && !holding) {
    now.kind = BTHOME_PRESS;  // a press packet on air stays the same payload, so the event gets its own counter
    now.event = event_due;
    event_due = 0;
  } else {
    return;
  }
  if (counter_advance(&ctr)) {
    store_mark();
  }
  publish();
}

static void led_step(void)
{
  sl_sleeptimer_restart_timer_ms(&t_led, LED_BLINK_MS, raise, (void *)(uintptr_t)SIG_LED_STEP, 0, 0);
}

// n blinks of LED_BLINK_MS on, LED_BLINK_MS off; a new pattern replaces a running one.
static void blink(uint8_t n)
{
  led_steps = (uint8_t)(2 * n - 1);
  hw_led(true);
  led_step();
}

static void led_tick(void)
{
  if (led_steps == 0) {
    return;
  }
  led_steps--;
  hw_led(led_steps & 1);  // odd steps left = on
  if (led_steps) {
    led_step();
  }
}

// A burst never arms the battery: only SIG_BATTERY does; the sample is taken on the next TX, normally an idle one.
static void burst_start(void)
{
  if (adv == SL_BT_INVALID_ADVERTISING_SET_HANDLE) {
    return;
  }
  (void)sl_bt_advertiser_stop(adv);  // not running at boot
  sl_status_t sc = sl_bt_advertiser_set_timing(adv, BURST_INTERVAL, BURST_INTERVAL, BURST_DURATION, 0);
  app_assert_status(sc);
  sc = sl_bt_legacy_advertiser_start(adv, sl_bt_legacy_advertiser_non_connectable);
  app_assert_status(sc);
}

static void idle_start(void)
{
  // A burst restarted just before a queued timeout is still running: stop it, as burst_start does.
  (void)sl_bt_advertiser_stop(adv);
  sl_status_t sc = sl_bt_advertiser_set_timing(adv, IDLE_INTERVAL, IDLE_INTERVAL, 0, 0);
  app_assert_status(sc);
  sc = sl_bt_legacy_advertiser_start(adv, sl_bt_legacy_advertiser_non_connectable);
  app_assert_status(sc);
}

static void light_start(void)
{
  hw_light_on();
  sl_sleeptimer_restart_timer_ms(&t_light_read, LIGHT_SETTLE_MS, raise,
                                 (void *)(uintptr_t)SIG_LIGHT_READ, 0, 0);
}

static void debounce_start(void)
{
  door_debounce_init(&deb, deb.stable);  // every edge restarts the count, as stock does
  sl_sleeptimer_restart_periodic_timer_ms(&t_debounce, DEBOUNCE_MS, raise,
                                          (void *)(uintptr_t)SIG_DEBOUNCE, 0, 0);
}

static void debounce_tick(void)
{
  door_result_t r = door_debounce_sample(&deb, hw_door_level());
  if (r == DOOR_CONTINUE) {
    return;
  }
  sl_sleeptimer_stop_timer(&t_debounce);
  if (r == DOOR_CHANGED) {
    now.door_open = deb.stable;
    if (now.kind == BTHOME_PRESS) {
      now.kind = BTHOME_NORMAL;  // PRESS means the press packet is on air; it has no door, a boot packet stays
    }
    update();       // a press packet on air less than KIND_HOLD_MS is replaced when the hold ends; a waiting event follows
    burst_start();  // a change during a burst restarts it
    blink(1);
  }
}

static void button_start(void)
{
  door_debounce_init(&btn, btn.stable);
  sl_sleeptimer_restart_periodic_timer_ms(&t_btn_debounce, DEBOUNCE_MS, raise,
                                          (void *)(uintptr_t)SIG_BUTTON_DEBOUNCE, 0, 0);
}

// A short release counts a click; CLICK_GAP_MS after the last release SIG_CLICK decides the sequence.
// The event goes out through update() and its burst stays on air; a hold of HOLD_MS reboots into the
// bootloader instead and drops the clicks.
static void button_tick(void)
{
  door_result_t r = door_debounce_sample(&btn, hw_button_level());
  if (r == DOOR_CONTINUE) {
    return;
  }
  sl_sleeptimer_stop_timer(&t_btn_debounce);
  if (r != DOOR_CHANGED) {
    return;
  }
  if (btn.stable == 0) {
    sl_sleeptimer_stop_timer(&t_click);  // a held press must not be decided before it ends
    sl_sleeptimer_restart_timer_ms(&t_hold, HOLD_MS, raise, (void *)(uintptr_t)SIG_HOLD, 0, 0);
    return;
  }
  bool running = false;  // not running: the hold fired already, or the release has no press before it
  sl_sleeptimer_is_timer_running(&t_hold, &running);
  if (running) {
    sl_sleeptimer_stop_timer(&t_hold);
    if (clicks < CLICKS_MAX) {
      clicks++;  // more than three count as three
    }
    sl_sleeptimer_restart_timer_ms(&t_click, CLICK_GAP_MS, raise, (void *)(uintptr_t)SIG_CLICK, 0, 0);
  }
}

static void click_decided(void)
{
  event_due = clicks;
#ifndef BUILD_RELEASE
  if (clicks == 3) {
    g_debug_fault = 1;  // debug build only: a triple click arms the fault hook
  }
#endif
  clicks = 0;
  update();
  burst_start();
  blink(BUTTON_BLINKS);
}

static void hold_fired(void)
{
  // At 3 s the debounced state decides: a press ends in an event on release or in a reboot, except
  // a release that races this expiry (~1 ms): it ends with neither. A release still in its debounce
  // window counts as held.
  clicks = 0;  // the sequence is dropped, also when the OTA is refused or the press is already released
  sl_sleeptimer_stop_timer(&t_click);
  if (btn.stable != 0) {
    return;
  }
  if (now.battery_mv != 0 && now.battery_mv < OTA_MIN_MV) {  // 0: no sample yet, allowed
    blink(OTA_REFUSED_BLINKS);
    return;
  }
  blink(3);
  sl_sleeptimer_restart_timer_ms(&t_reboot, BLINK_END_MS, raise, (void *)(uintptr_t)SIG_REBOOT, 0, 0);
}

static void boot(void)
{
  hw_init();

  uint32_t rstcause = EMU->RSTCAUSE;
  RMU_ResetCauseClear();
  bool word_valid = RESET_WORD->signature == BOOTLOADER_RESET_SIGNATURE_VALID;
  uint16_t reset_reason = RESET_WORD->reason;
  RESET_WORD->signature = BOOTLOADER_RESET_SIGNATURE_INVALID;  // the next reset must not see it
  memset(&diag, 0, sizeof diag);
  Ecode_t de = nvm3_readData(nvm3_defaultHandle, NVM3_KEY_DIAG, &diag, sizeof diag);
  app_assert(de == ECODE_NVM3_OK || de == ECODE_NVM3_ERR_KEY_NOT_FOUND, "nvm3 read 0x%lx", (unsigned long)de);
  if (diag_boot(&diag, rstcause, word_valid, reset_reason)) {
    de = nvm3_writeData(nvm3_defaultHandle, NVM3_KEY_DIAG, &diag, sizeof diag);
    app_assert(de == ECODE_NVM3_OK, "nvm3 write 0x%lx", (unsigned long)de);
  }

  uint32_t mark = 0;
  Ecode_t e = nvm3_readData(nvm3_defaultHandle, NVM3_KEY_COUNTER_MARK, &mark, sizeof mark);
  // Only "not found" means first boot. Any other error must not fall back to 100 (replayed nonces).
  app_assert(e == ECODE_NVM3_OK || e == ECODE_NVM3_ERR_KEY_NOT_FOUND, "nvm3 read 0x%lx", (unsigned long)e);
  if (e == ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    // A restarted counter must never run under an old key.
    Ecode_t kd = nvm3_deleteObject(nvm3_defaultHandle, NVM3_KEY_BINDKEY);
    app_assert(kd == ECODE_NVM3_OK || kd == ECODE_NVM3_ERR_KEY_NOT_FOUND, "nvm3 delete 0x%lx", (unsigned long)kd);
  }
  bool ok = counter_boot(&ctr, e == ECODE_NVM3_OK, mark);
  app_assert(ok, "counter mark 0x%lx out of range", (unsigned long)mark);
  store_mark();

  // The key lives in NVM3, not in the image (spec, "Bindkey"); a hand-off over SWD replaces it.
  if (g_bindkey_handoff.magic == BINDKEY_HANDOFF_MAGIC) {
    uint8_t staged[16];
    for (int i = 0; i < 16; i++) {
      staged[i] = g_bindkey_handoff.key[i];
    }
    Ecode_t ke2 = nvm3_writeData(nvm3_defaultHandle, NVM3_KEY_BINDKEY, staged, sizeof staged);
    app_assert(ke2 == ECODE_NVM3_OK, "nvm3 write 0x%lx", (unsigned long)ke2);
    mbedtls_platform_zeroize(staged, sizeof staged);
    for (int i = 0; i < 16; i++) {
      g_bindkey_handoff.key[i] = 0;
    }
    g_bindkey_handoff.magic = 0;  // after a successful write only
  }
  uint8_t bindkey[16];
  Ecode_t ke = nvm3_readData(nvm3_defaultHandle, NVM3_KEY_BINDKEY, bindkey, sizeof bindkey);
  app_assert(ke == ECODE_NVM3_OK || ke == ECODE_NVM3_ERR_KEY_NOT_FOUND, "nvm3 read 0x%lx", (unsigned long)ke);
  bool has_key = ke == ECODE_NVM3_OK;
  if (has_key) {
    psa_status_t ps = bthome_key_import(bindkey, &key_id);
    app_assert(ps == PSA_SUCCESS, "key import %ld", (long)ps);
  }
  mbedtls_platform_zeroize(bindkey, sizeof bindkey);

  bd_addr addr;
  uint8_t type;
  sl_status_t sc = sl_bt_gap_get_identity_address(&addr, &type);
  app_assert_status(sc);
  for (int i = 0; i < 6; i++) {
    mac[i] = addr.addr[5 - i];  // bd_addr is little-endian
  }

  hw_door_irq_register(door_edge);  // before the level read, so no edge after it is lost
  door_debounce_init(&deb, hw_door_level());
  hw_button_irq_register(button_edge);
  door_debounce_init(&btn, hw_button_level());
  now.door_open = deb.stable;
  // now.battery_mv stays 0 until the first sample, 10 s after boot (t_battery_first): the first
  // packet has no voltage object, so HA keeps its last value under load (owner, 2026-11-16).
  now.light = BTHOME_LIGHT_NONE;  // no 3D object until the first 10 s sample (owner, 2026-11-17)
  now.resets = diag.count;
  now.cause = diag.cause;
  now.version = SL_APPLICATION_VERSION;
  now.kind = BTHOME_BOOT;  // until the boot burst ends

  blink(1);

  if (has_key) {
    sc = sl_bt_advertiser_create_set(&adv);
    app_assert_status(sc);
    int16_t set_power;
    sc = sl_bt_advertiser_set_tx_power(adv, TX_POWER_DBM_X10, &set_power);
    app_assert_status(sc);
    publish();  // the first packet uses the boot counter, which is below the stored mark
    burst_start();
  }

  light_start();
  sl_sleeptimer_start_periodic_timer_ms(&t_light, LIGHT_PERIOD_MS, raise,
                                        (void *)(uintptr_t)SIG_LIGHT, 0, 0);
  sl_sleeptimer_start_periodic_timer_ms(&t_battery, BATTERY_PERIOD_MS, raise,
                                        (void *)(uintptr_t)SIG_BATTERY, 0, 0);
  // The boot burst's first TX is inside the boot load transient (~2.57 V): sample 10 s later.
  sl_sleeptimer_start_timer_ms(&t_battery_first, BATTERY_FIRST_MS, raise,
                               (void *)(uintptr_t)SIG_BATTERY, 0, 0);
}

static void on_signals(uint32_t sig)
{
  if (sig & SIG_DOOR_EDGE) {
    debounce_start();
  } else if (sig & SIG_DEBOUNCE) {
    debounce_tick();  // skipped when an edge came in the same batch: the first sample is 10 ms after it
  }
  if (sig & SIG_BUTTON_EDGE) {
    button_start();
  } else if (sig & SIG_BUTTON_DEBOUNCE) {
    button_tick();
  }
  if (sig & SIG_CLICK) {
    click_decided();
  }
  if (sig & SIG_HOLD) {
    hold_fired();
  }
  if (sig & SIG_KIND_HOLD) {
    update();
  }
  if (sig & SIG_REBOOT) {
    bootloader_rebootAndInstall();
  }
  if (sig & SIG_LIGHT) {
    hw_wdog_feed();
#ifndef BUILD_RELEASE
    if (g_debug_fault == 1) {
      (void)*(volatile uint32_t *)0xFFFFFFF0u;  // BusFault -> HardFault -> WDOG0 within ~256 s
    }
#endif
    light_start();
    // A lost PA3 edge would leave the door wrong until the next change: recheck once a minute.
    if (hw_door_level() != deb.stable) {
      debounce_start();
    }
  }
  if (sig & SIG_LIGHT_READ) {
    now.light = hw_light_read();
    update();
  }
  if (sig & SIG_BATTERY) {
    hw_battery_arm(battery_done);  // the next idle TX (<= 10 s) samples it
    update();
  }
  if (sig & SIG_BATTERY_DONE) {
    now.battery_mv = battery_mv(battery_loaded);
    update();
  }
  if (sig & SIG_LED_STEP) {
    led_tick();
  }
}

void app_init(void)  // cppcheck-suppress unusedFunction ; called by the SDK (main.c)
{
}

void app_process_action(void)  // cppcheck-suppress unusedFunction ; called by the SDK (main.c)
{
  if (app_is_process_required()) {
  }
}

void sl_bt_on_event(sl_bt_msg_t *evt)  // cppcheck-suppress unusedFunction ; called by the SDK (Bluetooth stack)
{
  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
      boot();
      break;
    case sl_bt_evt_system_external_signal_id:
      on_signals(evt->data.evt_system_external_signal.extsignals);
      break;
    case sl_bt_evt_advertiser_timeout_id:
      now.kind = BTHOME_NORMAL;  // the boot and press packets stay on air only for their burst
      update();
      if (sent.kind == BTHOME_NORMAL && !event_due && bthome_payload_equal(&now, &sent)) {
        idle_start();
      } else {
        burst_start();  // a packet waits for the kind hold, or a press just went out: keep the burst
      }
      break;
    default:
      break;
  }
}
