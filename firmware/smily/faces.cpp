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
  // The resting size squish() flexes around. Defaults to the full body, so
  // every existing caller is unchanged; renderWeather sets a smaller base so
  // its Smily stays small even after squish() (which otherwise snaps w/h back
  // to the full BODY_W/BODY_H and was blowing the weather-view Smily up).
  int16_t baseW = BODY_W;
  int16_t baseH = BODY_H;

  // Shrink to an absolute size (used by the weather view). Call before squish.
  void resize(int16_t newW, int16_t newH) {
    baseW = w = newW;
    baseH = h = newH;
  }

  // squish keeps the mochi's area roughly constant: wider means flatter
  void squish(int16_t s) {
    w = baseW + s;
    h = baseH - s;
  }
  int16_t eyeY() const { return bottom - (h * MOCHI_EYE_Y_PCT) / 100; }
  int16_t eyeDX() const { return (w * MOCHI_EYE_DX_PCT) / 100; }
  // eye height scales with the body so a small Smily gets small eyes
  int16_t eyeH() const { return smax((h * MOCHI_EYE_H) / BODY_H, 4); }
  int16_t mouthY() const { return eyeY() + eyeH() - 1; }
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
  // Limb radius scales with the body so a small Smily (weather view) gets
  // proportionally small arms/feet instead of fixed r=4 stumps.
  const int16_t limb = sclamp((p.h * 4) / BODY_H, 2, 4);
  const int16_t armY = p.bottom - p.h / 3;
  u8g2.drawDisc(x - 1, armY, limb);
  u8g2.drawDisc(p.right() + 1, armY, limb);
  u8g2.drawDisc(p.cx - p.w / 4, p.bottom + 2, limb);
  u8g2.drawDisc(p.cx + p.w / 4, p.bottom + 2, limb);

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

// a small sad frown, the mirror of mouthHappy's omega
static void mouthSad(int16_t x, int16_t y) {
  u8g2.setDrawColor(INK);
  for (int8_t t = 0; t < 2; t++) {
    u8g2.drawLine(x - 4, y + 3 + t, x, y + t);
    u8g2.drawLine(x, y + t, x + 4, y + 3 + t);
  }
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
// Weather. Open-Meteo hands back a WMO weather_code; we bucket it into a few
// looks (see config.h). SE Asia, so the snow codes never realistically fire
// and fold into CLOUD rather than get their own winter art.
// ---------------------------------------------------------------------------
SkyId sky_from_code(int code) {
  if (code < 0) return SKY_NONE;
  // Open-Meteo WMO weather codes. Drizzle 51/53 (light/moderate) is folded into
  // CLOUD, not rain -- on a keychain "a few droplets" reads better as cloudy,
  // and it stops the rain face flashing for weather you can barely feel. Only
  // dense drizzle (55) and actual rain/showers show the rain face.
  if (code == 0) return SKY_CLEAR;
  if (code == 1 || code == 2 || code == 3) return SKY_CLOUD;
  if (code == 45 || code == 48) return SKY_CLOUD;          // fog -> cloud look
  if (code == 51 || code == 53) return SKY_CLOUD;          // light/moderate drizzle
  if (code == 95 || code == 96 || code == 99) return SKY_STORM;
  if (code == 55 || (code >= 56 && code <= 67) ||
      (code >= 80 && code <= 82)) return SKY_RAIN;          // dense drizzle + real rain
  if (code >= 71 && code <= 86) return SKY_CLOUD;          // snow/ice (n/a here)
  return SKY_CLOUD;                                         // anything else: safe
}

// A plain filled sun: disc plus eight short rays. Lit, with a 1 px dark halo
// so it stays readable whether it sits on the white body or the black margin.
static void sunGlyph(int16_t cx, int16_t cy, int8_t r) {
  u8g2.setDrawColor(0);
  u8g2.drawDisc(cx, cy, r + 1);          // halo
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx, cy, r);
  for (uint8_t a = 0; a < 8; a++) {
    const uint8_t ang = (uint8_t)(a * 32);
    const int16_t x0 = cx + (icos(ang) * (r + 2)) / 127;
    const int16_t y0 = cy + (isin(ang) * (r + 2)) / 127;
    const int16_t x1 = cx + (icos(ang) * (r + 4)) / 127;
    const int16_t y1 = cy + (isin(ang) * (r + 4)) / 127;
    u8g2.drawLine(x0, y0, x1, y1);
  }
}

