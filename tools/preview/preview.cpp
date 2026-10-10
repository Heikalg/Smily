// ---------------------------------------------------------------------------
// Smily preview + behaviour harness (host only).
//
// Builds the real firmware modules against stub Arduino/Wire/u8g2 headers so
// that we can:
//   1. render every animation frame to a PGM sprite sheet and look at it
//   2. drive the state machine with synthetic taps and a synthetic MPU and
//      assert the transitions, the frame interval and the sleep behaviour
//
// Run: tools/preview/build.sh
// ---------------------------------------------------------------------------
#define _USE_MATH_DEFINES  // MSYS2/MinGW only exposes M_PI with this set
#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "animation.h"
#include "config.h"
#include "display.h"
#include "faces.h"
#include "input_handler.h"
#include "motion.h"
#include "power.h"
#include "state_machine.h"

// --- stub globals ----------------------------------------------------------
uint32_t hostMillis = 0;
int hostTouchLevel = LOW;
HostSerial Serial;
HostTwoWire Wire;
HostMpuState hostMpu;

// --- frame capture ---------------------------------------------------------
static const int FW = 128, FH = 64;
static bool g_capture = false;
static std::vector<std::vector<uint8_t>> g_frames;
static std::vector<uint32_t> g_flushTimes;

void U8G2Host::sendBuffer() {
  g_flushTimes.push_back(hostMillis);
  if (!g_capture) return;
  std::vector<uint8_t> f(FW * FH);
  for (int y = 0; y < FH; y++)
    for (int x = 0; x < FW; x++) f[y * FW + x] = buf[y][x];
  g_frames.push_back(f);
}

// --- tiny 3x5 digit font for frame labels ----------------------------------
static const uint8_t DIGITS[10][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111}, {0b010, 0b110, 0b010, 0b010, 0b111},
    {0b111, 0b001, 0b111, 0b100, 0b111}, {0b111, 0b001, 0b111, 0b001, 0b111},
    {0b101, 0b101, 0b111, 0b001, 0b001}, {0b111, 0b100, 0b111, 0b001, 0b111},
    {0b111, 0b100, 0b111, 0b101, 0b111}, {0b111, 0b001, 0b001, 0b001, 0b001},
    {0b111, 0b101, 0b111, 0b101, 0b111}, {0b111, 0b101, 0b111, 0b001, 0b001}};

struct Canvas {
  int w, h;
  std::vector<uint8_t> px;
  Canvas(int w_, int h_, uint8_t fill) : w(w_), h(h_), px(w_ * h_, fill) {}
  void set(int x, int y, uint8_t v) {
    if (x >= 0 && x < w && y >= 0 && y < h) px[y * w + x] = v;
  }
  void rect(int x, int y, int rw, int rh, uint8_t v) {
    for (int j = 0; j < rh; j++)
      for (int i = 0; i < rw; i++) set(x + i, y + j, v);
  }
  void digit(int x, int y, int d, int scale, uint8_t v) {
    for (int r = 0; r < 5; r++)
      for (int c = 0; c < 3; c++)
        if (DIGITS[d][r] & (1 << (2 - c)))
          rect(x + c * scale, y + r * scale, scale, scale, v);
  }
  void number(int x, int y, int n, int scale, uint8_t v) {
    char s[8];
    snprintf(s, sizeof(s), "%d", n);
    for (int i = 0; s[i]; i++)
      digit(x + i * 4 * scale, y, s[i] - '0', scale, v);
  }
  bool writePgm(const std::string &path) const {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(px.data(), 1, px.size(), f);
    fclose(f);
    return true;
  }
};

static void writeSheet(const std::string &path, int cols, int scale,
                       int firstLabel) {
  if (g_frames.empty()) return;
  const int n = (int)g_frames.size();
  const int rows = (n + cols - 1) / cols;
  const int cw = FW * scale + 2, ch = FH * scale + 2;  // 1 px border
  const int gap = 4;
  Canvas sheet(cols * cw + (cols + 1) * gap, rows * ch + (rows + 1) * gap, 60);

  for (int i = 0; i < n; i++) {
    const int ox = gap + (i % cols) * (cw + gap);
    const int oy = gap + (i / cols) * (ch + gap);
    sheet.rect(ox, oy, cw, ch, 150);  // cell border
    for (int y = 0; y < FH; y++)
      for (int x = 0; x < FW; x++) {
        const uint8_t v = g_frames[i][y * FW + x] ? 255 : 0;
        sheet.rect(ox + 1 + x * scale, oy + 1 + y * scale, scale, scale, v);
      }
    sheet.number(ox + 3, oy + 3, firstLabel + i, scale, 120);
  }
  sheet.writePgm(path);
  printf("  wrote %-34s %d frames, %dx%d\n", path.c_str(), n, sheet.w, sheet.h);
}

