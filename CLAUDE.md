# CLAUDE.md — CTX310 / CTX311 firmware

Persistent context for Claude Code working in this repository. Read
this before touching anything.

---

## Repository layout, and a trap

**The code is not on `main`.** `main` holds only a README. Everything
lives on branch `claude/impact-detect-vib-sensor-yccai9`. Check out that
branch first; a clone that looks empty is this, not a broken clone.

```
CTX310_Vibration_revH/     shipping impact + vibration monitor (rev H)
CTX311_LossOfSupport_revA/ loss-of-support monitor (rev A) — NEW
docs/REGISTER_MAP.md       CTX310 map v8
docs/REGISTER_MAP_CTX311.md CTX311 map v9
docs/HARDWARE_VALIDATION.md real measurements from real units
test/run_tests.sh          builds and runs BOTH suites
test/test_registers.cpp    CTX310, 79 checks
test/test_ctx311.cpp       CTX311, 130 checks
test/stubs/                host stand-ins for core, SPI, EEPROM, ADXL
tools/ctx310_client.py     Modbus client
```

## Build and test

```sh
./test/run_tests.sh
```

No AVR toolchain needed — the stubs let both sketches compile and run on
the host. Current state: **79 + 130 checks, 0 failures.** Both suites
must stay green.

There is no AVR toolchain in the container by default. If a task needs
flash/SRAM figures, install `arduino-cli` or `gcc-avr` and say in the
commit message that the numbers came from a build, not from inspection.

## What these devices are

**CTX310** — triaxial impact and vibration monitor. 1600 Hz, ADXL345 over
SPI, ATmega328P, Modbus RTU. Drives an interlock on PC0.

**CTX311** — derived from CTX310 rev H. Detects **loss of support** on a
jacked or climbing structural assembly and drives an **arrest / brake
device** on PC1.

CTX311 is a **protective function**. It is built from commercial parts
with no redundancy and was not developed under IEC 61508 or ISO 13849.

- **Never add a SIL or PL claim** to any file, comment, doc or commit.
- **Never remove or soften** the limitation notices in the sketch header
  or `docs/REGISTER_MAP_CTX311.md`.
- If a change would weaken a diagnostic or a fail-safe default, stop and
  say so rather than making it.

## Invariants — do not break these

1. **Both test suites stay green.** The 79 CTX310 checks are a
   regression net for field-proven behaviour. If CTX311 work breaks one,
   something that works in the field has been broken. Fix the code, not
   the assertion, unless the change is deliberate and documented.
2. **Decisions in the ISR, presentation in `loop()`.** No roots, no
   divides, no unit conversion, no EEPROM, no Modbus in interrupt
   context.
3. **The ISR may open an output. `loop()` may only close it.**
4. **One meaning per output pin.** PC0 = impact. PC1 = loss of support.
   PC2 = health. Never make one pin mean two things — rev H exists
   largely because rev G did that.
5. **Loss-of-support detection reads RAW `x,y,z`, before the DC
   tracker.** Everything below the tracker is AC-coupled through a
   0.124 Hz corner and is structurally blind to a sustained unload.
6. **Registers are never renumbered.** Removed ones become reserved
   holes reading 0. Any change that could make an existing master
   misread the device bumps `REGISTER_MAP_VERSION`.
7. **Settings hold; triggers self-clear.** A settings register keeps its
   written value and echoes through a paired effective-value register;
   `publishBlock()` must never write one. Trigger registers self-clear
   and report through the acknowledge registers.
8. **No dynamic allocation, no floating point, no blocking delays.**
   Fixed-point integer maths only. Check the map file for float library
   linkage if you touch arithmetic.
9. **Trip timing uses the fixed `LOS_ODR_HZ`, never the live measured
   rate.** A protective function's trip time must not drift with a
   measurement.

## House style

The existing comments explain **why**, including what was tried and
rejected and what the failure mode of a choice is. Match that. A comment
that restates the code is worse than none.

Where a constant encodes a trade-off, name the trade-off and its failure
signature — see `PEAK_CONFIRM` and `LOS_SPIKE_TOLERANCE`, which pull in
opposite directions for opposite reasons.

## Claims discipline

From `docs/HARDWARE_VALIDATION.md`: a claim is a hypothesis until a unit
confirms it. Static analysis of compiled output is not evidence.

When writing a figure into a doc or comment, mark how it was obtained:
measured on hardware, computed from a build, or estimated from source.
The ISR cost figure in the CTX311 header is currently an **estimate** and
is labelled as one. Do not quietly promote it.
