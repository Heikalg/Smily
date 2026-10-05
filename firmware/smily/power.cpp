#include "power.h"

#include <Arduino.h>

#include "config.h"
#include "display.h"
#include "input_handler.h"
#include "motion.h"
#include "util.h"

#if SMILY_TARGET_ESP32
#include "esp_sleep.h"
#endif

static const uint32_t TOUCH_RELEASE_SETTLE_MS = 150;

static uint32_t s_lastActivityMs = 0;
static uint32_t s_quietSinceMs = 0;

WakeCause power_begin(uint32_t now) {
  s_lastActivityMs = now;
  s_quietSinceMs = now;

#if SMILY_TARGET_ESP32
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_GPIO:
      return WAKE_TOUCH;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return WAKE_COLD;
    default:
      return WAKE_OTHER;
  }
#else
  return WAKE_COLD;
#endif
}

void power_noteActivity(uint32_t now) { s_lastActivityMs = now; }

bool power_idleExpired(uint32_t now) {
  return elapsed(now, s_lastActivityMs) >= SLEEP_TIMEOUT_MS;
}

void power_shutdownPeripherals() {
  display_sleep();
  motion_sleep();
}

bool power_readyToSleep(uint32_t now) {
  if (input_touchLevel()) {
    s_quietSinceMs = now;
    return false;
  }
  return elapsed(now, s_quietSinceMs) >= TOUCH_RELEASE_SETTLE_MS;
}

void power_deepSleep() {
#if SMILY_TARGET_ESP32
  SMILY_LOG("[power] deep sleep, wake on GPIO %d HIGH\n", PIN_TOUCH);
#if SMILY_SERIAL_LOG
  Serial.flush();
#endif
  // Only RTC-capable pins (GPIO 0..5 on the C3) can do this.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_TOUCH, ESP_GPIO_WAKEUP_GPIO_HIGH);
  esp_deep_sleep_start();  // never returns; the next boot starts at setup()
#endif
}
