// ---------------------------------------------------------------------------
// Smily - secrets TEMPLATE.
//
// HOW TO USE:
//   1. Copy this file to  secrets.h  (same folder).
//   2. Fill in your Wi-Fi SSID and password below.
//   3. secrets.h is gitignored, so your real credentials never hit GitHub.
//      This template (secrets.example.h) IS committed, as documentation.
//
// Open-Meteo (the weather API) needs no key, so there is nothing else to
// fill in yet. If you later swap in an API that needs a key, add it here.
// ---------------------------------------------------------------------------
#pragma once

// Your home / phone-hotspot Wi-Fi. The ESP32-C3 is 2.4 GHz only -- it cannot
// join a 5 GHz network, so pick the 2.4 GHz SSID if your router has both.
#define SMILY_WIFI_SSID      "YOUR_WIFI_SSID"
#define SMILY_WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"