// Crescent moon: a lit disc with a second disc punched out of it, offset.
static void moonGlyph(int16_t cx, int16_t cy, int8_t r) {
  u8g2.setDrawColor(0);
  u8g2.drawDisc(cx, cy, r + 1);          // halo
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx, cy, r);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(cx + r - 1, cy - 1, r);  // bite out of the top-right
  u8g2.setDrawColor(1);
}

// A soft cloud: three overlapping lit discs on a flat base, dark-haloed.
static void cloudGlyph(int16_t cx, int16_t cy, int8_t s) {
  // halo first (slightly larger discs in dark), then the lit cloud on top
  for (int8_t pass = 0; pass < 2; pass++) {
    const uint8_t col = pass == 0 ? 0 : 1;
    const int8_t g = pass == 0 ? 1 : 0;  // halo grows the shape by 1 px
    u8g2.setDrawColor(col);
    u8g2.drawDisc(cx - s, cy + 1, s - 1 + g);
    u8g2.drawDisc(cx + s, cy + 1, s - 1 + g);
    u8g2.drawDisc(cx, cy - 1, s + g);
    u8g2.drawBox(cx - s - (s - 1) + (pass ? 1 : 0) - g, cy, 2 * s + 2 * (s - 1) + 2 * g, s + g);
  }
  u8g2.setDrawColor(1);
}

// A single short rain streak (a 1 px slanted line), used by the ambient layer
// and under the overview cloud.
static void rainStreak(int16_t x, int16_t y, int8_t len) {
  u8g2.setDrawColor(0);                   // thin dark halo keeps it readable
  u8g2.drawLine(x - 1, y, x - 1, y + len);
  u8g2.setDrawColor(1);
  u8g2.drawLine(x, y, x, y + len);
}

// A lightning bolt: a short lit zig-zag with a dark halo.
static void boltGlyph(int16_t x, int16_t y, int8_t h) {
  for (int8_t pass = 0; pass < 2; pass++) {
    u8g2.setDrawColor(pass == 0 ? 0 : 1);
    const int16_t o = pass == 0 ? 0 : 0;
    (void)o;
    u8g2.drawLine(x + 2, y, x - 1, y + h / 2);
    u8g2.drawLine(x - 1, y + h / 2, x + 1, y + h / 2);
    u8g2.drawLine(x + 1, y + h / 2, x - 2, y + h);
    if (pass == 0) {  // widen into a halo on the first pass
      u8g2.drawLine(x + 3, y, x, y + h / 2);
      u8g2.drawLine(x, y + h / 2, x - 3, y + h);
    }
  }
  u8g2.setDrawColor(1);
}

