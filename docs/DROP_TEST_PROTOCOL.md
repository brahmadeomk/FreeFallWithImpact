# Drop test protocol — CTX311 rev A

**Mostly a template.** The tables are blank on purpose — filling them in
is T-10's job, done at the rig with a real unit. Do not populate them
from simulation, from the host tests, or from expectation.

**One field result IS recorded**, from the first installation — see
"Field result 1" below. It settled one question and left two open.

This mirrors `HARDWARE_VALIDATION.md`: measurements taken from a real
unit over Modbus, one row per drop. A claim is a hypothesis until a unit
confirms it.

## Quick bench check — is detection working at all?

Five minutes, no rig. This proves the detector fires and reports; it
tunes nothing and proves nothing about the arrest device. The full
protocol below is what tuning needs.

**You do not need a real fall.** The default threshold (register 50) is
850 mg held for 25 ms (register 52) — that is "lost about 15 % of
support for 40 samples", not free fall. Accelerating the sensor downward
by hand, or dropping it 20–30 cm onto a cushion, is enough.

**Safety:** do this with nothing connected to PC1, or with a bench lamp
or meter on it. Not a real arrest device — that needs H-01 closed first.

### The loop

1. **Check it is armed.** Register 49 **bit 6 must be clear** and
   register 61 must be **0**. For the first ~2 s after power-up bit 6 is
   set, detection is suppressed, and a drop in that window proves
   nothing. Register 60 should read ~1000 mg at rest — that is gravity,
   and it is the quickest proof the sensor is alive.

2. **Note register 54** (LOS trip count) before you start.

3. **Drop it**, or accelerate it downward by hand.

4. **Read back:**

   | Register | Expect | Meaning |
   |---|---|---|
   | 54 | incremented | it fired |
   | 49 bit 0 | 1 | output latched |
   | 49 bit 5 | **0** | a real event, not a fault trip |
   | 61 | 0 | no fault |
   | 56 | well below 850 | how far the magnitude fell — the real evidence |
   | 55 | ≥ 25 | how long it stayed down, ms |

5. **Re-arm** before the next one — the output latches by default:

   ```sh
   tools/ctx311_client.py --port /dev/ttyUSB0 command clear-los
   ```

Or watch it live, which prints a summary per event automatically:

```sh
tools/ctx311_capture.py --port /dev/ttyUSB0 --out bench.csv
```

### Reading the result honestly

**Register 56 is the one to trust.** It is the lowest magnitude reached.
A real drop approaches 0 mg; a hand movement that just clipped the
threshold sits in the 700s. Both increment register 54, and only
register 56 tells them apart.

**Register 57 will usually say "not valid", and that is correct.** Height
is only reported when the assembly actually reached free-fall depth
(register 49 bit 3, minimum below 300 mg). A short bench drop rarely
does. It is withholding a number it cannot justify, not failing.

**If register 49 bit 5 is set, the trip was a FAULT, not a fall.** The
device tripped because it lost its detection channel. Check register 61
and fix that first — the drop told you nothing.

**If nothing trips:** confirm bit 6 is clear (armed), confirm register 60
moves when you move the sensor, and check that the drop actually lasted
25 ms — at 1 g that is only ~3 cm of travel, but a hand movement that
decelerates early may never hold below threshold for long enough.

## Field result 1 — first installation, confirm time raised to 50 ms

Recorded because it is real evidence from a real machine, which is worth
more than anything in this repository's host tests.

### What was done

| | |
|---|---|
| Setting | Register 50 = **500 mg**, register 52 = **25 ms** |
| Observed | Arrest output triggered during **normal operation** |
| Suspected cause | Motor acceleration at start of descent |
| Machine | Maximum vertical speed ~**1 m/s** |
| Change | Register 52 raised to **50 ms** |
| Observed after | **No trigger during normal operation** |
| Confirmation | Sensor hand-held, released from **1 m**; arrest output triggered correctly at 500 mg / 50 ms |

### What it establishes

**The false trip was a transient, not the acceleration ramp.** At a
500 mg threshold the assembly must be accelerating downward at more than
0.5 g for the detector to see anything at all. A *sustained* 0.5 g would
take ~204 ms to reach 1 m/s and would therefore have tripped 50 ms just
as easily as 25 ms. Since 50 ms does **not** trip, the sustained
acceleration is below 0.5 g and the 25 ms trips were caused by something
lasting between 25 and 50 ms — a start-up jerk or backlash take-up, not
the steady ramp. Raising the confirm time is the correct lever for that,
and the fix is sound.

