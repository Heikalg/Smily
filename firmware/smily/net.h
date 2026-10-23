// ---------------------------------------------------------------------------
// Net - Wi-Fi + weather, done the Smily way: NON-BLOCKING.
//
// Everything here is driven by millis() timers and polled once per loop, so
// it never stalls the 15 FPS frame clock or breaks the no-delay() rule. The
// device connects to Wi-Fi in the background, then once a day-ish fetches the
// weather from Open-Meteo and caches it. Nothing here flushes the panel or
// touches state; the state machine reads net_weather() when it wants to draw.
//
// Build note: on the host preview (SMILY_TARGET_ESP32 == 0) this whole module
// is stubbed to a no-op so tools/preview keeps compiling without a radio.
// ---------------------------------------------------------------------------
#pragma once

#include <stdint.h>

enum NetStage : uint8_t {
  NET_IDLE = 0,     // nothing started yet
  NET_CONNECTING,   // WiFi.begin() issued, waiting for an IP
  NET_ONLINE,       // joined the network, have an IP
  NET_FETCHING,     // an HTTP request is in flight
  NET_FAILED        // gave up for now; will retry on NET_RETRY_MS
};

// Cached weather. Valid only when .have == true.
struct Weather {
  bool    have = false;     // have we ever got a good reading?
  float   tempC = 0.0f;     // temperature, Celsius
  int     code = -1;        // Open-Meteo WMO weather code (0=clear, 61=rain...)
  int     humidity = -1;    // relative humidity %, -1 if unknown (Open-Meteo)
  bool    isDay = true;     // sun up or down, drives sun-vs-moon icon
  int8_t  hour = -1;        // local hour 0..23 from forecast slot, -1 if unknown
  int8_t  minute = -1;      // local minute 0..59, -1 if unknown
  uint32_t ageMs = 0;       // how long ago (ms) this reading was taken
};

// Last fetch outcome, so the UI can show an error ONLY when something actually
// went wrong. `failed` is true after a fetch attempt that did not produce a
// usable reading; it is cleared the moment a fetch succeeds. `code` carries
// the reason for diagnosis / display:
//   > 0  : an HTTP status we did not accept (e.g. 500)
//   < 0  : an HTTPClient transport error (e.g. -11 = read timeout)
//   0    : a non-HTTP failure (empty body / parse error / no 'current')
struct NetError {
  bool failed = false;   // did the most recent fetch attempt fail?
  int  code = 0;         // why (see above); meaningful only when failed
};

// Call once in setup(), AFTER display/input/motion are up.
void net_begin(uint32_t now);

// Call every loop(). Advances the connect / fetch state machine a little.
// Returns the current stage so the caller can show a connecting face if it
// wants. Never blocks longer than one short socket timeout.
NetStage net_poll(uint32_t now);

// Latest cached weather (check .have before using it).
Weather net_weather(uint32_t now);

// Last fetch outcome. .failed is true only after a fetch that produced no
// usable reading, and is cleared on the next success -- so the UI can show an
// error state exactly when (and only when) something is actually wrong.
NetError net_error();

// Current stage without advancing anything.
NetStage net_stage();

// True once we have an IP address on the network.
bool net_online();
