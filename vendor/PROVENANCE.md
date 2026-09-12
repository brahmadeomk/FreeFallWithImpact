# Vendored third-party code

## `SparkFun_ADXL345-master/`

The ADXL345 driver both sketches depend on. Vendored here so the
firmware can be built, reviewed and reasoned about from a clean clone —
previously it lived only on a developer's machine, which blocked the
resource figures, the ISR cycle count and the self-test question all at
once.

| | |
|---|---|
| Upstream | `github.com/sparkfun/SparkFun_ADXL345_Arduino_Library` |
| Commit | `6d795cce285d94014c294d297573e33266a4c7d5` (2019-11-06) |
| Version | 1.0.0 per `library.properties` |
| Files taken | `src/SparkFun_ADXL345.cpp`, `src/SparkFun_ADXL345.h`, `library.properties` |
| Modifications | **none** — byte-for-byte upstream |

**Licence: open source**, per upstream's own README, which states it
directly:

> ## License Information
>
> This product is _**open source**_!
>
> Please review the LICENSE.md file for license information.
>
> If you have any questions or concerns on licensing, please contact
> techsupport@sparkfun.com.
>
> Distributed as-is; no warranty is given.

The `LICENSE.md` it refers to is **not present in the repository at this
commit**, so no licence text could be vendored alongside the code and the
specific identifier (MIT, CC-BY-SA, beerware — SparkFun have used all
three across their projects) is not pinned here. The statement above is
the licence evidence this repository carries.

If a specific SPDX identifier is ever needed — for an SBOM, a customer
licence schedule, or an attribution notice — take it from a later
upstream release or from techsupport@sparkfun.com, and record it here.
Nothing about the vendoring or the build depends on that.

The file headers credit E. Robert at SparkFun Electronics and note the
source as a modified Bildr ADXL345 driver.

### Why it is here and not beside the sketches

Arduino resolves `#include "SparkFun_ADXL345-master/SparkFun_ADXL345.cpp"`
relative to the sketch folder, so for an IDE build the directory has to
sit beside the `.ino`. It deliberately does **not** live there in git:

the host test suite (`test/run_tests.sh`) compiles each sketch against
the stubs in `test/stubs/`, including a stand-in ADXL driver that replays
samples the tests push into it. A quoted include resolves in the
includer's directory first, so a real driver sitting beside the sketch
would shadow the stub, drag in `Wire`, and break the tests — the stub
would never be reached.

Keeping the real driver at `vendor/` means:

- `test/run_tests.sh` gets the stub, as it must;
- `tools/build_avr.sh` is pointed at `vendor/SparkFun_ADXL345-master`
  and copies it into the build directory, so the AVR build gets the real
  driver;
- for an Arduino IDE build, copy or symlink
  `vendor/SparkFun_ADXL345-master/` next to the `.ino`.

### The one sketch-side accommodation

Both sketches define `ADXL345_INT_DATA_READY_BIT` themselves, because the
host stub does not provide it. The official driver defines the same
constant (`0x07`), so against a real build the two collided and produced
a redefinition warning. The sketches now guard their copy with `#ifndef`,
which keeps the stub build working and silences the warning without
changing any value.
