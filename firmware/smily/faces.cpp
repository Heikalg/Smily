#include "faces.h"

#include <Arduino.h>
#include <stdio.h>

#include "display.h"
#include "util.h"

// ---------------------------------------------------------------------------
// Integer trig: quarter-wave table, 64 steps per quadrant, scaled to +-127.
// ---------------------------------------------------------------------------
static const uint8_t SIN_Q[65] = {
    0,   3,   6,   9,   12,  16,  19,  22,  25,  28,  31,  34,  37,
    40,  43,  46,  49,  51,  54,  57,  60,  63,  65,  68,  71,  73,
    76,  78,  81,  83,  85,  88,  90,  92,  94,  96,  98,  100, 102,
    104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120, 121,
    122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127};

int8_t isin(uint8_t a) {
  if (a < 64) return (int8_t)SIN_Q[a];
  if (a < 128) return (int8_t)SIN_Q[128 - a];
  if (a < 192) return -(int8_t)SIN_Q[a - 128];
  return -(int8_t)SIN_Q[256 - a];
}

int8_t icos(uint8_t a) { return isin((uint8_t)(a + 64)); }

// sine scaled to an amplitude in pixels
static inline int16_t wave(uint8_t angle, int16_t amp) {
  return ((int16_t)isin(angle) * amp) / 127;
}

// ---------------------------------------------------------------------------
// Palette. The mochi is lit, the face is cut out of it. In outline mode the
// body is hollow, so the face has to be the lit colour instead.
// ---------------------------------------------------------------------------
#if SMILY_MOCHI_OUTLINE
static const uint8_t INK = 1;    // face features
static const uint8_t SPARK = 0;  // eye shine
#else
static const uint8_t INK = 0;
static const uint8_t SPARK = 1;
#endif

// ---------------------------------------------------------------------------
// Body geometry
// ---------------------------------------------------------------------------
static const int16_t BODY_CX = 64;
static const int16_t BODY_BOTTOM = MOCHI_BOTTOM;
static const int16_t BODY_W = MOCHI_W;
static const int16_t BODY_H = MOCHI_H;

struct Pose {
  int16_t cx = BODY_CX;
  int16_t bottom = BODY_BOTTOM;
  int16_t w = BODY_W;
  int16_t h = BODY_H;

  // squish keeps the mochi's area roughly constant: wider means flatter
  void squish(int16_t s) {
    w = BODY_W + s;
    h = BODY_H - s;
  }
  int16_t eyeY() const { return bottom - (h * MOCHI_EYE_Y_PCT) / 100; }
  int16_t eyeDX() const { return (w * MOCHI_EYE_DX_PCT) / 100; }
  int16_t mouthY() const { return eyeY() + MOCHI_EYE_H - 1; }
  int16_t left() const { return cx - w / 2; }
  int16_t right() const { return cx + w / 2; }
};

static void drawSilhouette(const Pose &p) {
  const int16_t x = p.left();
  const int16_t y = p.bottom - p.h;
  int16_t r = (p.h * MOCHI_ROUND_PCT) / 100;
  const int16_t rmax = smin(p.w, p.h) / 2 - 1;
  r = sclamp(r, 1, smax(rmax, 1));

  u8g2.setDrawColor(1);

  // stubby arms, one each side, and two little feet peeking out underneath.
  // Draw them at their true body-relative position and let u8g2 clip anything
  // off-screen: clamping the x to the panel edge used to strand a detached
  // blob of "dots" against the left/right edge when the body leaned.
  const int16_t armY = p.bottom - p.h / 3;
  u8g2.drawDisc(x - 1, armY, 4);
  u8g2.drawDisc(p.right() + 1, armY, 4);
  u8g2.drawDisc(p.cx - p.w / 4, p.bottom + 2, 4);
  u8g2.drawDisc(p.cx + p.w / 4, p.bottom + 2, 4);

  if (p.h >= 5) {
    u8g2.drawRBox(x, y, p.w, p.h, r);
  } else {
    u8g2.drawBox(x, y, p.w, smax(p.h, 1));  // too flat for rounded corners
  }

#if SMILY_MOCHI_OUTLINE
  // Hollow the body out and leave a 2 px rim. The arms and feet sit outside
  // the erased rectangle, so they stay solid.
  if (p.w > 8 && p.h > 8) {
    u8g2.setDrawColor(0);
    u8g2.drawRBox(x + 2, y + 2, p.w - 4, p.h - 4, smax(r - 2, 1));
    u8g2.setDrawColor(1);
  }
#endif
}

