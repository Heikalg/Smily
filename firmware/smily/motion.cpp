#include "motion.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"
#include "util.h"

// --- register map ----------------------------------------------------------
static const uint8_t REG_SMPLRT_DIV = 0x19;
static const uint8_t REG_CONFIG = 0x1A;
static const uint8_t REG_GYRO_CONFIG = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t REG_PWR_MGMT_1 = 0x6B;
static const uint8_t REG_PWR_MGMT_2 = 0x6C;
static const uint8_t REG_WHO_AM_I = 0x75;

// accel(6) + temp(2) + gyro(6), or just accel when the gyro is standing by
static const uint8_t BURST_LEN = MOTION_GYRO_ENABLED ? 14 : 6;

static uint8_t s_addr = 0;
static bool s_present = false;
static uint8_t s_whoami = 0;

static uint32_t s_nextSampleMs = 0;
static uint32_t s_nextRetryMs = 0;
static uint8_t s_warmup = 0;

static bool s_havePrev = false;
static int16_t s_pax = 0, s_pay = 0, s_paz = 0;

static int32_t s_tiltFilt = 0;
static int8_t s_tiltPx = 0;
static uint8_t s_jolt = 0;
static int16_t s_jerk = 0;
static int16_t s_gyro = 0;

static uint8_t s_hits = 0;
static uint32_t s_firstHitMs = 0;
static uint32_t s_lastHitMs = 0;
static uint32_t s_cooldownMs = 0;

#if SMILY_MOTION_DEBUG
static uint32_t s_nextDbgMs = 0;
static int16_t s_peakJerk = 0;
static int16_t s_peakGyro = 0;
#endif

