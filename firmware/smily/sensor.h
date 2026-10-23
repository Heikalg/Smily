// ---------------------------------------------------------------------------
// Sensor - on-board DHT11 temperature + humidity, done the Smily way:
// NON-BLOCKING.
//
// This is what makes Smily a real sensing IoT device: instead of re-displaying
// a number fetched from a weather API, it MEASURES its own environment (indoor
// temp + humidity) and shows that. The sky condition and clock still come from
// the net layer (a DHT11 cannot see the sky or know the time); only the
// displayed temperature now comes from here.
//
// Like net, everything is driven by millis() timers and polled once per loop,
// so it never stalls the 15 FPS frame clock or breaks the no-delay() rule. A
// DHT11 can only be read about once every 2 s, so we poll on a timer and cache
// the last good reading.
//
// Build note: on the host preview (SMILY_TARGET_ESP32 == 0) this whole module
// is stubbed to a no-op so tools/preview keeps compiling without the sensor.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

// Cached environment reading. Valid only when .have == true.
struct Reading {
  bool     have = false;    // have we ever got a good reading?
  float    tempC = 0.0f;    // measured temperature, Celsius
  float    humidity = 0.0f; // measured relative humidity, percent
  uint32_t ageMs = 0;       // how long ago (ms) this reading was taken
};

// Call once in setup(), after Serial is up.
void sensor_begin(uint32_t now);

// Call every loop(). Takes a reading when one is due (never more often than
// the DHT11 allows) and caches it. Never blocks longer than one read.
void sensor_poll(uint32_t now);

// Latest cached reading (check .have before using it).
Reading sensor_reading(uint32_t now);