// ---------------------------------------------------------------------------
// Ambient weather layer. Drawn FIRST (behind the mochi), sparse and slow, so
// the weather reads as a mood in the margins without ever cluttering the face.
// The silhouette is drawn on top afterwards and occludes anything behind it.
// `f` is the frame counter, used purely for drift.
// ---------------------------------------------------------------------------
static void drawAmbient(SkyId sky, bool isDay, uint16_t f) {
#if SMILY_WEATHER_AMBIENT
  // Keep every ambient element inside a safe inset. The panel is a 132-wide
  // SH1106 clone with a column offset, so anything closer than ~16 px to a
  // wall smears into a stranded strip against the edge (the "left border").
  const int16_t SAFE_L = 18;
  const int16_t SAFE_R = 110;
  switch (sky) {
    case SKY_CLEAR: {
      // day: clean (nothing). night: a couple of still stars so a clear night
      // reads as night even on the idle face.
      if (!isDay) {
        starOutlined(SAFE_L, 10, 1);
        starOutlined(SAFE_R, 16, 1);
      }
      break;
    }
    case SKY_CLOUD: {
      // one small cloud drifting slowly across the TOP band, but bouncing
      // between the safe margins instead of sliding off the walls (which is
      // what left the stranded strip on the left edge). Triangle wave L<->R.
      const int16_t span = SAFE_R - SAFE_L;
      const int16_t t = (int16_t)((f) % (uint16_t)(2 * span));
      const int16_t x = SAFE_L + (t < span ? t : (2 * span - t));
      cloudGlyph(x, 10, 4);
      if (!isDay) moonGlyph(SAFE_L, 11, 3);  // a small moon marks a cloudy night
      break;
    }
    case SKY_RAIN:
    case SKY_STORM: {
      // 4 sparse streaks falling in the side margins, staggered phases so it
      // never looks like a marching grid. Columns pulled in from the walls.
      static const int16_t COLX[4] = {SAFE_L, SAFE_L + 10, SAFE_R - 10, SAFE_R};
      for (uint8_t i = 0; i < 4; i++) {
        const int16_t y = (int16_t)(((f * 3) + i * 17) % 72) - 8;
        rainStreak(COLX[i], y, 4);
      }
      if (!isDay) moonGlyph(64, 10, 3);  // a small moon up top marks a wet night
      if (sky == SKY_STORM && ((f / 8) % 11) == 0) {
        // a brief, rare flash: a bolt in the top band, centred
        boltGlyph(64, 2, 10);
      }
      break;
    }
    case SKY_NONE:
    default:
      break;
  }
#else
  (void)sky;
  (void)f;
#endif
}

// ---------------------------------------------------------------------------
// Tiny procedural digits (3x5), drawn as lit boxes with a 1 px dark halo so a
// temperature readout stays legible on either colour. Procedural so the
// preview and the panel render identically and no font is linked into flash.
// ---------------------------------------------------------------------------
static const uint8_t DIGIT3x5[11][5] = {
    {0b111, 0b101, 0b101, 0b101, 0b111},  // 0
    {0b010, 0b110, 0b010, 0b010, 0b111},  // 1
    {0b111, 0b001, 0b111, 0b100, 0b111},  // 2
    {0b111, 0b001, 0b111, 0b001, 0b111},  // 3
    {0b101, 0b101, 0b111, 0b001, 0b001},  // 4
    {0b111, 0b100, 0b111, 0b001, 0b111},  // 5
    {0b111, 0b100, 0b111, 0b101, 0b111},  // 6
    {0b111, 0b001, 0b001, 0b001, 0b001},  // 7
    {0b111, 0b101, 0b111, 0b101, 0b111},  // 8
    {0b111, 0b101, 0b111, 0b001, 0b111},  // 9
    {0b000, 0b000, 0b100, 0b000, 0b000},  // 10 = minus sign
};

static void drawDigit(int16_t x, int16_t y, uint8_t d, int8_t scale) {
  for (int8_t pass = 0; pass < 2; pass++) {
    u8g2.setDrawColor(pass == 0 ? 0 : 1);
    const int8_t off = pass == 0 ? 1 : 0;  // halo = draw 1 px out all round
    for (int8_t r = 0; r < 5; r++)
      for (int8_t c = 0; c < 3; c++)
        if (DIGIT3x5[d][r] & (1 << (2 - c))) {
          if (pass == 0) {
            u8g2.drawBox(x + c * scale - 1, y + r * scale - 1, scale + 2, scale + 2);
          } else {
            u8g2.drawBox(x + c * scale, y + r * scale, scale, scale);
          }
        }
    (void)off;
  }
  u8g2.setDrawColor(1);
}