// ---------------------------------------------------------------------------
// 1. Animation sheets
// ---------------------------------------------------------------------------
static void stepToNextFrame() {
  // advance the virtual clock 1 ms at a time until the engine emits a frame
  for (int guard = 0; guard < 1000; guard++) {
    if (anim.update(hostMillis)) return;
    hostMillis++;
  }
}

static void captureAnim(AnimId id, const char *name, int frames, int cols,
                        int scale, bool sweepTilt) {
  g_frames.clear();
  g_capture = true;
  hostMillis += 500;
  anim.begin(hostMillis);
  anim.play(id, hostMillis);
  for (int i = 0; i < frames; i++) {
    if (sweepTilt) anim.setTilt((int8_t)(((int)isin((uint8_t)(i * 4)) * 8) / 127));
    stepToNextFrame();
    hostMillis++;
  }
  g_capture = false;
  writeSheet(std::string("out/") + name + ".pgm", cols, scale, 0);
}

static void captureFidget(FidgetId fid, uint16_t lenMs, const char *name) {
  g_frames.clear();
  g_capture = true;
  FaceCtx ctx;
  ctx.leanPx = ctx.glancePx = 0;
  ctx.jolt = 0;
  ctx.blink = false;
  ctx.noMotion = false;
  ctx.dbgA = ctx.dbgB = 0;
  ctx.fidget = fid;
  ctx.fidgetLenMs = lenMs;
  ctx.mood = MOOD_NEUTRAL;
  const uint16_t n = lenMs / FRAME_INTERVAL_MS;
  for (uint16_t i = 0; i < n; i++) {
    ctx.fidgetMs = (uint16_t)(i * FRAME_INTERVAL_MS);
    faces_render(ANIM_IDLE, i, ctx);
    u8g2.sendBuffer();
  }
  g_capture = false;
  writeSheet(std::string("out/") + name + ".pgm", 6, 2, 0);
}

static void captureMoods() {
  g_frames.clear();
  g_capture = true;
  FaceCtx ctx;
  ctx.leanPx = ctx.glancePx = 0;
  ctx.jolt = 0;
  ctx.blink = false;
  ctx.noMotion = false;
  ctx.dbgA = ctx.dbgB = 0;
  ctx.fidget = FIDGET_NONE;
  ctx.fidgetMs = ctx.fidgetLenMs = 0;
  for (uint8_t m = 0; m < MOOD_COUNT; m++) {
    ctx.mood = (MoodId)m;
    faces_render(ANIM_IDLE, 0, ctx);  // frame 0: no breathing offset
    u8g2.sendBuffer();
  }
  g_capture = false;
  writeSheet("out/09-moods.pgm", MOOD_COUNT, 3, 0);
}

static void captureHero() {
  // one representative frame per state, rendered large
  struct Pick {
    AnimId id;
    uint16_t frame;
  };
  const Pick picks[] = {{ANIM_IDLE, 0},  {ANIM_PET, 4},       {ANIM_DIZZY, 6},
                        {ANIM_WAKE, 12}, {ANIM_GOODNIGHT, 8}, {ANIM_IDLE, 3}};
  g_frames.clear();
  g_capture = true;
  FaceCtx ctx;
  for (size_t i = 0; i < sizeof(picks) / sizeof(Pick); i++) {
    ctx.leanPx = (i == 5) ? 7 : 0;  // last cell shows the tilt lean
    ctx.glancePx = (i == 5) ? 3 : 0;
    ctx.jolt = 0;
    ctx.blink = (i == 5);
    ctx.noMotion = false;
    ctx.dbgA = ctx.dbgB = 0;
    ctx.fidget = FIDGET_NONE;
    ctx.fidgetMs = ctx.fidgetLenMs = 0;
    ctx.mood = MOOD_NEUTRAL;
    faces_render(picks[i].id, picks[i].frame, ctx);
    u8g2.sendBuffer();
  }
  g_capture = false;
  writeSheet("out/hero.pgm", 2, 3, 0);
}

// ---------------------------------------------------------------------------
// 2. Behaviour simulation
// ---------------------------------------------------------------------------
static int g_fail = 0;

static void check(bool ok, const char *what) {
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) g_fail++;
}

static void loopBody() {
  const uint32_t now = millis();
  const InputEvents in = input_poll(now);
  const MotionEvents mo = motion_poll(now);
  sm_update(now, in, mo);
}

