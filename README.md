# Impact detection — CTX310 / VRM315 triaxial impact & vibration monitor

ATmega328-based Modbus RTU slave built around an ADXL345 at 1600 Hz,
detecting impacts down to 0.63 ms and driving an interlock output.

Current firmware: **rev H, register map version 8.** Have the master read
register 43 on connect and refuse to ingest an unexpected value.

- `CTX310_Vibration_revH/` — the firmware sketch plus the patched
  `SimpleModbusSlave` library it needs
- `docs/REGISTER_MAP.md` — the register map, written for whoever is
  talking to this thing from a Pi or a PLC
- `tools/ctx310_client.py` — Raspberry Pi client that reads the map and
  sends commands correctly
- `test/` — host-side tests for the register logic, no AVR toolchain
  needed: `./test/run_tests.sh`

## Register 28 always reads back 0. That is correct.

If you write a command to register 28 from the Pi and read it back, you
will see 0 — while register 22 (the threshold) happily holds whatever you
write. That looks like the command write is being dropped. It is not.

The two registers are different kinds of thing:

- **Register 22 is a setting.** It holds its value, is saved to EEPROM,
  and reads back as written.
- **Register 28 is a trigger.** The firmware runs the command on the next
  pass of `loop()` and immediately zeroes the register, so a read-back
  always shows 0.

Register 28 has to self-clear. If it held its value the command would
re-run thousands of times a second — one `CLEAR_PEAKHOLD` would pin the
peak-hold registers to zero permanently.

What was genuinely missing was any way for the master to confirm the
command landed. Firmware rev G adds it:

| Register | Meaning |
|---|---|
| 45 | Echo of the last command code received |
| 46 | 0 = idle, 1 = accepted and run, 2 = code not recognised |
| 47 | Count of accepted commands since boot |

Read register 47, write the command, read 47 again and confirm it moved.
That works even when the same command is sent twice in a row, which an
echo alone cannot tell you. `tools/ctx310_client.py` does exactly this:

```
./tools/ctx310_client.py --port /dev/ttyUSB0 command clear-peakhold
```

## One trip path (rev H)

Rev G could open the interlock two ways: the per-sample peak test
(register 22) and a sustained vector-RMS test (register 35). A trip
therefore meant either *something hit it* or *it has been shaking for a
while*, and nothing in the map said which — register 25 bit 0 is the same
bit either way. For an interlock, that ambiguity is a liability.

**The sustained-RMS trip is removed.** There is now exactly one path to
the output: register 22, tested per sample, 0.63 ms. Gone with it are the
register 35 threshold, its register 48 echo, the EEPROM field, the
validation branch and the ISR comparison.

Sustained vibration is still fully **measured** — registers 38–41 (1 s
RMS) and register 30 (peak vector-RMS hold) are untouched. It just no
longer operates the output. Trend and alarm on it on the Pi, where the
time constant and hysteresis are visible and adjustable, rather than
inside an ISR where they are neither.

Registers 35 and 48 are kept as reserved holes that always read 0, so the
rest of the map does not renumber and the existing dashboard keeps
working. Map version goes to **8**: a master written for map 7 would
write register 35 and believe it had armed a trip that no longer exists.

## Also fixed in rev G: writes to registers 21 and 35 could vanish

Register values are refreshed inside the function-3 handler, so they are
current at read time. Rev F also rewrote registers 21 (slave ID) and 35
(sustained RMS threshold) from the device's own values during that
refresh. If a poll landed between your write and the next pass of
`loop()`, the write was overwritten with the old value before the
firmware ever looked at it, and the setting silently reverted.

Rev G stops the read path from touching any writable register.
`test/test_registers.cpp` covers it — the test fails against the rev F
behaviour and passes against rev G.

Register 35 also has an effective-value echo now (register 48), matching
what register 24 already did for register 22, so a rejected out-of-range
write is distinguishable from an accepted one.

## Also fixed in rev G: the ISR was overrunning its sample budget

A field unit reported **1203 µs** in register 19 (worst-case ISR time)
against a **625 µs** sample period at 1600 Hz — the ISR was still running
when the next two samples arrived.

The cause was `isqrt32()`. Its comment claimed ~200 cycles; the real cost
was ~4200. AVR has no barrel shifter, and at `-Os` gcc compiles
`n >> 30` into a **30-iteration bit-shift loop** — 209 cycles, executed
16 times per call. The block boundary calls `isqrt32()` four times, so
every 16th sample spent ~1050 µs on square roots alone.