// ---------------------------------------------------------------------------
// Eyes
// ---------------------------------------------------------------------------
static const int16_t EYE_W = MOCHI_EYE_W;
static const int16_t EYE_H = MOCHI_EYE_H;

static void eyeOpen(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  u8g2.drawRBox(x - EYE_W / 2, y - EYE_H / 2, EYE_W, EYE_H,
                smax(EYE_W / 2 - 1, 1));  // tall soft oval
  u8g2.setDrawColor(SPARK);
  u8g2.drawBox(x - EYE_W / 2 + 2, y - EYE_H / 2 + 2, 2, 2);  // highlight
}

static void eyeBlink(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  u8g2.drawBox(x - EYE_W / 2, y - 1, EYE_W + 1, 2);
}

// smiling closed eyes: ^  ^
static void eyeHappy(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  for (int8_t t = 0; t < 2; t++) {
    u8g2.drawLine(x - 5, y + 3 + t, x, y - 2 + t);
    u8g2.drawLine(x, y - 2 + t, x + 5, y + 3 + t);
  }
}

// relaxed closed eyes, a shallow downward arc
static void eyeSleep(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  for (int8_t t = 0; t < 2; t++) {
    u8g2.drawLine(x - 5, y - 1 + t, x - 2, y + 2 + t);
    u8g2.drawHLine(x - 2, y + 2 + t, 5);
    u8g2.drawLine(x + 2, y + 2 + t, x + 5, y - 1 + t);
  }
}

// dizzy swirl: two rings plus a spoke that rotates with the phase
static void eyeSwirl(int16_t x, int16_t y, uint8_t phase) {
  u8g2.setDrawColor(INK);
  u8g2.drawCircle(x, y, 5);
  u8g2.drawCircle(x, y, 2);
  const int16_t x1 = x + (icos(phase) * 2) / 127;
  const int16_t y1 = y + (isin(phase) * 2) / 127;
  const int16_t x2 = x + (icos(phase) * 5) / 127;
  const int16_t y2 = y + (isin(phase) * 5) / 127;
  u8g2.drawLine(x1, y1, x2, y2);
}

// ---------------------------------------------------------------------------
// Mouths
// ---------------------------------------------------------------------------
static void mouthNeutral(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  u8g2.drawBox(x - 2, y, 5, 2);
}

static void mouthSmall(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  u8g2.drawBox(x - 1, y, 3, 2);
}

// happy little omega
static void mouthHappy(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  for (int8_t t = 0; t < 2; t++) {
    u8g2.drawLine(x - 6, y + t, x - 3, y + 3 + t);
    u8g2.drawLine(x - 3, y + 3 + t, x, y + t);
    u8g2.drawLine(x, y + t, x + 3, y + 3 + t);
    u8g2.drawLine(x + 3, y + 3 + t, x + 6, y + t);
  }
}

static void mouthO(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  u8g2.drawDisc(x, y + 2, 2);
}

// big yawning oval, h grows with the yawn
static void mouthYawn(int16_t x, int16_t y, int16_t h) {
  if (h < 2) {
    mouthNeutral(x, y);
    return;
  }
  u8g2.setDrawColor(INK);
  u8g2.drawDisc(x, y + h / 2, 3);
  if (h > 6) u8g2.drawBox(x - 3, y + 2, 7, h - 4);
}

static void blush(const Pose &p, int16_t y) {
  u8g2.setDrawColor(INK);
  for (int8_t i = 0; i < 3; i++) {
    u8g2.drawHLine(p.left() + 6, y + i * 2, 9);
    u8g2.drawHLine(p.right() - 15, y + i * 2, 9);
  }
}

// ---------------------------------------------------------------------------
// Floating props. These drift across the body, so each one is drawn with a
// 1 px halo in the opposite colour to stay readable on white and on black.
// ---------------------------------------------------------------------------
static void heartShape(int16_t x, int16_t y, int8_t r, uint8_t color) {
  u8g2.setDrawColor(color);
  u8g2.drawDisc(x - r, y, r);
  u8g2.drawDisc(x + r, y, r);
  u8g2.drawTriangle(x - 2 * r, y, x + 2 * r, y, x, y + 2 * r + 1);
}

static void heartOutlined(int16_t x, int16_t y, int8_t r) {
  heartShape(x - 1, y, r, 0);
  heartShape(x + 1, y, r, 0);
  heartShape(x, y - 1, r, 0);
  heartShape(x, y + 1, r, 0);
  heartShape(x, y, r, 1);
}

