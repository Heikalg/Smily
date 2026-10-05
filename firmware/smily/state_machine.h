// ---------------------------------------------------------------------------
// State Machine - the only module that decides anything. It consumes events
// from the input and motion handlers, owns the state and the timeouts, and
// tells the Animation Engine what to play.
//
//   WAKE  --2 s-->            IDLE
//   IDLE  --single tap-->     PET      --2 s-->   IDLE
//   IDLE  --3 taps / shake--> DIZZY    --done-->  IDLE
//   IDLE  --15 s quiet-->     SLEEP    --touch--> WAKE (via Deep Sleep reset)
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#include "input_handler.h"
#include "motion.h"
#include "power.h"

enum SystemState : uint8_t {
  STATE_WAKE,
  STATE_IDLE,
  STATE_PET,
  STATE_DIZZY,
  STATE_SLEEP,
};

void sm_begin(uint32_t now, WakeCause cause);
void sm_update(uint32_t now, const InputEvents &in, const MotionEvents &mo);
SystemState sm_state();
