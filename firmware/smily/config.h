// ---------------------------------------------------------------------------
// Smily - Smart Interactive Keychain : build-time configuration
//
// Everything tunable lives here. See docs/pinout.md and
// docs/behavior-decisions.md for the reasoning behind these numbers.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define SMILY_TARGET_ESP32 1
#else
#define SMILY_TARGET_ESP32 0   // host build (tools/preview)
#endif

// --- Pin map ---------------------------------------------------------------
// GPIO 8/9 are shared by the OLED (0x3C) and the MPU (0x68).
// The TTP223 must stay on GPIO 0..5: only those are RTC-capable on the
// ESP32-C3 and can wake the chip from Deep Sleep.
#define PIN_I2C_SDA          8
#define PIN_I2C_SCL          9
#define PIN_TOUCH            3
#define TOUCH_ACTIVE_LEVEL   HIGH      // TTP223 jumper: momentary, active-HIGH

// I2C at 400 kHz. The display and the MPU share the bus, so the display
// cannot use software I2C any more (see README "Why hardware I2C now").
#define I2C_BUS_HZ           400000UL
#ifndef SMILY_USE_SW_I2C
#define SMILY_USE_SW_I2C     0
#endif

// --- Frame engine (spec 2.2, non-negotiable) -------------------------------
// ~15 FPS. Nothing in the codebase may push a frame faster than this.
#define FRAME_INTERVAL_MS    66

// --- State timings (ms) ----------------------------------------------------
#define WAKE_ANIM_MS         2000
#define PET_ANIM_MS          2000
#define DIZZY_ANIM_MS        2300
#ifndef GOODNIGHT_ANIM_MS
#define GOODNIGHT_ANIM_MS    860      // set to 0 to sleep instantly, no goodbye
#endif
#define SLEEP_TIMEOUT_MS     15000

// --- Idle liveliness -------------------------------------------------------
#define BLINK_MIN_MS         3000     // spec 5: random blink every 3-6 s
#define BLINK_MAX_MS         6000
#define BLINK_HOLD_MS        132      // 2 frames with the lids shut
#define DOUBLE_BLINK_GAP_MS  120      // 1 blink in 4 is a cute double blink
#define GLANCE_MIN_MS        4500     // eyes drift sideways now and then
#define GLANCE_MAX_MS        9000
#define GLANCE_HOLD_MS       520

// --- Input handler ---------------------------------------------------------
#define TOUCH_DEBOUNCE_MS    30
#define TAP_WINDOW_MS        450      // spec proposes 400-600; tune on bench
#define LONG_PRESS_MS        1000     // Phase 4 hook (weather), not routed yet
#define TRIPLE_TAP_COUNT     3

// --- Motion / MPU-6050 (or MPU-6500 / MPU-9250) ----------------------------
#define MOTION_SAMPLE_MS     20       // 50 Hz, independent of the frame clock
#define MOTION_RETRY_MS      1000     // re-probe if the MPU is absent at boot

// Shake = "moved very fast". Two independent triggers, either one counts:
//   jerk   : change in the accel vector between samples, 256 units == 1 g
//   gyroMag: total rotation rate, roughly 1 unit == 1 deg/s
// Build with SMILY_MOTION_DEBUG 1 and read the serial peaks to tune these.
#define SHAKE_JERK_THRESHOLD 300      // ~1.2 g of change in 20 ms
#define SHAKE_GYRO_DPS       320
#define SHAKE_HITS_REQUIRED  3        // ignores a single knock or a drop
#define SHAKE_WINDOW_MS      700
#define SHAKE_REFRACTORY_MS  60
#define SHAKE_COOLDOWN_MS    1800     // no re-trigger while dizzy is playing

// The gyro is only a second opinion for shake detection. Standing it by cuts
// most of the MPU's active current (the gyro is the expensive half of the
// chip) at the cost of relying on accelerometer jerk alone. Worth trying once
// the Phase 1 current measurements are in.
#ifndef MOTION_GYRO_ENABLED
#define MOTION_GYRO_ENABLED  1
#endif

// Tilt -> how far the mochi leans. 0 = accel X, 1 = Y, 2 = Z.
// Flip MOTION_TILT_INVERT if it leans the wrong way once mounted.
#define MOTION_TILT_AXIS     0
#define MOTION_TILT_INVERT   0
#define MOTION_TILT_DIV      300      // raw LSB per pixel of lean
#define MOTION_TILT_MAX_PX   8
#define MOTION_TILT_SMOOTH   3        // IIR shift: higher = slower, calmer

// --- Character geometry ----------------------------------------------------
// The whole mochi is built from these. Everything else (eye spacing, mouth
// position, squish) is derived, so the character can be reshaped from here.
#ifndef MOCHI_W
#define MOCHI_W          76   // resting body width
#endif
#ifndef MOCHI_H
#define MOCHI_H          46   // resting body height
#endif
#ifndef MOCHI_BOTTOM
#define MOCHI_BOTTOM     56   // baseline; the feet stick out ~5 px below it
#endif
#ifndef MOCHI_ROUND_PCT
#define MOCHI_ROUND_PCT  48   // corner radius as a percentage of body height
#endif
#ifndef MOCHI_EYE_W
#define MOCHI_EYE_W      9
#endif
#ifndef MOCHI_EYE_H
#define MOCHI_EYE_H      13
#endif
#ifndef MOCHI_EYE_DX_PCT
#define MOCHI_EYE_DX_PCT 22   // eye offset from centre, percentage of width
#endif
#ifndef MOCHI_EYE_Y_PCT
#define MOCHI_EYE_Y_PCT  52   // eye height above the baseline, pct of height
#endif

// --- Look & feel -----------------------------------------------------------
// 0 = solid white mochi with cut-out face (the Dasai Mochi look).
// 1 = outline only: ~8x fewer lit pixels, so noticeably less OLED current.
//     Keep 0 unless the Phase 1 power measurement says otherwise.
#ifndef SMILY_MOCHI_OUTLINE
#define SMILY_MOCHI_OUTLINE  0
#endif

// --- Debug -----------------------------------------------------------------
#ifndef SMILY_SERIAL_LOG
#define SMILY_SERIAL_LOG     1        // state transitions + boot report
#endif
#ifndef SMILY_MOTION_DEBUG
#define SMILY_MOTION_DEBUG   0        // 10 Hz stream of jerk/gyro/tilt peaks
#endif
#ifndef SMILY_MOTION_OVERLAY
#define SMILY_MOTION_OVERLAY 0        // draw raw motion numbers over the face
#endif