static void starShape(int16_t x, int16_t y, int8_t s, uint8_t color) {
  u8g2.setDrawColor(color);
  u8g2.drawHLine(x - s, y, 2 * s + 1);
  u8g2.drawVLine(x, y - s, 2 * s + 1);
}

static void starOutlined(int16_t x, int16_t y, int8_t s) {
  starShape(x - 1, y, s, 0);
  starShape(x + 1, y, s, 0);
  starShape(x, y - 1, s, 0);
  starShape(x, y + 1, s, 0);
  starShape(x, y, s, 1);
}

static void zShape(int16_t x, int16_t y, int8_t s, uint8_t color) {
  u8g2.setDrawColor(color);
  u8g2.drawHLine(x, y, s);
  u8g2.drawLine(x + s - 1, y, x, y + s - 1);
  u8g2.drawHLine(x, y + s - 1, s);
}

static void zOutlined(int16_t x, int16_t y, int8_t s) {
  zShape(x - 1, y, s, 0);
  zShape(x + 1, y, s, 0);
  zShape(x, y - 1, s, 0);
  zShape(x, y + 1, s, 0);
  zShape(x, y, s, 1);
}

// ---------------------------------------------------------------------------
// Keyframe interpolation, used by the wake-up pop
// ---------------------------------------------------------------------------
struct KF {
  uint8_t frame;
  int8_t value;
};

static int16_t kfValue(const KF *k, uint8_t n, uint16_t f) {
  if (f <= k[0].frame) return k[0].value;
  for (uint8_t i = 1; i < n; i++) {
    if (f <= k[i].frame) {
      const int16_t span = (int16_t)k[i].frame - k[i - 1].frame;
      const int16_t d = (int16_t)f - k[i - 1].frame;
      return k[i - 1].value +
             (((int16_t)k[i].value - k[i - 1].value) * d) / (span ? span : 1);
    }
  }
  return k[n - 1].value;
}

// ---------------------------------------------------------------------------
// ANIM_IDLE - slow breathing, random blink, leans with the accelerometer.
// On top of that the engine occasionally asks for a fidget: a yawn, a slow
// look-around, or a wiggle. Each is a short gesture whose progress arrives in
// ctx.fidgetMs / ctx.fidgetLenMs, so the renderer owns the shape but not the
// clock. A triangular 0..255..0 envelope eases every fidget in and out.
// ---------------------------------------------------------------------------
static uint8_t fidgetEnv(const FaceCtx &ctx) {
  if (!ctx.fidgetLenMs) return 0;
  uint16_t half = ctx.fidgetLenMs / 2;
  if (!half) return 0;
  uint16_t t = ctx.fidgetMs < ctx.fidgetLenMs ? ctx.fidgetMs : ctx.fidgetLenMs;
  uint16_t up = t < half ? t : (uint16_t)(ctx.fidgetLenMs - t);
  uint32_t v = (uint32_t)up * 255 / half;
  return (uint8_t)(v > 255 ? 255 : v);
}

static void renderIdle(uint16_t f, const FaceCtx &ctx) {
  const uint8_t env = fidgetEnv(ctx);  // 0 at rest, 255 at the peak

  Pose p;
  // ~5.6 s breathing cycle, plus whatever squish the last jolt left behind
  int16_t squish = wave((uint8_t)(f * 3), 2) + (int16_t)ctx.jolt;
  int16_t gaze = ctx.glancePx + ctx.leanPx / 2;  // eyes lead the lean
  int16_t cx = BODY_CX + ctx.leanPx;

  // Fidget overrides, applied as offsets on top of the resting pose/gaze.
  if (ctx.fidget == FIDGET_YAWN) {
    squish -= (env * 7) / 255;            // stretch up as the yawn peaks
  } else if (ctx.fidget == FIDGET_WIGGLE) {
    // three quick sways, amplitude eased by the envelope
    const int16_t sway = wave((uint8_t)(ctx.fidgetMs / 4), (env * 7) / 255);
    cx += sway;
    gaze += sway / 2;
  } else if (ctx.fidget == FIDGET_LOOK) {
    // sweep the gaze L -> R -> C across the gesture
    const uint8_t ph = (uint8_t)((uint32_t)ctx.fidgetMs * 255 / smax(ctx.fidgetLenMs, 1));
    gaze += (isin(ph) * 5) / 127;
  }

  p.squish(squish);
  p.cx = cx;
  drawSilhouette(p);

  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();
  const int16_t exL = p.cx - dx + gaze;
  const int16_t exR = p.cx + dx + gaze;

  // Eyes: a yawn squeezes them shut and a blink overrides; otherwise the eyes
  // stay the default open oval. Mood is expressed through the MOUTH only -
  // swapping eye SHAPES every few seconds snapped harshly, so it was dropped.
  if (ctx.fidget == FIDGET_YAWN && env > 90) {
    eyeHappy(exL, eyeY);
    eyeHappy(exR, eyeY);
  } else if (ctx.blink) {
    eyeBlink(exL, eyeY);
    eyeBlink(exR, eyeY);
  } else {
    eyeOpen(exL, eyeY);
    eyeOpen(exR, eyeY);
  }

  // Mouth carries the mood; the yawn overrides it with its growing oval.
  const int16_t mx = p.cx + gaze / 2;
  const int16_t my = p.mouthY();
  if (ctx.fidget == FIDGET_YAWN) {
    mouthYawn(mx, my - 1, (env * 12) / 255);
  } else {
    switch (ctx.mood) {
      case MOOD_HAPPY:   mouthHappy(mx, my - 1); break;  // omega smile
      case MOOD_CONTENT: mouthSmall(mx, my);     break;  // small content mouth
      case MOOD_SLEEPY:  mouthSmall(mx, my);     break;  // small relaxed mouth
      case MOOD_NEUTRAL:
      default:           mouthNeutral(mx, my);   break;
    }
  }
}

