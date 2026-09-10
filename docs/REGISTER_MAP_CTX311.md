# CTX311 register map — map version 13 (firmware rev A)

Modbus RTU slave, 9600 8N1, default slave ID 71. All registers are
holding registers (function 3 to read, 6 or 16 to write).

**Read register 43 on connect.** It carries the map version, now **13**
(9 added the loss-of-support block, 10 tilt in 64–68, 11 supply
monitoring in 69–70, 12 the low-supply advisory in 71–72, 13 the
sensor-communication fault in register 61 bit 7). A
CTX310 master expecting version 8 must refuse to ingest — and CTX311 changes
more than the map, so this check matters more than it did before.

## Read this before wiring anything

The CTX311 drives an **arrest / brake device**. That makes it a
protective function, not a monitor. It is built on commercial-grade
parts with no redundancy and was not developed under IEC 61508 or
ISO 13849, so it carries **no SIL or PL rating and none may be
claimed** — not in documentation, not in marketing, not in a lift plan.
It is intended as an additional, faster channel on top of the mechanical
fail-safes the jacking or climbing system already has.

## Three outputs, not one

| Pin | Meaning | Level |
|---|---|---|
| PC0 / A0 | Impact interlock — unchanged from CTX310 | HIGH = closed, LOW = tripped |
| PC1 / A1 | **Loss of support → arrest device** | HIGH = safe, LOW = arrest |
| PC2 / A2 | Health | HIGH = healthy, LOW = fault |

**The fail-safe direction is not established by firmware.** On power
loss these pins go high impedance, not low. Whether that engages the
arrest depends on an external pull-down and on the arrest device
engaging when de-energised. Confirm both on hardware.

**Monitor PC2 in the PLC and treat it as a stop condition.** A sensor
that has lost its detection channel is not a sensor.

## The arming window — read this before commissioning

**For the first ~2 s after power-up or reset the device cannot detect
anything, and PC2 (health) is OPEN throughout.**

Loss-of-support detection is suppressed for `SETTLE_SAMPLES` = 3200
samples at the fixed 1600 Hz sample rate, so the assembly is
**unprotected by this device for about two seconds** after every reset.
The suppression exists because the DC path needs to settle; a detector
armed on its first samples would trip on its own start-up transient.

Health is held open through that window deliberately. An unarmed
protective device is not a healthy one, and the alternative — closing
PC2 while the trip test is suppressed — tells the PLC the assembly is
protected during the one window in which it provably is not.

**Telling arming apart from a fault.** Both open PC2, and only one is a
defect:

| Condition | PC2 | Reg 49 bit 6 | Reg 61 | Arrest (PC1) |
|---|---|---|---|---|
| Still arming | open | **1** | 0 | released |
| Faulted | open | 0 | non-zero | depends on reg 63 |
| Armed and healthy | closed | 0 | 0 | released |

Arming never engages the arrest, never sets a fault flag, and clears
itself. Poll register 49 bit 6 if you need the distinction without
watching the pin.

**⚠ This changes power-up behaviour, and it may stop the machine.** If
your PLC treats health-open as a hard stop, the machine will now refuse
to start for ~2 s after a sensor reset, where a previous build would
have let it run. That is the intended reading — the device genuinely
cannot protect anything yet — but it is a behavioural change to plan
for, not to discover on site. Sequence the PLC to wait for reg 49 bit 6
to clear, or accept the 2 s delay.

**If the assembly can be under load during that window, the arrest is
unprotected through it.** No firmware setting closes that gap; it is a
commissioning question about whether a reset can occur under load.

## What CTX310 users must know before reflashing

| Change | Consequence |
|---|---|
| Impact threshold floor raised 10 → **1200 mg** | An existing config below 1200 mg is rejected into defaults on first boot. Check register 22 after upgrading |
| Status register 25 **bit 2 removed** | Dashboards keying on it must be updated. It re-latched within seconds of any clear, so it was a permanent fault light for normal polling. Register 26 remains the counter |
| Registers 49–72 added | Response is now 151 bytes for a full sweep; library `BUFFER_SIZE` raised 128 → 160, leaving **9 bytes spare** |
| Watchdog **enabled** | A hung device now resets instead of holding the output wherever it was. Register 29 will show WDRF and register 61 will latch `FAULT_WDT_RESET` |
| EEPROM offsets 0–6 unchanged | Slave ID and impact threshold survive the upgrade, as they did across G→H |

## Why loss of support and not free fall

