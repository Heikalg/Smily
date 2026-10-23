#include "display.h"

#include <Arduino.h>
#include <Wire.h>

#include "util.h"

#if SMILY_USE_SW_I2C
// Software I2C cannot share the bus with the MPU - kept only as a fallback
// for bring-up on a board without the motion sensor fitted.
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, PIN_I2C_SCL, PIN_I2C_SDA,
                                        U8X8_PIN_NONE);
#else
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_I2C_SCL,
                                        PIN_I2C_SDA);
#endif

#if SMILY_SEG_REMAP && SMILY_TARGET_ESP32
// Zero the ENTIRE panel RAM: all 132 columns x 8 pages. u8g2's clearDisplay()
// only covers the 128 columns it owns, leaving the SH1106 clone's extra edge
// columns lit with stale GDDRAM (the constant left-wall strip). Writing 132
// bytes of 0x00 per page reaches those columns too. One-shot at boot.
static void blankPanelEdges() {
  u8x8_t *u8x8 = u8g2.getU8x8();
  static uint8_t zeros[132] = {0};         // 132 bytes of 0x00 (full panel width)
  for (uint8_t page = 0; page < 8; page++) {
    u8x8_cad_StartTransfer(u8x8);
    u8x8_cad_SendCmd(u8x8, 0x0B0 | page);  // set page address
    u8x8_cad_SendCmd(u8x8, 0x000);         // lower column = 0
    u8x8_cad_SendCmd(u8x8, 0x010);         // upper column = 0
    u8x8_cad_SendData(u8x8, 132, zeros);   // zero all 132 columns in this page
    u8x8_cad_EndTransfer(u8x8);
  }
}
#endif

void display_begin() {
  // Bring Wire up ourselves so the MPU can use the same bus straight away,
  // regardless of what u8g2 does internally.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_BUS_HZ);

  u8g2.setBusClock(I2C_BUS_HZ);  // must precede begin() to take effect
  u8g2.begin();

  // This SH1106 clone folds its left columns (edge-test showed the left ruler
  // collapsing to a block and the left frame missing) -- its segment mapping
  // runs the opposite way to what the NONAME driver assumes. Re-issue the
  // SH1106 "segment re-map" command directly: 0xA1 maps column 131->SEG0
  // (reversed), 0xA0 is the default. This fixes the horizontal mapping WITHOUT
  // the vertical flip that setFlipMode() would also apply, and without swapping
  // to the VCOMH0/WINSTAR constructors that previously froze this panel.
  // COM scan direction (0xC8/0xC0) is left as the driver set it.
#if SMILY_SEG_REMAP && SMILY_TARGET_ESP32
  u8x8_t *u8x8 = u8g2.getU8x8();
  u8x8_cad_StartTransfer(u8x8);
  u8x8_cad_SendCmd(u8x8, 0x0A1);   // segment re-map: column address 131 -> SEG0
  u8x8_cad_EndTransfer(u8x8);
#endif

  // Wipe ALL 132 columns of panel RAM *after* the re-map, so the few columns
  // sendBuffer() never writes (the 132-vs-128 difference) are zeroed in the
  // orientation the panel now uses. Done after the re-map on purpose: clearing
  // before it would wipe the OLD orientation and leave the now-left-side
  // untouched columns showing garbage -- that was the leftover left-wall strip.
  u8g2.clearDisplay();

#if SMILY_SEG_REMAP && SMILY_TARGET_ESP32
  // u8g2.clearDisplay() still only spans the 128 columns it owns, so the 2-4
  // edge columns the panel has beyond that stay lit with stale GDDRAM -- the
  // constant left-wall strip that never changes with animation. Zero the FULL
  // 132-column x 8-page RAM directly through u8x8. One-shot: those columns are
  // never written again, so this need not run per frame.
  blankPanelEdges();
#endif

#if SMILY_COL_OFFSET
  // Optional manual column shift for a clone whose visible area does not start
  // at the driver's column 0. Try 0 / 2 / 4 if a faint edge strip remains.
  u8g2.getU8x8()->x_offset = SMILY_COL_OFFSET;
#endif

  u8g2.setPowerSave(0);
  u8g2.setDrawColor(1);
  u8g2.clearBuffer();
  u8g2.sendBuffer();

#if SMILY_EDGE_TEST
  display_edge_test();
#endif
}

#if SMILY_EDGE_TEST
// Static diagnostic. Draws a full-screen frame plus single-pixel vertical lines
// at columns 0,1,2,3 (left) and 124,125,126,127 (right), with a 4px gap between
// each so they are countable, and a cross in the middle. Photograph the panel:
// - a lit column with NO line under it = a column the panel shows but u8g2 is
//   not addressing (the 132-vs-128 offset) -> fix is the right constructor.
// - the leftmost drawn line (col 0) sitting away from the glass edge = visible
//   area starts right of column 0 -> a column offset is needed.
// This holds forever; the main loop renders nothing while SMILY_EDGE_TEST=1.
void display_edge_test() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  // outer frame: shows all four true edges of the 128x64 buffer
  u8g2.drawFrame(0, 0, 128, 64);
  // RIGHT edge ruler: one vertical line per column at x = 127,126,125,...,120.
  // Count how many of these 8 you can actually SEE on the glass. The number
  // MISSING on the right = how many pixels the window is shifted = the offset
  // we must apply. (You saw only 2 of 4 before, so expect ~2-3 missing here.)
  for (int8_t i = 0; i < 8; i++) u8g2.drawVLine(127 - i, 10, 44);
  // LEFT edge ruler: same, x = 0,1,2,...,7, so you can see if the garbage strip
  // sits to the LEFT of column 0 (it will, if the window is shifted right).
  for (int8_t i = 0; i < 8; i++) u8g2.drawVLine(i, 10, 20);
  // centre cross for reference
  u8g2.drawHLine(56, 32, 16);
  u8g2.drawVLine(64, 24, 16);
  u8g2.sendBuffer();
}
#endif

void display_flush() { u8g2.sendBuffer(); }

void display_sleep() {
  u8g2.clearBuffer();
  u8g2.sendBuffer();       // leave the panel RAM blank
  u8g2.setPowerSave(1);    // SH1106 display-off / low-power state
}
