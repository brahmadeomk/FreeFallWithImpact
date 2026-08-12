# Hardware validation log

Measurements taken from a real unit over Modbus, one row per firmware
build. Numbers are register values as read, not simulations. This exists
because most of the changes in rev G/H were justified by static analysis
of compiled AVR output, and static analysis is a hypothesis until a unit
confirms it.

## Timing and rate

| Register | Rev F | Rev G (isqrt32 fix) | Rev H (roots out of ISR) |
|---|---:|---:|---:|
| 19 — max ISR time (µs) | **1203** | **517** | **153–157** |
| 20 — max loop time (×100 µs) | 65535 (saturated) | 1260 | 1342 |
| 26 — blocks missed | 2742 | 3568 | 12830 |
| 27 — measured sample rate | 1650 | 1589 | 1575 |

**Register 19** is the headline. The sample period is ~635 µs at the
measured ODR, so rev F's ISR overran by 568 µs on every block boundary;
rev H uses 24% of the period. The two steps were the `isqrt32()`
shift-by-30 fix (1203 → 517) and moving the square roots out of the ISR
entirely (517 → ~155). Rev H came in well under the ~290 µs predicted,
which means the non-root block work was cheaper than estimated.

The rev H figure is confirmed twice over: **157 µs** as a since-boot
maximum, and **153 µs** as a maximum over a ~5 hour run following a
`CLEAR_DIAG`. The second is the one that matters — it is a steady-state
worst case across ~28 million samples, not a short sample. The ISR has
~480 µs of headroom in every sample period.

**Register 27 falling from 1650 to 1589 was the corroboration**, not a
regression. Rev F's ISR ran longer than Timer0's 1.024 ms overflow
period, so `millis()` lost ticks and every rate derived from it read
high. Once the ISR fitted inside that period the clock became honest.
1575–1589 is the true ODR, within ADXL345 tolerance of nominal 1600.

**Register 20 and register 26 are bandwidth, not compute.** 1342 = 134 ms
is a full 49-register read at 9600 baud, and missed blocks are the
arithmetic consequence of blocks arriving every 10 ms while the line is
busy. Neither improves by making the firmware faster.

How much that matters depends entirely on poll rate, and the soak
measured it: 36532 blocks missed over 4 h 48 min is **2.1 per second
against 100 produced per second — about 2%**. At ~12 blocks lost per
full-map read that works out to one poll every ~6 seconds, which is what
the deployed dashboard actually does.

This corrects an earlier assumption in this file's own analysis. At a
4 Hz poll the line would be ~50% occupied and split polling or a higher
baud rate would be worth doing; at one poll per 6 seconds the line is
~2% occupied and neither is needed. Raise the baud rate if you want a
faster dashboard, not because the device is struggling.

## Rev H feature checks

| Check | Expected | Observed | |
|---|---|---|---|
| reg 42 — firmware version | 264 (0x0108) | 264 | pass |
| reg 43 — map version | 8 | 8 | pass |
| reg 44 — build date | build day | 13569 = 2026-08-01 | pass |
| reg 35 — reserved | 0 | 0 | pass |
| reg 48 — reserved | 0 | 0 | pass |
| reg 37 — read-to-clear | 0 after being read | 0, with 17 trips recorded | pass |
| regs 38–41 — 1 s RMS vector | √(3²+7²+7²) ≈ 10.3 | 11 | pass |

Registers 38–41 agreeing with their own vector sum confirms on hardware
that `msX + msY + msZ` is the right identity — the change that let the
square roots leave the ISR.

## Command path — validated on hardware

`CLEAR_TRIPCOUNT` (code 2) written to register 28, read back after:

| Register | Observed | Meaning |
|---|---:|---|
| 31 — trip count | **0** | was 17; the command ran |
| 45 — last command echo | **2** | the code the device received |
| 46 — command status | **1** | accepted and executed |
| 47 — command count | **1** | one accepted command since boot |
| 30 / 36 — peak holds | **unchanged** | `CLEAR_TRIPCOUNT` must not touch these |

This is the answer to the original report that started this work —
"the command register does not change value when written from the Pi".
Register 28 self-clears by design and always reads back 0, so a read-back
can never confirm anything. Registers 45–47 are how you confirm it, and
they now do so on hardware: the code that was sent, the fact it was
recognised, and a counter that moves even when the same command is issued
twice in a row.

Registers 30 and 36 staying at their previous values is worth noting
separately: it shows the command switch acts on the specific code rather
than clearing everything, so `CLEAR_TRIPCOUNT` does not silently discard
peak-hold history.

## CLEAR_DIAG — validated, and it exposes a design wart

`CLEAR_DIAG` (code 3) issued. Observed immediately after:

