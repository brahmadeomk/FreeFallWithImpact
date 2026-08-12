# Free-Fall Detection Sensor — Architecture Definition Document
**Version 0.2 — supersedes v0.1 in full**
**Framework:** TOGAF 10 ADM (tailored for embedded product development)
**Baseline:** CTX310 / VRM315 Triaxial Impact & Vibration Monitor, firmware **rev H**, register map **version 8**
**Baseline source:** `github.com/brahmadeomk/Impact-detection-`, branch `claude/impact-detect-vib-sensor-yccai9`, commit `860839a`
**Target:** Free-Fall Detection Sensor (working name **CTX311**)
**Date:** 12 August 2026

---

## 0. What Changed From v0.1, and Why

v0.1 was written without the code. Six of its assumptions were wrong, and two of its Architecture Decision Records were invalid. This revision is grounded in the actual source.

| v0.1 assumption | Reality in rev H | Consequence |
|---|---|---|
| A1: ADXL345 on I²C | **SPI**, MODE3, 4 MHz, CS on D4 | A4/A5 and the whole analog port are free |
| A2: output latched until reset | **3-second auto-reset hold** (`TRIP_HOLD_MS`), active-low on PC0 | Latch behaviour is a design decision, not an inheritance |
| A6 / ADR-004: Modbus must be built, UART conflict unresolved | **Modbus RTU already shipping** — patched `SimpleModbusSlave`, 49 registers, map v8, hardware UART at 9600, DE on D3 | ADR-004 is closed. The largest work package in v0.1 mostly disappears |
| ADR-001: use the on-chip FREE_FALL interrupt | **No external interrupt pin is available** — D2 is DATA_READY, D3 is the RS-485 direction pin | ADR-001 is invalid and replaced by ADR-101 |
| ADR-002: ±16 g full-res is a choice to make | Already set exactly that way, deliberately | Confirmed, no action |
| ADR-003: SRAM too tight to buffer samples | Static footprint ≈ 620 B of 2048 B; ISR uses 153 µs of a 629 µs budget | Much more headroom than assumed — but free fall needs no buffer at all |

**The finding that reorganises the whole design** is in the ISR: rev H performs *dynamic gravity removal* on every sample, and every register downstream of it is AC-coupled. Free fall is a DC event. This is covered in §C7.2 and is the reason the target architecture is not a small delta.

---

# PHASE A — Architecture Vision (revised)

## A.1 What the Baseline Actually Is

The CTX310 is not a simple impact switch. It is a 1600 Hz triaxial DSP instrument with:

- Per-sample impact detection at **0.63 ms** latency (`PEAK_CONFIRM = 1`), validated over ~27.5 million samples with zero false trips
- Integer-only DSP: fixed-point `isqrt32`, no floating point, no dynamic allocation
- A disciplined ISR/loop split — decisions in the ISR, presentation in `loop()`
- A 49-register Modbus map with version negotiation, command acknowledgement, read-to-clear semantics, and settings/trigger separation
- A documented hardware validation log with real field measurements

**The most important thing inherited is not code. It is a design philosophy**, stated explicitly in the rev H changelog:

> Rev G could open the interlock two ways… A trip therefore meant either "something hit it" or "it has been shaking a while", and nothing in the map said which. For an impact monitor driving an interlock that ambiguity is a liability, so there is now exactly ONE path to the output.

This single principle drives the central decision of the target architecture (§A.4 and ADR-103).

## A.2 Vision Statement

> A free-fall detection sensor on the unmodified CTX310 hardware, detecting the onset of free fall in software at the existing 1600 Hz sample rate, driving the existing interlock output through exactly one decision path, and publishing fall characterisation through an extension of the existing Modbus register map.

## A.3 Revised Architecture Principles

v0.1's PR-01 to PR-08 stand. Four are added, all derived from the baseline's own established practice:

| ID | Principle | Source in the baseline |
|----|-----------|---|
| **PR-09** | **One path to the output.** Exactly one test may open the interlock. Everything else is measured and published but does not operate the output | Rev H changelog, the sustained-RMS removal |
| **PR-10** | **Decisions in the ISR, presentation in `loop()`.** Anything that must see every sample or every block stays in interrupt context; roots, divides and unit conversion do not | Rev F change A, rev H change E |
| **PR-11** | **Map version is a contract.** Any change that could make an existing master misread the device bumps `REGISTER_MAP_VERSION`. Registers are never renumbered; removed ones become reserved holes reading 0 | Regs 35 and 48; map history 4→8 |
| **PR-12** | **A claim is a hypothesis until a unit confirms it.** Static analysis of compiled AVR output is not evidence | `docs/HARDWARE_VALIDATION.md`, opening paragraph |

## A.4 The Central Decision

