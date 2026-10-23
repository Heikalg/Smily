// ---------------------------------------------------------------------------
// Net - implementation. Non-blocking Wi-Fi + Open-Meteo weather.
//
// Shape of it: a tiny state machine polled once per loop(). It never calls
// delay() and never spins waiting for the network -- each poll does at most a
// little work (check if connected yet, kick off one HTTP GET, read the reply)
// and returns immediately. That keeps the 15 FPS face animating the whole
// time the radio is doing its thing.
//
//   NET_IDLE -> NET_CONNECTING -> NET_ONLINE -> (every 15 min) NET_FETCHING
//                                     ^                              |
//                                     +------- cache weather --------+
//
// On the host preview build the radio does not exist, so the whole file is a
// stub that reports NET_IDLE and no weather. All real logic is ESP32-only.
// ---------------------------------------------------------------------------
#include "net.h"

#include "config.h"
#include "util.h"

#if SMILY_TARGET_ESP32 && SMILY_NET_ENABLED

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>

#include "secrets.h"

static NetStage  s_stage      = NET_IDLE;
static Weather   s_weather;                 // cached reading
static NetError  s_err;                     // last fetch outcome
static uint32_t  s_weatherAtMs = 0;         // millis() when it was taken
static uint32_t  s_connectStartMs = 0;
static uint32_t  s_nextFetchMs = 0;
static uint32_t  s_retryAtMs   = 0;

// Record why a fetch failed, so the UI can show an error only on a real
// failure. HTTP status (>0), transport error (<0, e.g. -11), or 0 for a
// non-HTTP failure (empty body / parse / missing field).
static bool fetchFailed(int code) {
  s_err.failed = true;
  s_err.code = code;
  return false;
}

// Build the Open-Meteo URL for our fixed location. Plain HTTP on purpose:
// Open-Meteo still serves http:// (most weather APIs are now TLS-1.3-only,
// which the ESP32-C3 cannot reliably handshake -- BMKG failed exactly there
// with -1). "current" asks for live conditions; "timezone=auto" makes
// current.time the location's LOCAL time (free clock, no NTP/RTC). No key.
static String weatherUrl() {
  String u = F("http://api.open-meteo.com/v1/forecast?latitude=");
  u += SMILY_LAT;
  u += F("&longitude=");
  u += SMILY_LON;
  u += F("&current=temperature_2m,relative_humidity_2m,weather_code,is_day&timezone=auto");
  return u;
}

// Outcome of a single GET attempt, so the retry loop can tell a transient
// stall (worth retrying) from a definitive HTTP error (not worth retrying).
enum FetchResult {
  FETCH_OK,       // parsed a good reading; s_weather is updated
  FETCH_RETRY,    // transient: timeout / empty / truncated body -- retry helps
  FETCH_HARD,     // definitive HTTP error (e.g. 404/429) -- retrying won't help
};

