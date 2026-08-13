# Drop test protocol — CTX311 rev A

**Template. No results have been recorded. Every table below is blank on
purpose** — filling one in is T-10's job, done at the rig with a real
unit. Do not populate these from simulation, from the host tests, or from
expectation.

This mirrors `HARDWARE_VALIDATION.md`: measurements taken from a real
unit over Modbus, one row per drop. A claim is a hypothesis until a unit
confirms it.

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

1. **Power.** A Raspberry Pi showing an undervoltage warning corrupts
   serial traffic. Fix that before trusting any drop data taken on it.
2. **RS-485 wiring** — A/B swapped, 120 Ω termination missing at either
   end, or no bias resistors.
3. **Frame length** — retry with `--chunk 16` to use short frames.
4. **Ground** — RS-485 needs a common reference, not just A and B.

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