Adding free-fall detection to a device that already trips on impact **re-introduces exactly the ambiguity rev H was written to remove**. Two paths to one output, and register 25 bit 0 identical either way.

Three options:

| Option | Description | Assessment |
|---|---|---|
| **(a) Shared output, cause in status bits** | Both tests trip PC0; new status bits say which | Repeats the rev G mistake. A master reading only reg 3 or reg 25 bit 0 still cannot tell a drop from a knock. Rejected |
| **(b) Second physical output** | Free fall on PC1, impact stays on PC0 | Clean and unambiguous, and PC1–PC5 are free. But it changes customer wiring, and a second DI is not always available. Offered as a build option |
| **(c) Free fall is the one path; impact is measured only** | On CTX311 firmware the peak test no longer operates the output. Registers 36, 37 and the impact snapshot are untouched and still published | **Selected.** Exactly the rev H pattern applied a second time |

**Selected: (c), with (b) as a documented build option.** This keeps PR-09 intact, keeps the register map meaning stable, and means a CTX311 trip has exactly one meaning: *this thing fell*. Impact severity remains fully available over Modbus for post-event assessment — which is what it was always best at.

## A.5 Success Criteria (revised against measured baseline)

| ID | Criterion | Target | Basis |
|---|---|---|---|
| SC-01 | Free-fall detection latency | Configured FF time + ≤ 1 sample period (0.63 ms) | Software detection at 1600 Hz |
| SC-02 | Missed detection for drops ≥ configured minimum | < 1 % | — |
| SC-03 | False positives | 0 over a ≥ 4 h idle soak, mirroring the rev H `PEAK_CONFIRM` soak protocol | `HARDWARE_VALIDATION.md` soak method |
| SC-04 | Height estimate accuracy, 0.3–2.0 m clean drops | ± 15 % | Better than v0.1's ± 20 %: full fall duration is measured, not just the excess (§C7.5) |
| SC-05 | **ISR worst case (reg 19) stays under 250 µs** | ≤ 250 µs of the 629 µs budget | Current 153 µs + free-fall path; PR-12 requires measurement |
| SC-06 | Firmware reuse | ≥ 80 % | Revised up sharply — Modbus, config, command path, diagnostics and DSP scaffolding all carry over |
| SC-07 | BOM delta | Zero | Confirmed achievable |
| SC-08 | Full-map read time at 9600 baud | ≤ 200 ms | 49 regs = 134 ms measured; ~61 regs ≈ 165 ms |

---

# PHASE B — Business Architecture (delta only)

## B.1 Capability Delta

Only three capabilities are genuinely new:

```
Event Detection
├── Free-fall onset detection        ← NEW, and the only new ISR work
├── Impact detection                 ← EXISTS, repurposed to measurement-only
└── Fall confirmation                ← NEW, loop context
Event Characterisation
├── Fall duration measurement        ← NEW
├── Drop height estimation           ← NEW
├── Minimum-g depth                  ← NEW
└── Impact severity                  ← EXISTS (regs 36, 37)
Data Publication                     ← EXISTS, extend map to v9
Device Management                    ← EXISTS, extend commands and config
```

## B.2 Business Rules (revised)

| ID | Rule | Change from v0.1 |
|---|---|---|
| BR-01 | Exactly one test opens the interlock: the free-fall test | Replaces the multi-mode annunciation rule. PR-09 |
| BR-02 | Impact is measured and published but never operates the output | New. §A.4 option (c) |
| BR-03 | Output hold behaviour is configurable: timed hold (default, inheriting the 3 s pattern) or latch-until-command | Was assumed latched; reality is a 3 s hold |
| BR-04 | Modbus failure never affects the output | Unchanged, and structurally true — the trip is set in the ISR by direct port manipulation |
| BR-05 | A settings register holds its value and echoes through a paired effective-value register; a trigger register self-clears and is confirmed through the acknowledge registers | Inherited verbatim from the baseline's own semantics |
| BR-06 | Fall counter is monotonic, cleared only by an explicit command | Aligns with `TripCountReg` / `CMD_CLEAR_TRIPCOUNT` |

---

# PHASE C (i) — Data Architecture

## C.1 SRAM Budget — Measured Statically From Source

| Consumer | Bytes |
|---|---:|
| `holdingRegs[49]` | 98 |
| ISR DSP state (DC tracker, block sums, peaks, indices) | ~50 |
| 1 s accumulators + handoff (`uint64` ×6) | 48 |
| Block handoff (`b_*`) | ~38 |
| Impact snapshot (`i_*`) | ~28 |
| Trip / peak / threshold state | ~30 |
| Diagnostics | ~10 |
| Config + flags | ~10 |
| ADXL345 object | ~20 |
| `SimpleModbusSlave` `frame[128]` + state | ~140 |
| `HardwareSerial` RX+TX buffers (64+64) + object | ~145 |
| **Static total (estimate)** | **~620** |
| **Free for stack and growth (of 2048)** | **~1400** |

