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

The estimate was low. Built figure below: **1107 B for CTX311**, 79 %
above the estimate.

## Figures

Arduino Nano, ATmega328P, 16 MHz. 32256 B flash available to the sketch
(bootloader occupies the rest), 2048 B SRAM.

| | CTX310 rev H | CTX311 rev A |
|---|---:|---:|
| Flash (`.text` + `.data`) | 14678 B (44.8 %) | 20608 B (62.9 %) |
| Static SRAM (`.data` + `.bss`) | 916 B (44.7 %) | **1107 B (54.1 %)** |
| Free SRAM (static) | 1132 B (55 %) | **941 B (46 %)** |
| `.text` | 14620 B | 20532 B |
| `.data` | 58 B | 76 B |
| `.bss` | 858 B | 1031 B |

CTX311 costs **+5930 B flash and +191 B static SRAM** over CTX310. The
SRAM delta is mostly the raised `BUFFER_SIZE` (128 → 160, +32 B) plus the
loss-of-support state and the extra registers.

**Against the ≥ 15 % free-SRAM acceptance threshold: 46 % free, passes
with a wide margin.**

The tilt block (map 10) costs **+1908 B flash and +33 B SRAM**. Most of
the flash is not the 182-byte sine table but the 32-bit divides the
normalisation needs — the ATmega328P has no divide instruction, so
libgcc's routines get linked in. The table itself is in PROGMEM: as
`.bss` it would have been 18 % of the free SRAM.

Supply monitoring (map 11) costs **+294 B flash and +15 B SRAM** — it
reuses `isqrt32`-free integer division and the otherwise idle ADC. The
low-supply advisory (map 12) adds a further **+330 B flash and +9 B
SRAM**. The sensor-communication fault (map 13 — the zero-data check
and the trip-instant attribution) adds **+294 B flash and +4 B SRAM**,
of which **+186 B is inside `myHandler()`**. Sensor re-initialisation
(map 14) adds **+152 B flash and +4 B SRAM**, and **none of it is in
the ISR** — `myHandler()` is unchanged at 3566 B, because the recovery
runs entirely on the 1 Hz tick in `loop()`.

**Watch the Modbus buffer, not the SRAM.** A full sweep is now 74
registers = **153 bytes**, against `BUFFER_SIZE` 160 — **7 bytes spare**,
so three more registers. SRAM is not the binding constraint here;
the frame is. Raising `BUFFER_SIZE` is affordable on these figures, but
it was listed out of scope in the work package pending exactly these
numbers, so it is a decision to take deliberately rather than by
drifting into it.

Figures include the T-05 arming-window change (+50 B flash, no SRAM
change).

### Where the ISR time goes

`myHandler()` is **3566 B** of that flash. Removing the loss-of-support
block entirely took it to 2848 B when the block was first landed, so
the block cost **532 B** then — **402 B** the detection itself and
130 B the stuck-data check — and the map-13 zero-data check has since
added **186 B** on top. The cycle counts are in the sketch header and
were computed the same way — see T-03.

The map-13 addition sits on the ISR's **sustained** path, so it is the
one to watch: 10 cycles (0.6 µs) on a live sensor with a non-zero X
axis, 16 (1.0 µs) if the part is mounted with X and Y both near zero.
Against a 629 µs sample period that is under 0.2 %. The trip-instant
attribution runs on one sample per event and does not enter the
sustained figure.

## What "free SRAM" here does and does not mean

The 941 B above is **static** free SRAM. It is the space the stack has
to live in, not headroom known to be spare. It does not say how much of
that the stack actually consumes — the deepest path is an ISR firing on
top of `loop()` inside a Modbus response. The instrumentation below
answers that, but only once it has been flashed.

### The stack high-water instrumentation

It is written, and it is **off by default**. Build it with:

```sh
EXTRA=-DCTX311_STACK_DEBUG tools/build_avr.sh \
    CTX311_LossOfSupport_revA <core-dir> vendor/SparkFun_ADXL345-master
```

**How it works.** A naked routine in `.init1` — before `.bss` is
cleared, before `main()`, before anything has touched the stack — paints
the whole free region with `0xC5`. It is written in assembler because a C
body would put its own locals on the very stack it is painting.
`stackUnusedBytes()` then counts canaries still standing from `_end`
upward, stopping at the first byte the stack has reached. That count is
**minimum free SRAM since reset** — the high-water mark. Small is bad.

**Where it reports, and why that is not a new register.** T-02 forbids
adding one, so it borrows **register 48**, a reserved hole that reads 0
in a release build. Register 48 rather than 35 on purpose: in map 8
register 35 was *writable*, so a legacy master could still write it and
fight the debug value on the wire, while 48 was only ever 35's read-only
echo. Borrowing a hole also costs no diagnostic — the soak needs
registers 19, 26, 27, 31 and 54 intact, and overloading any of those
would spoil the run this exists to serve.

**This image must not ship.** A release build publishes 0 in register
48; the debug build does not. That is the whole reason it is behind a
flag.

| Build | Flash | Static SRAM | `stackPaint` in image |
|---|---:|---:|---|
| release | 20608 B | 1107 B | absent |
| `-DCTX311_STACK_DEBUG` | 20654 B (+46 B) | 1107 B (no change) | present |

Verified in the linked image rather than assumed: the painter survives
`--gc-sections`, loads `Z = _end` (0x0512), the canary `0xC5`, and loops
to `__stack` (0x08FF) — 1005 bytes painted — and the counter's result is
stored to `holdingRegs+0x60`, which is register 48.

`test/run_tests.sh` builds and runs the CTX311 suite a third time with
the flag on. The guarded code is invisible to a normal build, so that
pass is what stops it rotting; and because the host stub returns 0, all
265 assertions must still hold — if that pass ever diverges, the debug
build has started changing behaviour it should not.

**The number itself still needs hardware.** Nothing here has been
flashed. Read register 48 at the end of the H-05 soak, then put a
release build back on.

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