A jacked or climbing assembly is restrained. The realistic failures —
a strand slipping, a climbing shoe releasing under load, a hose
bursting — unload the assembly *partially*. They never reach the
sustained near-zero-g a free-fall detector waits for.

So register 50 defaults to **850 mg**, not to a free-fall threshold.
It means "this assembly has lost about 15% of its support".

Register 52 (confirm time) is the number that sizes your arrestor.

**These figures assume the assembly starts FROM REST.** That is the
easy case and it is not the one to size against — see the next table.

| Confirm time | Drop before output | Velocity at arrest |
|---|---|---|
| 25 ms (default) | 0.3 cm | 0.25 m/s |
| 50 ms | 1.2 cm | 0.49 m/s |
| 150 ms | 11.0 cm | 1.47 m/s |
| 300 ms | 44.1 cm | 2.94 m/s |

### If the assembly is already moving — size against this instead

A failure during a descent starts from the descent speed, not from
zero. The confirm time then adds to a velocity that is already there,
and because arrest energy goes with the **square** of velocity, the
difference is much larger than it looks.

At a **1 m/s** descent — a realistic hoist speed, and the one measured
on the first field installation:

| Confirm time | From rest | Descending at 1 m/s | Energy ratio |
|---|---|---|---|
| 25 ms | 0.25 m/s, 0.3 cm | **1.25 m/s, 2.8 cm** | 25× |
| 50 ms | 0.49 m/s, 1.2 cm | **1.49 m/s, 6.2 cm** | 9.2× |

Then **add the arrest device's own engagement time**, which continues
accelerating from there. With 50 ms confirm and a 50 ms engagement, from
a 1 m/s descent: **1.98 m/s and 14.9 cm** before the arrest bites.

That last figure is the one H-06 must size against, not the 1.2 cm in
the first table.

## Registers 35 and 48 are reserved holes

Both read **0** and are pinned to 0 on every read, so a stray write from
an older master cannot stick. They are not reused, so the rest of the map
does not renumber.

**One exception, and it never ships.** A firmware built with
`-DCTX311_STACK_DEBUG` reports minimum free SRAM in bytes through
**register 48** instead of 0. That image exists for the H-05 soak run
only — see `docs/RESOURCE_BUDGET.md`. If a device in the field reads
non-zero at register 48, it is running a debug build and should be
reflashed with a release image.

## New registers (49–72)

| Reg | Name | Access | Units / notes |
|---|---|---|---|
| 49 | LOS status | R | bit0 latched, bit1 active now, bit2 impact followed, bit3 reached free-fall depth, bit4 impact clipped, **bit5 tripped by fault, not by an event**, **bit6 still arming — detection suppressed, health open** |
| 50 | LOS threshold | **R/W** | mg, 200–980, default 850 |
| 51 | LOS threshold effective | R | echo of 50 |
| 52 | LOS confirm time | **R/W** | ms, 5–500, default 25 |
| 53 | LOS confirm time effective | R | echo of 52 |
| 54 | LOS trip count | R | monotonic; `CLEAR_LOSCOUNT` zeroes it |
| 55 | Last event duration | R | ms below threshold |
| 56 | **Last event minimum magnitude** | R | mg — the discriminator between a real drop and a constrained descent |
| 57 | Last event height estimate | R | cm; **0xFFFF = not valid** |
| 58 | Last event impact peak | R | mg |
| 59 | LOS output hold | **R/W** | ms; **0 = latch until commanded** (default) |
| 60 | Live raw magnitude | R | mg, **gravity included** — ~1000 mg at rest. Not comparable with register 23, which is AC-coupled |
| 61 | Fault flags | R | see below |
| 62 | Boot check | R | 0 pending, 1 pass, 2 fail |
| 63 | Fault action | **R/W** | 0 = health output only, 1 = a detection-lost fault also arrests. **Default 1** |
| 64 | Tilt angle | R | 0.1°, **0xFFFF = not valid**. Monitoring only — see the tilt section |
| 65 | Tilt status | R | bit0 ref set, bit1 at rest, bit2 valid |
| 66 | Tilt reference X | R | mg, **SIGNED** |
| 67 | Tilt reference Y | R | mg, **SIGNED** |
| 68 | Tilt reference Z | R | mg, **SIGNED** |
| 69 | **Supply now** | R | mV. `0xFFFF` = not measured yet |
| 70 | **Supply minimum** | R | mV, lowest since boot or `CLEAR_DIAG`. The sag catcher |
| 71 | Low-supply limit | **R/W** | mV, 3000–5500, **0 = disabled**. Default 4500 |
| 72 | Low-supply limit effective | R | echo of 71 |

