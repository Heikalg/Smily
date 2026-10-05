#include "input_handler.h"

#include <Arduino.h>

#include "config.h"
#include "util.h"

static bool s_raw = false;       // last sampled level
static bool s_stable = false;    // debounced level
static uint32_t s_edgeMs = 0;    // when the raw level last changed
static uint32_t s_pressMs = 0;   // start of the current press
static uint32_t s_lastTapMs = 0; // most recent accepted tap
static uint8_t s_taps = 0;       // taps collected in the open window
static bool s_longFired = false;
static bool s_swallow = false;   // ignore contact until the pad is released

void input_begin(uint32_t now, bool ignoreHeldTouch) {
  pinMode(PIN_TOUCH, INPUT);
  s_raw = s_stable = ignoreHeldTouch;
  s_edgeMs = now;
  s_pressMs = now;
  s_lastTapMs = now;
  s_taps = 0;
  s_longFired = false;
  s_swallow = ignoreHeldTouch;
}

bool input_touchLevel() { return s_stable; }

InputEvents input_poll(uint32_t now) {
  InputEvents ev;

  const bool raw = (digitalRead(PIN_TOUCH) == TOUCH_ACTIVE_LEVEL);
  if (raw != s_raw) {
    s_raw = raw;
    s_edgeMs = now;
  }

  if (raw != s_stable && elapsed(now, s_edgeMs) >= TOUCH_DEBOUNCE_MS) {
    s_stable = raw;
    if (s_stable) {
      s_pressMs = now;
      s_longFired = false;
      if (!s_swallow) {
        if (s_taps < 250) s_taps++;
        s_lastTapMs = now;
      }
    } else {
      s_swallow = false;  // released: start counting taps again
    }
  }

  if (s_stable) {
    ev.touching = true;
    ev.activity = true;  // holding the pad keeps the device awake
  }

  // Long press, reported once while still held. It cancels the pending taps
  // so a hold never also registers as a pet.
  if (s_stable && !s_swallow && !s_longFired &&
      elapsed(now, s_pressMs) >= LONG_PRESS_MS) {
    s_longFired = true;
    s_taps = 0;
    ev.longPress = true;
    ev.activity = true;
  }

  // The window only closes once the finger is off, so a hold cannot finalise
  // a tap, and a tap is only "single" after the window expires quietly.
  if (s_taps && !s_stable && elapsed(now, s_lastTapMs) >= TAP_WINDOW_MS) {
    if (s_taps >= TRIPLE_TAP_COUNT) {
      ev.tripleTap = true;
    } else {
      ev.tap = true;
    }
    ev.activity = true;
    s_taps = 0;
  }

  return ev;
}