Taking the same two bits out of the top byte compiles to five
single-cycle instructions instead, because gcc turns a shift by a
multiple of 8 into register moves:

```c
rem = (rem << 2) | ((uint8_t)(n >> 24) >> 6);   /* NOT (n >> 30) */
```

| | per call | block boundary (4 calls) |
|---|---:|---:|
| before | ~4200 cycles (265 µs) | ~1050 µs |
| after | ~900 cycles (57 µs) | ~230 µs |

Same algorithm, bit-identical output — `test_registers.cpp` compares it
against the old form and against `floor(sqrt(n))` across the full uint32
range on every run.

Confirmed on hardware: register 19 went **1203 → 517 µs**. Register 27
also dropped 1650 → 1589, which is the corroboration — the old ISR ran
longer than Timer0's 1.024 ms overflow period, so `millis()` was losing
ticks and inflating every rate derived from it. 1589 is the true ODR.

## Then: no square roots in the ISR at all

The remaining 517 µs was still ~230 µs of square roots. They are gone
now, using an identity that makes them unnecessary:

```
sqrt(msX)² + sqrt(msY)² + sqrt(msZ)²  ==  msX + msY + msZ
```

The ISR needs the vector magnitude only to *compare* — peak hold and
which block wins the impact snapshot — and comparisons work identically
on squares. So the ISR hands `loop()` mean
squares, and `loop()` takes the roots purely for presentation. That is
the same split register 36 has always used with `peakMag2Hold`.

**The decisions stay in the ISR.** Moving them to `loop()` would have
been a genuine fault, not just a latency change: `loop()` never sees most
blocks while a Modbus read is in flight (register 26 read 3568 on the
field unit), so peak hold would under-report.

Two visible consequences, both improvements:

- Registers 23, 30 and 41 read **1–2 counts (~4–8 mg) higher** — the old
  form rounded each axis down before squaring it back up.

Neither changes a register's units or meaning, so those alone would not
have moved the map version. Removing the RMS trip did — see above.

## `PEAK_CONFIRM` 2 → 1: a single sample now trips

Requiring two consecutive samples over threshold was not just latency —
it was the floor on how sharp an impact the device could see at all.
Anything briefer than 1.26 ms was missed at *any* amplitude, and a hard
metal-on-metal strike is easily shorter than that.

| | before | after |
|---|---:|---:|
| detection latency | 1.26 ms | **0.63 ms** |
| shortest detectable strike | 1.26 ms | **0.63 ms** |

One sample period is the hard floor; nothing shorter is detectable at
any amplitude, whatever the confirm count.

The trade is real: with no consecutive-sample confirmation, a single
corrupted SPI read can trip the output. Sensor noise cannot — the
ADXL345 at 800 Hz bandwidth is ~10–15 mg RMS per axis, so the 3000 mg
default sits roughly 200σ away — but a garbled read is not Gaussian and
only has to happen once. Watch register 31 (trip count) against register
37 (impact magnitude): a trip whose recorded magnitude is implausible
for the machine is the signature. `PEAK_CONFIRM` is one `#define` if you
want it back at 2.

## Building

Open `CTX310_Vibration_revH/CTX310_Vibration_revH.ino` in the Arduino
IDE. The sketch folder carries its own patched copy of
`SimpleModbusSlave` — do **not** let the IDE pick up an installed copy of
the stock library instead. The patches are:

1. `BUFFER_SIZE` raised from 64 to 128, so all 49 registers come back in
   a single function-3 request rather than two.
2. A `modbus_read_complete()` callback after the response is transmitted,
   which is what makes register 37 (last impact magnitude) read-to-clear.
3. A one-byte buffer overrun fixed in the receive loop — the stock
   library writes `frame[BUFFER_SIZE]` before the overflow flag takes
   effect, corrupting whatever follows the array when a noisy line fills
   the buffer.

The ADXL345 driver (`SparkFun_ADXL345-master/`) is not vendored here and
must be present alongside the sketch.

## Tests

```
./test/run_tests.sh
```

Compiles the sketch against host stubs for the AVR core, SPI, EEPROM and
the ADXL driver, then exercises the loop-context register logic: command
execution and acknowledgement, setting validation and rejection,
read-to-clear behaviour, and the unit conversions. The ISR is not covered
— it needs real interrupt timing.