### Register 56 is the one to trend

Minimum magnitude tells you *what kind* of event happened. A clean drop
approaches 0 mg. A guided or partially arrested descent sits well above
it. A trip at 800 mg and a trip at 50 mg are very different incidents
and register 49 alone cannot tell them apart.

### Register 57 is withheld, not guessed

Height is only reported when the assembly actually reached something
near free fall (minimum magnitude below 300 mg). A partial slip that was
guided the whole way down would make h = ½gt² badly overstate the drop,
so the firmware returns 0xFFFF rather than a confident wrong number.
Even when reported it is a **lower bound** — air drag and pre-release
motion both shorten true free-fall time.

## Tilt (registers 64–68) — monitoring only

**Tilt is not a protective function.** It operates no output, sets no
fault, and is unreachable from the arrest path. The arrest path is
registers 49–63. Trend tilt, alarm on it in the PLC if you want to, but
wiring it into a safety decision would be a claim this device does not
support.

Tilt needs no extra sensing. The DC tracker already follows gravity —
it has to, because everything below it is AC-coupled — and a gravity
vector *is* a tilt measurement. Registers 64–68 turn that vector into an
angle against a stored reference.

| Reg | Name | Access | Units / notes |
|---|---|---|---|
| 64 | **Tilt angle** | R | tenths of a degree from the reference. **0xFFFF = not valid** |
| 65 | Tilt status | R | bit0 reference set, bit1 at rest, bit2 angle valid |
| 66 | Tilt reference X | R | mg, **SIGNED** |
| 67 | Tilt reference Y | R | mg, **SIGNED** |
| 68 | Tilt reference Z | R | mg, **SIGNED** |
| 69 | **Supply now** | R | mV. `0xFFFF` = not measured yet |
| 70 | **Supply minimum** | R | mV, lowest since boot or `CLEAR_DIAG`. The sag catcher |
| 71 | Low-supply limit | **R/W** | mV, 3000–5500, **0 = disabled**. Default 4500 |
| 72 | Low-supply limit effective | R | echo of 71 |

### Setting the reference

The angle is measured against a baseline you capture with the assembly
installed and known-good:

```sh
tools/ctx311_client.py --port /dev/ttyUSB0 command set-tilt-ref
```

That is command `0x0007`, and it is **refused unless the device is at
rest** — a baseline taken while moving is wrong, and every later reading
would be measured against it. `0x0008` (`clear-tilt-ref`) forgets it.

The reference survives power loss but a **factory reset forgets it**: it
describes where this unit was installed, not what the product is, and
carrying a stale one into a different mounting would be worse than
having none.

### It updates at rest, and HOLDS otherwise

An accelerometer cannot separate tilt from linear acceleration — they
are the same measurement. So the angle is recomputed **only** after the
1 s AC-coupled vector RMS has stayed below **30 mg** continuously for
**4 s**, and it **holds its last value** the rest of the time.

The 4 s is not arbitrary: the DC tracker has τ = 1.28 s, so after any
movement the gravity vector needs several τ to settle. 4 s is ≈ 3.1 τ.
Publishing sooner would report the filter still converging, which on a
trend looks exactly like the structure slowly moving.

**A held value is the last trustworthy reading, not the attitude now.**
Register 65 bit 1 tells you which you are looking at.

| Reg 65 | Meaning |
|---:|---|
| 0 | No reference. Register 64 reads 0xFFFF |
| 1 | Reference set, but never yet at rest long enough |
| 5 | Reference set, angle valid, **but held** — not at rest right now |
| 7 | Reference set, at rest, angle live. The normal state |

### Accuracy

Computed with integer maths only — no floating point anywhere in the
firmware. Swept against double precision across the whole 0–180° range
at the real 1 g magnitude, the worst error is **0.25°**, and most of
that is input quantisation: at 3.9 mg/LSB one count is already ~0.2° of
direction.

Resolution over time is better than that suggests, because the DC
tracker is heavily filtered — the rev H idle soak recorded the gravity
vector moving by single millig over 4 h 48 min, which is about a tenth
of a degree.

**No yaw, ever.** Rotation about the gravity vector does not move the
gravity vector. Two axes of freedom, never three.

## Supply monitoring (registers 69–70, and register 29)

For logging and troubleshooting. **Nothing here operates an output or
raises a fault** — see "why there is no supply fault" below.

