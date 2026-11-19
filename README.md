# Smily firmware

Animated Dasai-Mochi-style face for the Smily keychain: ESP32-C3 SuperMini,
1.3" SH1106 OLED, TTP223 touch pad, MPU-6050, DHT11. Phases 1-4 of the spec are
implemented - power, animation engine, the full state machine, the shake
gesture, and the networked weather layer (Wi-Fi + Open-Meteo) with an on-board
DHT11 temperature/humidity sensor.

```
firmware/smily/     Arduino sketch, one file per module from spec section 4
docs/               locked pin map, behaviour decisions, power log template
tools/preview/      host harness: renders every frame to PNG, asserts behaviour
```

## Wiring

| Signal | GPIO |
| --- | --- |
| OLED + MPU SDA | 8 |
| OLED + MPU SCL | 9 |
| TTP223 OUT | 3 |
| DHT11 DATA | 4 |

Both I2C devices sit on the same bus (OLED `0x3C`, MPU `0x68`). The DHT11 is on
its own line (GPIO4) and must be powered from 3V3, not 5V. Full reasoning and
boot-time cautions in [docs/pinout.md](docs/pinout.md).

## Build

Libraries: **U8g2** by olikraus (display) and **ArduinoJson** by bblanchon
(weather parsing). The MPU is driven through `Wire` directly, so no sensor
library is needed; the DHT11 is also read directly.

Arduino IDE: board *ESP32C3 Dev Module*, **USB CDC On Boot: Enabled** (without
it the serial log never appears over USB), then open
`firmware/smily/smily.ino`.

Before the first networked build, copy `firmware/smily/secrets.example.h` to
`firmware/smily/secrets.h` and fill in your Wi-Fi SSID and password. `secrets.h`
is gitignored and never committed. Set your location in `config.h` (`SMILY_LAT`
/ `SMILY_LON`). To build the pure offline keychain, set `SMILY_NET_ENABLED 0`.

```sh
arduino-cli compile --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc firmware/smily
arduino-cli upload -p /dev/ttyACM0 --fqbn esp32:esp32:esp32c3:CDCOnBoot=cdc firmware/smily
```

## What it does

| State | Trigger | Animation |
| --- | --- | --- |
| WAKE | boot or touch wake | squashed pancake springs up, overshoots, blinks awake, 2 s |
| IDLE | default | slow breathing, random blink every 3-6 s (one in four is a double), occasional sideways glance, body leans with the accelerometer, a sparse ambient weather layer (stars/cloud/rain) drifts behind the face |
| PET | 1-2 taps | bouncing hop, `^ ^` eyes, blushing, hearts rising up both sides, 2 s |
| DIZZY | 3 rapid taps **or** a fast shake | body wobbles, swirl eyes spin, stars orbit the head, settles down, 2.3 s |
| WEATHER | long press (1 s) | full weather overview: small Smily + sky glyph + temperature, holds 8 s then eases back to IDLE |
| SLEEP | 100 s with no input | flattens, closes its eyes, three z's drift off, then the panel powers down and the chip deep-sleeps |

No fonts are used anywhere - the whole character is primitives, which keeps
flash small and leaves the art tunable from `config.h` (`MOCHI_W`, `MOCHI_H`,
`MOCHI_ROUND_PCT`, eye size and spacing).

Rendered frame-by-frame sheets live in `tools/preview/out/*.png`.

## Networking (Phase 4)

A non-blocking Wi-Fi + weather module (`net.cpp`) polled once per loop - it
never calls `delay()` and never stalls the 15 FPS face. It connects in the
background, then every 15 minutes fetches current conditions from Open-Meteo
over plain HTTP (keyless; the sky condition and local clock come from the API,
while the displayed temperature/humidity come from the on-board DHT11). The
reading is cached and survives deep sleep; the error face only shows after a
fetch fails with no cached reading.

Open-Meteo is used over plain HTTP on purpose: most weather APIs (including
BMKG) are now TLS-1.3-only, which the ESP32-C3 Arduino core cannot reliably
handshake.

### Phone-hotspot hardening

Phone hotspots (iOS especially) are fussy APs: they sleep the radio and their
NAT often refuses the first outbound TCP from a freshly-joined client, so a
fetch can fail with `GET -> -1` even on a strong link with working DNS. The
fetch path mitigates this with `Connection: close` + no socket reuse, a
`User-Agent`, HTTP/1.0, and a ~2 s gap between in-fetch retries so the NAT
table has time to prime. A real router needs none of this; it is free insurance
for portable use.

### Diagnostics

With `SMILY_SERIAL_LOG 1`, the net layer logs enough to tell every failure
apart at 115200 baud:

- **On connect:** IP, gateway, DNS and RSSI - so weak signal, a missing DNS and
  a bad gateway are each visible at a glance.