// One blocking-but-bounded HTTP GET + parse. Bounded by NET_HTTP_TIMEOUT_MS, so
// the worst case is a few seconds once every 15 minutes -- acceptable, and only
// ever reached when a fetch is actually due. Does NOT set the error flag; the
// caller (fetchWeather) decides when to surface failure after exhausting retries.
static FetchResult fetchWeatherOnce(uint32_t now, int *outCode) {
  HTTPClient http;
  http.setConnectTimeout(NET_HTTP_TIMEOUT_MS);
  http.setTimeout(NET_HTTP_TIMEOUT_MS);

  if (!http.begin(weatherUrl())) {
    SMILY_LOG("[net] http.begin failed\n");
    *outCode = 0;
    return FETCH_RETRY;            // local setup hiccup: a retry may succeed
  }

  const int rc = http.GET();
  *outCode = rc;
  if (rc != 200) {
    SMILY_LOG("[net] weather GET -> %d\n", rc);
    http.end();
    // rc < 0 is a transport error (e.g. -11 read timeout) -- transient, retry.
    // rc > 0 is a real HTTP status (4xx/5xx) -- definitive, don't hammer it.
    return (rc < 0) ? FETCH_RETRY : FETCH_HARD;
  }

  // Read the WHOLE body into a string first, then parse. Parsing straight off
  // http.getStream() can hand the parser a half-arrived body when the socket
  // stalls (the same flakiness behind the -11 read-timeouts) -- that produced
  // an incomplete JSON with no 'current' but no hard parse error. Buffering
  // the body means a truncated download fails cleanly here instead.
  const String body = http.getString();
  http.end();

  if (body.length() == 0) {
    SMILY_LOG("[net] empty body\n");
    *outCode = 0;
    return FETCH_RETRY;           // truncated/stalled download -- retry helps
  }

  // Pull just the fields we need out of the JSON. A filter keeps the parser
  // tiny -- we never allocate room for the parts of the reply we ignore.
  JsonDocument filter;
  filter["current"]["temperature_2m"]     = true;
  filter["current"]["relative_humidity_2m"] = true;
  filter["current"]["weather_code"]       = true;
  filter["current"]["is_day"]             = true;
  filter["current"]["time"]               = true;

  JsonDocument doc;
  const DeserializationError err =
      deserializeJson(doc, body, DeserializationOption::Filter(filter));

  if (err) {
    SMILY_LOG("[net] json parse: %s\n", err.c_str());
    *outCode = 0;
    return FETCH_RETRY;           // half-arrived body parses dirty -- retry helps
  }

  JsonObject cur = doc["current"];
  if (cur.isNull()) {
    SMILY_LOG("[net] json had no 'current' (body: %s)\n", body.c_str());
    *outCode = 0;
    return FETCH_RETRY;           // incomplete reply -- retry helps
  }

  s_weather.tempC    = cur["temperature_2m"]       | 0.0f;
  s_weather.code     = cur["weather_code"]         | -1;   // WMO; see sky_from_code()
  s_weather.humidity = cur["relative_humidity_2m"] | -1;
  s_weather.isDay    = (cur["is_day"] | 1) != 0;

  // current.time is ISO local "YYYY-MM-DDTHH:MM" (timezone=auto). Pull HH:MM
  // straight out by character offset -- cheap and robust, no date parser.
  s_weather.hour = -1;
  s_weather.minute = -1;
  const char *t = cur["time"] | "";
  if (t && strlen(t) >= 16 && t[10] == 'T' && t[13] == ':') {
    const int hh = (t[11] - '0') * 10 + (t[12] - '0');
    const int mm = (t[14] - '0') * 10 + (t[15] - '0');
    if (hh >= 0 && hh <= 23 && mm >= 0 && mm <= 59) {
      s_weather.hour = (int8_t)hh;
      s_weather.minute = (int8_t)mm;
    }
  }

  s_weather.have  = true;
  s_weatherAtMs   = now;

  SMILY_LOG("[net] weather: %.1f C, %d%%, code %d, %s, %02d:%02d\n",
            s_weather.tempC, s_weather.humidity, s_weather.code,
            s_weather.isDay ? "day" : "night", s_weather.hour, s_weather.minute);
  return FETCH_OK;
}

// Fetch with a small in-place retry. A single stalled read (-11) or a
// truncated body is common on flaky Wi-Fi and usually clears on the very next
// attempt, so we try up to NET_FETCH_ATTEMPTS times with a short gap before we
// ever show the error face. A definitive HTTP status (4xx/5xx) is NOT retried.
// Only the final failure is surfaced via fetchFailed(); a success clears any
// prior error so the error face never lingers after recovery.
static bool fetchWeather(uint32_t now) {
  int code = 0;
  for (int attempt = 1; attempt <= NET_FETCH_ATTEMPTS; ++attempt) {
    const FetchResult r = fetchWeatherOnce(now, &code);
    if (r == FETCH_OK) {
      s_err.failed = false;       // a good reading clears any prior error
      s_err.code   = 0;
      return true;
    }
    if (r == FETCH_HARD) break;   // real HTTP error -- retrying won't help
    if (attempt < NET_FETCH_ATTEMPTS) {
      SMILY_LOG("[net] fetch attempt %d/%d failed (code %d), retrying\n",
                attempt, NET_FETCH_ATTEMPTS, code);
      delay(NET_FETCH_RETRY_GAP_MS);
    }
  }
  return fetchFailed(code);       // all attempts exhausted: now show the error
}