### What it does NOT establish, and both matter

**1. The drop test barely tested the timing.** Free fall from 1 m lasts
**452 ms** at ~0 mg — about **9×** the 50 ms confirm time. That drop
would have tripped at any confirm time up to ~450 ms. It confirms the
detector fires and the output works; it says almost nothing about
whether 50 ms is the right number rather than 100 ms or 200 ms.

**2. Nothing tested the case the product exists for.** A hand-released
drop is a *clean free fall* — magnitude goes to ~0 mg, which passes any
threshold. It cannot tell you anything about a **partial** loss of
support, which is the realistic failure (see "Why loss of support and
not free fall" in the register map).

And the threshold is now set where that matters:

| Register 50 | Means | A failure unloading 30 % (~700 mg) |
|---:|---|---|
| 850 mg (default) | lost ~15 % of support | **trips** |
| **500 mg (in use)** | lost ~50 % of support | **does not trip** |

At 500 mg the device will catch a free fall and a severe partial
failure, and will **miss** a strand slip or shoe release that unloads
less than about half. That may be an acceptable trade for this
installation — but it is a trade, it was not measured, and the drop test
performed cannot detect it.

**To close it**, run the constrained cases in "Drop types that must be
covered" below: partial unload at ~15 %, at ~30 %, and marginally under
threshold. Those are the drops that discriminate. If the machine turns
out to need 500 mg to stay quiet, that is a finding about the machine
worth writing down, not a setting to adopt silently.

### The ramp is not the problem; the transient on top of it is

The assembly starts from rest and accelerates to 1 m/s, so it is worth
being precise about what the *steady* ramp looks like to the detector.

Sensed magnitude during a downward acceleration `a` is `(1 − a/g) g`,
and the ramp lasts far longer than any confirm time — so if the ramp
crosses the threshold at all, it trips.

| 0 → 1 m/s in | Accel | Magnitude during ramp | 850 mg | 500 mg |
|---|---|---:|---|---|
| 0.30 s | 3.33 m/s² | 660 mg | **trips** | ok |
| 0.50 s | 2.00 m/s² | 796 mg | **trips** | ok |
| 0.68 s | 1.47 m/s² | 850 mg | borderline | ok |
| 1.00 s | 1.00 m/s² | 898 mg | ok | ok |
| 2.00 s | 0.50 m/s² | 949 mg | ok | ok |

**The crossovers:** the ramp dips under **850 mg** once acceleration
exceeds 1.47 m/s² (0 → 1 m/s in under **0.68 s**), and under **500 mg**
only above 4.91 m/s² (under **0.20 s**).

At 500 mg the ramp cannot have caused the false trips unless this hoist
reaches 1 m/s in under 0.2 s, which is implausible. So the cause really
was a transient 25–50 ms long sitting on top of the ramp.

### Field result 2 — 850 mg at 50 ms also runs clean

**Observed:** confirm time 50 ms with the threshold back at the **850 mg
default**, no arrest trigger during normal movement.

**This is the better setting, and it should be kept.** It has the same
observed false-trip behaviour as 500 mg / 50 ms and roughly three times
the sensitivity to the failure the product exists for:

| Register 50 | Detects a loss of | A 30 % unload (~700 mg) |
|---:|---|---|
| **850 mg** | ~15 % of support | **trips** |
| 500 mg | ~50 % of support | does not trip |

It also means the concern raised under field result 1 — that 500 mg
would miss a partial slip — no longer applies. The installation is back
on the design default.

**What it tells us about the transient.** The prediction below was that
850 mg would probably *not* survive 50 ms. It did, and the reasoning
that follows is left in place because being wrong about it is
informative: the transient must be a **sharp spike**, not a gradual sag.
The entire excursion — down through 850 mg, past 500 mg, and back — fits
inside 50 ms. That is the signature of backlash take-up or a mechanical
snatch, not of a slow load transfer.

### The prediction this disproved, and why it was wrong

It was argued here that 850 mg probably could **not** be restored: the
transient went below 500 mg, so it is necessarily below 850 mg for
*longer* — it crosses the higher threshold earlier and recovers past it
later — and 50 ms would therefore not reject it.

The first half of that is still true. The conclusion was not. Duration
below 850 mg is indeed longer than duration below 500 mg, but both are
under 50 ms, so the confirm time rejects both. The error was assuming
the difference would be large, which only holds for a *slow* dip. For a
sharp spike the two durations are close together and the argument does
not bite.

**Measure, do not infer.** The device records what is needed to settle
this directly — see below.

**The measurement that settles it** is the depth and duration of that
transient, and the device already records both:

| Register | What it tells you |
|---|---|
| **56** | Minimum magnitude reached — how deep the dip went |
| **55** | Duration below threshold — how long it lasted |

Provoke a false trip at a low threshold and short confirm (25 ms) and
read registers 55 and 56, or capture normal operation with
`tools/ctx311_capture.py`, which summarises both per event. With those
two numbers the threshold/time corner can be chosen rather than guessed.

### Measuring the margin — registers 55 and 56 record near-misses

Neither field result says *how close* the setting came to tripping, and
that is what decides whether it survives a colder morning, a heavier
load or a worn component. The device already records it, because both
registers track a below-threshold run **whether or not it ever trips**:

| Register | What it holds | Margin it gives you |
|---|---|---|
| **56** | Deepest magnitude reached since boot or `clear-los` — it accumulates, so it is the worst dip in the whole window | **Threshold margin.** Compare with register 50 |
| **55** | Duration of the **last** below-threshold run | **Time margin.** Compare with register 52 |

Procedure:

1. `tools/ctx311_client.py --port /dev/ttyUSB0 command clear-los` to
   reset the record.
2. Run normal operation for a representative period.
3. Read registers 55 and 56.

If register 55 reads 30 ms against a 50 ms confirm, there is 20 ms of
margin. If it reads 48 ms, the setting is on the edge and one stiffer
start will trip it. Register 56 works the same way against register 50:
850 mg configured and 700 mg observed is 150 mg of headroom.

Better still, run `tools/ctx311_capture.py` through normal operation —
it polls continuously, so it catches runs a single read would miss, and
register 55 only holds the **last** one.

> Note: register 56 reads **0** when nothing has dipped below threshold
> at all, which is indistinguishable from a true 0 mg reading. Check
> register 54 and register 55 alongside it — all three at zero means
> nothing happened.

### Still unproven: how long is "normal operation"?

Both field results are absence-of-trip observations, and absence over a
short window is weak evidence. The original false trips at 25 ms
appeared at some rate that was never recorded; if they were occasional,
a handful of clean cycles at 50 ms proves little.

Before treating this as commissioned, run the **false-positive run**
below for a period comparable to the rev H idle soak in
`HARDWARE_VALIDATION.md` — 4 h 48 min there — with registers 54, 55, 56
and 61 read at the end.

### Arrestor sizing must be recomputed — H-06

The sizing table in the register map assumed the assembly starts from
rest. **Starting from rest describes the start of the move, not the
state in which a support failure occurs.** The assembly reaches 1 m/s
and then stays there for most of its travel — 60 % of the time on a 2 m
move at 1 m/s², rising to ~90 % on a 5 m move at 2 m/s² — so a failure
at full speed is both the worst case and the likeliest one:

| | From rest | Descending at 1 m/s |
|---|---|---|
| 50 ms confirm | 0.49 m/s, 1.2 cm | **1.49 m/s, 6.2 cm** |
| + 50 ms arrestor engagement | — | **1.98 m/s, 14.9 cm** |

Energy goes with the square of velocity, so at 50 ms the arrestor sees
about **9× the energy** the from-rest table implies. The register map has
been corrected to show both cases. **H-06 should be re-checked against
14.9 cm and 1.98 m/s**, not against 1.2 cm.

---

## Register codes — what a raw value means

Every register that carries **bits** or **coded values** rather than a
plain number. Bitfields add up: a register showing 11 has bits 0, 1 and 3
set. Anything not listed here is a plain integer in the units the map
gives.

### Register 49 — LOS status (bitfield, read-only)

| Bit | Hex | Dec | Meaning |
|---|---|---:|---|
| 0 | `0x01` | 1 | **Latched** — the arrest output is open and stays open until cleared |
| 1 | `0x02` | 2 | **Active now** — magnitude is below threshold at this instant |
| 2 | `0x04` | 4 | Impact followed — the impact detector also fired during the event |
| 3 | `0x08` | 8 | **Reached free-fall depth** — minimum went below 300 mg. Register 57 is only valid when this is set |
| 4 | `0x10` | 16 | Impact clipped — the peak hit the ±16 g ceiling, true peak is higher |
| 5 | `0x20` | 32 | **Tripped by FAULT, not by an event** — see register 61. Exclude from tuning |
| 6 | `0x40` | 64 | **Still arming** — detection suppressed, health open. Not a fault |

**Values you will actually see:**

| Value | Hex | Means |
|---:|---|---|
| 0 | `0x00` | Armed, healthy, nothing happening — the normal resting state |
| 64 | `0x40` | Still arming (first ~2 s after reset). Wait for it to clear |
| 3 | `0x03` | Event in progress — latched and still below threshold |
| 1 | `0x01` | Event over, still latched. Needs `clear-los` |
| 11 | `0x0B` | Latched + active + reached free-fall depth — a genuine drop |
| 33 | `0x21` | Latched **by a fault**. Not a fall. Check register 61 |

### Register 61 — Fault flags (bitfield, read-only)

**Detection lost** means the protective function is gone, and the arrest
engages if register 63 is 1. **Advisory** opens health only.

| Bit | Hex | Dec | Name | Class | Meaning |
|---|---|---:|---|---|---|
| 0 | `0x01` | 1 | RATE | **detection lost** | Sample rate outside 1200–2000 Hz — dead part, dead SPI, detached INT1 |
| 1 | `0x02` | 2 | STUCK | **detection lost** | Bit-identical samples ~1 s — **or** a loss-of-support trip whose entire confirm window was bit-identical. A real ADXL345 always dithers |
| 2 | `0x04` | 4 | IMPLAUSIBLE | **detection lost** | Magnitude away from 1 g at rest >2 s. Suspended during a real event |
| 3 | `0x08` | 8 | CONFIG | advisory | EEPROM was defaulted. Check registers 22, 50, 52 |
| 4 | `0x10` | 16 | BOOTCHECK | **detection lost** | Boot plausibility check failed |
| 5 | `0x20` | 32 | WDT_RESET | advisory | Watchdog fired at some point. Sticky until cleared |
| 6 | `0x40` | 64 | SUPPLY | advisory | Controller rail below the register 71 limit for ~3 s. Opens health, never arrests |
| 7 | `0x80` | 128 | ZERO_DATA | **detection lost** | All three axes reading exact zero for ~10 ms. **Sensor communication failure — check the MISO wiring**, not the structure |

Detection-lost mask = `0x97` (bits 0, 1, 2, 4, 7). If `reg61 & 0x97` is
non-zero, `CLEAR_LOS` will be **refused** until the fault clears.

#### Bit 7 is a wiring fault, not a fall

If **MISO alone** fails — open wire, or shorted to ground — the ADXL345
still receives its reads over SCLK/MOSI/CS, still clears DATA_READY, and
register 27 shows a perfect ~1589 Hz. Only the data coming back is
missing. A grounded MISO reads 0 mg, which is below every threshold, so
without this check the device would call a severed cable a fall.

**Signature of a MISO failure, as seen from the master:**

| Register | Reads |
|---|---|
| 27 sample rate | normal, ~1589 Hz — this is why the rate check misses it |
| 60 raw magnitude | **0 mg** (grounded) or ~7 mg (open, pulled high) |
| 61 fault flags | `0x80` ZERO_DATA, or `0x02` STUCK for the open-high case |
| 49 LOS status | bit 5 **set** — tripped by fault |
| PC1 arrest | engaged |

**Signature of a genuine fall, for contrast:** register 61 is `0`,
register 49 bit 5 is **clear**, register 56 shows a minimum magnitude
that is low but not zero, and register 55 a plausible duration.

If you want to reproduce it on the bench: with the unit powered and
running, pull the MISO wire off the ADXL345 and short that input to
ground. The arrest must engage within ~10 ms and register 61 must read
`0x80`. Reconnect, then `CLEAR_FAULTS` followed by `CLEAR_LOS` — the
second command is refused until the live zero run has actually stopped,
so a still-dead bus cannot be cleared away.

### Register 25 — Impact status (bitfield, read-only)

| Bit | Hex | Dec | Meaning |
|---|---|---:|---|
| 0 | `0x01` | 1 | Impact output tripped right now |
| 1 | `0x02` | 2 | Impact latched (snapshot held) |
| 2 | `0x04` | 4 | **Always 0** — removed in map 9. Register 26 is the blocks-missed counter |
| 3 | `0x08` | 8 | Config was defaulted at boot |

### Register 65 — Tilt status (bitfield, read-only)

Monitoring only. Nothing here operates an output.

| Bit | Hex | Dec | Meaning |
|---|---|---:|---|
| 0 | `0x01` | 1 | Reference set. Without it register 64 reads `0xFFFF` |
| 1 | `0x02` | 2 | **At rest** — the angle is being updated right now |
| 2 | `0x04` | 4 | Angle valid |

| Value | Means |
|---:|---|
| 0 | No reference. Register 64 is `0xFFFF` |
| 1 | Reference set, but never yet still long enough to measure |
| **5** | Valid, **but HELD** — not at rest, so register 64 is the last trustworthy reading, not the attitude now |
| **7** | Reference set, at rest, angle live. The normal resting state |

### Register 29 — Reset cause (raw MCUSR, read-only)

More than one can be set.

| Bit | Hex | Dec | Meaning |
|---|---|---:|---|
| 0 | `0x01` | 1 | PORF — power-on reset. Normal for a cold start |
| 1 | `0x02` | 2 | EXTRF — external reset pin |
| 2 | `0x04` | 4 | BORF — **brown-out**, the rail collapsed. Only works if the BOD fuse is set; see the register map. Cross-check register 70 |
| 3 | `0x08` | 8 | WDRF — **watchdog fired**. Sets `FAULT_WDT_RESET` in register 61 |

### Enumerated registers (a single code, not bits)

| Reg | Code | Meaning |
|---|---:|---|
| 46 command status | 0 | Idle — no command since boot |
| | 1 | Accepted and executed |
| | 2 | **Not accepted** — unknown code, *or* a known command refused (e.g. `CLEAR_LOS` while faulted) |
| 62 boot check | 0 | Pending — not finished yet |
| | 1 | Pass |
| | 2 | **FAIL** — sets `FAULT_BOOTCHECK` |
| 63 fault action | 0 | A detection-lost fault opens health only |
| | 1 | A detection-lost fault **also engages the arrest** (default) |
| 3 output state | 0 | Impact output tripped |
| | 1 | Impact output closed (safe) |

### Register 28 — Command codes (write only, self-clearing)

Always reads back 0. Confirm through registers 45–47, never by reading 28.

| Code | Name | Effect |
|---|---|---|
| `0x0001` | CLEAR_PEAKHOLD | Zeroes peak holds (registers 30, 36) |
| `0x0002` | CLEAR_TRIPCOUNT | Zeroes impact trip count (register 31) |
| `0x0003` | CLEAR_DIAG | Zeroes diagnostics (registers 20, 26) |
| `0x0004` | **CLEAR_LOS** | Clears the arrest latch and re-arms. **Refused while a detection-lost fault stands** |
| `0x0005` | CLEAR_LOSCOUNT | Zeroes LOS trip count (register 54) only |
| `0x0006` | CLEAR_FAULTS | Clears latched fault bits. A condition still present is re-raised within ~1 s |
| `0x0007` | SET_TILT_REF | Captures the current gravity vector as the tilt baseline. **Refused unless at rest** |
| `0x0008` | CLEAR_TILT_REF | Forgets the tilt baseline; register 64 returns to `0xFFFF` |
| `0x5A5A` | FACTORY_RESET | All settings to defaults, **slave ID returns to 71** |

### Sentinel values

| Reg | Value | Meaning |
|---|---|---|
| 57 height | `0xFFFF` (65535) | **Not valid** — never reached free-fall depth. Not a 655 m fall |
| 64 tilt | `0xFFFF` (65535) | **Not valid** — no reference set, or never yet at rest. Not 6553.5° |
| 69 / 70 supply | `0xFFFF` (65535) | **Not measured yet** — not 65.5 V |
| 59 output hold | `0` | Latch until commanded (the default) — not "no hold" |
| 48 reserved | non-zero | You are running a `-DCTX311_STACK_DEBUG` image. Reflash a release build |

---

---

## Troubleshooting — the device stops responding

### It should recover on its own in 500 ms

A 500 ms watchdog is enabled at the end of `setup()`, and `wdt_reset()`
is the first statement in `loop()`. Anything that stops `loop()` running
therefore resets the controller within half a second. **If a freeze
lasts longer than that, the watchdog path itself is broken** — that is
the thing to investigate, not the hang.

After any suspected freeze, power-cycle and read:

| Register | What it tells you |
|---|---|
| 61 bit 5 (`0x20`) | `WDT_RESET` — the watchdog fired. Sticky until `CLEAR_FAULTS` |
| 29 | Raw `MCUSR`. Bit 3 (`0x08`) = WDRF, bit 0 (`0x01`) = PORF (power-on) |

### Why a freeze can outlast the watchdog

**The AVR watchdog stays armed across a reset.** After it fires, the MCU
restarts into the *bootloader* with the watchdog still running at
500 ms. What happens next depends on which bootloader the Nano has:

- **Optiboot ("new bootloader")** — reads `MCUSR`, sees WDRF and jumps
  straight to the sketch. Recovers cleanly. **But it clears `MCUSR`
  first**, so register 29 reads **0** and `FAULT_WDT_RESET` never
  latches. If reg 29 reads 0 after a reset you know was a watchdog
  reset, this is why — reg 29 is not trustworthy on that bootloader.
- **The old bootloader** (Nanos are still shipped with it; the IDE lists
  it as "ATmega328P (Old Bootloader)") — waits ~2 s for a programmer
  before starting the sketch. The 500 ms watchdog fires *during that
  wait*, resetting the board again, forever. **The board appears
  permanently dead and only a power cycle clears it.** This is the
  classic "watchdog bricks the Nano" failure and it matches a freeze
  that will not clear on its own.

**Telling them apart in the field:** watch the Nano's on-board LED
(pin 13). The bootloader flashes it at every start, so a reset loop is a
repeating blink. A genuine hang is a steady or dark LED.

**If it is the old bootloader, reflash with Optiboot.** Do not shorten
or remove the watchdog to work around it — the watchdog is what stops a
hang leaving the arrest output wherever it happened to be.

### Freezing while connecting the sensor

This had a specific cause and it is fixed in firmware. `INT1` (D2) was
configured `INPUT` with no pull-up. With no sensor attached the pin
floats, and a floating CMOS input chatters. Every chatter edge is a
rising edge, every rising edge runs the ISR for ~166 µs, and once edges
arrive faster than that `loop()` never gets the CPU — so `wdt_reset()`
is never reached and the watchdog fires. On the old bootloader that is
the reset loop above.

It is now `INPUT_PULLUP`, which holds INT1 high when nothing is driving
it. No rising edges, no storm, and a disconnected sensor is reported
properly as `FAULT_RATE` within ~1.25 s. (Previously the chatter could
land inside the 1200–2000 Hz band by luck and show *no* fault at all.)

**The pull-up does not make hot-plugging safe.** It removes the
interrupt storm; it does nothing about the supply and ground transients
of connecting a part to a running board. **Power down to connect the
sensor.**

### Readings go flat when the sensor is connected to a live controller

Different failure, different cause. The ADXL345 is configured once, in
`setup()`. A part connected **after** the controller has booted comes up
in its power-on defaults — **standby mode, ±2 g, 100 Hz, all interrupts
disabled** — so it never asserts DATA_READY. The ISR never runs and
registers 0–2 sit **flat at whatever they last were**. Power-cycling the
controller re-runs `setup()`, which is why that fixed it.

The device is not wrong about its state while this is happening:
register 27 reads 0, `FAULT_RATE` (`0x01`) is up in register 61 within
~1.25 s, and the arrest is engaged. Check those before assuming the
sensor is dead.

**Firmware now recovers it.** Once a second, while the sample rate is
out of band, the controller reads `DEVID` and `POWER_CTL` over SPI. If
the part answers `0xE5` and its MEASURE bit is clear, it has restarted
and gets reconfigured in place. **Register 73 counts that.**

| Reg 73 | Reg 61 | Meaning |
|---|---|---|
| 0 | 0 | Normal. This is what a healthy unit reads for its whole life |
| increments | `0x01` then clears after `CLEAR_FAULTS` | The sensor restarted and was recovered. **Maintenance finding — intermittent sensor supply or connector** |
| stays 0 | `0x01` persists | Nothing answering on SPI, or INT1 is not getting through. Recovery cannot help; check the wiring |

**Recovery restores the sample stream. It does not re-arm anything.**
Faults stay latched and the arrest stays engaged until an operator sends
`CLEAR_FAULTS` and then `CLEAR_LOS`, in that order. That is deliberate —
a protective function that silently re-armed itself after its sensor
vanished would be worse than one that stayed latched.

**This is a recovery path, not a licence to hot-plug.** It does nothing
about the supply and ground transients of connecting a part to a running
board. Power down to connect the sensor.

### Ways to restart the controller

| Method | Reaches a frozen device? | Notes |
|---|---|---|
| Watchdog (automatic) | **yes** | 500 ms. Already enabled; this is the intended mechanism |
| Power cycle | **yes** | Clears a bootloader reset loop, which nothing else will |
| Pull the Nano `RESET` pin low | **yes** | ~10 µs to GND. A spare PLC output through a transistor works; this is the option to wire if you want remote recovery |
| A Modbus reset command | **no** | A frozen controller is not answering Modbus, so this cannot solve the problem it looks like it solves |

**On a Modbus reset command specifically:** it is not implemented, and
it should not be added casually. `setup()` drives PC1 **high — arrest
released** — and then enters the ~2 s arming window with health open. A
reset command would therefore be a way to release a latched arrest and
leave the assembly unprotected for two seconds, from the bus, without
going through `CLEAR_LOS` — which exists precisely to refuse that while
a detection-lost fault stands. If you want one, it needs to be gated at
least as tightly as `CLEAR_LOS`, and that is a decision to take
deliberately.

## Before the first drop — H-01 must be closed

**Do not connect a real arrest device until H-01 is confirmed.** On loss
of MCU power PC0/PC1/PC2 go high impedance, not low. The arrest is only
fail-safe if there is an external pull-down on PC1 *and* the device
engages when de-energised. If it engages on energisation instead, the
firmware polarity is backwards and the architecture is wrong for the job
— escalate, do not patch.

Drops against a bench indicator rather than a live arrestor are fine
before H-01. Drops that are supposed to arrest something are not.

## What this is tuning

| Setting | Register | Default | What the drop data decides |
|---|---|---|---|
| LOS threshold | 50 | 850 mg | How much unloading counts as loss of support |
| LOS confirm time | 52 | 25 ms | How long it must persist before the arrest fires |
| `LOS_SPIKE_TOLERANCE` | — (`#define`) | see sketch | How many samples above threshold may interrupt a run without discarding it |

Register 52 also **sizes the arrestor** (H-06). Arrest energy goes with
the square of velocity, so doubling the confirm time quadruples what the
mechanical device must absorb. Add its own engagement time to every
figure.

## Rig setup

Record once per session, not per drop.

| | |
|---|---|
| Date | |
| Unit serial / board rev | |
| Firmware version (reg 42) | |
| Map version (reg 43) | |
| Build date (reg 44) | |
| ADXL345 supply rail (V) | |
| Mounting — axis orientation, fixing method | |
| Rig description | |
| Arrest device fitted? (see H-01) | |
| Register 50 in force (reg 51) | |
| Register 52 in force (reg 53) | |
| `LOS_SPIKE_TOLERANCE` in the build | |
| Register 63 fault action | |
| Register 59 output hold | |

## Capture command

One capture per drop, named so the CSV and the row below match:

```sh
tools/ctx311_capture.py --port /dev/ttyUSB0 --out drop_<date>_<n>.csv
```

Confirm the device is armed before releasing: register 49 bit 6 must be
**clear** and PC2 closed. A drop taken during the ~2 s arming window
proves nothing about the threshold — the detector is suppressed and
cannot trip. Discard and repeat.

After a trip the output latches by default. Re-arm with:

```sh
tools/ctx311_client.py --port /dev/ttyUSB0 command clear-los
```

If that is **refused**, the detection channel is still faulted. Fix the
fault first — a refused re-arm is a correct refusal, not a tool problem.

### If you get checksum errors

A full sweep is 64 registers — a **133-byte** RTU frame, ~139 ms at
9600 baud. One corrupted bit anywhere in it fails the CRC and the whole
read is thrown away, so long frames are the first thing a marginal line
breaks.

A checksum error means the device **answered** and the reply was
corrupted in transit. It is not a wrong address and not a dead device —
those give you a timeout instead. Check, in this order:

1. **Something else has the port open.** This is the usual cause and it
   does not look like one. Two programs reading one serial port each
   steal bytes from the other, so both see fragmented frames and both
   blame the wiring. A capture still running in another window, a second
   Thonny tab, or a leftover process from the previous run will all do
   it. Check with `fuser -v /dev/ttyUSB0` or `lsof /dev/ttyUSB0`.
2. **Power.** A Raspberry Pi showing an undervoltage warning corrupts
   serial traffic. Fix that before trusting any drop data taken on it.
3. **RS-485 wiring** — A/B swapped, 120 Ω termination missing at either
   end, or no bias resistors.
4. **Frame length** — retry with `--chunk 16` to use short frames.
5. **Ground** — RS-485 needs a common reference, not just A and B.

> Recorded because it cost time once: a checksum error on this rig was a
> shared port, not the line. The wiring and the supply were both fine.

`--chunk` is a fallback, not the default, and it costs something real:
chunks are separate requests, so the register block can change between
them and an event can straddle the split. Use the single read for drop
captures if the line will carry it.

## Drop log

One row per drop. `Result` is the tester's judgement, not the device's.

| # | CSV | Drop type | Mass (kg) | Height (m) | Restraint | Reg 49 | Reg 55 dur (ms) | Reg 56 min (mg) | Reg 57 height | Reg 58 impact (mg) | Trip cause | Arrest fired? | Result |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | | | | | | | | | | | | | |
| 2 | | | | | | | | | | | | | |
| 3 | | | | | | | | | | | | | |
| 4 | | | | | | | | | | | | | |
| 5 | | | | | | | | | | | | | |

**Trip cause** must be event or fault, taken from the capture tool's
summary (register 49 bit 5). A trip caused by the device faulting
measures the device, not the drop, and must never be averaged into
threshold tuning.

**Register 57** is `not valid` whenever the assembly never reached
free-fall depth. That is the expected result for most constrained drops
and is not a defect — record it as `invalid`, never as a number.

## Drop types that must be covered

The constrained cases are the point of the product. A detector tuned only
on clean vertical drops will be tuned for the failure that does not
happen.

| Drop type | Covered? | Notes |
|---|---|---|
| Clean vertical free drop | | The easy case; establishes the floor |
| Partial unload (~15 % lost) | | The design case — register 50's default |
| Partial unload, marginal (just under threshold) | | Must NOT trip |
| Tumbling / rotating | | Magnitude is not monotonic; spike tolerance matters |
| Tethered — arrested part way | | |
| Partially arrested, then released again | | |
| Shock-loaded (snatch, no net descent) | | Impact path (PC0), not LOS |
| Jolt / knock with no loss of support | | Must NOT trip |
| Normal jacking or climbing motion | | Must NOT trip — the false-positive case |

## False-positive runs

A threshold that never false-trips on a bench and false-trips on a
machine is not tuned. Run the rig through normal motion for a period
comparable to the idle soak in `HARDWARE_VALIDATION.md`.

| | |
|---|---|
| Duration | |
| Normal cycles performed | |
| Register 54 — LOS trip count | |
| **Spurious trips** | |
| Register 61 — faults raised | |
| Register 19 — max ISR time (µs) | |
| Register 48 — min free SRAM (debug image only) | |

Register 19 is the check that the loss-of-support block did not push the
ISR past its budget (H-03, target ≤ 250 µs). The figure in the sketch
header is computed from a build, not measured; this is where it becomes
a measurement.

Register 48 only carries a number if the unit is running an image built
with `-DCTX311_STACK_DEBUG` (see `docs/RESOURCE_BUDGET.md`). It reports
minimum free SRAM in bytes — the one figure the static budget cannot
give. Run the soak on that image if you want it, then **reflash a
release image before the unit goes into service**: a release build reads
0 there, and a field device reading non-zero is running a debug build.

## Conclusions

Leave blank until the drops are done.

| Question | Answer | Evidence |
|---|---|---|
| Final register 50 | | |
| Final register 52 | | |
| Final `LOS_SPIKE_TOLERANCE` | | |
| Does the arrestor absorb the implied energy? (H-06) | | |
| Any drop type that defeats detection | | |
| Any normal motion that false-trips | | |
