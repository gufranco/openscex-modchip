// SPDX-FileCopyrightText: 2026 Gustavo Franco <gufranco@users.noreply.github.com>
// SPDX-License-Identifier: MIT

#include "led_test.h"

#include <stdbool.h>
#include <stdint.h>

#include "pscu/led.h"
#include "pscu/supply.h"

// Host tests for the status LED and the supply guard behind code 7. The display
// is pure arithmetic on elapsed time, so each stage is driven to the
// millisecond either side of where it must change.

static pscu_check_fn g_check;

static pscu_led_step_t led_after(pscu_led_t state, uint32_t ms) {
  return pscu_led_step(state, PSCU_LED_EVENT_NONE, 0U, ms);
}

static void test_led_boot(void) {
  pscu_led_t gate = pscu_led_init(1U, 0U);
  g_check(led_after(gate, 0U).on, "led: board blink lit at start");
  g_check(!led_after(gate, 310U).on, "led: board blink gap is dark");
  // Boundaries: a flash lasts exactly its on time, and a pass that took no
  // measurable time must not move the pattern, let alone pin it at the end.
  g_check(!led_after(gate, 300U).on, "led: a blink is dark from its 300th ms");
  pscu_led_step_t still = led_after(gate, 0U);
  g_check((still.state.phase_ms == 0U) && (still.state.stage == PSCU_LED_BOARD),
          "led: a zero step leaves the phase where it was");
  pscu_led_step_t waiting = led_after(gate, 600U);
  g_check((waiting.state.stage == PSCU_LED_WAIT) && !waiting.on,
          "led: one gate blink then the heartbeat wait");

  pscu_led_t carrier = pscu_led_init(2U, PSCU_LED_CODE_WATCHDOG);
  g_check(led_after(carrier, 610U).on, "led: carrier second blink lit");
  pscu_led_step_t replay = led_after(carrier, 1200U);
  g_check((replay.state.stage == PSCU_LED_REPLAY) && replay.on,
          "led: watchdog code replayed after the board blinks");
  pscu_led_step_t replay_dark = led_after(replay.state, 750U);
  g_check(!replay_dark.on, "led: replay code flash gap is dark");
  pscu_led_step_t after_replay = led_after(replay.state, 7000U);
  g_check(after_replay.state.stage == PSCU_LED_WAIT, "led: replay plays once then waits");
  g_check(led_after(replay.state, 6999U).state.stage == PSCU_LED_REPLAY,
          "led: replay still showing just before its one cycle ends");

  pscu_led_t moved = pscu_led_init(1U, PSCU_LED_CODE_BOARD_CHANGED);
  pscu_led_step_t moved_replay = led_after(moved, 600U);
  g_check((moved_replay.state.stage == PSCU_LED_REPLAY) && (moved_replay.state.replay == 6U),
          "led: a board change is replayed as code 6");

  pscu_led_step_t early = pscu_led_step(gate, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 10U);
  g_check(early.state.stage == PSCU_LED_BOARD, "led: a fault does not cut the board blinks");
}

static void test_led_heartbeat(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  g_check(!led_after(wait, 1959U).on, "led: heartbeat dark before the blip");
  g_check(led_after(wait, 1960U).on, "led: heartbeat blip at the end of the period");
  g_check(led_after(wait, 1999U).on, "led: heartbeat blip lasts 40 ms");
  g_check(!led_after(wait, 2000U).on, "led: heartbeat blip ends with the period");
}