void net_begin(uint32_t now) {
  // Full, clean radio bring-up. On the ESP32-C3 a stale association from a
  // previous boot (common during rapid reflash cycles) makes the next connect
  // fail with shifting status codes (0/4/6). Clearing persistent state and
  // disconnecting the radio first gives every attempt the same clean start.
  WiFi.persistent(false);       // don't reuse flash-saved connection state
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);        // drop any half-held association, radio off
  WiFi.mode(WIFI_STA);          // radio back on, fresh
  WiFi.setAutoReconnect(true);
  WiFi.begin(SMILY_WIFI_SSID, SMILY_WIFI_PASSWORD);
  s_stage = NET_CONNECTING;
  s_connectStartMs = now;
  SMILY_LOG("[net] connecting to \"%s\"...\n", SMILY_WIFI_SSID);
}

NetStage net_poll(uint32_t now) {
  switch (s_stage) {
    case NET_IDLE:
      break;

    case NET_CONNECTING:
      if (WiFi.status() == WL_CONNECTED) {
        s_stage = NET_ONLINE;
        s_nextFetchMs = now;  // fetch immediately on first connect
        SMILY_LOG("[net] online, IP %s\n", WiFi.localIP().toString().c_str());
      } else if (elapsed(now, s_connectStartMs) >= NET_CONNECT_TIMEOUT_MS) {
        s_stage = NET_FAILED;
        s_retryAtMs = now + NET_RETRY_MS;
        // Status code names WHY it failed: 1=SSID not found (name wrong or
        // hotspot asleep), 4=connect failed (usually wrong password),
        // 6=disconnected. 3 would be WL_CONNECTED (success).
        SMILY_LOG("[net] connect timed out (status %d), retry in %lus\n",
                  (int)WiFi.status(), (unsigned long)(NET_RETRY_MS / 1000));
      }
      break;

    case NET_ONLINE:
      // Dropped off the network? Fall back to reconnecting.
      if (WiFi.status() != WL_CONNECTED) {
        s_stage = NET_FAILED;
        s_retryAtMs = now + NET_RETRY_MS;
        break;
      }
      // Time for a refresh?
      if ((int32_t)(now - s_nextFetchMs) >= 0) {
        s_stage = NET_FETCHING;
      }
      break;

    case NET_FETCHING: {
      const bool ok = fetchWeather(now);
      // Whether or not it worked, schedule the next refresh and go back online.
      s_nextFetchMs = now + NET_WEATHER_INTERVAL_MS;
      s_stage = (WiFi.status() == WL_CONNECTED) ? NET_ONLINE : NET_FAILED;
      if (!ok && s_stage == NET_FAILED) s_retryAtMs = now + NET_RETRY_MS;
      break;
    }

    case NET_FAILED:
      if ((int32_t)(now - s_retryAtMs) >= 0) {
        // Tear the previous attempt down cleanly before starting a new one.
        // disconnect(true) also turns the radio off; begin() turns it back on
        // with a fresh config, which avoids the "sta is connecting, cannot set
        // config" error that a back-to-back disconnect+begin provokes.
        WiFi.disconnect(true);
        WiFi.mode(WIFI_STA);
        WiFi.begin(SMILY_WIFI_SSID, SMILY_WIFI_PASSWORD);
        s_stage = NET_CONNECTING;
        s_connectStartMs = now;
        SMILY_LOG("[net] retrying connect...\n");
      }
      break;
  }
  return s_stage;
}

Weather net_weather(uint32_t now) {
  Weather w = s_weather;
  w.ageMs = w.have ? elapsed(now, s_weatherAtMs) : 0;
  return w;
}

NetError net_error() { return s_err; }

NetStage net_stage() { return s_stage; }

bool net_online() { return s_stage == NET_ONLINE || s_stage == NET_FETCHING; }

#else  // ---- host preview build, or networking compiled out -----------------

void      net_begin(uint32_t)        {}
NetStage  net_poll(uint32_t)         { return NET_IDLE; }
Weather   net_weather(uint32_t)      { return Weather(); }
NetError  net_error()                { return NetError(); }
NetStage  net_stage()                { return NET_IDLE; }
bool      net_online()               { return false; }

#endif