static void advance(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    hostMillis++;
    loopBody();
  }
}

static void tapN(int count, uint32_t holdMs, uint32_t gapMs) {
  for (int i = 0; i < count; i++) {
    hostTouchLevel = HIGH;
    advance(holdMs);
    hostTouchLevel = LOW;
    advance(gapMs);
  }
}

static void restAtTable() {
  hostMpu.ax = 0;
  hostMpu.ay = 0;
  hostMpu.az = 4096;
  hostMpu.gx = hostMpu.gy = hostMpu.gz = 0;
}

static void shakeFor(uint32_t ms, double hz, double g, double dps) {
  for (uint32_t t = 0; t < ms; t++) {
    const double ph = 2.0 * M_PI * hz * ((double)t / 1000.0);
    hostMpu.ax = (int16_t)(4096.0 * g * std::sin(ph));
    hostMpu.az = (int16_t)(4096.0 + 4096.0 * g * 0.6 * std::sin(ph * 1.3));
    hostMpu.gz = (int16_t)(32.0 * dps * std::sin(ph));
    hostMillis++;
    loopBody();
  }
  restAtTable();
}

static void runSim() {
  Serial.verbose = true;
  printf("\n--- boot ---\n");
  hostMillis = 1000;
  hostTouchLevel = LOW;
  restAtTable();

  const uint32_t now = millis();
  power_begin(now);
  display_begin();
  input_begin(now, false);
  const bool mpuFound = motion_begin();
  sm_begin(now, WAKE_COLD);
  check(mpuFound, "MPU detected over the shared I2C bus");

  printf("\n--- wake -> idle ---\n");
  advance(2200);
  check(sm_state() == STATE_IDLE, "WAKE auto-advances to IDLE after 2 s");

  printf("\n--- single tap -> pet ---\n");
  g_flushTimes.clear();
  tapN(1, 60, 500);  // one tap, then the window closes
  check(sm_state() == STATE_PET, "single tap enters PET");
  advance(2200);
  check(sm_state() == STATE_IDLE, "PET returns to IDLE after 2 s");

  printf("\n--- frame pacing ---\n");
  bool paced = true;
  for (size_t i = 1; i < g_flushTimes.size(); i++)
    if (g_flushTimes[i] - g_flushTimes[i - 1] < FRAME_INTERVAL_MS) paced = false;
  check(paced && g_flushTimes.size() > 20,
        "every flush is at least 66 ms after the previous one");

  printf("\n--- three rapid taps -> dizzy ---\n");
  tapN(3, 50, 120);
  advance(500);
  check(sm_state() == STATE_DIZZY, "triple tap enters DIZZY, not PET");
  advance(2500);
  check(sm_state() == STATE_IDLE, "DIZZY returns to IDLE");

  printf("\n--- gentle handling must NOT trigger dizzy ---\n");
  for (int i = 0; i < 2000; i++) {  // slow 25 degree tilt over 2 s
    const int16_t v = (int16_t)(1800.0 * (i / 2000.0));
    // drive whichever axis config.h selects for the lean, so this test
    // stays valid when MOTION_TILT_AXIS changes (e.g. a rotated mounting)
#if MOTION_TILT_AXIS == 1
    hostMpu.ay = v;
#elif MOTION_TILT_AXIS == 2
    hostMpu.az = 4096 + v;
#else
    hostMpu.ax = v;
#endif
    hostMillis++;
    loopBody();
  }
  check(sm_state() == STATE_IDLE, "slow tilt leaves the state alone");
  const MotionEvents leaned = motion_poll(millis());
  // Direction depends on MOTION_TILT_AXIS / MOTION_TILT_INVERT (a mounting
  // detail tuned on hardware), so assert a lean is produced, not its sign.
  check(leaned.tiltPx != 0, "tilt produces a lean offset for the renderer");
  restAtTable();
  advance(600);

  printf("\n--- a single knock must NOT trigger dizzy ---\n");
  hostMpu.ax = 12000;  // one hard spike
  advance(40);
  restAtTable();
  advance(400);
  check(sm_state() == STATE_IDLE, "one isolated knock is ignored");

  printf("\n--- fast shake -> dizzy ---\n");
  shakeFor(600, 12.0, 2.0, 500.0);
  check(sm_state() == STATE_DIZZY, "shaking it fast enters DIZZY");
  advance(2600);
  check(sm_state() == STATE_IDLE, "DIZZY returns to IDLE after the shake");

  printf("\n--- inactivity -> sleep ---\n");
  advance(SLEEP_TIMEOUT_MS + 1500);
  check(sm_state() == STATE_SLEEP, "15 s of quiet enters SLEEP");
  check(u8g2.powerSave, "SH1106 was put into its low-power state");
  check(hostMpu.asleep, "MPU was parked before deep sleep");
  const size_t flushesAtSleep = g_flushTimes.size();
  advance(3000);
  check(g_flushTimes.size() == flushesAtSleep,
        "no frames are pushed once the panel is asleep");

  printf("\n--- wake touch is not a pet ---\n");
  // emulate the deep-sleep wake path: finger already on the pad at boot
  hostTouchLevel = HIGH;
  hostMillis += 10;
  input_begin(millis(), /*ignoreHeldTouch=*/true);
  bool sawTap = false;
  for (int i = 0; i < 400; i++) {
    hostMillis++;
    if (i == 200) hostTouchLevel = LOW;  // released after 200 ms
    const InputEvents ev = input_poll(millis());
    if (ev.tap || ev.tripleTap) sawTap = true;
  }
  advance(600);
  check(!sawTap, "the touch that woke the device is swallowed");

  printf("\n--- tap after that wake is a pet ---\n");
  hostMillis += 10;
  bool sawTap2 = false;
  hostTouchLevel = HIGH;
  for (int i = 0; i < 60; i++) {
    hostMillis++;
    input_poll(millis());
  }
  hostTouchLevel = LOW;
  for (int i = 0; i < 600; i++) {
    hostMillis++;
    const InputEvents ev = input_poll(millis());
    if (ev.tap) sawTap2 = true;
  }
  check(sawTap2, "the next tap after release is reported normally");

  printf("\n--- missing MPU must not break anything ---\n");
  hostMpu.present = false;
  advance(2500);
  const MotionEvents gone = motion_poll(millis());
  check(!gone.present && gone.tiltPx == 0,
        "absent MPU reports not-present and zero tilt");
  hostMpu.present = true;
  hostMpu.asleep = false;
  advance(1500);
  const MotionEvents back = motion_poll(millis());
  check(back.present, "MPU is re-probed and picked back up when it returns");
}

// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  Serial.verbose = false;

  // Shape-lab mode: render only the hero card to the given path. Used to
  // compare -DMOCHI_* geometry variants against each other.
  if (argc > 1) {
    hostMillis = 100;
    restAtTable();
    display_begin();
    g_frames.clear();
    g_capture = true;
    FaceCtx ctx;
    ctx.leanPx = ctx.glancePx = 0;
    ctx.jolt = 0;
    ctx.blink = false;
    ctx.noMotion = false;
    ctx.dbgA = ctx.dbgB = 0;
    ctx.fidget = FIDGET_NONE;
    ctx.fidgetMs = ctx.fidgetLenMs = 0;
    ctx.mood = MOOD_NEUTRAL;
    faces_render(ANIM_IDLE, 0, ctx);
    u8g2.sendBuffer();
    faces_render(ANIM_PET, 4, ctx);
    u8g2.sendBuffer();
    g_capture = false;
    writeSheet(argv[1], 2, 3, 0);
    return 0;
  }

  printf("Smily preview harness\n");

  hostMillis = 100;
  restAtTable();
  display_begin();

  printf("\nrendering sheets:\n");
  captureAnim(ANIM_WAKE, "01-wake", anim_frameCount(ANIM_WAKE), 6, 2, false);
  captureAnim(ANIM_IDLE, "02-idle", 90, 6, 2, true);
  captureAnim(ANIM_PET, "03-pet", anim_frameCount(ANIM_PET), 6, 2, false);
  captureAnim(ANIM_DIZZY, "04-dizzy", anim_frameCount(ANIM_DIZZY), 6, 2, false);
  captureAnim(ANIM_GOODNIGHT, "05-goodnight",
              anim_frameCount(ANIM_GOODNIGHT), 6, 2, false);
  captureFidget(FIDGET_YAWN, FIDGET_YAWN_MS, "06-fidget-yawn");
  captureFidget(FIDGET_LOOK, FIDGET_LOOK_MS, "07-fidget-look");
  captureFidget(FIDGET_WIGGLE, FIDGET_WIGGLE_MS, "08-fidget-wiggle");
  captureMoods();
  captureHero();

  runSim();

  printf("\n%s (%d failure%s)\n", g_fail ? "FAILURES PRESENT" : "all checks passed",
         g_fail, g_fail == 1 ? "" : "s");
  return g_fail ? 1 : 0;
}
