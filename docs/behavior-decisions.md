# Behaviour decisions

Answers to the five open questions in spec section 5, plus the rules for the
motion sensor, which the spec predates. These are what the firmware actually
implements, so Phase 3 has no ambiguity left. Every number lives in
`config.h`; "tune on hardware" means change it there, not in the logic.

## 1. Single tap vs. triple tap

A tap window of **450 ms** (`TAP_WINDOW_MS`, spec proposed 400-600). Taps are
counted while the window keeps being pushed back, and only resolved once the
pad has been released and the window expires quietly:

- 1 or 2 taps -> `STATE_PET`
- 3 or more taps -> `STATE_DIZZY`

So a single tap never pets instantly, and a triple tap never pets first. Cost
is 450 ms of latency on a single tap, which is the price of the rule.
Debounce is 30 ms (`TOUCH_DEBOUNCE_MS`).

## 2. Inactivity timer reset

Any touch event or shake resets the 15 s countdown. **Finishing an animation
does not restart it.** Petting therefore buys 15 s from the moment of the tap,
not 15 s after the animation ends. Holding a finger on the pad also counts as
continuous activity, so the device cannot fall asleep mid-touch.

The timeout is only evaluated in `STATE_IDLE`, so Smily never drops to sleep
in the middle of an animation.

## 3. Wake touch

**Not a pet**, as proposed. On a touch wake the pad is still held, so that
first contact is swallowed until it is released (`input_begin(now, true)`); it
only triggers `STATE_WAKE`. The next tap counts normally.

## 4. Input during animations

Taps and shakes during `STATE_PET` and `STATE_DIZZY` are **ignored** for state
purposes, as proposed. They still reset the inactivity timer, so poking a busy
Smily does keep it awake.

One addition: a touch during the goodnight animation, before the panel has
powered down, cancels sleep and returns to `STATE_IDLE`. Catching it as it
nods off should not be a dead input.

## 5. Weather display trigger (Phase 4)

Long press is detected (`LONG_PRESS_MS` 1000) and reported as an event, but
not routed anywhere; it logs a line and cancels the pending taps so a hold is
never also a pet. `STATE_WEATHER` is Phase 4 work.

## 6. Shake -> DIZZY (new; the spec predates the MPU)

"Moved very fast" is deliberately hard to trigger by accident. A *hit* is a
sample where either

- **jerk** - the change in the acceleration vector between two samples -
  exceeds `SHAKE_JERK_THRESHOLD` (300, where 256 units is 1 g), or
- **gyro magnitude** exceeds `SHAKE_GYRO_DPS` (320 deg/s)

and `SHAKE_HITS_REQUIRED` (3) hits must land inside `SHAKE_WINDOW_MS` (700),
no closer together than `SHAKE_REFRACTORY_MS` (60) so one impulse cannot count
three times. After firing, the detector is quiet for `SHAKE_COOLDOWN_MS`
(1800) so the dizzy animation cannot retrigger itself.

Consequences, all covered by assertions in `tools/preview`:

- a single knock or the keychain being set down hard does nothing
- slowly tilting or carrying it does nothing
- about half a second of real shaking triggers it

Tune on hardware: build with `SMILY_MOTION_DEBUG 1` and watch the 10 Hz peak
jerk/gyro report over serial while handling the device normally, then while
shaking it. Put the threshold in the gap between the two.

Sampling is 50 Hz (`MOTION_SAMPLE_MS` 20), independent of the 15 FPS frame
clock.

## 7. Tilt and jolt (cosmetic, new)

Below the shake threshold the MPU still drives two things, which is also the
easiest way to confirm the sensor is alive:

- **tilt** - heavily smoothed gravity on one axis leans the body up to 8 px
  and nudges the eyes slightly further, so Smily looks like it notices being
  turned. Axis and direction are `MOTION_TILT_AXIS` / `MOTION_TILT_INVERT`;
  fix them once the board is actually mounted in the case.
- **jolt** - a small motion impulse squishes the body for a few frames.

Neither changes state, so neither can interfere with the state machine.
