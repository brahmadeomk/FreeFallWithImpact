# Flash and SRAM budget — CTX310 rev H and CTX311 rev A

**Built, not estimated.** Figures come from a real `avr-gcc` link for the
ATmega328P against the ADXL345 driver now vendored in this repository at
`vendor/SparkFun_ADXL345-master/`. Reproduce with `tools/build_avr.sh`.

Still not a measurement of a running unit: nothing has been flashed. See
"What free SRAM does and does not mean".

## Why this exists

The architecture document (v0.2, §C7) carries a static SRAM figure of
**~620 B**, arrived at by reading the source. That number had never been
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
| Flash (`.text` + `.data`) | 14678 B (44.8 %) | 17630 B (53.8 %) |
| Static SRAM (`.data` + `.bss`) | 916 B (44.7 %) | **1042 B (50.9 %)** |
| Free SRAM (static) | 1132 B (55 %) | **1006 B (49 %)** |
| `.text` | 14620 B | 17564 B |
| `.data` | 58 B | 66 B |
| `.bss` | 858 B | 976 B |

CTX311 costs **+2952 B flash and +126 B static SRAM** over CTX310. The
SRAM delta is mostly the raised `BUFFER_SIZE` (128 → 160, +32 B) plus the
loss-of-support state and the extra registers.

**Against the ≥ 15 % free-SRAM acceptance threshold: 49 % free, passes
with a wide margin.**

Figures include the T-05 arming-window change (+50 B flash, no SRAM
change).

### Where the ISR time goes

`myHandler()` is 3380 B of that flash. Removing the loss-of-support
block entirely takes it to 2848 B, so the block costs **532 B**, of
which **402 B is the detection itself** and 130 B the stuck-data check.
The cycle counts are in the sketch header and were computed the same
way — see T-03.

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
tools/build_avr.sh CTX311_LossOfSupport_revA <core-dir> vendor/SparkFun_ADXL345-master
```

- toolchain: `avr-gcc (GCC) 7.3.0`, `GNU size (GNU Binutils) 2.26.20160125`
  (Ubuntu noble `gcc-avr` 1:7.3.0+Atmel3.7.0-1, `avr-libc`
  1:2.0.0+Atmel3.7.0-1)
- core: `github.com/arduino/ArduinoCore-avr`, tag `1.8.6`
- driver: `vendor/SparkFun_ADXL345-master/` — see `vendor/PROVENANCE.md`
- flags: `-Os -ffunction-sections -fdata-sections -mmcu=atmega328p
  -DF_CPU=16000000L`, linked `-Wl,--gc-sections`

`arduino-cli` was installed but could **not** fetch the AVR core:
`downloads.arduino.cc` returns **403 Forbidden** through the container's
proxy, for both `package_index.tar.bz2` and `library_index.tar.bz2`.
`github.com` is reachable, so the core was taken from source there
instead. Reporting the blocked domain rather than working around it, as
the work package asks.

## A note on the driver and these numbers

An earlier revision of this document was marked PROVISIONAL because the
driver was not in the repository and the build had substituted the
upstream SparkFun library. The driver has since been confirmed as the
official SparkFun library and vendored, and **the figures did not move** —
they were built against the same code all along. The PROVISIONAL banner
and the substitution caveat are withdrawn.

That earlier revision also inferred, from a redefinition warning on
`ADXL345_INT_DATA_READY_BIT`, that the product's driver must differ from
upstream. **That inference was wrong.** Both define the same value (the
sketch as `7`, the driver as `0x07`); the warning simply meant the two
definitions collided. The sketch now guards its copy with `#ifndef`, so
the warning is gone and the constant still resolves for the host stub
build, where the driver is not present.

The remaining figure that is genuinely still open is the stack
high-water mark, above.