// ---------------------------------------------------------------------------
// ANIM_WAKE - squashed pancake springs up, overshoots, settles, opens eyes
// ---------------------------------------------------------------------------
// Negative squish stretches the body upwards, so the peaks are limited to
// what still fits above MOCHI_BOTTOM on a 64 px panel.
static const KF WAKE_SQUISH[] = {{0, 34},  {2, 32},   {5, -8}, {8, 9},
                                 {11, -4}, {14, 3},   {17, -1}, {20, 0},
                                 {26, 2},  {29, 0}};

static void renderWake(uint16_t f, const FaceCtx &ctx) {
  (void)ctx;
  Pose p;
  p.squish(kfValue(WAKE_SQUISH, sizeof(WAKE_SQUISH) / sizeof(KF), f));
  drawSilhouette(p);

  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();

  if (f < 5) {
    eyeSleep(p.cx - dx, eyeY);
    eyeSleep(p.cx + dx, eyeY);
  } else if (f < 9 || f == 25 || f == 26) {
    // shut during the pop, plus one more blink before handing over to idle
    eyeBlink(p.cx - dx, eyeY);
    eyeBlink(p.cx + dx, eyeY);
  } else {
    eyeOpen(p.cx - dx, eyeY);
    eyeOpen(p.cx + dx, eyeY);
  }

  if (p.h >= 26) {
    if (f >= 9 && f < 24) {
      mouthHappy(p.cx, p.mouthY());
    } else if (f >= 24) {
      mouthNeutral(p.cx, p.mouthY());
    }
  }

  // a pair of sparkles for the "pop"
  if (f >= 8 && f < 16) {
    const int8_t s = (int8_t)(3 - (f - 8) / 3);
    starOutlined(p.left() - 6, p.bottom - p.h + 2, smax(s, 1));
    starOutlined(p.right() + 6, p.bottom - p.h + 6, smax(s, 1));
  }
}

// ---------------------------------------------------------------------------
// ANIM_PET - bouncing, blushing, hearts rising
// ---------------------------------------------------------------------------
static void renderPet(uint16_t f, const FaceCtx &ctx) {
  (void)ctx;
  const uint16_t total = PET_ANIM_MS / FRAME_INTERVAL_MS;
  int16_t amp = 6 - (int16_t)(f / 8);  // bounce decays a little
  amp = smax(amp, 3);

  const uint8_t phase = (uint8_t)(f * 20);
  const int16_t sn = isin(phase);

  Pose p;
  p.squish(-(sn * amp) / 127);  // stretched airborne, squashed on landing
  p.bottom = BODY_BOTTOM - (sn > 0 ? (sn * 4) / 127 : 0);
  drawSilhouette(p);

  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();

  if (f + 3 >= total) {  // ease back to the idle face
    eyeOpen(p.cx - dx, eyeY);
    eyeOpen(p.cx + dx, eyeY);
    mouthNeutral(p.cx, p.mouthY());
  } else {
    eyeHappy(p.cx - dx, eyeY);
    eyeHappy(p.cx + dx, eyeY);
    mouthHappy(p.cx, p.mouthY());
  }
  blush(p, eyeY + 7);

  // Two hearts per side on staggered phases. They stay in the margins beyond
  // the arms and drift outwards as they rise, so the face never gets busy.
  for (uint8_t k = 0; k < 4; k++) {
    const uint16_t t = (f * 3 + (k / 2) * 22) % 44;  // 0..43, then repeats
    const int16_t side = (k & 1) ? 1 : -1;
    const int16_t hx = BODY_CX + side * (48 + (int16_t)t / 6) +
                       wave((uint8_t)(t * 5 + k * 40), 2);
    const int16_t hy = 54 - (int16_t)t;
    heartOutlined(hx, hy, 2);
  }
}

