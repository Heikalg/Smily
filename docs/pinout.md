# Pin map (locked)

ESP32-C3 SuperMini. This is the table the spec's "Immediate Next Steps" item 1
asks for; everything in `firmware/smily/config.h` follows it.

| Signal | GPIO | Notes |
| --- | --- | --- |
| OLED SDA + MPU SDA | 8 | shared I2C bus, 400 kHz |
| OLED SCL + MPU SCL | 9 | shared I2C bus |
| TTP223 OUT | 3 | Deep Sleep wake source, active HIGH, momentary |
| MPU INT | — | not connected (see "wake on shake" below) |

I2C addresses: SH1106 `0x3C`, MPU `0x68` (AD0 low). No conflict, one bus.

## Why GPIO 3 for the touch pad

The wake pin has to be RTC-capable, which on the C3 means GPIO 0-5. Within
that range:

- GPIO 2 is a strapping pin
- GPIO 4-7 are the JTAG pins (MTMS/MTDI/MTCK/MTDO)
- GPIO 0, 1 are free but usually wanted for analog
- **GPIO 3 is free, RTC-capable, and not strapping** - the right choice

## Boot-time caution on GPIO 8/9

Both are strapping pins on the C3: GPIO 9 selects boot mode (it is the BOOT
button on most SuperMini boards) and GPIO 8 must not be held low at reset.
I2C idles high through its pull-ups, so a healthy bus boots fine. If the board
ever refuses to boot or drops into download mode, check for a short or a
missing pull-up holding either line low at reset.

## TTP223 jumper

Firmware assumes **momentary, active-HIGH** (`TOUCH_ACTIVE_LEVEL` in
`config.h`). Confirm the A/B pads on the module before blaming the firmware:
in toggle mode the output latches and every other tap looks like a release.

## Wake on shake (not implemented)

The MPU can wake the chip too, via its motion-detect interrupt into an
RTC-capable pin - GPIO 0, 1 or 5 are still free. It needs:

1. the MPU left in low-power accelerometer-only cycle mode instead of the
   SLEEP bit, configured through `MOT_THR` (0x1F) and `MOT_DUR` (0x20) with
   the motion interrupt enabled in `INT_ENABLE` (0x38)
2. a second wake source added in `power_deepSleep()`
3. a fresh standby-current measurement, because the sensor is then awake all
   the time instead of parked

The register path differs between the MPU-6050 and the MPU-6500/9250, so it
needs testing against whichever chip is actually fitted. Left out for now: the
spec makes the TTP223 the sole wake source, and keeping it that way is what
protects the standby figure.