There are **two** supply diagnostics and they catch different failures.
Neither alone is enough.

| | Register 69 / 70 (ADC) | Register 29 bit 2, BORF (brown-out) |
|---|---|---|
| What | Actual rail voltage in mV | A reset happened because the rail collapsed |
| Catches | **Slow sag** — a rail drooping under load | **Fast collapse** — milliseconds |
| Misses | Anything shorter than the 1 Hz sampling | Anything that does not cross the BOD threshold |
| When | Live, continuously | Post-mortem, and only after a reset |
| Depends on | Nothing | The **BOD fuse** being set |

### Registers 69 and 70 — measured, no extra hardware

The ATmega328P reads its own supply by measuring the internal 1.1 V
bandgap against AVcc: `Vcc = 1.1 × 1024 / reading`. No pin, no external
components, and the ADC was otherwise unused — the analog pins are driven
as digital outputs and nothing samples them.

**Register 70 is the one worth trending.** A rail that is fine whenever
you poll it and dips when the arrest relay pulls in will look perfect in
register 69 forever. The minimum is what exposes it. `CLEAR_DIAG`
rebases it, alongside the other diagnostic extremes in registers 20 and
26.

**Absolute accuracy is poor, and that is inherent.** The bandgap is
untrimmed and specified 1.0–1.2 V, so the reading can be several percent
out unit to unit. It is a good *relative* instrument: trend one unit
against itself over time, and do not compare two units without
calibrating each. If an absolute number ever matters, calibrate per unit
and store the correction on the master.

**It cannot see a fast sag.** Sampling is 1 Hz, so a droop lasting
milliseconds is invisible. That is exactly the case brown-out detection
covers in hardware.

### Register 29 bit 2 — brown-out, and its catch

`BORF` means the controller reset because the rail fell below the
brown-out threshold. It is a genuine supply event and it is free.

**But it depends on a fuse, not on firmware.** `BODLEVEL` lives in the
extended fuse byte. If brown-out detection is disabled, BORF never sets
and this diagnostic is *silently dead* — it looks the same as a healthy
supply. Arduino boards are commonly fused at **2.7 V**, which on a 5 V
rail is near-total collapse: a rail sagging to 4.2 V, which is a real
fault worth finding, would never trip it.

Check the fuse at the bench before relying on it:

```sh
avrdude -p m328p -c <programmer> -U efuse:r:-:h
```

`0xFD` = BODLEVEL 2.7 V, `0xFC` = 4.3 V, `0xFF` = **disabled**.

Register 29 is captured once at boot and never changes during a run — it
describes the reset that started *this* run. `MCUSR` is cleared
immediately after it is read, so the bits do not accumulate across
resets and each run reports only its own cause.

### The low-supply advisory (registers 71–72, fault bit 6)

Register 61 bit 6 (`SUPPLY`, `0x40`) is raised when the rail sits below
register 71.

**It is ADVISORY.** It opens the health output and never engages the
arrest — bit 6 is deliberately outside the detection-lost mask, so it
also does not block `CLEAR_LOS`. A low rail does not mean detection has
failed: the part is either running correctly or it is not, and if it is
not, the rate, stuck and plausibility checks catch that on their own
evidence rather than by inference from a voltage. Engaging a brake
because a number crossed a configurable threshold — one measured by an
untrimmed bandgap — would be acting on the weakest signal in the device.

⚠️ **It still has teeth.** If your PLC treats health-open as a stop
condition, as the map recommends, a rail below this limit *will* stop
the machine. That is the point of raising it, but plan for it.

**Debounced over 3 consecutive readings** (~3 s at the 1 Hz sampling).
One bad ADC reading must not open health; three seconds of low rail is a
supply problem.

**Commission the limit against the actual unit.** Default is 4500 mV,
because the ATmega328P at 16 MHz is out of spec below 4.5 V — that is
the line with a technical meaning rather than a round number. But the
bandgap is untrimmed, so read register 69 on *this* unit with a healthy
supply and set the limit below what it reports:

```sh
tools/ctx311_client.py --port /dev/ttyUSB0 supply-limit 4300
```

**Writing 0 disables it entirely**, which is the escape hatch for a unit
whose reference reads low enough to alarm on a perfectly good rail.
Registers 69 and 70 keep working either way, so you lose the alarm, not
the data.

## Fault flags (register 61)

