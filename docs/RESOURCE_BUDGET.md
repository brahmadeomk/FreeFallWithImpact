# Flash and SRAM budget — CTX310 rev H and CTX311 rev A

**Status: PROVISIONAL. Built, not estimated — but not built against the
product's own ADXL345 driver.** Read "The substitution" before quoting
any number here. T-02's acceptance is *not* met by this document; the
figures become final when the build is repeated with the real driver.

## Why this exists

The architecture document (v0.2, §C7) carries a static SRAM figure of
**~620 B**, arrived at by reading the source. That number has never been
linked. Static SRAM is a bad thing to estimate by eye: `.bss` is
dominated by what the libraries pull in — `HardwareSerial`'s two 64-byte
ring buffers, the Modbus frame buffer, the register array — and not by
the sketch's own declarations, which are what you see when you read the
source.

The estimate was low. Measured below: **1042 B for CTX311**, 68 % above
the estimate.

## Figures

Arduino Nano, ATmega328P, 16 MHz. 32256 B flash available to the sketch
(bootloader occupies the rest), 2048 B SRAM.

| | CTX310 rev H | CTX311 rev A |
|---|---:|---:|
| Flash (`.text` + `.data`) | 14678 B (44.8 %) | 17580 B (53.6 %) |
| Static SRAM (`.data` + `.bss`) | 916 B (44.7 %) | **1042 B (50.9 %)** |
| Free SRAM (static) | 1132 B (55 %) | **1006 B (49 %)** |
| `.text` | 14620 B | 17514 B |
| `.data` | 58 B | 66 B |
| `.bss` | 858 B | 976 B |

CTX311 costs **+2902 B flash and +126 B static SRAM** over CTX310. The
SRAM delta is mostly the raised `BUFFER_SIZE` (128 → 160, +32 B) plus the
loss-of-support state and the extra registers.

**Against the ≥ 15 % free-SRAM acceptance threshold: 49 % free, passes
with a wide margin** — and see the caveat below, which says the real
figure is very likely better still, not worse.

## The substitution

Both sketches contain:

```c
#include "SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp"
```

That file **is not in this repository** and never has been — the CTX310
README says so outright: *"The ADXL345 driver
(`SparkFun_ADXL345-master/`) is not vendored here and must be present
alongside the sketch."* The only copy here is a 33-line host stub under
`test/stubs/`, written for the tests, which is not the driver.

So the build above substituted the **upstream** SparkFun library
(`github.com/sparkfun/SparkFun_ADXL345_Arduino_Library`) for the
product's `mFFT_`-prefixed variant. Every method and constant the
sketches use is present upstream, so it links — but it is a different
file, and two things say the product's copy is genuinely modified:

- the `mFFT_` prefix itself;
- the sketch has to `#define ADXL345_INT_DATA_READY_BIT` itself, and
  doing so against the upstream header produces a *redefinition warning*
  — meaning the product's driver evidently does **not** define it, and
  upstream does.

**Direction of the error.** The substitution is expected to *overstate*
both figures. Upstream pulls in `Wire` (I²C) — a `TwoWire` instance with
its own buffers — which this product cannot use: it runs the ADXL345 over
**SPI**. A driver built for the SPI path alone should link smaller. So
treat 1042 B as an upper bound on static SRAM and 49 % as a lower bound
on free SRAM. The threshold conclusion holds either way; the exact
numbers do not.

## What "free SRAM" here does and does not mean

The 1006 B above is **static** free SRAM. It is the space the stack has
to live in, not headroom known to be spare. Nothing here measures how
much of it the stack actually consumes — the deepest path is an ISR
firing on top of `loop()` inside a Modbus response, and that has not been
instrumented.

**The stack high-water instrumentation asked for in T-02 has not been
written.** Painting free SRAM at boot and reading back the high-water
mark only produces a number once it has been flashed and soaked on a
unit, and nothing has been flashed. Adding untested code to an
arrest-path sketch to produce a figure nobody can read yet is the wrong
order of operations; it should go in alongside the soak run (H-05).

## Reproducing

```sh
tools/build_avr.sh CTX311_LossOfSupport_revA <core-dir> <adxl-driver-dir>
```

- toolchain: `avr-gcc (GCC) 7.3.0`, `GNU size (GNU Binutils) 2.26.20160125`
  (Ubuntu noble `gcc-avr` 1:7.3.0+Atmel3.7.0-1, `avr-libc`
  1:2.0.0+Atmel3.7.0-1)
- core: `github.com/arduino/ArduinoCore-avr`, tag `1.8.6`
- flags: `-Os -ffunction-sections -fdata-sections -mmcu=atmega328p
  -DF_CPU=16000000L`, linked `-Wl,--gc-sections`

`arduino-cli` was installed but could **not** fetch the AVR core:
`downloads.arduino.cc` returns **403 Forbidden** through the container's
proxy, for both `package_index.tar.bz2` and `library_index.tar.bz2`.
`github.com` is reachable, so the core was taken from source there
instead. Reporting the blocked domain rather than working around it, as
the work package asks.

## To finalise

1. Drop the real `SparkFun_ADXL345-master/` beside each sketch.
2. Re-run `tools/build_avr.sh` for both.
3. Replace the table above and delete the PROVISIONAL banner.

Until then the ~620 B figure in the architecture document should be read
as **superseded and wrong**, and the numbers here as close but not final.
