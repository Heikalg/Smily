// ---------------------------------------------------------------------------
// Animation Engine - the only thing in the firmware allowed to advance a
// frame, and it does so at most once every FRAME_INTERVAL_MS.
//
// The State Machine drives it with a single call: play(ANIM_X). The engine
// owns the frame clock plus the "performance" timers that make the idle face
// feel alive (random blink, random glance). It knows nothing about pixels -
// rendering is delegated to faces_render().
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#include "config.h"

enum AnimId : uint8_t {
  ANIM_WAKE = 0,   // startup pop, 2 s
  ANIM_IDLE,       // looping: breathing + blink + tilt lean
  ANIM_PET,        // happy bounce with hearts, 2 s
  ANIM_DIZZY,      // shaken: swirl eyes and orbiting stars
  ANIM_WEATHER,    // long-press: full weather overview (temp + glyph)
  ANIM_GOODNIGHT,  // short "see you" before the panel powers down
  ANIM_BLANK,      // nothing lit
  ANIM_COUNT
};

// Idle fidgets: short one-off gestures layered on the idle loop. FIDGET_NONE
// is the resting idle; the rest are picked at random by the engine's fidget
// timer and reported to the renderer with how long they have been running.
enum FidgetId : uint8_t {
  FIDGET_NONE = 0,
  FIDGET_YAWN,
  FIDGET_LOOK,
  FIDGET_WIGGLE,
  FIDGET_COUNT
};

// Idle moods: the resting expression the idle face wears, re-rolled at random
// by the engine so Smily does not always look the same when doing nothing.
enum MoodId : uint8_t {
  MOOD_NEUTRAL = 0,  // the default resting face
  MOOD_CONTENT,      // soft happy eyes, small smile
  MOOD_SLEEPY,       // relaxed half-closed eyes
  MOOD_HAPPY,        // bright happy eyes + omega smile
  MOOD_COUNT
};

// Weather "sky": the Open-Meteo WMO code bucketed into the few looks Smily
// actually draws. SE Asia, so no snow bucket (see config.h). SKY_NONE means
// no weather reading yet -- draw nothing weather-ish.
enum SkyId : uint8_t {
  SKY_NONE = 0,  // no data yet
  SKY_CLEAR,     // code 0        (sun / moon)
  SKY_CLOUD,     // 1,2,3,45,48   (clouds, fog)
  SKY_RAIN,      // 51..67,80..82 (drizzle to showers)
  SKY_STORM      // 95,96,99      (thunderstorm)
};

// Fold a raw Open-Meteo weather_code into a SkyId. Defined in faces.cpp.
SkyId sky_from_code(int code);

// Everything a renderer is allowed to know about the outside world.
struct FaceCtx {
  int8_t leanPx;    // body lean from the smoothed accelerometer tilt
  int8_t glancePx;  // idle eye drift
  uint8_t jolt;     // 0-3: recent motion energy, squishes the body
  bool blink;       // lids shut this frame
  bool noMotion;    // MPU did not answer (only drawn when overlay is on)
  int16_t dbgA;     // motion overlay: jerk
  int16_t dbgB;     // motion overlay: gyro magnitude
  FidgetId fidget;      // active idle fidget, or FIDGET_NONE
  uint16_t fidgetMs;    // ms the current fidget has been running
  uint16_t fidgetLenMs; // total duration of the current fidget
  MoodId mood;          // resting idle expression

  // Weather, read from the net layer and bucketed by the state machine. The
  // idle face uses `sky` for its quiet ambient layer; the weather view uses
  // all three. tempC/isDay are only meaningful when sky != SKY_NONE.
  SkyId sky = SKY_NONE;  // bucketed weather look, SKY_NONE if no reading yet
  int8_t tempC = 0;      // whole degrees Celsius, for the overview readout
  int8_t humidity = -1;  // measured relative humidity %, -1 if no sensor reading
  bool isDay = true;     // sun vs moon on CLEAR
  int8_t hour = -1;      // local clock HH, -1 if unknown
  int8_t minute = -1;    // local clock MM, -1 if unknown