| Bit | Name | Detection lost? | Meaning |
|---|---|---|---|
| 0 | RATE | **yes** | Sample rate outside 1200–2000 Hz. Catches a dead part, dead SPI bus, or detached INT1 |
| 1 | STUCK | **yes** | Bit-identical samples for ~1 s, **or** a loss-of-support trip whose entire confirm window was bit-identical. A real ADXL345 always dithers |
| 2 | IMPLAUSIBLE | **yes** | Magnitude away from 1 g at rest for >2 s. Suspended during a genuine event |
| 3 | CONFIG | no | EEPROM defaulted |
| 4 | BOOTCHECK | **yes** | Boot check failed |
| 5 | WDT_RESET | no | Sticky: the watchdog fired. Survives until `CLEAR_FAULTS` |
| 6 | SUPPLY | no | Controller rail below register 71 for 3 consecutive readings. Advisory — see below |
| 7 | ZERO_DATA | **yes** | All three axes reading exact zero for ~10 ms. The data path is returning an undriven bus, not the part |

### Bit 7 — the sensor-communication fault (new in map 13)

This is the one accelerometer failure the rate check cannot see. If
**MISO alone** fails — an open wire, or a short to ground — SCLK, MOSI
and CS still reach the ADXL345, so it keeps clearing DATA_READY, the
interrupt keeps arriving, and register 27 shows a perfect 1589 Hz. Every
read returns the idle bus.

A grounded MISO reads 0,0,0 — a magnitude of **0 mg**, which is below
every settable loss-of-support threshold. Without this check the
loss-of-support path trips first and the device reports **a severed
cable as a genuine fall**: register 49 bit 5 clear, register 55 showing a
plausible run length, and an operator free to clear it and carry on.

Two checks close that:

1. **ZERO_DATA (bit 7)** — 16 consecutive samples of exact 0,0,0, about
   **10 ms**. Raises the fault and engages the arrest as a fault
   (register 49 bit 5 **set**). Exact zeros are impossible on a live
   part: at rest gravity puts ~256 counts on some axis, and in free fall
   the part's own noise dithers by several LSB.
2. **Trip-instant attribution** — when the loss-of-support path latches,
   if the *entire* confirm window was bit-identical, the trip is
   attributed to a fault and `STUCK` (bit 1) is raised immediately
   rather than a second later. This catches the other frozen patterns —
   an open MISO pulled high reads −1,−1,−1, about 7 mg — and works at
   any threshold and confirm-time setting.

**The output is the same either way: the arrest engages.** What changes
is the reason reported, and whether `CLEAR_LOS` will re-arm. The live
zero run survives `CLEAR_FAULTS` deliberately, so a `CLEAR_FAULTS` /
`CLEAR_LOS` pair cannot re-arm the device over a bus that is still dead.

Setting register 63 to 0 suppresses the *fault-initiated* trip, but the
loss-of-support path still trips on a magnitude of zero, because zero is
below the threshold whatever caused it. A diagnostic setting does not
weaken the fail-safe direction.

**Detection-lost faults engage the arrest** (when register 63 = 1),
because if the channel is dead the protective function is gone.
Advisory faults open the health output only — a factory-fresh unit must
not arrest its own machine merely for never having been configured.

`CLEAR_LOS` is **refused** while a detection-lost fault stands, and the
refusal is reported through register 46. Re-arming a device that cannot
detect would be a lie.

The rate check rides the 1 Hz tick, so **worst-case detection of a dead
sensor is about one second.** Quote that figure, not "immediate". The
precise worst case is ~1.25 s: a window fails only once the count drops
below 1200 of 1589, so a part that dies in the last quarter of a window
is caught at the end of the next one.

`ZERO_DATA` is the exception and is much faster — **~10 ms** — because it
has to be, to beat the loss-of-support confirm time.

## New command codes

| Code | Name | Effect |
|---|---|---|
| `0x0004` | CLEAR_LOS | Clears the latch and re-arms. Refused while detection-lost |
| `0x0005` | CLEAR_LOSCOUNT | Zeroes register 54 only |
| `0x0006` | CLEAR_FAULTS | Clears latched fault bits. Live conditions re-raise within ~1 s |

Confirm all of these the way CTX310 taught: write register 28, then read
register 47 and check it moved. Register 28 always reads back 0.

## Registers 0–48

Unchanged from CTX310 map version 8, with the two exceptions above
(register 22's floor, register 25 bit 2). Registers 35 and 48 remain
reserved holes reading 0 and were **not** reused — a map-8 master could
still write them.