static void test_led_results(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  pscu_led_step_t fired = pscu_led_step(wait, PSCU_LED_EVENT_FIRED, 0U, 0U);
  g_check((fired.state.stage == PSCU_LED_INJECT) && !fired.on,
          "led: injection leaves the LED to the string flashes");

  pscu_led_step_t accepted = pscu_led_step(fired.state, PSCU_LED_EVENT_ACCEPTED, 0U, 0U);
  g_check((accepted.state.code == PSCU_LED_CODE_ACCEPTED) && accepted.on,
          "led: accepted shows code 1");
  g_check(!led_after(accepted.state, 1000U).on, "led: code 1 has one flash then a pause");
  g_check(led_after(accepted.state, 3000U).on, "led: code 1 repeats after the pause");
  pscu_led_step_t done = led_after(accepted.state, 9000U);
  g_check((done.state.stage == PSCU_LED_DARK) && !done.on,
          "led: result shown three times then dark");
  g_check(led_after(accepted.state, 8999U).state.stage == PSCU_LED_CODE,
          "led: result still showing just before its third repeat ends");

  pscu_led_step_t refused = pscu_led_step(fired.state, PSCU_LED_EVENT_REFUSED, 0U, 0U);
  g_check(refused.state.code == PSCU_LED_CODE_REFUSED, "led: refused shows code 2");
  g_check(led_after(refused.state, 1000U).on, "led: code 2 second flash lit");
  g_check(!led_after(refused.state, 2000U).on, "led: code 2 has only two flashes");
}

static void test_led_faults(void) {
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  pscu_led_step_t live = pscu_led_step(wait, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 0U);
  g_check((live.state.code == PSCU_LED_CODE_NO_SQCK) && live.state.live,
          "led: a fault takes the waiting LED");
  pscu_led_step_t held =
      pscu_led_step(live.state, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 20000U);
  g_check((held.state.stage == PSCU_LED_CODE) && (held.state.phase_ms == 20000U),
          "led: a live code repeats for as long as the fault holds");
  pscu_led_step_t cleared = pscu_led_step(held.state, PSCU_LED_EVENT_NONE, 0U, 0U);
  g_check(cleared.state.stage == PSCU_LED_WAIT, "led: a cleared fault returns to the heartbeat");

  pscu_led_t result = pscu_led_step(wait, PSCU_LED_EVENT_ACCEPTED, 0U, 0U).state;
  pscu_led_step_t swap = pscu_led_step(result, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 0U);
  g_check(swap.state.code == PSCU_LED_CODE_NO_SQCK, "led: a fault replaces a result code");
  g_check(pscu_led_step(result, PSCU_LED_EVENT_NONE, 0U, 0U).state.code == PSCU_LED_CODE_ACCEPTED,
          "led: a result code is not cut short without a fault");

  pscu_led_t dark = led_after(result, 9000U).state;
  g_check(pscu_led_step(dark, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 0U).state.stage ==
              PSCU_LED_CODE,
          "led: a fault wakes the dark LED");
  pscu_led_t inject = pscu_led_step(wait, PSCU_LED_EVENT_FIRED, 0U, 0U).state;
  g_check(pscu_led_step(inject, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_NO_SQCK, 0U).state.stage ==
              PSCU_LED_CODE,
          "led: a fault interrupts waiting for a result");

  pscu_led_t shown = pscu_led_step(wait, PSCU_LED_EVENT_ACCEPTED, 0U, 0U).state;
  g_check(pscu_led_disc_arrived(shown).stage == PSCU_LED_WAIT,
          "led: a new disc ends the last result");
  g_check(pscu_led_disc_arrived(inject).stage == PSCU_LED_WAIT,
          "led: a new disc ends the string wait");
  g_check(pscu_led_disc_arrived(dark).stage == PSCU_LED_WAIT,
          "led: a new disc wakes the heartbeat");
  pscu_led_t beating = led_after(wait, 1500U).state;
  g_check(pscu_led_disc_arrived(beating).phase_ms == 1500U, "led: the heartbeat keeps its phase");
  g_check(pscu_led_disc_arrived(live.state).live, "led: a live fault survives a new disc");
  pscu_led_t booting = pscu_led_init(2U, 0U);
  g_check(pscu_led_disc_arrived(booting).stage == PSCU_LED_BOARD,
          "led: the boot blinks are not cut");

  pscu_led_t full = wait;
  full.phase_ms = 0xFFFFFFF0UL;
  g_check(led_after(full, 0x100U).state.phase_ms == 0xFFFFFFFFUL, "led: phase saturates");
}

