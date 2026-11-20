// Copyright (C) 2026 xMase
// SPDX-License-Identifier: GPL-3.0-only
// Additional permission under GNU GPL version 3 section 7: see LICENSE-EXCEPTION.

// Host test of firmware/app.c on the fake SDK (sdk/fake_sdk.h, fake_sdk.c). Each power cycle is a
// forked child that boots app.c, runs a scenario and ends without any shutdown, as a reset does.
// The NVM3 and the counter table live in shared memory, so they survive the "reset". The fake
// advertiser fails the run when a packet carries a counter at or above the stored mark, or when a
// counter goes out with two different packets: the nonce-reuse invariant "counter_advance ->
// store_mark -> publish" (app.c update() and boot()).
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "psa/crypto.h"
#include "fake_sdk.h"
#include "check.h"

extern volatile struct {
  uint32_t magic;
  uint8_t key[16];
} g_bindkey_handoff;
extern volatile uint8_t g_debug_fault;

#define HANDOFF_MAGIC 0x4B455931u
#define RST_POR 0x1u
#define RST_WDOG0 0x8u
#define RST_SYSREQ 0x40u
#define BURST_END_MS 3100u
#define LOW_BATTERY_CODE 1607u   // 1902 mV
#define GOOD_BATTERY_CODE 2453u  // 2900 mV
#define CRASH_EXIT 43            // a signal reached the child: a crash, or the debug fault hook
#define BT_CALLS 12              // Bluetooth calls app.c checks in bt_calls_of_a_session

static const uint8_t bindkey[16] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                     0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff };

// Runs scenario in a child and returns its wait status.
static int fork_run(void (*scenario)(void))
{
  int status = 0;
  fflush(NULL);
  fake_arm_replay_check();
  pid_t pid = fork();
  CHECK(pid >= 0);
  if (pid == 0) {
    scenario();
    exit(0);
  }
  CHECK(waitpid(pid, &status, 0) == pid);
  return status;
}

