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

void display_begin() {
  // Bring Wire up ourselves so the MPU can use the same bus straight away,
  // regardless of what u8g2 does internally.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_BUS_HZ);

  u8g2.setBusClock(I2C_BUS_HZ);  // must precede begin() to take effect
  u8g2.begin();
  u8g2.setPowerSave(0);
  u8g2.setDrawColor(1);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

void display_flush() { u8g2.sendBuffer(); }

void display_sleep() {
  u8g2.clearBuffer();
  u8g2.sendBuffer();       // leave the panel RAM blank
  u8g2.setPowerSave(1);    // SH1106 display-off / low-power state
}
