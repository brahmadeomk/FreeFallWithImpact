# ADXL345 electrostatic self-test: finding, and why it is still not being added

**Outcome: not implemented. This is a reasoned refusal, not an omission.**
`bootCheck()` is left exactly as it is.

T-04 allowed either a working boot-only self-test or a written finding
explaining why it should not be added. This is the second.

**This document has been revised.** It originally rested on two
blockers. The driver has since been confirmed as the official SparkFun
library and vendored into the repository, which **removes the first
blocker entirely and corrects a claim made here**. The second blocker
stands on its own and is sufficient.

## Blocker 1 — RESOLVED

The entry point exists. In the vendored driver
(`vendor/SparkFun_ADXL345-master/SparkFun_ADXL345.h`):

```c
bool getSelfTestBit();
void setSelfTestBit(bool selfTestBit);
```

`setSelfTestBit()` is implemented as
`setRegisterBit(ADXL345_DATA_FORMAT, 7, selfTestBit)` — bit 7 of `0x31`,
which is the correct SELF_TEST bit. Every method and constant the two
sketches use is present. There is nothing speculative left about the API.

### Correction to the earlier finding

This document previously argued that the product's driver must diverge
from upstream, on two grounds: the `mFFT_` filename prefix, and a
redefinition warning on `ADXL345_INT_DATA_READY_BIT` when the sketch was
compiled against the upstream header.

**Both grounds were wrong.**

The driver is the official SparkFun library, unmodified. The redefinition
warning did not indicate divergence: the sketch defines the constant as
`7` and the driver defines it as `0x07` — the *same value*. The warning
meant only that two identical definitions collided, which is what happens
when the sketch supplies a constant for the benefit of the host stub
build and the real driver also supplies it. The sketch now guards its
copy with `#ifndef` and the warning is gone.

The filename prefix was a naming artefact and carried no information
about the contents. Inferring modification from it was not sound.

## Blocker 2 — the pass/fail limits are still not determinable

This is the one that matters, and vendoring the driver does nothing for
it.

T-04 requires the per-axis deltas be compared "against the datasheet
response limits **for the configured range and supply voltage**". The
ADXL345's self-test response is specified per supply voltage, and the
deflection scales substantially with it — the limits at 2.5 V and at
3.3 V are not the same numbers.

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

## Why the current `bootCheck()` is the right thing to keep meanwhile

It verifies the part delivers plausible, *changing* data before arming:
raw magnitude within `PLAUSIBLE_MIN_MG`..`PLAUSIBLE_MAX_MG`, and
`stuckCount` below 100 so an unchanging value is caught. It is honest
about its own strength — the comment says outright that it is "NOT a real
self-test".

That is weaker than an electrostatic deflection test. It is also correct,
bounded, and cannot false-trip on an unconfirmed threshold. The work
package's own framing applies: *a fragile self-test in an arrest path is
worse than an honest boot plausibility check.*

## What is left to unblock this

Only one thing now:

1. **Confirm the ADXL345 supply rail on the board**, and take the
   corresponding self-test response limits from the datasheet for ±16 g
   full resolution.

The implementation itself is then a small piece of work: assert the bit
inside the `SETTLE_SAMPLES` window, let the part settle, take the
per-axis deltas, clear the bit, publish to register 62 and set
`FAULT_BOOTCHECK` on failure.

**The gating requirement survives regardless.** Whenever this is picked
up, the self-test must be unreachable while the detector is armed —
`bootState == BOOT_PENDING` and inside `SETTLE_SAMPLES`, with a test that
proves a call outside that window cannot deflect the axes. It perturbs
the signal that operates the arrest device; that is the whole reason it
is boot-only.

Note that T-05 has since made the arming window visible on the health
output and on register 49 bit 6, so a boot self-test would now run inside
a window a master can already see and distinguish from a fault. That
makes the eventual implementation easier to integrate, not more urgent.