static void cycle(void (*scenario)(void))
{
  int status = fork_run(scenario);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

// What the bootloader and the chip leave before a boot.
static void set_reset(uint32_t rstcause, uint16_t signature, uint16_t reason)
{
  fake_emu_regs.RSTCAUSE = rstcause;
  FAKE_RESET_WORD->signature = signature;
  FAKE_RESET_WORD->reason = reason;
}

static void stage_handoff(void)
{
  memcpy((void *)g_bindkey_handoff.key, bindkey, sizeof bindkey);
  g_bindkey_handoff.magic = HANDOFF_MAGIC;
}

static void boot(void)
{
  fake_send(sl_bt_evt_system_boot_id);
  fake_dispatch();
}

// A pin edge: the level changes, the interrupt runs, the 30 ms debounce completes.
static void door_to(uint8_t level)
{
  fake_door = level;
  fake_door_cb();
  fake_dispatch();
  fake_run(40);
}

static void button_to(uint8_t level)
{
  fake_button = level;
  fake_button_cb();
  fake_dispatch();
  fake_run(40);
}

static void press_release(void)
{
  button_to(0);
  fake_run(60);
  button_to(1);
  fake_run(60);
}

static void nvm_mark(uint32_t *mark)
{
  CHECK(fake_nvm_get(FAKE_NVM_MARK, mark, sizeof *mark));
}

// First boot, no mark: an old bindkey is deleted, so nothing is sent and the counter stays put.
static void first_boot_without_key(void)
{
  uint8_t stale[16] = { 1 };
  uint32_t mark;
  fake_nvm_put(FAKE_NVM_BINDKEY, stale, sizeof stale);
  boot();
  CHECK(!fake_nvm_get(FAKE_NVM_BINDKEY, stale, sizeof stale));
  nvm_mark(&mark);
  CHECK(mark == 132);
  press_release();
  fake_run(500);
  door_to(1);
  fake_run(70000);  // light tick, first battery sample
  CHECK(fake_packets == 0 && fake_wdog_feeds >= 1);
}

// Hand-off at boot: the key goes to NVM3, the staging area is wiped, the boot packet goes out
// with the boot counter and the burst ends in the idle interval. Then door changes and a click.
// The cycle ends in the middle of a burst, after the counter crossed the mark.
static void handoff_boot_and_events(void)
{
  uint8_t stored[16];
  boot();
  CHECK(g_bindkey_handoff.magic == 0 && g_bindkey_handoff.key[5] == 0);
  CHECK(fake_nvm_get(FAKE_NVM_BINDKEY, stored, sizeof stored) && memcmp(stored, bindkey, 16) == 0);
  CHECK(fake_packets == 1 && fake_last_counter == 132 && fake_adv_interval == 80);
  fake_run(BURST_END_MS);
  CHECK(fake_packets == 2 && fake_adv_interval == 16000 && fake_adv_running);
  fake_battery_code = GOOD_BATTERY_CODE;
  fake_light_code = 1234;
  fake_run(20000);  // light read and first battery sample
  CHECK(fake_packets >= 4);
  door_to(1);
  press_release();  // a click soon after a state packet: the event waits for the kind hold
  fake_run(450);
  fake_send(sl_bt_evt_advertiser_timeout_id);  // a timeout queued before the click: the burst goes on
  fake_run(5000);
  fake_door = 0;  // lost edge: the next light tick finds it
  fake_run(60000);
  for (int i = 0; i < 40; i++) {  // a door change every 200 ms crosses the stored mark
    door_to(i & 1);
    fake_run(160);
  }
}

// A click then a door change inside the press packet's kind hold; double click; the debounce bounce
// of the door and of the button; an anomalous reset was logged at boot.
static void clicks_and_bounces(void)
{
  uint8_t diag[4] = { 0 };
  boot();
  CHECK(fake_nvm_get(FAKE_NVM_DIAG, diag, 1) && diag[0] == 1);
  fake_run(12000);
  press_release();
  fake_run(500);
  door_to(!fake_door);  // press packet on air less than one second: the state packet waits
  fake_run(2000);
  press_release();
  press_release();
  fake_run(1000);
  fake_door = !fake_door;  // bounce: back to the old level before the third sample
  fake_door_cb();
  fake_dispatch();
  fake_run(15);
  fake_door = !fake_door;
  fake_run(100);
  fake_button = 0;
  fake_button_cb();
  fake_dispatch();
  fake_run(15);
  fake_button = 1;
  fake_run(100);
  app_init();
  app_process_action();
  fake_process_required = true;
  app_process_action();
  fake_send(0x7fffffffu);  // an event app.c does not handle
}

// A second boot starts timers that run already: the sleeptimer refuses, and app.c keeps the old ones.
static void boot_twice(void)
{
  boot();
  boot();
  fake_run(BURST_END_MS);
}

// PB0 held 3 s without a battery sample: the board reboots into the bootloader, no click follows.
static void hold_reboots(void)
{
  boot();
  button_to(0);
  fake_run(3100);
  CHECK(fake_reboots == 0);
  fake_run(700);
  CHECK(fake_reboots == 1);
  button_to(1);
  fake_run(1000);
  CHECK(fake_reboots == 1);
}

// A battery below 2000 mV refuses the OTA: five blinks and no reboot. A reset by the bootloader
// (valid word, GO) is not anomalous.
static void hold_refused_at_low_battery(void)
{
  boot();
  fake_battery_code = LOW_BATTERY_CODE;
  fake_run(12000);
  button_to(0);
  fake_run(4000);
  CHECK(fake_reboots == 0);
  button_to(1);
  fake_run(1000);
  uint8_t diag[4];
  CHECK(fake_nvm_get(FAKE_NVM_DIAG, diag, 1) && diag[0] == 1);  // still the count of the WDOG0 reset
  fake_battery_code = GOOD_BATTERY_CODE;
  fake_run(3600000);  // the hourly sample
  button_to(0);
  fake_run(4000);
  CHECK(fake_reboots == 1);
}

// The release completes in the same event as the 3 s hold: nothing happens.
static void release_races_hold(void)
{
  boot();
  button_to(0);
  fake_run(2960);
  fake_button = 1;
  fake_button_cb();
  fake_dispatch();
  fake_run(1000);
  CHECK(fake_reboots == 0);
}

// A failed assertion exits with FAKE_ASSERT_EXIT. The handler turns SIGABRT and SIGSEGV into
// CRASH_EXIT, so a crash of the fake or the test never passes for an assertion, and a child that
// dies on purpose still flushes its coverage data.
static void on_fault(int sig)
{
  (void)sig;
  exit(CRASH_EXIT);
}

// The debug fault hook: a fourth click counts as a triple click, which arms g_debug_fault. The
// next light tick writes to 0xFFFFFFF0 and the process dies with SIGSEGV (CRASH_EXIT).
static void triple_click_faults(void)
{
  boot();
  fake_run(5000);
  for (int i = 0; i < 4; i++) {
    press_release();
  }
  fake_run(1000);
  CHECK(g_debug_fault == 1);
  fake_run(61000);
}

// First boot with a hand-off and an anomalous reset: 7 NVM3 calls, each one checked by app.c.
static void first_boot_all_nvm_calls(void)
{
  fake_nvm_erase(FAKE_NVM_MARK);
  boot();
}

static void bt_calls_of_a_session(void)
{
  boot();
  fake_run(BURST_END_MS);
  door_to(1);
  CHECK(fake_bt_calls == BT_CALLS);  // reached only when no call failed
}

static void store_mark_fails_in_update(void)
{
  boot();
  fake_nvm_fail_at = fake_nvm_calls + 1;  // the next NVM3 call is the mark store of the crossing
  for (int i = 0; i < 40; i++) {
    door_to(i & 1);
  }
}

static void crypto_down_at_boot(void)
{
  mbedtls_psa_crypto_free();
  boot();
}

static void crypto_down_at_update(void)
{
  boot();
  mbedtls_psa_crypto_free();
  door_to(1);
}

static void mark_out_of_range(void)
{
  uint32_t bad = 0x80000001u;
  fake_nvm_put(FAKE_NVM_MARK, &bad, sizeof bad);
  boot();
}

static void expect_exit(void (*scenario)(void), int code)
{
  int status = fork_run(scenario);
  CHECK(WIFEXITED(status) && WEXITSTATUS(status) == code);
}

static void expect_abort(void (*scenario)(void))
{
  expect_exit(scenario, FAKE_ASSERT_EXIT);
}

// Fails the 1st, 2nd, ... call of a session, each in its own power cycle: every one must stop
// the firmware (calls = how many calls the session makes).
static void expect_abort_at_each_call(void (*scenario)(void), unsigned *fail_at, unsigned calls)
{
  for (unsigned n = 1; n <= calls; n++) {
    *fail_at = n;
    expect_abort(scenario);
  }
  *fail_at = 0;
}

// The failures of the hardware and the SDK: app.c stops instead of sending on.
static void failing_calls(void)
{
  expect_abort_at_each_call(bt_calls_of_a_session, &fake_bt_fail_at, BT_CALLS);
  cycle(bt_calls_of_a_session);
  expect_abort(store_mark_fails_in_update);
  expect_abort(crypto_down_at_boot);
  expect_abort(crypto_down_at_update);
  uint32_t mark;  // saved after the last sending cycle: restoring an older mark would reuse counters
  nvm_mark(&mark);
  expect_abort(mark_out_of_range);
  fake_nvm_put(FAKE_NVM_MARK, &mark, sizeof mark);
}

int main(void)
{
  fake_init();
  CHECK(psa_crypto_init() == PSA_SUCCESS);  // the SDK does this before the stack starts
  signal(SIGABRT, on_fault);
  signal(SIGSEGV, on_fault);
  set_reset(RST_WDOG0, BOOTLOADER_RESET_SIGNATURE_INVALID, 0);
  stage_handoff();
  expect_abort_at_each_call(first_boot_all_nvm_calls, &fake_nvm_fail_at, 7);
  g_bindkey_handoff.magic = 0;
  fake_nvm_erase(FAKE_NVM_MARK);  // back to a device that never booted
  fake_nvm_erase(FAKE_NVM_BINDKEY);
  fake_nvm_erase(FAKE_NVM_DIAG);

  set_reset(RST_POR, BOOTLOADER_RESET_SIGNATURE_INVALID, 0);
  cycle(first_boot_without_key);
  stage_handoff();
  cycle(handoff_boot_and_events);
  g_bindkey_handoff.magic = 0;  // the key is in NVM3 now, as on the device
  set_reset(RST_WDOG0, BOOTLOADER_RESET_SIGNATURE_INVALID, 0);
  cycle(clicks_and_bounces);
  set_reset(RST_POR, BOOTLOADER_RESET_SIGNATURE_INVALID, 0);
  cycle(hold_reboots);
  cycle(boot_twice);
  cycle(release_races_hold);
  set_reset(RST_SYSREQ, BOOTLOADER_RESET_SIGNATURE_VALID, 0x0201);
  cycle(hold_refused_at_low_battery);
  set_reset(RST_POR, BOOTLOADER_RESET_SIGNATURE_INVALID, 0);
  failing_calls();
  expect_exit(triple_click_faults, CRASH_EXIT);
  return 0;
}