  // Fetch-error state, so the weather view can show an error ONLY on a real
  // failure with no good reading to fall back on. errCode carries the reason
  // (<0 transport e.g. -11, >0 HTTP status, 0 other) for display.
  bool netError = false;
  int16_t errCode = 0;
};

struct AnimDef {
  uint16_t durationMs;  // 0 == endless loop
  bool loop;
};

// Frames in a one-shot animation; 0 for the looping ones.
uint16_t anim_frameCount(AnimId id);

class Animator {
 public:
  void begin(uint32_t now);

  // Restart an animation from frame 0. Re-playing the current animation is
  // a no-op so the state machine can call it idempotently.
  void play(AnimId id, uint32_t now);

  // Renders and flushes at most one frame. Returns true if a frame went out.
  bool update(uint32_t now);

  // One-shot animations only: true once the last frame has been shown.
  bool finished() const { return _finished; }

  AnimId current() const { return _id; }
  uint16_t frame() const { return _frame; }

  // Inputs the renderers react to, pushed in by the state machine.
  void setTilt(int8_t px) { _tilt = px; }
  void setJolt(uint8_t j) {
    if (j > _jolt) _jolt = j;
  }
  void setMotionDebug(bool present, int16_t jerk, int16_t gyro) {
    _noMotion = !present;
    _dbgA = jerk;
    _dbgB = gyro;
  }

  // Weather, pushed in by the state machine. `sky`/`isDay` come from the net
  // layer; `tempC`/`humidity` come from the on-board DHT11 sensor (humidity
  // < 0 means no sensor reading yet). Carried into every FaceCtx so both the
  // idle ambient and the weather view can read it.
  void setWeather(SkyId sky, int8_t tempC, int8_t humidity, bool isDay) {
    _sky = sky;
    _tempC = tempC;
    _humidity = humidity;
    _wIsDay = isDay;
  }

  // Local clock from the weather fetch (timezone=auto). -1 = unknown.
  void setClock(int8_t hour, int8_t minute) {
    _hour = hour;
    _minute = minute;
  }

  // Last fetch error, shown on the weather view only when there is no good
  // reading to fall back on.
  void setNetError(bool failed, int16_t code) {
    _netError = failed;
    _errCode = code;
  }

 private:
  void scheduleBlink(uint32_t now);
  void scheduleGlance(uint32_t now);
  void scheduleFidget(uint32_t now);
  void scheduleMood(uint32_t now);

  AnimId _id = ANIM_BLANK;
  uint16_t _frame = 0;
  uint16_t _frameCount = 0;  // 0 == loop forever
  uint32_t _lastFrameMs = 0;
  bool _finished = false;

  uint32_t _nextBlinkMs = 0;
  uint32_t _blinkUntilMs = 0;
  bool _blinkAgain = false;
  uint32_t _nextGlanceMs = 0;
  uint32_t _glanceUntilMs = 0;
  int8_t _glance = 0;

  // Idle fidgets: one gesture at a time, with its own start/end clock.
  uint32_t _nextFidgetMs = 0;
  uint32_t _fidgetStartMs = 0;
  uint32_t _fidgetUntilMs = 0;
  FidgetId _fidget = FIDGET_NONE;

  // Idle mood: the resting expression, re-rolled on its own timer.
  uint32_t _nextMoodMs = 0;
  MoodId _mood = MOOD_NEUTRAL;

  int8_t _tilt = 0;
  uint8_t _jolt = 0;
  bool _noMotion = false;
  int16_t _dbgA = 0;
  int16_t _dbgB = 0;

  // Latest weather, mirrored into every FaceCtx.
  SkyId _sky = SKY_NONE;
  int8_t _tempC = 0;
  int8_t _humidity = -1;
  bool _wIsDay = true;
  bool _netError = false;
  int16_t _errCode = 0;
  int8_t _hour = -1;
  int8_t _minute = -1;
};

extern Animator anim;