// --- raw I2C ---------------------------------------------------------------
static bool regWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool regRead(uint8_t addr, uint8_t reg, uint8_t *buf, uint8_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

static bool configure() {
  // No device reset: it needs a ~100 ms settle, and blocking waits are banned.
  if (!regWrite(REG_PWR_MGMT_1, 0x00)) return false;  // leave sleep, 8 MHz osc
  regWrite(REG_SMPLRT_DIV, 0x07);                     // 1 kHz / 8 = 125 Hz
  regWrite(REG_CONFIG, 0x03);                         // DLPF ~44 Hz
  regWrite(REG_GYRO_CONFIG, 0x10);                    // +-1000 deg/s
  regWrite(REG_ACCEL_CONFIG, 0x10);                   // +-8 g, 4096 LSB/g
#if MOTION_GYRO_ENABLED
  regWrite(REG_PWR_MGMT_2, 0x00);  // everything running
#else
  // Gyro X/Y/Z into standby: the accelerometer alone is enough for the shake
  // trigger and it saves the bulk of the sensor's active current.
  regWrite(REG_PWR_MGMT_2, 0x07);
#endif
  s_warmup = 3;
  s_havePrev = false;
  return true;
}

static bool probe() {
  const uint8_t candidates[2] = {0x68, 0x69};
  for (uint8_t i = 0; i < 2; i++) {
    uint8_t id = 0;
    if (!regRead(candidates[i], REG_WHO_AM_I, &id, 1)) continue;
    // 0x68 MPU-6050, 0x70 MPU-6500, 0x71/0x73 MPU-9250/9255
    if (id == 0x68 || id == 0x70 || id == 0x71 || id == 0x73) {
      s_addr = candidates[i];
      s_whoami = id;
      return configure();
    }
  }
  return false;
}

bool motion_begin() {
  s_present = probe();
  if (s_present) {
    SMILY_LOG("[motion] MPU at 0x%02X, WHO_AM_I 0x%02X\n", s_addr, s_whoami);
  } else {
    SMILY_LOG("[motion] no MPU on the bus - tilt and shake disabled\n");
  }
  return s_present;
}

void motion_sleep() {
  if (!s_present) return;
  regWrite(REG_PWR_MGMT_1, 0x40);  // SLEEP bit: down to microamps
}

// --- sampling --------------------------------------------------------------
static void sample(uint32_t now, MotionEvents &ev) {
  uint8_t b[14] = {0};
  if (!regRead(s_addr, REG_ACCEL_XOUT_H, b, BURST_LEN)) {
    s_present = false;  // sensor fell off the bus; retry later
    s_nextRetryMs = now + MOTION_RETRY_MS;
    return;
  }

  const int16_t ax = (int16_t)((b[0] << 8) | b[1]);
  const int16_t ay = (int16_t)((b[2] << 8) | b[3]);
  const int16_t az = (int16_t)((b[4] << 8) | b[5]);
  // b[6..7] is temperature, unused
  const int16_t gx = (int16_t)((b[8] << 8) | b[9]);
  const int16_t gy = (int16_t)((b[10] << 8) | b[11]);
  const int16_t gz = (int16_t)((b[12] << 8) | b[13]);

  if (s_warmup) {
    s_warmup--;
    s_pax = ax;
    s_pay = ay;
    s_paz = az;
    s_havePrev = true;
    return;
  }

  // --- tilt: heavily smoothed gravity component on the chosen axis --------
#if MOTION_TILT_AXIS == 0
  int32_t tiltRaw = ax;
#elif MOTION_TILT_AXIS == 1
  int32_t tiltRaw = ay;
#else
  int32_t tiltRaw = az;
#endif
#if MOTION_TILT_INVERT
  tiltRaw = -tiltRaw;
#endif
  s_tiltFilt += (tiltRaw - s_tiltFilt) / (1 << MOTION_TILT_SMOOTH);
  s_tiltPx = (int8_t)sclamp((int16_t)(s_tiltFilt / MOTION_TILT_DIV),
                            -MOTION_TILT_MAX_PX, MOTION_TILT_MAX_PX);

  // --- jerk: how hard the acceleration vector changed since last sample ---
  // Scaled by 16 so the squares stay inside int32. 256 units == 1 g.
  int16_t jerk = 0;
  if (s_havePrev) {
    const int32_t dx = ((int32_t)ax - s_pax) / 16;
    const int32_t dy = ((int32_t)ay - s_pay) / 16;
    const int32_t dz = ((int32_t)az - s_paz) / 16;
    jerk = (int16_t)isqrt32((uint32_t)(dx * dx + dy * dy + dz * dz));
  }
  s_pax = ax;
  s_pay = ay;
  s_paz = az;
  s_havePrev = true;

  // --- gyro magnitude: /32 makes one unit roughly one degree per second ---
  const int32_t rx = gx / 32, ry = gy / 32, rz = gz / 32;
  const int16_t gyroMag = (int16_t)isqrt32((uint32_t)(rx * rx + ry * ry + rz * rz));

  s_jerk = jerk;
  s_gyro = gyroMag;

  // Small wobble for anything short of a real shake. Recomputed per sample;
  // the animator latches the peak and decays it over the following frames.
  s_jolt = (uint8_t)sclamp(jerk / (SHAKE_JERK_THRESHOLD / 2), 0, 3);

#if SMILY_MOTION_DEBUG
  if (jerk > s_peakJerk) s_peakJerk = jerk;
  if (gyroMag > s_peakGyro) s_peakGyro = gyroMag;
  if (timeReached(now, s_nextDbgMs)) {
    s_nextDbgMs = now + 100;
    SMILY_LOG("[motion] jerk %4d gyro %4d tilt %3d (peak j%4d g%4d)\n", jerk,
              gyroMag, (int)s_tiltPx, s_peakJerk, s_peakGyro);
    s_peakJerk = 0;
    s_peakGyro = 0;
  }
#endif

  // --- shake: several hard hits close together ---------------------------
  if (!timeReached(now, s_cooldownMs)) return;

  const bool hit = (jerk > SHAKE_JERK_THRESHOLD) || (gyroMag > SHAKE_GYRO_DPS);
  if (!hit) {
    // window expired without enough hits, forget them
    if (s_hits && elapsed(now, s_firstHitMs) > SHAKE_WINDOW_MS) s_hits = 0;
    return;
  }

  if (elapsed(now, s_lastHitMs) < SHAKE_REFRACTORY_MS) return;  // same impulse
  if (!s_hits || elapsed(now, s_firstHitMs) > SHAKE_WINDOW_MS) {
    s_hits = 0;
    s_firstHitMs = now;
  }
  s_lastHitMs = now;
  s_hits++;

  if (s_hits >= SHAKE_HITS_REQUIRED) {
    s_hits = 0;
    s_cooldownMs = now + SHAKE_COOLDOWN_MS;
    ev.shake = true;
    SMILY_LOG("[motion] shake (jerk %d, gyro %d)\n", jerk, gyroMag);
  }
}

MotionEvents motion_poll(uint32_t now) {
  MotionEvents ev;

  if (!s_present) {
    if (timeReached(now, s_nextRetryMs)) {
      s_nextRetryMs = now + MOTION_RETRY_MS;
      s_present = probe();
      if (s_present) SMILY_LOG("[motion] MPU came back at 0x%02X\n", s_addr);
    }
  } else if (timeReached(now, s_nextSampleMs)) {
    s_nextSampleMs = now + MOTION_SAMPLE_MS;
    sample(now, ev);
  }

  ev.present = s_present;
  ev.tiltPx = s_present ? s_tiltPx : 0;
  ev.jolt = s_jolt;
  ev.jerk = s_jerk;
  ev.gyro = s_gyro;
  return ev;
}
