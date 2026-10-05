# Power log

Nothing here is measured yet. Phase 1 exit criteria require the standby figure
on the assembled unit, not datasheet numbers, so the table is deliberately
empty rather than filled with estimates.

Measure at the battery, on the assembled board, with USB disconnected.

| Condition | Current | Notes |
| --- | --- | --- |
| Deep Sleep, MPU parked | | the number that decides battery life |
| Deep Sleep, MPU left awake | | shows what `motion_sleep()` is worth |
| IDLE, animating, MPU running | | |
| IDLE, animating, `MOTION_GYRO_ENABLED 0` | | gyro standby saving |
| WAKE / PET / DIZZY peak | | OLED lit pixels dominate |
| Filled mochi vs `SMILY_MOCHI_OUTLINE 1` | | outline lights far fewer pixels |

Battery: 3.7 V 550 mAh LiPo.

Estimate to fill in once the first two rows exist:

```
hours = 550 / (standby_mA + active_mA * (active_seconds_per_hour / 3600))
```

Things worth checking while the meter is already attached:

- quiescent draw of the board LDO, the TP4056 and the TTP223, which the spec
  flags as the real risk - the chip's own deep sleep current is tiny next to
  the three of them
- whether the SH1106 module keeps drawing after `setPowerSave(1)`; if it does,
  a load switch on its rail is the fix
- whether the I2C pull-ups leak through either sleeping device
