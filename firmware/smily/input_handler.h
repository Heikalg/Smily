// ---------------------------------------------------------------------------
// Input Handler - debounce, tap counting, tap-window logic.
// Produces events only. It never looks at or changes the system state.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

struct InputEvents {
  bool tap = false;        // one (or two) taps, finalised after the window
  bool tripleTap = false;  // three or more taps inside the window
  bool longPress = false;  // >= LONG_PRESS_MS held (Phase 4 weather hook)
  bool touching = false;   // debounced level, right now
  bool activity = false;   // anything happened: resets the inactivity timer
};

// ignoreHeldTouch: true when we just woke from Deep Sleep. The finger is
// still on the pad, and per the behaviour decisions a wake touch is not a
// pet, so that first contact is swallowed until the pad is released.
void input_begin(uint32_t now, bool ignoreHeldTouch);

// Call every loop - debounce needs finer granularity than the frame clock.
InputEvents input_poll(uint32_t now);

// Debounced level without consuming events (used by the sleep sequence).
bool input_touchLevel();
