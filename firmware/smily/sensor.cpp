// ---------------------------------------------------------------------------
// Sensor - implementation. Non-blocking DHT11 temp + humidity.
//
// Shape of it: polled once per loop(). A DHT11 refuses to be read more than
// about once every 2 s, so we only touch it when SENSOR_READ_INTERVAL_MS has
// passed; every other poll returns immediately. The read itself is a short
// bit-banged exchange (~25 ms) -- bounded like net's HTTP GET, and only ever
// reached when a reading is actually due, so the frame clock keeps running.
//
// First read after power-on (including every Deep Sleep wake, since the sensor
// shares the 3V3 rail and powers down with the chip) can be garbage while the
// sensor stabilises -- we simply discard a failed read and try again next
// interval; the cached last-good value keeps showing until a fresh one lands.
//
// On the host preview build the sensor does not exist, so the whole file is a
// stub that reports no reading. All real logic is ESP32-only.
// ---------------------------------------------------------------------------
#include "sensor.h"

#include "config.h"
#include "util.h"

#if SMILY_TARGET_ESP32 && SMILY_SENSOR_ENABLED

#include <Arduino.h>
#include <DHT.h>            // Adafruit DHT sensor library

static DHT      s_dht(PIN_DHT11, SMILY_DHT_TYPE);
static Reading  s_reading;                  // cached reading
static uint32_t s_readingAtMs = 0;          // millis() when it was taken
static uint32_t s_nextReadMs  = 0;          // when the next read is due

void sensor_begin(uint32_t now) {
  s_dht.begin();
  // Give the DHT11 its first-read settle window before we trust a value.
  s_nextReadMs = now + SENSOR_WARMUP_MS;
  SMILY_LOG("[sensor] DHT%d on GPIO %d, warming up\n",
            (int)SMILY_DHT_TYPE, (int)PIN_DHT11);
}

void sensor_poll(uint32_t now) {
  if ((int32_t)(now - s_nextReadMs) < 0) return;   // not due yet
  s_nextReadMs = now + SENSOR_READ_INTERVAL_MS;

  const float h = s_dht.readHumidity();
  const float t = s_dht.readTemperature();         // Celsius

  // readXxx() returns NaN on a failed/garbled read (common on the very first
  // read after wake). Discard it -- the cached last-good value stays.
  if (isnan(h) || isnan(t)) {
    SMILY_LOG("[sensor] read failed (NaN), keeping last good\n");
    return;
  }

  s_reading.tempC    = t;
  s_reading.humidity = h;
  s_reading.have     = true;
  s_readingAtMs      = now;
  SMILY_LOG("[sensor] %.1f C, %.0f%% RH\n", t, h);
}

Reading sensor_reading(uint32_t now) {
  Reading r = s_reading;
  r.ageMs = r.have ? elapsed(now, s_readingAtMs) : 0;
  return r;
}

#else  // ---- host preview build, or sensor compiled out ---------------------

void    sensor_begin(uint32_t)   {}
void    sensor_poll(uint32_t)    {}
Reading sensor_reading(uint32_t) { return Reading(); }

#endif
