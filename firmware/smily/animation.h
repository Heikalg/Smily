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
};

extern Animator anim;
