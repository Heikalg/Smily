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

// DHT11 environment sensor data line. Free GPIO, NOT a strapping pin, clear of
// the I2C bus (8/9) and the touch pad (3). Power the sensor from 3V3 (never
// 5V -- the C3 is a 3.3V part); a 3-pin module has its pull-up built in, a
// bare 4-pin sensor needs a 10k pull-up from DATA to 3V3.
#define PIN_DHT11            4

// I2C at 400 kHz. The display and the MPU share the bus, so the display
// cannot use software I2C any more (see README "Why hardware I2C now").
#define I2C_BUS_HZ           400000UL
#ifndef SMILY_USE_SW_I2C
#define SMILY_USE_SW_I2C     0
#endif

// Column offset for a 132-wide SH1106 clone whose visible area does not start
// at driver column 0. The real fix for THIS panel turned out to be the segment
// re-map (SMILY_SEG_REMAP / 0xA1) below, not an offset -- leave this 0. It is
// kept only as a fallback knob if a different panel ever needs a pixel nudge.
#ifndef SMILY_COL_OFFSET
#define SMILY_COL_OFFSET     0
#endif

// This SH1106 clone maps its columns the reverse way to the NONAME default,
// folding the left columns (see edge-test). 1 = re-issue segment re-map 0xA1
// after begin() to correct the horizontal mapping. Set 0 to disable.
#ifndef SMILY_SEG_REMAP
#define SMILY_SEG_REMAP      1
#endif

// Diagnostic: when 1, display_begin() draws a STATIC edge-test pattern and the
// main loop renders nothing, so the panel holds a still image you can photograph
// to see exactly which columns the panel lights wrongly (the left-wall strip).
// Set back to 0 for normal operation. Costs nothing when off.
#ifndef SMILY_EDGE_TEST
#define SMILY_EDGE_TEST      0
#endif

// --- Frame engine (spec 2.2, non-negotiable) -------------------------------
// ~15 FPS. Nothing in the codebase may push a frame faster than this.
#define FRAME_INTERVAL_MS    66

// --- State timings (ms) ----------------------------------------------------
#define WAKE_ANIM_MS         2000
#define PET_ANIM_MS          2000
#define DIZZY_ANIM_MS        2300
#ifndef GOODNIGHT_ANIM_MS
#define GOODNIGHT_ANIM_MS    15000     // watch Smily drift off for 15 s before
                                       // the panel actually powers down. Set to
                                       // 0 to sleep instantly with no goodbye.
                                       // A touch during this window cancels it
                                       // and wakes straight back to IDLE.
#endif
#define WEATHER_VIEW_MS      8000      // long-press weather overview holds this
                                       // long, then eases back to IDLE
#define SLEEP_TIMEOUT_MS     100000

// --- Idle liveliness -------------------------------------------------------
#define BLINK_MIN_MS         3000     // spec 5: random blink every 3-6 s
#define BLINK_MAX_MS         6000
#define BLINK_HOLD_MS        132      // 2 frames with the lids shut
#define DOUBLE_BLINK_GAP_MS  120      // 1 blink in 4 is a cute double blink
#define GLANCE_MIN_MS        4500     // eyes drift sideways now and then
#define GLANCE_MAX_MS        9000
#define GLANCE_HOLD_MS       520

// --- Idle fidgets ----------------------------------------------------------
// Every so often a bored idle Smily performs one short gesture (a yawn, a
// look-around, a wiggle) and then settles back. These are variations layered
// on top of the breathing idle loop; they are NOT separate states, so the
// state machine never sees them. Set FIDGET_MAX_MS <= FIDGET_MIN_MS to disable.
#define FIDGET_MIN_MS        8000     // earliest a fidget may fire after the last
#define FIDGET_MAX_MS        20000    // latest; roll is uniform in [min, max)
#define FIDGET_YAWN_MS       1400     // stretch up, mouth wide, eyes squeezed
#define FIDGET_LOOK_MS       1600     // slow deliberate eye sweep L -> R -> C
#define FIDGET_WIGGLE_MS     1100     // quick side-to-side body sway

// --- Idle mood -------------------------------------------------------------
// The resting idle expression is not fixed: the engine rolls a new mood every
// so often (neutral / content / sleepy / happy), so an idle Smily does not
// always wear the same face. Blink, glance and fidgets layer on top of it.
#define MOOD_MIN_MS          6000     // earliest a new resting mood is rolled
#define MOOD_MAX_MS          14000    // latest; roll is uniform in [min, max)

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
#define SHAKE_JERK_THRESHOLD 250      // ~1.2 g of change in 20 ms
#define SHAKE_GYRO_DPS       270
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
#define MOTION_TILT_AXIS     1
#define MOTION_TILT_INVERT   1
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

