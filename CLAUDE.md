# CLAUDE.md — VRM315-IM / VRM315-IMLOS firmware

Persistent context for Claude Code working in this repository. Read
this before touching anything.

---

## Repository layout, and a trap

**The code is not on `main`.** `main` holds only a README. Everything
lives on branch `claude/impact-detect-vib-sensor-yccai9`. Check out that
branch first; a clone that looks empty is this, not a broken clone.

```
VRM315_IM_revH/                   impact + vibration monitor (rev H)
VRM315_IMLOS_revA/                loss-of-support monitor (rev A)
docs/REGISTER_MAP_VRM315_IM.md    VRM315-IM map v8
docs/REGISTER_MAP_VRM315_IMLOS.md VRM315-IMLOS map v15
docs/HARDWARE_VALIDATION.md       real measurements from real units
docs/DROP_TEST_PROTOCOL.md        field procedure, register codes, runbooks
docs/RESOURCE_BUDGET.md           built flash/SRAM figures
test/run_tests.sh                 builds and runs ALL FOUR passes
test/test_vrm315_im.cpp           VRM315-IM, 79 checks
test/test_vrm315_imlos.cpp        VRM315-IMLOS, 279 checks
test/test_modbus.cpp              SimpleModbusSlave frames, 54 checks
test/stubs/                       host stand-ins for core, SPI, EEPROM, ADXL
tools/vrm315_im_client.py         Modbus client, map v8
tools/vrm315_imlos_client.py      Modbus client, map v15
tools/vrm315_imlos_capture.py     drop-rig CSV capture
tools/build_avr.sh                real AVR build, flash/SRAM figures
vendor/                           third-party code -- see vendor/PROVENANCE.md
```

The ADXL345 driver lives at `vendor/SparkFun_ADXL345-master/`, NOT beside
the sketches. Putting it beside a sketch shadows the host test stub and
breaks `test/run_tests.sh` -- a quoted include resolves in the includer's
directory first. `tools/build_avr.sh` copies it in for AVR builds.

## Build and test

```sh
./test/run_tests.sh
```

No AVR toolchain needed — the stubs let both sketches compile and run on
the host. Current state: **54 + 79 + 279 + 279 checks, 0 failures** — the
third pass is the IMLOS suite rebuilt with `-DVRM315_STACK_DEBUG`. All
three must stay green.

There is no AVR toolchain in the container by default. If a task needs
flash/SRAM figures, install `arduino-cli` or `gcc-avr` and say in the
commit message that the numbers came from a build, not from inspection.

## Naming

The series is **VRM315**. Two models:

- **VRM315-IM** — impact and vibration. Was CTX310 before the rename.
- **VRM315-IMLOS** — adds loss-of-support detection. Was CTX311.

`CTX310` and `CTX311` appear nowhere in the tree any more. Commits before
the rename use the old names; that is history, not a second product.

## What these devices are

**VRM315-IM** — triaxial impact and vibration monitor. 1600 Hz, ADXL345 over
SPI, ATmega328P, Modbus RTU. Drives an interlock on PC0.

**VRM315-IMLOS** — derived from VRM315-IM rev H. Detects **loss of support** on a
jacked or climbing structural assembly and drives an **arrest / brake
device** on PC1.

VRM315-IMLOS is a **protective function**. It is built from commercial parts
with no redundancy and was not developed under IEC 61508 or ISO 13849.

- **Never add a SIL or PL claim** to any file, comment, doc or commit.
- **Never remove or soften** the limitation notices in the sketch header
  or `docs/REGISTER_MAP_VRM315_IMLOS.md`.
- If a change would weaken a diagnostic or a fail-safe default, stop and
  say so rather than making it.

## Invariants — do not break these

1. **Both test suites stay green.** The 79 VRM315-IM checks are a
   regression net for field-proven behaviour. If VRM315-IMLOS work breaks one,
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

The ISR cost figure in the VRM315-IMLOS header is now **computed from a build**
(disassembly of `myHandler()`, cycles counted per path) and is labelled
as such. It replaced an estimate that was low by ~2.5x. It is still not
a measurement: it excludes interrupt entry/exit. **Do not promote it to
"measured" until register 19 has been read off a flashed unit (H-03).**

The flash and SRAM figures in `docs/RESOURCE_BUDGET.md` are likewise
built, not measured.

The stack high-water mark is **instrumented but unmeasured**. The code
exists behind `-DVRM315_STACK_DEBUG` and reports through reserved
register 48; nothing has been flashed, so there is still no number.
A release build must read 0 at register 48 — if you ever change that,
you have shipped a debug image.
