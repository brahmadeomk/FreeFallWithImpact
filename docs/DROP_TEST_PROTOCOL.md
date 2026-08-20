# Drop test protocol — CTX311 rev A

**Template. No results have been recorded. Every table below is blank on
purpose** — filling one in is T-10's job, done at the rig with a real
unit. Do not populate these from simulation, from the host tests, or from
expectation.

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
| 1 | `0x02` | 2 | STUCK | **detection lost** | Bit-identical samples ~1 s. A real ADXL345 always dithers |
| 2 | `0x04` | 4 | IMPLAUSIBLE | **detection lost** | Magnitude away from 1 g at rest >2 s. Suspended during a real event |
| 3 | `0x08` | 8 | CONFIG | advisory | EEPROM was defaulted. Check registers 22, 50, 52 |
| 4 | `0x10` | 16 | BOOTCHECK | **detection lost** | Boot plausibility check failed |
| 5 | `0x20` | 32 | WDT_RESET | advisory | Watchdog fired at some point. Sticky until cleared |

Detection-lost mask = `0x17` (bits 0, 1, 2, 4). If `reg61 & 0x17` is
non-zero, `CLEAR_LOS` will be **refused** until the fault clears.

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