// --- Networking (Phase 4) --------------------------------------------------
// Wi-Fi + weather. Set SMILY_NET_ENABLED 0 to compile the radio out entirely
// (back to the pure offline keychain). SSID/password live in secrets.h.
#ifndef SMILY_NET_ENABLED
#define SMILY_NET_ENABLED    1
#endif

// Where Smily thinks it is, for the weather lookup. We hardcode the location
// (simplest, no geolocation call) -- change these to YOUR city's coordinates.
// Default below is Bandung, Indonesia. Find yours: right-click a spot on
// Google Maps -> the lat,lon is the first menu entry you can copy.
// (We use Open-Meteo over plain HTTP: BMKG is TLS-1.3-only and the ESP32-C3
//  cannot reliably handshake it -- it failed with -1 connection-refused.)
#ifndef SMILY_LAT
#define SMILY_LAT            "-6.9174"
#endif
#ifndef SMILY_LON
#define SMILY_LON            "107.6191"
#endif

// How often to refresh the weather. Open-Meteo updates ~hourly, so there is
// no point fetching more often. 15 min is friendly and keeps Wi-Fi mostly off.
#define NET_WEATHER_INTERVAL_MS  900000UL   // 15 minutes
#define NET_CONNECT_TIMEOUT_MS   45000UL    // give up a connect attempt after 45 s
                                            // (iPhone hotspots can be slow to
                                            //  accept a brand-new device)
#define NET_RETRY_MS             30000UL    // after a failure, wait 30 s then retry
#define NET_HTTP_TIMEOUT_MS      15000UL    // per-request socket timeout; Open-Meteo
                                            // over home Wi-Fi can take >8 s on a
                                            // cold request, so give it room
#define NET_FETCH_ATTEMPTS       3          // total GET attempts per fetch before the
                                            // error face shows; a stalled read (-11) or
                                            // a truncated/empty body is retried in-place
#define NET_FETCH_RETRY_GAP_MS   400UL      // short pause between those in-fetch retries

// --- Weather face (Phase 4) ------------------------------------------------
// A long press opens the weather view. The idle face also carries a quiet
// ambient layer (a few rain streaks / a drifting cloud / night stars) drawn
// BEHIND the mochi, so the weather reads as a mood without cluttering the
// face. Set SMILY_WEATHER_AMBIENT 0 to keep the idle face perfectly plain and
// show weather only on the long-press overview.
//
// Open-Meteo WMO weather_code is bucketed into a handful of looks. We are in
// SE Asia, so the snow/ice codes (71-77, 85-86) never realistically fire and
// are folded into "cloud" rather than given their own winter art:
//   0            -> CLEAR   (sun by day, moon by night)
//   1,2,3        -> CLOUD   (partly to overcast)
//   45,48        -> FOG     (drawn as cloud)
//   51..67,80..82-> RAIN    (drizzle through heavy showers)
//   95,96,99     -> STORM   (rain + an occasional flash)
#ifndef SMILY_WEATHER_AMBIENT
#define SMILY_WEATHER_AMBIENT    1
#endif

// --- Environment sensor (DHT11) --------------------------------------------
// Smily now MEASURES its own temperature + humidity with an on-board DHT11
// instead of re-displaying the number fetched from Open-Meteo. That is the
// real IoT story: a device sensing the physical world. The sky condition and
// the clock still come from the net layer (the DHT11 cannot see the sky or
// know the time); only the displayed temperature comes from the sensor now.
//
// Set SMILY_SENSOR_ENABLED 0 to compile the DHT11 out and fall back to the
// Open-Meteo temperature (requires SMILY_NET_ENABLED). Wiring + pin: see the
// pin map above (PIN_DHT11). Needs the Adafruit DHT sensor library.
#ifndef SMILY_SENSOR_ENABLED
#define SMILY_SENSOR_ENABLED     1
#endif

// Which sensor is actually fitted. The Adafruit DHT library uses a numeric id:
//   11 = DHT11   22 = DHT21/DHT22/AM2302
// DHT11 and DHT22 share identical wiring and the same library -- only this
// number differs. The DHT22 is more accurate (+-0.5 C / +-2% RH, one decimal)
// and reads just as happily on PIN_DHT11. Set to 11 for a DHT11.
#ifndef SMILY_DHT_TYPE
#define SMILY_DHT_TYPE           11
#endif

// A DHT11 cannot be read faster than ~once per 2 s. We read on this interval
// and cache the last good value; the display just shows the cache.
#define SENSOR_READ_INTERVAL_MS  5000UL     // re-read every 5 s while awake
#define SENSOR_WARMUP_MS         1200UL     // settle window before the 1st read
                                            // (also covers every Deep Sleep wake,
                                            //  since the sensor powers down with
                                            //  the chip on the shared 3V3 rail)

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