**This is an estimate from source inspection, not a measured figure — no AVR toolchain was available in this environment.** Per PR-12 it must be confirmed against the linker map before it is relied upon. Even allowing wide error, the conclusion holds: v0.1's ADR-003 was solving a problem that does not exist at this scale.

**Free fall needs no sample buffer at all.** Detection is a running counter over consecutive sub-threshold samples, not a windowed statistic. The additional state is roughly **20 bytes**:

| Variable | Type | Purpose |
|---|---|---|
| `ffCount` | uint16 | Consecutive sub-threshold samples |
| `ffTolerance` | uint8 | Leaky allowance for brief spikes during tumbling |
| `ffThresholdSq` | uint32 | Raw-magnitude threshold, squared, counts² |
| `ffSamplesRequired` | uint16 | Configured FF time converted to samples |
| `ffMinMag2` | uint32 | Minimum raw magnitude seen during the fall |
| `ffDurationSamples` | uint16 | Total sub-threshold run length of the last fall |
| `fallLatched`, `fallCount`, `ffActive` | uint8 ×2, uint16 | State and counter |

## C.2 Time Base

The baseline has no RTC and none is added. Events are stamped with `millis()` uptime, consistent with `tripMs`. The master timestamps its own reads. **No traceable-time claim may be made** — unchanged from v0.1.

Fall duration is measured in **samples**, not milliseconds, and converted for publication using the measured ODR from reg 27. At 1575–1589 Hz measured versus 1600 nominal, using the nominal rate would introduce ~1.5 % systematic error in duration and ~3 % in height. Use the measured value.

---

# PHASE C (ii) — Application Architecture

## C7.1 The Gravity Removal Problem

This is the core technical finding.

```c
/* ---- dynamic gravity removal ----
   err is both the tracker error AND the gravity-free AC sample. */
int32_t eX = (int32_t)x - (dcAccX >> DC_SHIFT);
```

Every published measurement — registers 0–18, 23, 30, 36, 37, 38–41 — derives from `ax, ay, az`, which are **gravity-free**. The DC tracker runs at `DC_SHIFT = 11`, τ ≈ 1.28 s, corner 0.124 Hz.

Free fall is defined by the *total* acceleration magnitude collapsing to zero — a DC event. An AC-coupled pipeline is structurally blind to it. Three specific consequences:

**1. The free-fall test must tap raw `x, y, z`, before DC removal.** Those values exist in the ISR (line 715, `adxl.readAccel(&x, &y, &z)`) and are discarded after the tracker update. The new test inserts between the read and the DC subtraction.

**2. Entering free fall produces a ~1000 mg step in the AC path.** During free fall raw x,y,z → 0 while `dcAcc` still holds gravity (≈ 256 counts at 3.9 mg/LSB), so `ax ≈ −256 counts ≈ 998 mg`. With the default 3000 mg threshold this is harmless. But `THRESHOLD_MIN_MG` is **10 mg** — any customer who sets register 22 below about 1000 mg gets an impact trip at the *start of every fall*, before the object has hit anything.

> This is a latent interaction in the baseline today, not something free fall introduces. It is simply unreachable in an impact-only product because nobody sets a 500 mg impact threshold. On CTX311, raise `THRESHOLD_MIN_MG` to 1200 mg or document the floor explicitly.

**3. The gravity estimate decays during a long fall, and needs ~4 s to recover afterwards.** `DC_MAX_INC = 256` is exactly the gravity magnitude in counts, so free fall drives the tracker at precisely its maximum slew rate: 256/2048 = 0.125 counts per sample. Over a 300 ms fall (≈ 480 samples) the estimate decays ~23 %. After impact it needs several τ to re-converge, so **registers 32–34 are not trustworthy for orientation comparison until ~4 s after the device comes to rest.** Any orientation-change figure must respect that settling time.

## C7.2 Where the New Code Goes

```
ISR (myHandler)                          ~4-5 µs added, of ~480 µs headroom
├── adxl.readAccel(&x,&y,&z)
├── ★ rawMag2 = x² + y² + z²             ← NEW: 3 MUL, 2 ADD
├── ★ free-fall counter + trip decision  ← NEW: compare, increment, PORTC
├── DC tracker update                    ← unchanged
├── AC squares sX, sY, sZ                ← unchanged
├── fast impact path                     ← RETAINED but no longer touches PORTC
├── block boundary DSP                   ← unchanged
└── ISR timing                           ← unchanged

loop()                                   no timing pressure
├── updateOutput()                       ← extended: hold vs latch mode
├── ★ fall characterisation              ← NEW: duration, height, min-g
├── update1sRms()                        ← unchanged
├── publishBlock()                       ← extended: new registers
├── checkModbusSlaveIdUpdate()           ← unchanged
├── ★ checkFfSettingsUpdate()            ← NEW, same pattern as reg 22
├── checkCommandRegister()               ← extended: two new codes
└── modbus_update()                      ← unchanged
```

