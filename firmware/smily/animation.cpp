#include "animation.h"

#include <Arduino.h>

#include "display.h"
#include "faces.h"
#include "util.h"

Animator anim;

// Animation table. Procedural renderers are used instead of stored bitmaps
// (128x64 is 1 KB per frame - 90 frames would not fit comfortably), but the
// interface is the same one a frame-list player would expose, so swapping in
// XBM assets later only touches faces.cpp.
static const AnimDef ANIMS[ANIM_COUNT] = {
    /* ANIM_WAKE      */ {WAKE_ANIM_MS, false},
    /* ANIM_IDLE      */ {0, true},
    /* ANIM_PET       */ {PET_ANIM_MS, false},
    /* ANIM_DIZZY     */ {DIZZY_ANIM_MS, false},
    /* ANIM_GOODNIGHT */ {GOODNIGHT_ANIM_MS, false},
    /* ANIM_BLANK     */ {0, true},
};

uint16_t anim_frameCount(AnimId id) {
  const AnimDef &d = ANIMS[id];
  if (d.loop || d.durationMs == 0) return 0;
  uint16_t n = d.durationMs / FRAME_INTERVAL_MS;
  return n ? n : 1;
}

void Animator::begin(uint32_t now) {
  _lastFrameMs = now - FRAME_INTERVAL_MS;  // render the first frame at once
  scheduleBlink(now);
  scheduleGlance(now);
  scheduleFidget(now);
  scheduleMood(now);
}

void Animator::play(AnimId id, uint32_t now) {
  if (_id == id && !_finished) return;
  _id = id;
  _frame = 0;
  _frameCount = anim_frameCount(id);
  _finished = false;
  // _lastFrameMs is deliberately left alone. A state change must not get a
  // free frame: the new animation starts on the next scheduled slot, so the
  // 66 ms floor between two transfers holds across transitions too. The cost
  // is up to one frame of latency, which is not perceptible.
  if (_frameCount == 0 && ANIMS[id].durationMs == 0 && id == ANIM_IDLE) {
    scheduleBlink(now);
    scheduleGlance(now);
    scheduleFidget(now);
    scheduleMood(now);
    _fidget = FIDGET_NONE;
  }
}

void Animator::scheduleBlink(uint32_t now) {
  _nextBlinkMs = now + smilyRandRange(BLINK_MIN_MS, BLINK_MAX_MS);
  _blinkAgain = (smilyRand() & 3) == 0;  // a quarter of them are doubles
}

void Animator::scheduleGlance(uint32_t now) {
  _nextGlanceMs = now + smilyRandRange(GLANCE_MIN_MS, GLANCE_MAX_MS);
}

void Animator::scheduleFidget(uint32_t now) {
  // Disabled when the window is empty or inverted.
  if (FIDGET_MAX_MS <= FIDGET_MIN_MS) {
    _nextFidgetMs = now + 0x3FFFFFFF;  // effectively never
    return;
  }
  _nextFidgetMs = now + smilyRandRange(FIDGET_MIN_MS, FIDGET_MAX_MS);
}

void Animator::scheduleMood(uint32_t now) {
  _nextMoodMs = now + smilyRandRange(MOOD_MIN_MS, MOOD_MAX_MS);
}

bool Animator::update(uint32_t now) {
  // The hard rate limit. Everything below runs at most 15 times a second.
  if (elapsed(now, _lastFrameMs) < FRAME_INTERVAL_MS) return false;
  _lastFrameMs = now;

  FaceCtx ctx;
  ctx.leanPx = _tilt;
  ctx.glancePx = 0;
  ctx.jolt = _jolt;
  ctx.blink = false;
  ctx.noMotion = _noMotion;
  ctx.dbgA = _dbgA;
  ctx.dbgB = _dbgB;
  ctx.fidget = FIDGET_NONE;
  ctx.fidgetMs = 0;
  ctx.fidgetLenMs = 0;
  ctx.mood = MOOD_NEUTRAL;

  if (_id == ANIM_IDLE) {
    // Blink: fire when due, then either queue the second half of a double
    // blink or roll a fresh 3-6 s interval. Starting a blink always pushes
    // _nextBlinkMs into the future, so this cannot retrigger itself.
    if (timeReached(now, _nextBlinkMs)) {
      _blinkUntilMs = now + BLINK_HOLD_MS;
      if (_blinkAgain) {
        _blinkAgain = false;
        _nextBlinkMs = now + BLINK_HOLD_MS + DOUBLE_BLINK_GAP_MS;
      } else {
        scheduleBlink(now);
      }
    }
    ctx.blink = !timeReached(now, _blinkUntilMs);

    // Glance: eyes drift to one side for half a second.
    if (timeReached(now, _nextGlanceMs)) {
      _glance = (smilyRand() & 1) ? 3 : -3;
      _glanceUntilMs = now + GLANCE_HOLD_MS;
      scheduleGlance(now);
    }
    if (!timeReached(now, _glanceUntilMs)) ctx.glancePx = _glance;

    // Mood: re-roll the resting expression on its own slow timer, but never
    // mid-fidget (a fidget owns the face while it runs). Picked uniformly.
    if (_fidget == FIDGET_NONE && timeReached(now, _nextMoodMs)) {
      _mood = (MoodId)(smilyRand() % MOOD_COUNT);
      scheduleMood(now);
    }
    ctx.mood = _mood;

    // Fidget: when idle has been resting long enough, perform one short
    // gesture. Only one runs at a time; a running fidget blocks the next
    // roll. Picking is uniform over the real gestures (skips FIDGET_NONE).
    if (_fidget == FIDGET_NONE && timeReached(now, _nextFidgetMs)) {
      _fidget = (FidgetId)(FIDGET_YAWN + (smilyRand() % (FIDGET_COUNT - 1)));
      uint16_t len = FIDGET_YAWN_MS;
      if (_fidget == FIDGET_LOOK) len = FIDGET_LOOK_MS;
      else if (_fidget == FIDGET_WIGGLE) len = FIDGET_WIGGLE_MS;
      _fidgetStartMs = now;
      _fidgetUntilMs = now + len;
    }
    if (_fidget != FIDGET_NONE) {
      if (timeReached(now, _fidgetUntilMs)) {
        _fidget = FIDGET_NONE;        // gesture finished, back to plain idle
        scheduleFidget(now);          // roll the next one
      } else {
        ctx.fidget = _fidget;
        ctx.fidgetMs = (uint16_t)elapsed(now, _fidgetStartMs);
        ctx.fidgetLenMs = (uint16_t)(_fidgetUntilMs - _fidgetStartMs);
        // A yawn owns the eyes (they squeeze shut), so the random blink must
        // not fight it; the look fidget owns the gaze instead of the glance.
        if (_fidget == FIDGET_YAWN) ctx.blink = false;
        if (_fidget == FIDGET_LOOK) ctx.glancePx = 0;
      }
    }
  }

  faces_render(_id, _frame, ctx);  // composes the whole off-screen buffer
  display_flush();                 // ... and exactly one transfer per frame

  if (_jolt) _jolt--;  // motion squish relaxes over a few frames

  if (_frameCount) {
    if (++_frame >= _frameCount) {
      _frame = _frameCount - 1;  // hold the last frame until we are told
      _finished = true;
    }
  } else {
    _frame++;  // looping: wraps harmlessly, renderers use phase arithmetic
  }
  return true;
}
