// ---------------------------------------------------------------------------
// Motion - InvenSense MPU driver (MPU-6050 / 6500 / 9250, all register
// compatible for what we use here) sharing the display's I2C bus.
//
// Produces events only, like the touch handler:
//   shake  -> "moved very fast", the trigger for STATE_DIZZY
//   tiltPx -> how far the mochi should lean, smoothed
//   jolt   -> small motion energy, squishes the body for a few frames
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

struct MotionEvents {
  bool shake = false;    // fires once per shake, then goes quiet for a while
  bool present = false;  // the chip answered
  int8_t tiltPx = 0;
  uint8_t jolt = 0;
  int16_t jerk = 0;  // latest |d accel| , 256 == 1 g  (tuning/debug)
  int16_t gyro = 0;  // latest |rotation| in deg/s     (tuning/debug)
};

// Probes 0x68 then 0x69 and configures the sensor. Non-blocking: no chip
// reset, because that would need a ~100 ms wait. Returns true if found.
bool motion_begin();

// Call every loop; it samples at MOTION_SAMPLE_MS internally and re-probes
// a missing sensor every MOTION_RETRY_MS.
MotionEvents motion_poll(uint32_t now);

// Park the MPU before Deep Sleep. Without this it keeps drawing milliamps
// and ruins the standby figure.
void motion_sleep();