**ISR cost estimate:** three 16×16 signed multiplies (~10 cycles each on AVR's `mul`), two 32-bit adds, one 32-bit compare, a counter increment and a conditional `CBI`. Roughly **60–80 cycles ≈ 4–5 µs**, against 480 µs of measured headroom. Under 1 % of budget. Per PR-12, confirm on reg 19 after the change.

**Note the reuse in the arithmetic:** the impact path already computes `mag2 = sX + sY + sZ` from AC values at zero marginal cost. The free-fall path cannot reuse those squares — it needs the raw ones — so this is genuinely three new multiplies, not a free ride.

## C7.3 Free-Fall Detector

```c
/* Inserted immediately after readAccel(), before DC removal. */
uint32_t rawMag2 = (uint32_t)((int32_t)x*x) +
                   (uint32_t)((int32_t)y*y) +
                   (uint32_t)((int32_t)z*z);

if (settleCount < SETTLE_SAMPLES) {          /* reuse the 2 s boot guard */
    ffCount = 0;
} else if (rawMag2 < ffThresholdSq) {
    if (rawMag2 < ffMinMag2) ffMinMag2 = rawMag2;
    ffTolerance = FF_SPIKE_TOLERANCE;        /* refill on a good sample */
    if (++ffCount >= ffSamplesRequired && !fallLatched) {
        PORTC &= ~(1 << OUTPUT_PORT_BIT);    /* atomic CBI — OPEN */
        fallLatched = 1;
        fallCount++;
        fallMs = millis();
        ffDurationSamples = ffCount;
    }
} else if (ffTolerance) {
    ffTolerance--;                           /* tolerate a brief spike */
    if (ffCount) ffCount++;                  /* keep the run alive */
} else {
    ffCount = 0;
    ffMinMag2 = 0xFFFFFFFF;
}
```

**Range safety:** at ±16 g full resolution, full scale is 4096 counts. `x²` ≤ 16.8 M, `rawMag2` ≤ 50.3 M — comfortably inside uint32, no clamping needed. This matches the baseline's own note that `mag2 ≤ 3 × 8192² = 201e6` fits uint32.

**Why software rather than the on-chip FREE_FALL engine (ADR-101):** the ADXL345 has a built-in free-fall interrupt with `THRESH_FF` (62.5 mg/LSB) and `TIME_FF` (5 ms/LSB). It cannot be used and should not be wanted:

1. **No pin.** D2 carries DATA_READY; D3 is `TxEnablePin` for the RS-485 direction control. Those are the only two external-interrupt-capable pins on the ATmega328P. Whether ADXL345 INT2 is even routed on the PCB is unknown and moot.
2. **It would be worse anyway.** On-chip timing granularity is 5 ms; sampling at 1600 Hz gives 0.63 ms. Threshold granularity is 62.5 mg on-chip versus 3.9 mg in software.
3. **It measures less.** The on-chip interrupt reports only that free fall *occurred*. The software path measures the full duration, the minimum-g depth, and the exact sample at which the run began.

The pin constraint forces the better design. That is a fortunate outcome, not a compromise.

## C7.4 The `FF_SPIKE_TOLERANCE` Trade

`PEAK_CONFIRM = 1` on the impact path exists because a hard strike can be shorter than one sample period, and requiring two consecutive samples set a floor on detectability. **The free-fall path has the opposite problem.** A fall lasts hundreds of milliseconds; the risk is not missing a brief event but *breaking a long one* — a tumbling object, a snagged tether or a glancing contact can push one sample above threshold and reset a counter that was 200 samples into a genuine fall.

Hence the leaky tolerance. Suggested starting value: **8 samples ≈ 5 ms** of cumulative excursion tolerated per fall. This is a tuning parameter for WP-13 and should be exposed as a `#define` with the same treatment `PEAK_CONFIRM` receives in the baseline — a documented trade with its failure signature named, not a silent constant.

## C7.5 Characterisation

**Fall duration.** Measured from the *first* sub-threshold sample, not from the moment the threshold time expired. This is the advantage over the on-chip engine, which can only report time beyond `TIME_FF`. Convert samples → ms using the measured ODR (reg 27), not nominal 1600.

**Height.** h = ½ · 9.81 · t², reported in cm.

| FF time | Samples @1589 Hz | Minimum height before detection |
|---|---:|---|
| 100 ms | 159 | ~5 cm |
| 150 ms | 238 | ~11 cm |
| 200 ms | 318 | ~20 cm |
| 300 ms | 477 | ~44 cm |
| 350 ms | 556 | ~60 cm |

The published height uses **total measured duration**, so it does not carry the systematic underestimate that a `TIME_FF`-only measurement would. Residual bias is air drag and any pre-release motion, both of which reduce true free-fall time — so the estimate remains a **lower bound**. Publish it flagged, never bare.

**Minimum-g depth** (new register): the smallest raw magnitude reached during the fall. A clean unimpeded drop approaches 0 mg; a slide down a chute or a tethered fall sits well above it. This is the single best discriminator between a genuine drop and a constrained descent, and it costs one compare per sample.

**Impact severity.** Registers 36 and 37 already do this, unchanged. Note the documented ceiling: √3 × 16 g = 27.7 g, and `HARDWARE_VALIDATION.md` records a real 26643 mg reading, meaning at least two axes were at full scale. **Anything above ~26 g is a lower bound, not a measurement.**

**Orientation change.** Available from regs 32–34, but only after the DC tracker re-converges — allow ≥ 4 s of rest (§C7.1 point 3).

## C7.6 State Machine

| State | Entry | Output | Exit |
|---|---|---|---|
| `SETTLING` | Boot | Closed | 2 s / `SETTLE_SAMPLES` elapsed |
| `ARMED` | Settled | Closed | `ffCount ≥ ffSamplesRequired` |
| `FALLING` | FF confirmed | **Open** | Raw magnitude recovers above threshold |
| `IMPACT_WINDOW` | Fall ended | Open | Impact captured, or window expires |
| `HELD` | Characterised | Open | Hold timer expires, or `CMD_CLEAR_FALL` |
| `ARMED` | Cleared | Closed | — |

Simpler than v0.1's six-state machine with four confirmation modes, because PR-09 removes the need for them: there is one trip path and it is unconditional. Impact confirmation is recorded in the event flags for the master to interpret, but it does not gate the output. **This is the correct trade for an interlock** — a device that waits to see whether an impact follows before opening the interlock has defeated the point of detecting the fall early.

## C7.7 Boot-During-Free-Fall Edge Case

`dcSeeded` seeds the gravity estimate from the very first sample. A device powered up while already falling would seed `dcAcc ≈ 0` and treat weightlessness as its resting orientation. The existing `SETTLE_SAMPLES` guard (2 s) suppresses trips during this window, which covers it in practice — but the seeded value would be wrong for ~4 s afterwards. Worth a plausibility check: if the seeded magnitude is below ~700 mg, flag config-suspect and re-seed on the next block that reads near 1 g.

---

# PHASE D — Technology Architecture (confirmed from source)

## D.1 Actual Pin Map (rev H, verified)

| Pin | Function | Source |
|---|---|---|
| D0/D1 | Hardware UART → RS-485 | `Serial.begin(9600)`, `modbus_configure(&Serial, …)` |
| D2 | ADXL345 INT1 — **DATA_READY** | `ADXL_INT1_PIN 2`, `attachInterrupt(…RISING)` |
| **D3** | **RS-485 DE/RE** | `TxEnablePin 3` |
| D4 | ADXL345 SPI chip select | `ADXL_CS_PIN 4` |
| D11/D12/D13 | SPI MOSI/MISO/SCK | `SPI.begin()`, MODE3, DIV4 = 4 MHz |
| **A0 (PC0)** | **Interlock output** — HIGH = closed, LOW = tripped | `OUTPUT_PORT_BIT PC0` |
| A1–A5 (PC1–PC5) | **Free** | Available for a second output (§A.4 option b) |
| D5–D10 | **Free** | Available for LED / buzzer / reset button |

**ADR-004 from v0.1 is closed.** The hardware UART carries Modbus at 9600 8N1 with D3 as the direction pin. There is no SoftwareSerial and no conflict — the question was answered in production two revisions ago.

## D.2 Sensor Configuration (rev H, verified)

| Setting | Value | Note |
|---|---|---|
| Interface | 4-wire SPI, MODE3, 4 MHz | ≥ 2 MHz required at 1600 Hz; 5 MHz max |
| Range | ±16 g | Headroom above the ±15 g spec figure |
| Full resolution | On, 3.9 mg/LSB | LSB pinned to 0 at this ODR → 7.8 mg effective step |
| Bandwidth | `ADXL345_BW_800` → 1600 Hz ODR | Measured 1575–1589 Hz |
| Interrupt | DATA_READY → INT1 | The only interrupt in use |

**Nothing here needs to change.** The configuration chosen for impact detection is already correct for free fall.

## D.3 Measured Performance Envelope

| Metric | Rev H measured | Free-fall budget |
|---|---:|---|
| ISR worst case (reg 19) | 153 µs | Target ≤ 250 µs after change (SC-05) |
| Sample period @1589 Hz | 629 µs | ~480 µs headroom |
| Max loop time (reg 20) | 134 ms | Bandwidth-bound, not compute-bound |
| Blocks missed | ~2 % at one poll per 6 s | Will rise slightly with a longer map |
| Full map read @9600 | 134 ms for 49 regs | ~165 ms for 61 regs (SC-08) |
| Modbus frame buffer | 128 B → 61 registers per FC03 | **61 is the hard ceiling.** The map must not exceed it without a further patch |

**The 61-register ceiling is a real constraint on the map design.** The patched library gives 128 bytes; a response is 5 + 2n bytes, so n ≤ 61. The target map lands at 62 registers (0–61), which is **one over**. Either the map stops at 61 (indices 0–60), or `BUFFER_SIZE` goes to 132+, or the master accepts split reads. §6.3 takes the first option.

## D.4 Architecture Decision Records (revised)

**ADR-101 — Free-fall detection entirely in software, in the existing ISR**
*Status:* Accepted. *Supersedes ADR-001.* *Context:* No external interrupt pin is available for the ADXL345 on-chip FREE_FALL output, and the on-chip engine is coarser than the existing 1600 Hz sample loop in both time (5 ms vs 0.63 ms) and amplitude (62.5 mg vs 3.9 mg). *Decision:* Detect in software from raw pre-DC-removal samples in `myHandler()`. *Consequence:* ~4–5 µs added to a 153 µs ISR; no hardware change; strictly better measurement resolution; full fall duration recoverable.

**ADR-102 — Tap raw samples before gravity removal**
*Status:* Accepted. *Context:* The entire published measurement chain is AC-coupled through a 0.124 Hz tracker; free fall is a DC event and is invisible downstream of it. *Decision:* Compute `rawMag2` from `x, y, z` immediately after `readAccel()`. *Consequence:* Three additional multiplies that cannot reuse the existing AC squares; a new raw-magnitude register that is deliberately *not* comparable with reg 23; and the ~1000 mg AC step at fall onset must be documented and guarded by raising `THRESHOLD_MIN_MG`.

**ADR-103 — One path to the output; impact becomes measurement-only**
*Status:* Accepted. *Supersedes ADR-005.* *Context:* PR-09 and the rev H rationale. *Decision:* On CTX311, the free-fall test is the sole operator of PC0. The impact path retains full measurement (regs 36, 37, snapshot) but its `PORTC` write is removed. *Consequence:* A CTX311 trip has exactly one meaning. Customers wanting both interlocks take the two-output build option (PC1) or deploy both SKUs. **This must be stated prominently in the datasheet** — a customer migrating a CTX310 to CTX311 firmware would otherwise lose impact tripping silently.

**ADR-104 — Extend the existing map to version 9; do not renumber**
*Status:* Accepted. *Context:* PR-11 and the map history 4→8. *Decision:* New registers append from index 49. Reserved holes 35 and 48 stay reserved and are **not** reused — a map-8 master could still write them. Bump `REGISTER_MAP_VERSION` to 9. *Consequence:* Existing dashboards and `ctx310_client.py` keep working for registers 0–48; a map-8 master correctly refuses to ingest, per the existing connect-time check on reg 43.

**ADR-105 — Output hold, not latch, by default**
*Status:* Proposed — needs a decision. *Context:* The baseline auto-closes after `TRIP_HOLD_MS = 3000`. For an impact monitor a 3 s hold is sensible; for a *drop*, auto-rearming after 3 s means the interlock closes while the fallen object is still on the floor. *Recommendation:* Default the CTX311 hold to a longer period (30 s suggested) and provide `FfHoldMsReg = 0` to mean latch-until-command, using the existing command path (`CMD_CLEAR_FALL`) for acknowledgement. *Consequence:* The `updateOutput()` state machine gains one branch. Needs confirmation of what the customer's PLC expects.

---

# PHASE E — Gap Analysis and Work Packages (revised)

## E.1 Gap Analysis Against Actual Rev H

| Component | Rev H state | CTX311 need | Gap |
|---|---|---|---|
| PCB / BOM / enclosure | Production | Identical | **None** |
| ADXL345 config | ±16 g, full-res, 1600 Hz, SPI, DATA_READY→INT1 | Identical | **None** |
| ISR framework, Timer1 timing, diagnostics | Mature, measured | Same + free-fall test | **~15 lines** |
| Raw-magnitude computation | Absent (discarded after DC removal) | Required | **New, small** |
| Free-fall counter and trip | Absent | Required | **New, ~20 lines** |
| Impact detection | Mature, validated | Retained, `PORTC` write removed | **One line deleted** |
| DC tracker, block DSP, 1 s RMS | Mature | Unchanged | **None** |
| `isqrt32`, unit conversions | Mature, swept-tested | Unchanged | **None** |
| Output driver | PC0, active-low, 3 s hold | + latch mode, longer default | **Small** |
| Fall characterisation | Absent | Duration, height, min-g | **New, loop context** |
| Modbus RTU slave | **Shipping**, patched, validated | Unchanged | **None** |
| Register map | 49 regs, v8 | +12 regs, v9 | **Extension** |
| Settings pattern (reg 21/22 + echo 24) | Mature, tested | Apply to FF threshold and time | **Pattern reuse** |
| Command path (regs 28, 45–47) | Mature, hardware-validated | +2 codes | **Small** |
| EEPROM config + CRC-equivalent magic | Mature, survived a struct change | +2 fields | **Small** |
| Host test harness | 79 checks passing | Extend | **Moderate** |
| Watchdog | **`wdt_disable()` in setup — not used** | Should be enabled | **Gap, see R-104** |
| Self-test / health output | **Absent** | Recommended | **Gap** |
| Event log (EEPROM history) | Absent | Optional | **Deferred — see below** |

**Revised reuse estimate: 80–85 %.** v0.1 said 55–65 % because it assumed Modbus had to be built. It does not.

**The EEPROM event log from v0.1 is dropped from scope.** The baseline stores only 7 bytes of config and has no logging infrastructure. Adding a 32-record circular log with wear levelling is a substantial piece of work whose value is largely duplicated by the master, which already polls every 6 seconds and historises. Recommend: publish the last event in registers, let the Pi/PLC keep history. Revisit only if a customer needs standalone forensics.

## E.2 Work Packages (revised)

| WP | Description | Effort |
|---|---|---|
| WP-101 | Raw magnitude + free-fall counter in the ISR | **S** |
| WP-102 | Remove the impact `PORTC` write; verify impact measurement intact | **XS** |
| WP-103 | Fall characterisation in `loop()` — duration, height, min-g | S |
| WP-104 | Output hold/latch mode extension (ADR-105) | S |
| WP-105 | Register map v9: 12 new registers, 2 command codes, EEPROM fields | M |
| WP-106 | Raise `THRESHOLD_MIN_MG`; document the AC-step interaction | XS |
| WP-107 | Extend the host test harness for the new logic | M |
| WP-108 | **Measure reg 19 after WP-101** (PR-12, SC-05) | S |
| WP-109 | Drop rig; threshold and tolerance tuning | **L** |
| WP-110 | Idle soak, mirroring the `PEAK_CONFIRM` protocol | M |
| WP-111 | Enable the watchdog (R-104) | S |
| WP-112 | Update `REGISTER_MAP.md`, `HARDWARE_VALIDATION.md`, `ctx310_client.py` | M |
| WP-113 | Datasheet: the impact-tripping change must be unmissable (ADR-103) | S |

**The firmware is no longer the critical path. WP-109 is.** Everything up to WP-108 is a few days of work on a codebase that is already doing the hard part. Getting the threshold and spike tolerance right against real drops is where the schedule actually lives.

---

## 6.3 Register Map Extension — Version 9 (Draft)

Registers 0–48 **unchanged**, with two exceptions noted below. New registers append from 49. Total 61 registers (0–60), landing exactly on the 128-byte buffer ceiling (§D.3).

| Reg | Name | Type | Units / Notes |
|---|---|---|---|
| 49 | `FallStatusReg` | bitfield | bit0 fall latched, bit1 free-fall active now, bit2 impact followed, bit3 height valid, bit4 impact clipped (≥26 g), bit5 orientation settled |
| 50 | `FfThresholdReg` | **R/W setting** | Raw-magnitude threshold, mg, 100–900, default 400 |
| 51 | `FfThresholdEffReg` | R | Echo of 50 — same pattern as reg 24 |
| 52 | `FfTimeMsReg` | **R/W setting** | Free-fall time, ms, 30–1000, default 150 |
| 53 | `FfTimeMsEffReg` | R | Echo of 52 |
| 54 | `FallCountReg` | R | Monotonic; cleared by `CMD_CLEAR_FALLCOUNT` |
| 55 | `LastFfDurationReg` | R | Total measured free-fall duration, ms |
| 56 | `LastFallHeightReg` | R | Estimated drop height, cm; 0xFFFF = invalid |
| 57 | `LastFallMinMagReg` | R | Minimum raw magnitude during the fall, mg — the drop-quality discriminator |
| 58 | `LastFallImpactReg` | R | Peak impact after the fall, mg; 0 = none |
| 59 | `FfHoldMsReg` | **R/W setting** | Output hold, ms; **0 = latch until command**; default 30000 |
| 60 | `RawMagReg` | R | Live raw vector magnitude, mg — **DC-inclusive, ~1000 mg at rest. Not comparable with reg 23** |

**Changes to existing registers:**

| Reg | Change | Rationale |
|---|---|---|
| 25 bit 2 | **Removed** (reads 0) | `HARDWARE_VALIDATION.md` documents it re-latching within seconds of any clear, making it a permanent fault light for normal polling. Reg 26 remains the diagnostic counter. This is the map revision it was waiting for |
| 22 | `THRESHOLD_MIN_MG` raised 10 → 1200 mg | The ~1000 mg AC step at fall onset (§C7.1) |
| 43 | `REGISTER_MAP_VERSION` → **9** | ADR-104 |

**New command codes** (reg 28, confirmed through regs 45–47 as established):

| Code | Name | Effect |
|---|---|---|
| `0x0004` | `CLEAR_FALL` | Clears the fall latch, closes the output, re-arms |
| `0x0005` | `CLEAR_FALLCOUNT` | Zeroes reg 54 only |

**EEPROM layout extension** — offsets 0–6 unchanged (magic, slave ID, threshold), so deployed units keep their configuration exactly as they did across the G→H struct change. New fields at offsets 9+: FF threshold, FF time, hold time.

---

## 12. Risk Register (revised)

| ID | Risk | L | I | Mitigation |
|---|---|---|---|---|
| **R-101** | **Product deployed in a personnel-safety role.** Commercial components, no redundancy, no IEC 61508 development, and now a single unconfirmed trip path | Med | **Severe** | State the limitation in datasheet, manual and quotation. This is unchanged from v0.1 and **still the most important open question** |
| **R-102** | ADR-103 not communicated: a customer reflashes a CTX310 to CTX311 and silently loses impact tripping | **High** | **High** | WP-113. Map version 9 forces a master-side failure, but the *interlock behaviour change* is invisible to the map check. Consider refusing to boot if EEPROM shows a CTX310 config signature |
| **R-103** | Free-fall threshold and spike tolerance prove site-specific | High | Med | Runtime-configurable (regs 50, 52); publish the time-vs-height table; WP-109 |
| **R-104** | **Watchdog is disabled** (`wdt_disable()` in `setup()`, `avr/wdt.h` included but unused) | Med | High | WP-111. A hang currently leaves the interlock in whatever state it was in. Note the fail-safe direction: PC0 low = open, so a *power* failure opens the interlock, but a firmware hang with the output closed does not |
| **R-105** | ISR budget exceeded after WP-101 | Low | High | 4–5 µs against 480 µs headroom. WP-108 measures it. PR-12 |
| **R-106** | Map hits the 61-register buffer ceiling with no room to grow | Med | Med | v9 lands exactly on it. Any v10 addition requires raising `BUFFER_SIZE`, which is a library patch and a memory cost. Consider going to 132 now while the patch is already being touched |
| **R-107** | Tumbling or tethered falls break the sub-threshold run | Med | High | `FF_SPIKE_TOLERANCE` (§C7.4); reg 57 min-g exposes constrained descents; include tumbling and tethered drops in WP-109 |
| **R-108** | Height treated as measurement rather than estimate | High | Med | Validity flag (reg 49 bit 3); document the lower-bound bias |
| **R-109** | Impact magnitude clipping misread as a measurement | Med | Med | Already documented in `HARDWARE_VALIDATION.md`; surface it as reg 49 bit 4 rather than leaving it as prose |
| **R-110** | Longer map worsens blocks-missed | Low | Low | 49→61 regs is 134→165 ms per read; at one poll per 6 s the line goes ~2.2 % → ~2.8 % occupied. Immaterial |
| **R-111** | Master-side signed decode bug (regs 32–34) repeats on new signed registers | Med | Low | All new registers are unsigned by design. Keep it that way |

---

## 14. Immediate Next Actions

1. **Answer R-101.** Will this ever be used to protect people? Everything else is engineering; this is not.
2. **Decide ADR-103.** Is impact-measurement-only correct for the CTX311 SKU, or is a second output on PC1 required? This determines the datasheet and the customer conversation, not just the code.
3. **Decide ADR-105.** Does the customer's PLC want a timed hold or a latch-until-acknowledge?
4. **Confirm the SRAM figure** against a linker map — my ~620 B is source inspection, not measurement (PR-12).
5. **Confirm the minimum drop height of interest.** This sets the default for reg 52 and is the single most consequential number in the product.
6. **Decide on `BUFFER_SIZE`** now rather than at v10 (R-106).

---

*Supersedes v0.1 in full. Grounded in commit `860839a`, firmware rev H, register map version 8.*
