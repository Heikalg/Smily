// Host stub of u8g2: a real 128x64 1-bit rasteriser so the preview tool
// produces the same picture the SH1106 will show.
#pragma once

#include <cstdint>
#include <cstring>

#define U8G2_R0 0
#define U8G2_R1 1
#define U8G2_R2 2
#define U8G2_R3 3
#define U8X8_PIN_NONE 255

static const uint8_t u8g2_font_5x7_tr[1] = {0};
static const uint8_t u8g2_font_ncenB08_tr[1] = {0};

class U8G2Host {
 public:
  static const int W = 128;
  static const int H = 64;

  U8G2Host(int = 0, int = 0, int = 0, int = 0) { clearBuffer(); }

  // --- lifecycle ---
  bool begin() { return true; }
  void setBusClock(uint32_t) {}
  void setPowerSave(uint8_t on) { powerSave = on != 0; }
  void setFont(const uint8_t *) {}
  void drawStr(int, int, const char *) {}  // overlay text not previewed

  void clearBuffer() { std::memset(buf, 0, sizeof(buf)); }
  void sendBuffer();  // defined in preview.cpp: captures the frame

  void setDrawColor(uint8_t c) { color = c; }

  // --- primitives ---
  void drawPixel(int x, int y) { put(x, y); }

  void drawHLine(int x, int y, int w) {
    for (int i = 0; i < w; i++) put(x + i, y);
  }
  void drawVLine(int x, int y, int h) {
    for (int i = 0; i < h; i++) put(x, y + i);
  }
  void drawBox(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) put(x + i, y + j);
  }
  void drawFrame(int x, int y, int w, int h) {
    drawHLine(x, y, w);
    drawHLine(x, y + h - 1, w);
    drawVLine(x, y, h);
    drawVLine(x + w - 1, y, h);
  }
  void drawRBox(int x, int y, int w, int h, int r) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++)
        if (inRRect(x + i, y + j, x, y, w, h, r)) put(x + i, y + j);
  }
  void drawRFrame(int x, int y, int w, int h, int r) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) {
        const int px = x + i, py = y + j;
        if (inRRect(px, py, x, y, w, h, r) &&
            !inRRect(px, py, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0))
          put(px, py);
      }
  }
  void drawDisc(int cx, int cy, int r) {
    for (int dy = -r; dy <= r; dy++)
      for (int dx = -r; dx <= r; dx++)
        if (dx * dx + dy * dy <= r * r + r) put(cx + dx, cy + dy);
  }
  void drawCircle(int cx, int cy, int r) {
    for (int dy = -r - 1; dy <= r + 1; dy++)
      for (int dx = -r - 1; dx <= r + 1; dx++) {
        const int d = dx * dx + dy * dy;
        if (d <= r * r + r && d >= r * r - r) put(cx + dx, cy + dy);
      }
  }
  void drawLine(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    const int sx = dx >= 0 ? 1 : -1, sy = dy >= 0 ? 1 : -1;
    dx = dx >= 0 ? dx : -dx;
    dy = dy >= 0 ? dy : -dy;
    int err = dx - dy;
    for (;;) {
      put(x0, y0);
      if (x0 == x1 && y0 == y1) break;
      const int e2 = err * 2;
      if (e2 > -dy) {
        err -= dy;
        x0 += sx;
      }
      if (e2 < dx) {
        err += dx;
        y0 += sy;
      }
    }
  }
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2) {
    const int minx = min3(x0, x1, x2), maxx = max3(x0, x1, x2);
    const int miny = min3(y0, y1, y2), maxy = max3(y0, y1, y2);
    for (int py = miny; py <= maxy; py++)
      for (int px = minx; px <= maxx; px++) {
        const long a = cross(x0, y0, x1, y1, px, py);
        const long b = cross(x1, y1, x2, y2, px, py);
        const long c = cross(x2, y2, x0, y0, px, py);
        const bool neg = (a <= 0) && (b <= 0) && (c <= 0);
        const bool pos = (a >= 0) && (b >= 0) && (c >= 0);
        if (neg || pos) put(px, py);
      }
  }

  uint8_t buf[H][W];
  uint8_t color = 1;
  bool powerSave = false;

 private:
  void put(int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    buf[y][x] = color;
  }
  static long cross(int ax, int ay, int bx, int by, int px, int py) {
    return (long)(bx - ax) * (py - ay) - (long)(by - ay) * (px - ax);
  }
  static int min3(int a, int b, int c) {
    int m = a < b ? a : b;
    return m < c ? m : c;
  }
  static int max3(int a, int b, int c) {
    int m = a > b ? a : b;
    return m > c ? m : c;
  }
  // Pixel inside a rounded rectangle: corner circles sit r in from each edge.
  static bool inRRect(int px, int py, int x, int y, int w, int h, int r) {
    if (px < x || px >= x + w || py < y || py >= y + h) return false;
    if (r <= 0) return true;
    int dx = 0, dy = 0;
    if (px < x + r) dx = (x + r) - px;
    else if (px > x + w - 1 - r) dx = px - (x + w - 1 - r);
    if (py < y + r) dy = (y + r) - py;
    else if (py > y + h - 1 - r) dy = py - (y + h - 1 - r);
    return dx * dx + dy * dy <= r * r;
  }
};

typedef U8G2Host U8G2_SH1106_128X64_NONAME_F_HW_I2C;
typedef U8G2Host U8G2_SH1106_128X64_NONAME_F_SW_I2C;
