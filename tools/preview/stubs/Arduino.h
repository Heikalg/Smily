// Host stub of the Arduino core, just enough to build the Smily modules on a
// desktop. Only used by tools/preview - never compiled into the firmware.
#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1

// Virtual clock and virtual touch pad, driven by the preview program.
extern uint32_t hostMillis;
extern int hostTouchLevel;

inline uint32_t millis() { return hostMillis; }
inline void pinMode(int, int) {}
inline int digitalRead(int) { return hostTouchLevel; }
inline void digitalWrite(int, int) {}

struct HostSerial {
  bool verbose = true;
  void begin(unsigned long) {}
  void flush() {}
  void printf(const char *fmt, ...) {
    if (!verbose) return;
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
  void print(const char *s) {
    if (verbose) std::printf("%s", s);
  }
  void println(const char *s) {
    if (verbose) std::printf("%s\n", s);
  }
};
extern HostSerial Serial;
