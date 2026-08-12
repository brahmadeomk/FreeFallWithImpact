# CTX310 / VRM315 register map — map version 8 (firmware rev H)

Modbus RTU slave, 9600 8N1, default slave ID 71. All registers are
holding registers (function 3 to read, 6 or 16 to write).

**Read register 43 on connect.** It carries the map version. If it is not
the value your client was written against, the register meanings may have
changed underneath you and the numbers you log will be plausible but
wrong. Refuse to ingest rather than guess.

## Why register 28 always reads back 0

This is the question the map exists to answer, so it goes first.

Register 28 (`CommandReg`) is a **trigger**, not a setting. The firmware
executes the code on the next pass of `loop()` — typically within a few
milliseconds — and immediately writes 0 back to the register. A master
that writes 28 and then reads it back **always** sees 0, even when the
command worked perfectly. That is not a lost write.

It has to behave this way. If register 28 held its value, the firmware
would re-run the command on every pass of `loop()`, thousands of times a
second. A single `CLEAR_PEAKHOLD` would pin registers 30 and 36 to zero
for ever, and the peak-hold feature would be dead.

Contrast register 22 (`ThresholdReg`), which is a **setting**: it holds
the written value, is saved to EEPROM, and reads back as written. That is
why the threshold "chases" your writes and the command register does not
— they are two different kinds of register.

To confirm a command landed, use the acknowledgement registers:

| Register | Meaning |
|---|---|
| 45 | Echo of the last code written to register 28 |
| 46 | 0 = no command yet, 1 = accepted and run, 2 = code not recognised |
| 47 | Count of accepted commands since boot, wraps at 65535 |

The reliable check is register 47: read it, write the command, read it
again, and confirm it incremented. It moves even when the same command is
issued twice in a row, which an echo alone cannot tell you. Register 46
distinguishes "ran it" from "did not recognise that code" — a typo'd
command code is reported rather than silently swallowed.

You can also just watch the side effect: `CLEAR_PEAKHOLD` drives
registers 30 and 36 to 0, `CLEAR_TRIPCOUNT` drives register 31 to 0.

### Command codes

| Code | Name | Effect |
|---|---|---|
| `0x0001` | CLEAR_PEAKHOLD | Zeroes the peak holds (regs 30, 36) |
| `0x0002` | CLEAR_TRIPCOUNT | Zeroes the trip counter (reg 31) |
| `0x0003` | CLEAR_DIAG | Zeroes the diagnostics (regs 19, 20, 26) |
| `0x5A5A` | FACTORY_RESET | Restores default slave ID and threshold, and writes EEPROM |

A factory reset returns the slave ID to 71. If you were talking to the
device on a different ID, you must reconnect on 71.

## The three kinds of register

**Measurements and diagnostics** (everything not listed below) are
read-only and refreshed at read time — the firmware recomputes them
inside the function-3 handler, so a read always returns the current
block rather than a stale one.

**Settings** — registers 21 and 22. They hold the written value, persist
to EEPROM, and read back as written. An out-of-range write is rejected:
the register reverts to the value still in force on the next pass of
`loop()`, so a read-back tells you whether it was accepted. Read register
24 for the peak threshold actually in force.

**Trigger** — register 28, described above.

**Reserved** — registers 35 and 48. They always read 0 and writes to them
do nothing; the firmware pins them to 0 on every read. They are holes
left by the removal of the sustained-RMS trip, kept so the rest of the
map did not renumber.

## Full map

Units: `mg` = milli-g (1000 = 1.000 g). `cm/s²` = centi-metres per second
squared (100 = 1.00 m/s²). All registers are **unsigned** except 32–34.

