// ---------------------------------------------------------------------------
// Smily - Smart Interactive Keychain
//
// Board   : ESP32-C3 SuperMini
// Display : 1.3" SH1106 128x64, I2C @ SDA GPIO 8 / SCL GPIO 9
// Touch   : TTP223 on GPIO 3 (RTC-capable -> Deep Sleep wake source)
// Motion  : MPU-6050 on the same I2C bus, address 0x68
//
// Module map (spec section 4). Input and motion produce events, the state
// machine decides states, the animation engine renders what it is told.
//
//   input_handler  debounce, tap counting, tap window
//   motion         MPU sampling, tilt smoothing, shake detection
//   state_machine  states, transitions, timeouts
//   animation      15 FPS frame scheduler, blink/glance timing
//   faces          procedural frame renderers (Dasai-Mochi-ish)
//   display        u8g2 ownership, one flush per frame, panel power
//   power          inactivity timer, wake cause, Deep Sleep entry
//
// Hard rules kept by this build:
//   - no delay() and no busy-wait anywhere
//   - every timer is a rollover-safe millis() comparison
//   - nothing reaches the panel faster than FRAME_INTERVAL_MS (66 ms)
// ---------------------------------------------------------------------------
#include "config.h"
#include "display.h"
#include "input_handler.h"
#include "motion.h"
#include "net.h"
#include "power.h"
#include "sensor.h"
#include "state_machine.h"
#include "util.h"

void setup() {
#if SMILY_SERIAL_LOG
  Serial.begin(115200);
#endif

  const uint32_t now = millis();

  // Wake cause first: it decides whether the finger already on the pad
  // should be swallowed instead of counted as a pet.
  const WakeCause cause = power_begin(now);
  SMILY_LOG("\n[boot] Smily up, wake cause: %s\n",
            cause == WAKE_TOUCH ? "touch" : (cause == WAKE_COLD ? "cold" : "other"));

  display_begin();
  input_begin(now, /*ignoreHeldTouch=*/cause == WAKE_TOUCH);
  motion_begin();
  net_begin(now);   // Wi-Fi connect starts in the background; non-blocking
  sensor_begin(now); // DHT11 temp/humidity; non-blocking, reads on a timer
  sm_begin(now, cause);
}

void loop() {
#if SMILY_EDGE_TEST
  // Diagnostic build: the static edge-test pattern is already on the panel from
  // display_begin(); do nothing so it stays put for photographing. Set
  // SMILY_EDGE_TEST 0 in config.h to restore normal operation.
  delay(1000);
  return;
#endif

  const uint32_t now = millis();

  const InputEvents in = input_poll(now);    // every pass: debounce needs it
  const MotionEvents mo = motion_poll(now);  // self-limits to 50 Hz
  net_poll(now);                             // advances Wi-Fi/weather, non-blocking
  sensor_poll(now);                          // reads DHT11 when due, non-blocking

  sm_update(now, in, mo);  // frame limiting happens inside the engine
}
