// ---------------------------------------------------------------------------
// Power Manager - inactivity timer, wake-cause detection, and the Deep Sleep
// entry sequence. The TTP223 is the only wake source (spec 2.3).
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

enum WakeCause : uint8_t {
  WAKE_COLD,   // power-on or reset
  WAKE_TOUCH,  // TTP223 pulled the wake pin high
  WAKE_OTHER,
};

WakeCause power_begin(uint32_t now);

// Any touch or shake event pushes the 15 s countdown back.
void power_noteActivity(uint32_t now);
bool power_idleExpired(uint32_t now);

// Panel into low-power state, MPU parked. Call before deep sleep.
void power_shutdownPeripherals();

// True once the touch pad has been quiet long enough that we will not wake
// straight back up on the finger that is still resting on it.
bool power_readyToSleep(uint32_t now);

void power_deepSleep();