// Draw an integer (handles a leading minus) centred-ish at x; returns the
// total pixel width drawn so the caller can place a degree mark after it.
static int16_t drawNumber(int16_t x, int16_t y, int value, int8_t scale) {
  char s[8];
  snprintf(s, sizeof(s), "%d", value);
  int16_t cx = x;
  for (int i = 0; s[i]; i++) {
    const uint8_t d = (s[i] == '-') ? 10 : (uint8_t)(s[i] - '0');
    drawDigit(cx, y, d, scale);
    cx += 4 * scale;  // 3 px glyph + 1 px gap
  }
  return cx - x;
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
  drawAmbient(ctx.sky, ctx.isDay, f);  // quiet weather hint, BEHIND the mochi
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
// ANIM_WEATHER - the long-press overview. Laid out in two columns that never
// overlap: a SMALL Smily lives in the left strip (x < ~42), and the whole
// right strip (x >= 46) stacks the data top-to-bottom:
//     clock HH:MM  (big, top)   ->   weather glyph (mid)   ->   temp (bottom)
// So Smily never obstructs the numbers. Night buckets (cloudy/rainy at night)
// tuck a small moon behind the cloud so "cloudy night" reads differently from
// "cloudy day". Lightly animated: rays shimmer, cloud drifts, rain falls.
// ---------------------------------------------------------------------------

// Two-digit zero-padded number (for a clock field). Returns width drawn.
static int16_t drawNum2(int16_t x, int16_t y, int v, int8_t scale) {
  if (v < 0) v = 0;
  drawDigit(x, y, (uint8_t)((v / 10) % 10), scale);
  drawDigit(x + 4 * scale, y, (uint8_t)(v % 10), scale);
  return 8 * scale - scale;  // two glyphs + one gap
}

// A colon between HH and MM: two small lit squares with a dark halo.
static void drawColon(int16_t x, int16_t y, int8_t scale) {
  for (int8_t pass = 0; pass < 2; pass++) {
    u8g2.setDrawColor(pass == 0 ? 0 : 1);
    const int8_t g = pass == 0 ? 1 : 0;
    u8g2.drawBox(x - g, y + scale - g, scale + 2 * g, scale + 2 * g);
    u8g2.drawBox(x - g, y + 3 * scale - g, scale + 2 * g, scale + 2 * g);
  }
  u8g2.setDrawColor(1);
}

static void renderWeather(uint16_t f, const FaceCtx &ctx) {
  const SkyId sky = ctx.sky;
  const bool err = ctx.netError;  // real failure AND no cached reading
  const bool night = !ctx.isDay;

  // --- left strip: a SMALL Smily, kept entirely left of x=42 so it can never
  // reach the numbers in the right strip. resize() sets the base so the later
  // squish() flexes around THIS small size instead of snapping back to full.
  Pose p;
  p.resize(32, 26);
  p.cx = 19;
  p.bottom = 56;
  p.squish(wave((uint8_t)(f * 3), 1));  // gentle breathing: clearly alive
  drawSilhouette(p);
  const int16_t eyeY = p.eyeY();
  const int16_t dx = p.eyeDX();
  if (err) {
    eyeSleep(p.cx - dx, eyeY);
    eyeSleep(p.cx + dx, eyeY);
    mouthSad(p.cx, p.mouthY());
  } else {
    eyeHappy(p.cx - dx, eyeY);
    eyeHappy(p.cx + dx, eyeY);
    mouthSmall(p.cx, p.mouthY());
  }

  // A thin divider so the two columns read as separate panels.
  u8g2.setDrawColor(1);
  u8g2.drawVLine(44, 6, 52);

  // --- error state: the only thing drawn in the right strip when a fetch
  // failed with no cached reading. A slashed cloud + the raw code.
  if (err) {
    cloudGlyph(96, 20, 7);
    u8g2.setDrawColor(0);
    u8g2.drawLine(84, 32, 108, 8);
    u8g2.drawLine(85, 32, 109, 8);
    u8g2.setDrawColor(1);
    u8g2.drawLine(84, 31, 108, 7);
    drawNumber(74, 46, ctx.errCode, 2);  // e.g. -11
    return;
  }

  if (sky == SKY_NONE) {
    // Still connecting / first fetch pending, nothing failed: "working on it".
    u8g2.setDrawColor(1);
    for (int8_t i = 0; i < 3; i++) u8g2.drawDisc(78 + i * 12, 30, 2);
    return;
  }

  // --- right strip, top: the clock HH:MM, big. Only drawn when we actually
  // have a time (hour >= 0); otherwise the glyph slides up to fill the space.
  const bool haveClock = ctx.hour >= 0 && ctx.minute >= 0;
  if (haveClock) {
    int16_t cx = 50;
    cx += drawNum2(cx, 4, ctx.hour, 2) + 3;
    drawColon(cx, 4, 2);
    cx += 2 + 4;
    drawNum2(cx, 4, ctx.minute, 2);
  }

  // --- right strip, mid: the weather glyph. Sits lower when the clock is
  // present, higher (centred) when it is not.
  const int16_t gx = 96;
  const int16_t gy = haveClock ? 30 : 20;

  // At night, mark cloud/rain/storm as NIGHT: a small crescent moon peeks out
  // up-left of the cloud, plus a couple of tiny stars, so "cloudy night" and
  // "rainy night" read clearly differently from their daytime versions.
  if (night && (sky == SKY_CLOUD || sky == SKY_RAIN || sky == SKY_STORM)) {
    moonGlyph(gx - 10, gy - 8, 4);
    starOutlined(gx + 9, gy - 9, 1);
    starOutlined(gx - 2, gy - 11, 1);
  }

  switch (sky) {
    case SKY_CLEAR:
      if (ctx.isDay) sunGlyph(gx, gy, 7);
      else           moonGlyph(gx, gy, 8);
      break;
    case SKY_CLOUD:
      cloudGlyph(gx, gy, 7);
      break;
    case SKY_RAIN:
      cloudGlyph(gx, gy, 6);
      for (uint8_t i = 0; i < 3; i++) {
        const int16_t rx = gx - 8 + i * 8;
        const int16_t ry = gy + 7 + (int16_t)(((f * 3) + i * 9) % 10);
        rainStreak(rx, ry, 4);
      }
      break;
    case SKY_STORM:
      cloudGlyph(gx, gy, 6);
      boltGlyph(gx, gy + 7, 11);
      break;
    default:
      break;
  }

  // --- right strip, bottom: the temperature, "NN" + a small degree ring.
  const int16_t ty = 48;
  const int16_t tx = 50;
  const int16_t w = drawNumber(tx, ty, ctx.tempC, 2);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(tx + w + 2, ty + 1, 2);
  u8g2.setDrawColor(1);
  u8g2.drawCircle(tx + w + 2, ty + 1, 1);

  // Humidity, measured by the DHT11, shown as a compact "NN%" at the far
  // bottom-right. Only drawn when the sensor has actually produced a reading
  // (humidity >= 0); a bare glyph with no number would just read as noise.
  if (ctx.humidity >= 0) {
    int16_t hx = tx + w + 10;          // start just past the degree ring
    const int16_t hy = ty + 3;         // a touch lower + smaller than the temp
    hx += drawNumber(hx, hy, ctx.humidity, 1);
    // A tiny percent sign: two dots with a thin slash between them.
    u8g2.drawPixel(hx + 1, hy);
    u8g2.drawLine(hx + 3, hy + 4, hx + 1, hy);   // slash, top-left to bottom
    u8g2.drawPixel(hx + 3, hy + 4);
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
    case ANIM_WEATHER:
      renderWeather(frame, ctx);
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