| Reg | Name | Units | Access | Notes |
|---:|---|---|---|---|
| 0 | X RMS | mg | R | 10 ms window, gravity removed |
| 1 | Y RMS | mg | R | |
| 2 | Z RMS | mg | R | |
| 3 | Output state | — | R | 1 = interlock closed, 0 = open (tripped). **Not a temperature** — this index was published as one in older firmware and there is no temperature sensor in this product |
| 4–6 | X/Y/Z positive peak | mg | R | Within the 10 ms block |
| 7–9 | X/Y/Z RMS | cm/s² | R | |
| 10–12 | X/Y/Z positive peak | cm/s² | R | |
| 13–15 | X/Y/Z negative peak | mg | R | Magnitude, reported positive |
| 16–18 | X/Y/Z negative peak | cm/s² | R | |
| 19 | Max ISR time | µs | R | Cleared by CLEAR_DIAG |
| 20 | Max loop time | 100 µs units | R | Multiply by 100 for µs |
| 21 | Slave ID | — | **R/W** | 1–247, saved to EEPROM |
| 22 | Peak trip threshold | mg | **R/W** | 10–15000, saved to EEPROM |
| 23 | Triaxial vector RMS | mg | R | √(x²+y²+z²), 10 ms window |
| 24 | Effective peak threshold | mg | R | Echo of reg 22 |
| 25 | Status bits | — | R | bit0 tripped, bit1 impact latched, bit2 block missed, bit3 config defaulted |
| 26 | Blocks missed | — | R | Cleared by CLEAR_DIAG |
| 27 | Measured sample rate | samples/s | R | ~1589 measured; nominal 1600 |
| 28 | Command | — | **W** | Self-clearing trigger, see above |
| 29 | Reset cause | — | R | Raw AVR `MCUSR` at boot |
| 30 | Peak vector RMS hold | mg | R | Since last CLEAR_PEAKHOLD |
| 31 | Trip count | — | R | Since last CLEAR_TRIPCOUNT |
| 32 | Gravity X | mg | R | **SIGNED** — decode as two's complement |
| 33 | Gravity Y | mg | R | **SIGNED** |
| 34 | Gravity Z | mg | R | **SIGNED** |
| 35 | *reserved* | — | R | Always 0. Was the sustained-RMS trip threshold, removed in rev H |
| 36 | Peak instantaneous \|a\| hold | mg | R | Since last CLEAR_PEAKHOLD |
| 37 | Last impact \|a\| | mg | R | **READ-TO-CLEAR** — zeroed once the response is on the wire |
| 38–40 | X/Y/Z RMS, 1 s window | mg | R | ~1.8% jitter, for trending |
| 41 | Vector RMS, 1 s window | mg | R | |
| 42 | Firmware version | — | R | `major << 8 \| minor`; rev H = 0x0108 |
| 43 | Register map version | — | R | **Check this first**; currently 8 |
| 44 | Build date | — | R | `(year-2000) << 9 \| month << 5 \| day` |
| 45 | Last command echo | — | R | See above |
| 46 | Command status | — | R | 0 idle, 1 accepted, 2 unknown code |
| 47 | Command count | — | R | Accepted commands since boot |
| 48 | *reserved* | — | R | Always 0. Was the echo of reg 35 |

Registers 32–34 carry a two's-complement bit pattern. Read them as
signed 16-bit; every other register is unsigned.

Register 37 is cleared as soon as the response frame carrying it has been
transmitted, so each impact is reported to exactly one reader. If two
masters poll the same device, they will steal impacts from each other.
An impact that arrives between the response and the clear is *not* wiped
— it goes out on the next read.

## Reading the whole map in one request

The firmware ships with a patched `SimpleModbusSlave` whose buffer is 128
bytes, so a single function-3 request can return up to 61 registers. All
49 registers come back in one sweep (response = 5 + 2×49 = 103 bytes).

Against the **unpatched** library (64-byte buffer) the cap is 29
registers and a full sweep needs two requests. If reading 49 registers
times out, you are running the unpatched library.

At 9600 baud a 49-register read occupies the line for roughly 130 ms, so
poll no faster than about 4 Hz and leave headroom.

## Accuracy and timing notes (rev G)

**Regs 23, 30 and 41 read 1–2 counts (~4–8 mg) higher than rev F.** The
old firmware computed the triaxial vector RMS by rooting each axis,
squaring the results back up and rooting the sum — rounding down three
times before the final root. Rev G sums the mean squares directly, which
is the same quantity without the double rounding. This is a correction,
not drift, but if you are comparing logs across the two firmwares expect
a small step. Register 43 tells you which side of it you are on.

**There is exactly one way to open the interlock: register 22, tested
per sample.** Rev G could also trip on sustained vector RMS via register
35. A trip therefore meant either "something hit it" or "it has been
shaking a while", with nothing in the map to say which — register 25 bit
0 is the same bit either way. That path is gone in rev H.

Sustained vibration is still fully measured. Registers 38–41 (1 s RMS)
and register 30 (peak vector-RMS hold) are untouched. Trend and alarm on
those on the Pi, where the time constant and hysteresis are visible and
adjustable, rather than inside an ISR where they are neither.

**Impact detection latency is 0.63 ms, one sample period.** Rev F
required two consecutive samples over threshold (1.26 ms) — that second
sample was also the floor on the shortest strike the device could see at
all. One sample period is the hard floor; nothing shorter is detectable
at any amplitude. The trade is that a single corrupted sample can now
trip the output, so watch reg 31 (trip count) against reg 37 (impact
magnitude): a trip whose magnitude is implausible for the machine is the
signature of a bad read rather than a real event.

## Map version history

| Version | Firmware | What changed for the master |
|---:|---|---|
| 4 | rev D | mg units, peak trip on reg 22, reg 23 was the L1 sum |
| 5 | rev E | Registers became unsigned, reg 23 became true vector RMS, reg 37 became read-to-clear, regs 38–41 added, reg 3 renamed from a temperature |
| 6 | rev F | Reg 19 became Timer1-based, identification regs 42–44 added |
| 7 | rev G | Command acknowledge regs 45–47 added, reg 48 added, writes to regs 21/35 are no longer discarded when a read interleaves. Regs 23/30/41 read 1–2 counts higher (double rounding removed) and impact latency halves to 0.63 ms — neither changes a register's meaning or units |
| 8 | rev H | **Sustained-RMS tripping removed.** Registers 35 and 48 are reserved and always read 0. A map-7 master would write register 35 and believe it had armed a trip that no longer exists — which is exactly what a map version is for |