// ---------------------------------------------------------------------------
// ANIM_DIZZY - the MPU test. Wobble, swirl eyes, stars orbiting the head.
// ---------------------------------------------------------------------------
static void renderDizzy(uint16_t f, const FaceCtx &ctx) {
  (void)ctx;
  const uint16_t total = DIZZY_ANIM_MS / FRAME_INTERVAL_MS;
  // taper everything off over the last 8 frames so the return to idle is calm
  const int16_t left = (int16_t)total - (int16_t)f;
  const int16_t decay = sclamp(left, 1, 8);

  Pose p;
  p.squish(wave((uint8_t)(f * 31), (4 * decay) / 8));
  p.cx = BODY_CX + wave((uint8_t)(f * 24), (8 * decay) / 8);
  drawSilhouette(p);

  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();

  if (left > 4) {
    const uint8_t spin = (uint8_t)(f * 22);
    eyeSwirl(p.cx - dx, eyeY, spin);
    eyeSwirl(p.cx + dx, eyeY, (uint8_t)(spin + 128));
    mouthO(p.cx + wave((uint8_t)(f * 24), 2), p.mouthY());
  } else {
    eyeBlink(p.cx - dx, eyeY);
    eyeBlink(p.cx + dx, eyeY);
    mouthNeutral(p.cx, p.mouthY());
  }

  // stars circling the head on a wide ellipse: out past the sides, and just
  // over the top of the body where the halo keeps them readable
  if (left > 3) {
    for (uint8_t k = 0; k < 3; k++) {
      const uint8_t a = (uint8_t)(f * 10 + k * 85);
      const int16_t sx = BODY_CX + (icos(a) * 42) / 127;
      const int16_t sy = 9 + (isin(a) * 7) / 127;
      starOutlined(sx, sy, 2);
    }
  }
}

// ---------------------------------------------------------------------------
// ANIM_GOODNIGHT - settles flat, eyes shut, three z's drift away
// ---------------------------------------------------------------------------
static void renderGoodnight(uint16_t f, const FaceCtx &ctx) {
  (void)ctx;
  const uint16_t total = smax(GOODNIGHT_ANIM_MS / FRAME_INTERVAL_MS, 1);

  Pose p;
  p.squish(((int16_t)f * 10) / (int16_t)total);
  drawSilhouette(p);

  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();
  eyeSleep(p.cx - dx, eyeY);
  eyeSleep(p.cx + dx, eyeY);
  mouthSmall(p.cx, p.mouthY());

  // Three z's on the same diagonal path, launched 4 frames apart. They move
  // 2 px per frame, so the stagger keeps them 8 px apart instead of piling
  // up, and each one grows a little as it floats away.
  for (uint8_t k = 0; k < 3; k++) {
    const int16_t t = (int16_t)f - (int16_t)k * 4;
    if (t <= 0) continue;
    const int16_t zy = 12 - 2 * t;
    if (zy < -6) continue;
    zOutlined(92 + 2 * t, zy, (int8_t)(3 + t / 4));
  }
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------
void faces_render(AnimId id, uint16_t frame, const FaceCtx &ctx) {
  u8g2.clearBuffer();

  switch (id) {
    case ANIM_WAKE:
      renderWake(frame, ctx);
      break;
    case ANIM_IDLE:
      renderIdle(frame, ctx);
      break;
    case ANIM_PET:
      renderPet(frame, ctx);
      break;
    case ANIM_DIZZY:
      renderDizzy(frame, ctx);
      break;
    case ANIM_GOODNIGHT:
      renderGoodnight(frame, ctx);
      break;
    case ANIM_BLANK:
    default:
      break;
  }

#if SMILY_MOTION_OVERLAY
  char line[24];
  snprintf(line, sizeof(line), "%c j%d g%d", ctx.noMotion ? '!' : 'M',
           (int)ctx.dbgA, (int)ctx.dbgB);
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(1, 7, line);
#endif

  u8g2.setDrawColor(1);  // hand the library back in a known state
}
