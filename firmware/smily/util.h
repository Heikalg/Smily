// ---------------------------------------------------------------------------
// Small shared helpers. No floating point anywhere: the ESP32-C3 is a RISC-V
// core without an FPU, so every float operation is a software call.
// ---------------------------------------------------------------------------
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>

#include "config.h"

// Arduino defines min/max as macros; keep our own so headers can't collide.
static inline int16_t smin(int16_t a, int16_t b) { return a < b ? a : b; }
static inline int16_t smax(int16_t a, int16_t b) { return a > b ? a : b; }
static inline int16_t sclamp(int16_t v, int16_t lo, int16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Rollover-safe "has this timestamp passed?" (spec 2.1). Both arguments are
// unsigned millis; the subtraction wraps correctly and the signed cast makes
// the comparison valid across the 49-day millis() rollover.
static inline bool timeReached(uint32_t now, uint32_t deadline) {
  return (int32_t)(now - deadline) >= 0;
}
static inline uint32_t elapsed(uint32_t now, uint32_t since) {
  return now - since;
}

// Integer square root (no FPU, no sqrtf).
static inline uint16_t isqrt32(uint32_t v) {
  uint32_t rest = v, root = 0, bit = 1UL << 30;
  while (bit > rest) bit >>= 2;
  while (bit) {
    if (rest >= root + bit) {
      rest -= root + bit;
      root = (root >> 1) + bit;
    } else {
      root >>= 1;
    }
    bit >>= 2;
  }
  return (uint16_t)root;
}

#if SMILY_TARGET_ESP32
#include "esp_system.h"
static inline uint32_t smilyRand() { return esp_random(); }
#else
static inline uint32_t smilyRand() { return (uint32_t)rand(); }
#endif

// Random value in [lo, hi).
static inline uint32_t smilyRandRange(uint32_t lo, uint32_t hi) {
  return (hi > lo) ? lo + (smilyRand() % (hi - lo)) : lo;
}

#if SMILY_SERIAL_LOG
#define SMILY_LOG(...)                                                         \
  do {                                                                         \
    Serial.printf(__VA_ARGS__);                                                \
  } while (0)
#else
#define SMILY_LOG(...)                                                         \
  do {                                                                         \
  } while (0)
#endif
