# ADXL345 electrostatic self-test: finding, and why it is not being added

**Outcome: not implemented. This is a reasoned refusal, not an omission.**
`bootCheck()` is left exactly as it is.

T-04 allowed either a working boot-only self-test or a written finding
explaining why it should not be added. This is the second. Two
independent blockers, either one sufficient on its own.

## What was asked

Read the vendored SparkFun driver, establish whether a self-test entry
point exists, what it is called, and what it does to `DATA_FORMAT`
(0x31). If it exists, implement a boot-only self-test gated inside the
`SETTLE_SAMPLES` window, comparing per-axis deltas against the datasheet
response limits for the configured range and supply voltage.

## Blocker 1 — there is no vendored driver to read

The sketch header and `bootCheck()` both say the self-test is unimplemented
because "that API was not confirmed against the vendored copy." The
reason it was never confirmed is more basic than it sounds:

**The vendored copy is not in this repository, and never has been.**

Both sketches do:

```c
#include "SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp"
```

and the CTX310 README states plainly: *"The ADXL345 driver
(`SparkFun_ADXL345-master/`) is not vendored here and must be present
alongside the sketch."* The only ADXL file under source control is
`test/stubs/SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp` — 33
lines, self-described as a host stand-in, whose `readAccel()` replays
whatever a test pushed into it. That is a test double. It tells you
nothing about the real driver's API.

So the question T-04 asks cannot be answered from this repository at all.

### What *can* be said, from upstream

Upstream SparkFun
(`github.com/sparkfun/SparkFun_ADXL345_Arduino_Library`) does have the
entry point:

```c
bool getSelfTestBit();
void setSelfTestBit(bool selfTestBit);
```

and `setSelfTestBit()` is implemented as
`setRegisterBit(ADXL345_DATA_FORMAT, 7, selfTestBit)` — i.e. bit 7 of
0x31, which is the correct SELF_TEST bit. Every method and constant the
two sketches use also exists upstream.

**This does not transfer to the product's driver.** The file is
`mFFT_`-prefixed, which announces it as a modified variant, and there is
direct evidence it diverges: the sketch defines
`ADXL345_INT_DATA_READY_BIT` itself, and compiling against the *upstream*
header raises a redefinition warning for exactly that symbol. Upstream
defines it; the product's copy evidently does not. A driver that has
already diverged on its constants is not one whose method set can be
assumed.

Implementing `adxl.setSelfTestBit(true)` against a driver nobody in this
container can see would be writing a call that may not compile, or worse,
may compile against a modified implementation that does something
different with 0x31 — in the boot path of a device that operates a brake.

## Blocker 2 — the pass/fail limits are not determinable

Even granting the API, the test needs thresholds, and T-04 is right to
require them "for the configured range and supply voltage." The ADXL345's
self-test response is specified per supply voltage, and the deflection
scales substantially with it — the limits at 2.5 V and at 3.3 V are not
the same numbers.

**The board's supply voltage is not established anywhere in this
repository.** It is not in the sketch, the register maps, the hardware
validation notes, or the README. In the v0.1 architecture it was
assumption **A3**, explicitly marked `[ASSUMED — CONFIRM]`, and v0.2's
table of corrected assumptions does not resolve it — v0.2 fixed the
interface (SPI, not I²C) but not the supply.

That leaves two ways to pick limits, both bad:

- **Too wide** — the test passes a part that is drifting, and the boot
  check is now theatre: it reports a pass that means nothing.
- **Too narrow** — the test fails a healthy part, sets `FAULT_BOOTCHECK`,
  and by the fault policy engages the arrest and opens health. A
  false-failing self-test on an arrest device stops the customer's
  machine at power-up for no reason, and the failure mode is one that
  gets diagnosed by disabling the check.

Guessing here would put an invented number in the trip path of a
protective function. The datasheet limits must come from the datasheet,
against a confirmed supply rail.

## Why the current `bootCheck()` is the right thing to keep

It verifies the part delivers plausible, *changing* data before arming:
raw magnitude within `PLAUSIBLE_MIN_MG`..`PLAUSIBLE_MAX_MG`, and
`stuckCount` below 100 so an unchanging value is caught. It is honest
about its own strength — the comment says outright that it is "NOT a real
self-test."

That is weaker than an electrostatic deflection test. It is also correct,
bounded, and cannot false-trip on an unconfirmed threshold. The work
package's own framing applies: *a fragile self-test in an arrest path is
worse than an honest boot plausibility check.*

## What would unblock this

In order — each is a prerequisite for the next:

1. Put `SparkFun_ADXL345-master/` under source control, beside each
   sketch. Nothing about this driver can be reviewed, built reproducibly,
   or reasoned about while it lives only on a developer's machine. This
   also blocks final resource figures (see `RESOURCE_BUDGET.md`) and any
   real ISR cycle count (T-03).
2. Confirm `setSelfTestBit()` exists in *that* copy and still writes only
   bit 7 of 0x31.
3. Confirm the ADXL345 supply rail on the board, and take the
   corresponding self-test response limits from the datasheet for ±16 g
   full resolution.

Only then is the implementation itself a small piece of work: assert the
bit inside the `SETTLE_SAMPLES` window, let the part settle, take the
per-axis deltas, clear the bit, publish to register 62 and set
`FAULT_BOOTCHECK` on failure.

**The gating requirement survives regardless.** Whenever this is picked
up, the self-test must be unreachable while the detector is armed —
`bootState == BOOT_PENDING` and inside `SETTLE_SAMPLES`, with a test that
proves a call outside that window cannot deflect the axes. It perturbs
the signal that operates the arrest device; that is the whole reason it
is boot-only.
