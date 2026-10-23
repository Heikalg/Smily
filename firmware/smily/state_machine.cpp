#include "state_machine.h"

#include <Arduino.h>

#include "animation.h"
#include "config.h"
#include "net.h"
#include "sensor.h"
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
    case STATE_WEATHER: return "WEATHER";
    case STATE_SLEEP: return "SLEEP";
  }
  return "?";
}
#endif

// Pull the latest weather + sensor readings and push them into the animator so
// both the idle ambient and the weather view can draw them.
//
// Division of labour now that Smily has its own sensor:
//   - TEMPERATURE + HUMIDITY come from the on-board DHT11 (sensor layer) --
//     the device measures its own environment. This is the "it's really IoT"
//     part: the number on screen is a real measurement, not an API echo.
//   - SKY (sun/cloud/rain/...) + is-day + the CLOCK still come from the net
//     layer (Open-Meteo), because a DHT11 cannot see the sky or know the time.
//
// Cheap enough to call every frame; both layers just hand back a cached copy.
static void feedWeather(uint32_t now) {
  const Weather w = net_weather(now);          // sky + is_day + clock
  const Reading r = sensor_reading(now);       // measured temp + humidity

  // The temperature shown is the SENSOR's when we have one; fall back to the
  // net reading only if the sensor has not produced a value yet (e.g. first
  // boot before the DHT11 has warmed up, or SMILY_SENSOR_ENABLED == 0).
  int8_t tempC = 0;
  int8_t humidity = -1;
  bool haveTemp = false;
  if (r.have) {
    tempC = (int8_t)(r.tempC + (r.tempC >= 0 ? 0.5f : -0.5f));   // round
    humidity = (int8_t)(r.humidity + 0.5f);
    haveTemp = true;
  } else if (w.have) {
    tempC = (int8_t)(w.tempC + (w.tempC >= 0 ? 0.5f : -0.5f));   // net fallback
    haveTemp = true;
  }

  // The sky bucket + day/night come from the net reading; the clock too.
  const SkyId sky = w.have ? sky_from_code(w.code) : SKY_NONE;
  const bool isDay = w.have ? w.isDay : true;

  // Show a reading when we have EITHER a sky look or a measured temperature --
  // the overview is useful even if only the sensor has answered so far.
  if (sky != SKY_NONE || haveTemp) {
    anim.setWeather(sky, tempC, humidity, isDay);
  } else {
    anim.setWeather(SKY_NONE, 0, -1, true);
  }
  anim.setClock(w.have ? w.hour : -1, w.have ? w.minute : -1);

  // Surface a net error ONLY when a fetch failed AND there is no sky to show.
  // A measured temperature alone does not clear a sky error -- the sky glyph
  // genuinely has no data -- but a cached sky stays silent as before.
  const NetError e = net_error();
  anim.setNetError(e.failed && sky == SKY_NONE, (int16_t)e.code);
}

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
    case STATE_WEATHER:
      anim.play(ANIM_WEATHER, now);
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

  feedWeather(now);  // keep the animator's weather fresh for ambient + view
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
        transition(STATE_WEATHER, now);
      } else if (power_idleExpired(now)) {
        transition(STATE_SLEEP, now);
      }
      break;

    case STATE_PET:
    case STATE_DIZZY:
      // Taps and shakes mid-animation are ignored (behaviour decision 4).
      if (anim.finished()) transition(STATE_IDLE, now);
      break;

    case STATE_WEATHER:
      // The overview holds for WEATHER_VIEW_MS, but a tap dismisses it early
      // so a long-press-then-tap gets you straight back to the face.
      if (in.tap || in.tripleTap || mo.shake || anim.finished()) {
        transition(STATE_IDLE, now);
      }
      break;

    case STATE_SLEEP:
      // Caught before the panel went dark? Stay up. With a 15 s goodnight the
      // window to change your mind is long and deliberate: any touch during it
      // cancels the farewell and bounces straight back to IDLE.
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