// pscu_led_fault(supply_low, seen, quiet_ms, since_ms, framed, armed).
static void test_led_fault_rules(void) {
  g_check(pscu_led_fault(false, false, 4999U, 0U, false, false) == 0U,
          "fault: no frames yet, still waiting");
  g_check(pscu_led_fault(false, false, 5000U, 0U, false, false) == PSCU_LED_CODE_NO_SQCK,
          "fault: no frame for 5 s after power-on means no SUBQ");
  g_check(pscu_led_fault(false, false, 19999U, 0U, false, false) == PSCU_LED_CODE_NO_SQCK,
          "fault: the install check still shows just before it ends");
  g_check(pscu_led_fault(false, false, 20000U, 0U, false, false) == 0U,
          "fault: a console left on with no disc stops showing the install check");
  g_check(pscu_led_fault(false, true, 60000U, 0U, false, false) == 0U,
          "fault: silence after frames were seen is just no disc");
  g_check(pscu_led_fault(false, true, 0U, 19999U, true, false) == 0U,
          "fault: frames, check not due yet");
  g_check(pscu_led_fault(false, true, 0U, 20000U, true, false) == PSCU_LED_CODE_NO_CHECK,
          "fault: frames but no region check for 20 s");
  g_check(pscu_led_fault(false, true, 0U, 20000U, true, true) == 0U,
          "fault: an armed disc is not a fault");
  g_check(pscu_led_fault(false, true, 0U, 20000U, false, false) == 0U,
          "fault: no frames since this disc is not a region-check fault");
}

// A low supply outranks every other fault: with the chip out of its rating no
// other reading can be trusted, and it is the one an installer must fix first.
static void test_led_supply_fault(void) {
  g_check(pscu_led_fault(true, true, 0U, 0U, true, true) == PSCU_LED_CODE_SUPPLY,
          "fault: a low supply shows code 7 on a healthy disc");
  g_check(pscu_led_fault(true, false, 5000U, 0U, false, false) == PSCU_LED_CODE_SUPPLY,
          "fault: a low supply outranks no SUBQ");
  g_check(pscu_led_fault(true, true, 0U, 20000U, true, false) == PSCU_LED_CODE_SUPPLY,
          "fault: a low supply outranks no region check");
  pscu_led_t wait = led_after(pscu_led_init(1U, 0U), 600U).state;
  pscu_led_step_t low = pscu_led_step(wait, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_SUPPLY, 0U);
  g_check((low.state.code == PSCU_LED_CODE_SUPPLY) && low.state.live, "led: code 7 is shown live");
  g_check(pscu_led_step(low.state, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_SUPPLY, 6000U).on,
          "led: code 7 has a seventh flash");
  g_check(!pscu_led_step(low.state, PSCU_LED_EVENT_NONE, PSCU_LED_CODE_SUPPLY, 7000U).on,
          "led: code 7 has no eighth flash");
  g_check(pscu_led_init(1U, PSCU_LED_CODE_SUPPLY).replay == PSCU_LED_CODE_SUPPLY,
          "led: 7 is the highest code the display accepts");
}

// The supply reading is the 1.1 V bandgap against VCC, raw = 1024 x 1.1 / VCC,
// so each bound is checked one step either side: 388 is 2903 mV, the last
// reading at or above 2.9 V, and 188 is 5991 mV, the first under 6 V.
static void test_supply(void) {
  g_check(pscu_supply_ok(388U), "supply: 2.90 V is enough");
  g_check(!pscu_supply_ok(389U), "supply: 2.896 V is too low");
  g_check(pscu_supply_ok(188U), "supply: 5.99 V is plausible");
  g_check(!pscu_supply_ok(187U), "supply: 6.02 V is not a real reading");
  g_check(pscu_supply_ok(225U), "supply: 5 V reads as good");
  g_check(pscu_supply_ok(341U), "supply: 3.3 V reads as good");
  g_check(!pscu_supply_ok(0U), "supply: a timed-out conversion blocks injection");
  g_check(!pscu_supply_ok(1023U), "supply: a full-scale reading blocks injection");
}
void led_tests(pscu_check_fn check) {
  g_check = check;
  test_led_boot();
  test_led_heartbeat();
  test_led_results();
  test_led_faults();
  test_led_fault_rules();
  test_led_supply_fault();
  test_supply();
}