- **On a connect timeout:** the decoded `WiFi.status()` name plus the real
  low-level 802.11 disconnect reason (`AUTH_EXPIRE`, `AUTH_LEAVE`,
  `NO_AP_FOUND`, ...) captured from the Wi-Fi event - this is the actual "why",
  which `WiFi.status()` alone (always just `6 = disconnected`) never tells you.
- **Before each fetch:** an explicit DNS resolve, so a `GET -> -1` is split into
  "DNS failed / no internet" versus "DNS OK but the TCP connection was refused".

Common `WiFi.status()` codes: `1` = SSID not found (wrong name or 5 GHz - the
C3 is 2.4 GHz only), `4` = connect failed (usually the wrong password), `6` =
disconnected (associating, handshake not completed - transient on cold boot,
persistent on weak/congested signal or a sleeping hotspot radio).

## SH1106 panel note

This build targets an SH1106 clone that maps its columns the reverse way to the
u8g2 NONAME default, which stranded a white strip down the left edge. The fix is
a segment re-map command (`0xA1`) re-issued after `begin()`, gated by
`SMILY_SEG_REMAP` in `config.h` (default 1). Set it to 0 for a panel that does
not need it. `SMILY_EDGE_TEST 1` draws a static column ruler for diagnosing a
different panel.

## Tuning the shake

Build with `SMILY_MOTION_DEBUG 1` in `config.h` and watch the serial log while
you handle the keychain normally, then while you shake it. Set
`SHAKE_JERK_THRESHOLD` / `SHAKE_GYRO_DPS` into the gap between the two. The
detector needs three hard hits inside 700 ms, so a single knock is ignored by
construction - see [docs/behavior-decisions.md](docs/behavior-decisions.md).

`SMILY_MOTION_OVERLAY 1` prints the live numbers on the OLED instead, which is
handy when the board is off USB.

## Preview harness

```sh
tools/preview/build.sh
```

Compiles the real firmware modules against stub `Arduino.h` / `Wire.h` /
`U8g2lib.h` headers. The u8g2 stub is a genuine 1-bit rasteriser, so the PNGs
in `tools/preview/out/` are what the panel will show. It then drives the state
machine with synthetic taps and a synthetic MPU and asserts 25 behaviours,
including that no two flushes are ever closer than 66 ms, that a single knock
does not trigger DIZZY, and that nothing is pushed to the panel after it has
been put to sleep. The networking module is stubbed out on the host (no radio),
so the harness exercises art and logic, not Wi-Fi.

This is not a substitute for bench testing - timing, I2C behaviour, current
draw and Wi-Fi all need real hardware - but it catches art and logic mistakes in
a second instead of a reflash.

## Constraints held

- no `delay()` and no busy-wait in the frame path; every timer is a
  rollover-safe `millis()` subtraction (the one bounded exception is the
  weather HTTP GET, which only runs once every 15 minutes when a fetch is due)
- nothing reaches the panel faster than 66 ms, including across state
  transitions
- each frame is composed fully in the off-screen buffer and flushed in one
  transfer
- the panel is blanked and put into its low-power state, and the MPU is
  parked, before Deep Sleep

## Changed from the original sketch

1. **Hardware I2C instead of software I2C.** The MPU shares GPIO 8/9 with the
   display, and `U8G2_..._SW_I2C` bit-bangs those pins itself, so it cannot
   coexist with `Wire` on the same bus. It is also what makes the frame budget
   work: a full 1 KB frame plus page addressing is roughly 24 ms at 400 kHz,
   which fits inside 66 ms with room for a sensor read, where a bit-banged bus
   would have been close to the whole budget on its own. The FPS cap is
   unchanged - the spec keeps it regardless, and this build respects it.
   `SMILY_USE_SW_I2C 1` restores the old path for a board with no MPU.
2. **The MPU is parked before Deep Sleep.** An MPU-6050 running normally draws
   milliamps. Left awake it would dominate standby current and undo the whole
   power design, so `motion_sleep()` sets the sleep bit on the way down.
   `MOTION_GYRO_ENABLED 0` additionally stands the gyro by while awake, which
   saves most of the sensor's active current; the shake detector still works
   on accelerometer jerk alone (verified in the harness).
3. **Deep sleep now waits for the pad to be released.** Sleeping with a finger
   still on an active-HIGH wake pin wakes the chip straight back up.
4. **`STATE_PET` actually animates.** The original returned to IDLE on the
   next frame, so the pet face appeared for 66 ms.
5. **Tap window added**, so a single tap and a triple tap are distinguishable
   at all, and the touch that wakes the device is no longer counted as a pet.

## Not done

- Phase 1 current measurements - [docs/power-log.md](docs/power-log.md) has
  the table waiting; nothing here has been measured on hardware
- Phase 5: BLE and the mobile app
- a cloud/database logging layer (teacher requirement) - the natural path is a
  small HTTP/MQTT proxy the device publishes to, which also sidesteps the
  hotspot and TLS limits above
- the `Smily` vs `Smiley` naming decision (spec next-step 5) - this repo uses
  `Smily` throughout

