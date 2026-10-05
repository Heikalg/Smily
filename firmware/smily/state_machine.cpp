#include "state_machine.h"

#include <Arduino.h>

#include "animation.h"
#include "config.h"
#include "util.h"

static SystemState s_state = STATE_WAKE;
static bool s_peripheralsOff = false;

#if SMILY_SERIAL_LOG
static const char *stateName(SystemState s) {
  switch (s) {
    case STATE_WAKE: return "WAKE";
    case STATE_IDLE: return "IDLE";
    case STATE_PET: return "PET";
    case STATE_DIZZY: return "DIZZY";
    case STATE_SLEEP: return "SLEEP";
  }
  return "?";
}
#endif

static void transition(SystemState next, uint32_t now) {
  SMILY_LOG("[fsm] %s -> %s\n", stateName(s_state), stateName(next));
  s_state = next;

  switch (next) {
    case STATE_WAKE:
      anim.play(ANIM_WAKE, now);
      break;
    case STATE_IDLE:
      anim.play(ANIM_IDLE, now);
      break;
    case STATE_PET:
      anim.play(ANIM_PET, now);
      break;
    case STATE_DIZZY:
      anim.play(ANIM_DIZZY, now);
      break;
    case STATE_SLEEP:
#if GOODNIGHT_ANIM_MS > 0
      anim.play(ANIM_GOODNIGHT, now);
#else
      anim.play(ANIM_BLANK, now);
      power_shutdownPeripherals();
      s_peripheralsOff = true;
#endif
      break;
  }
}

void sm_begin(uint32_t now, WakeCause cause) {
  (void)cause;
  anim.begin(now);
  s_peripheralsOff = false;
  s_state = STATE_WAKE;
  anim.play(ANIM_WAKE, now);
}

SystemState sm_state() { return s_state; }

void sm_update(uint32_t now, const InputEvents &in, const MotionEvents &mo) {
  // Any contact or a shake pushes the inactivity countdown back. Finishing an
  // animation deliberately does not (behaviour decision 2).
  if (in.activity || mo.shake) power_noteActivity(now);

  anim.setTilt(mo.tiltPx);
  anim.setJolt(mo.jolt);
  anim.setMotionDebug(mo.present, mo.jerk, mo.gyro);

  switch (s_state) {
    case STATE_WAKE:
      // Input during the startup animation only resets the idle timer.
      if (anim.finished()) transition(STATE_IDLE, now);
      break;

    case STATE_IDLE:
      if (mo.shake || in.tripleTap) {
        transition(STATE_DIZZY, now);
      } else if (in.tap) {
        transition(STATE_PET, now);
      } else if (in.longPress) {
        SMILY_LOG("[fsm] long press (reserved for STATE_WEATHER, Phase 4)\n");
      } else if (power_idleExpired(now)) {
        transition(STATE_SLEEP, now);
      }
      break;

    case STATE_PET:
    case STATE_DIZZY:
      // Taps and shakes mid-animation are ignored (behaviour decision 4).
      if (anim.finished()) transition(STATE_IDLE, now);
      break;

    case STATE_SLEEP:
      // Caught before the panel went dark? Stay up.
      if (!s_peripheralsOff && in.activity) {
        transition(STATE_IDLE, now);
        break;
      }
      if (!s_peripheralsOff && anim.finished()) {
        power_shutdownPeripherals();
        s_peripheralsOff = true;
      }
      if (s_peripheralsOff && power_readyToSleep(now)) power_deepSleep();
      break;
  }

  // Nothing is flushed once the panel is asleep.
  if (!(s_state == STATE_SLEEP && s_peripheralsOff)) anim.update(now);
}
