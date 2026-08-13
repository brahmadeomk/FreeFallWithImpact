# CTX311 register map — map version 9 (firmware rev A)

Modbus RTU slave, 9600 8N1, default slave ID 71. All registers are
holding registers (function 3 to read, 6 or 16 to write).

**Read register 43 on connect.** It carries the map version. A CTX310
master expecting version 8 must refuse to ingest — and CTX311 changes
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
| Registers 49–63 added | Response is now 133 bytes for a full sweep; library `BUFFER_SIZE` raised 128 → 160 |
| Watchdog **enabled** | A hung device now resets instead of holding the output wherever it was. Register 29 will show WDRF and register 61 will latch `FAULT_WDT_RESET` |
| EEPROM offsets 0–6 unchanged | Slave ID and impact threshold survive the upgrade, as they did across G→H |

## Why loss of support and not free fall

A jacked or climbing assembly is restrained. The realistic failures —
a strand slipping, a climbing shoe releasing under load, a hose
bursting — unload the assembly *partially*. They never reach the
sustained near-zero-g a free-fall detector waits for.

So register 50 defaults to **850 mg**, not to a free-fall threshold.
It means "this assembly has lost about 15% of its support".

Register 52 (confirm time) is the number that sizes your arrestor:

| Confirm time | Drop before output | Velocity at arrest |
|---|---|---|
| 25 ms (default) | 0.3 cm | 0.25 m/s |
| 50 ms | 1.2 cm | 0.49 m/s |
| 150 ms | 11.0 cm | 1.47 m/s |
| 300 ms | 44.1 cm | 2.94 m/s |

Arrest energy goes with the **square** of velocity. Doubling the
confirm time quadruples what the arrestor absorbs. Add the arrest
device's own engagement time to every figure.

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

## New registers (49–63)

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

## Fault flags (register 61)

| Bit | Name | Detection lost? | Meaning |
|---|---|---|---|
| 0 | RATE | **yes** | Sample rate outside 1200–2000 Hz. Catches a dead part, dead SPI bus, or detached INT1 |
| 1 | STUCK | **yes** | Bit-identical samples for ~1 s. A real ADXL345 always dithers |
| 2 | IMPLAUSIBLE | **yes** | Magnitude away from 1 g at rest for >2 s. Suspended during a genuine event |
| 3 | CONFIG | no | EEPROM defaulted |
| 4 | BOOTCHECK | **yes** | Boot check failed |
| 5 | WDT_RESET | no | Sticky: the watchdog fired. Survives until `CLEAR_FAULTS` |

**Detection-lost faults engage the arrest** (when register 63 = 1),
because if the channel is dead the protective function is gone.
Advisory faults open the health output only — a factory-fresh unit must
not arrest its own machine merely for never having been configured.

`CLEAR_LOS` is **refused** while a detection-lost fault stands, and the
refusal is reported through register 46. Re-arming a device that cannot
detect would be a lie.

The rate check rides the 1 Hz tick, so **worst-case detection of a dead
sensor is about one second.** Quote that figure, not "immediate".

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