| Register | Before | After | |
|---|---:|---:|---|
| 26 — blocks missed | 12830 | **110** | cleared, then climbing again within seconds |
| 20 — max loop time | 1342 | **1340** | cleared, then refilled by the very next full read |
| 30 / 36 — peak holds | 12075 / 26643 | **unchanged** | correct: `CLEAR_DIAG` must not touch these |
| 31 — trip count | 0 | **0** | correct: that is `CLEAR_TRIPCOUNT`'s job |

Scoping is right — each command touches only its own registers, now
confirmed for two different codes.

**But register 25 read 4 again within seconds of the clear.** Bit 2 is
set whenever `blockMissed > 0`, and blocks are missed on every poll by
construction: they arrive every 10 ms while a full read holds the line
for 134 ms. So bit 2 re-latches almost immediately after any clear and
stays set for ever.

On a commercial dashboard that is a permanent fault indicator for
behaviour that is entirely normal. It is the same class of problem as the
sustained-RMS trip removed in rev H: a signal that looks like an alarm
but is not one. Recommend dropping bit 2 from the status word in the next
map revision and leaving register 26 as the diagnostic counter it already
is.

**EEPROM survived the rev G → H struct change.** Register 25 bit 3
(config defaulted) is clear, so `loadConfig()` matched the magic and
passed the range checks rather than falling back to defaults. Rev H
removed the `rmsThresholdMg` field at EEPROM offsets 7–8 but left the
magic, slave ID and threshold offsets alone, so deployed units keep their
configuration across the upgrade.

## Idle soak — PEAK_CONFIRM = 1 passes

The last open risk from rev H. Register 31 was cleared to 0 at 10:21 and
read again at 15:09.

| | |
|---|---|
| Elapsed | 4 h 48 min |
| Samples at 1592 Hz | **~27.5 million** |
| Register 31 — trip count | **2** |
| Deliberate test strikes in that window | **2** |
| **Spurious trips** | **0** |

`PEAK_CONFIRM = 1` removed the consecutive-sample confirmation, so a
single corrupted sample could in principle open the interlock. Across
~27.5 million samples on a real machine that did not happen once. The
change is validated: detection latency and the shortest detectable strike
are both 0.63 ms, and nothing was paid for it.

Sensor noise was never the threat — the ADXL345 at 800 Hz bandwidth is
~10–15 mg RMS per axis against a 3000 mg threshold, roughly 200 sigma.
The open question was SPI read corruption, which is not Gaussian and only
has to happen once. A ~27.5 million sample run with zero false trips
bounds that risk at the level this product operates.

**The gravity vector was also stable across the soak**, which is a
quieter but useful result: 1083 mg at 10:21 and 1076 mg at 15:09, with
per-axis values moving by single millig. The DC tracker is not drifting
and the mount is not moving.

## Open items from the same reading

**Registers 33 and 34 were displayed unsigned.** Raw 65009 and 64677 are
−527 mg and −859 mg. Decoded correctly the gravity vector is

    √(397² + 527² + 859²) = 1083 mg

which is gravity, 8% high — consistent with ADXL345 gain and offset
tolerance plus mounting. Displayed unsigned it looks like a 64 g fault.
This is a master-side decode bug, not a firmware fault: regs 32–34 are
documented SIGNED. Nothing else in the map is.

**Register 36 read 26643 mg, which means the part clipped.** The ADXL345
is configured for ±16 g per axis, so the largest representable vector
magnitude is √3 × 16 g = 27.7 g. A reading of 26.6 g means at least two
axes were at or near full scale at the same time. Impact magnitudes in
this region are **lower bounds** — the real strike was harder than the
number says. Anything approaching 27 g on reg 36 or reg 37 should be
read as "off the top of the scale", not as a measurement.

**Register 31** has been cleared and re-soaked — see the soak section
above. Resolved.

## Still to do

Rev H has no open validation risks. What remains is master-side or
cosmetic.

1. **Fix the signed decode** for registers 32–34 in Node-RED. Currently
   displayed unsigned, which makes a correct ~1080 mg gravity vector look
   like a 64 g fault.
2. **Status bit 2**, next map revision. It re-latches within seconds of
   any clear and is a permanent fault light for normal polling.
3. Optional, and lower priority than previously stated: the nine cm/s²
   duplicate registers (7–18) and a higher baud rate. At the measured
   poll rate the line is ~2% occupied, so these buy dashboard
   responsiveness, not device headroom.

## The cm/s² registers are exact duplicates — measured

From one reading, the negative-peak set:

| Axis | mg (regs 13–15) | cm/s² (regs 16–18) | mg × 0.980665 |
|---|---:|---:|---:|
| X | 27 | 26 | 26.5 |
| Y | 19 | 19 | 18.6 |
| Z | 15 | 15 | 14.7 |

Nine registers carrying a fixed multiply of nine others, to within
rounding. This is the redundancy noted in the map-cleanup
recommendation, confirmed against live data rather than argued from the
source. Dropping them costs nothing but a master-side multiply.
