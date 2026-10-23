// ---------------------------------------------------------------------------
// Display Driver - owns the u8g2 object, the panel power state, and the one
// and only flush per frame. Nothing else in the firmware calls sendBuffer().
// ---------------------------------------------------------------------------
#pragma once

#include <U8g2lib.h>

#include "config.h"

#if SMILY_USE_SW_I2C
extern U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2;
#else
extern U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2;
#endif

void display_begin();

// Push the composed off-screen buffer in a single transfer (spec 2.2).
void display_flush();

// Blank the panel RAM, then put the SH1106 into its low-power state.
// Called before Deep Sleep so nothing flashes on the next wake (spec 2.3).
void display_sleep();

#if SMILY_EDGE_TEST
// Diagnostic: draw a static edge-test pattern (see display.cpp).
void display_edge_test();
#endif
