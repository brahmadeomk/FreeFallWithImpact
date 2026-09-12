# Free-Fall Detection Sensor — Architecture Definition Document
**Framework:** TOGAF 10 ADM (tailored for embedded product development)
**Baseline:** Impact Detection Sensor (existing product)
**Target:** Free-Fall Detection Sensor (FFD-100)
**Version:** 0.1 (Draft for review)
**Date:** 12 August 2026

---

## 0. Reading Notes & Open Assumptions

This document was produced **without access to the impact detection sensor source code or schematic**. Everything derived from the baseline is marked **[ASSUMED — CONFIRM]**. Replace these before baselining.

| # | Assumption | Impact if wrong |
|---|---|---|
| A1 | ADXL345 is connected over I²C (A4/A5) with ALT ADDRESS tied to give 0x53 | Pin map and driver layer change; SPI frees A4/A5 but consumes D10–D13 |
| A2 | Impact output to customer is an **opto-isolated / relay dry contact**, latched until reset | Output driver reuse claim in §7 |
| A3 | Board is powered from 12–24 VDC field supply via on-board regulator | Power budget, brown-out design |
| A4 | Impact detection uses ADXL345 tap/activity interrupts, not pure software thresholding | Reuse ratio in gap analysis (§9) |
| A5 | Product is **indication/monitoring only**, not a certified personnel-safety function | See §12 Risk R-01 — this is the single most important item to confirm |
| A6 | Customer system is a PLC/DCS acting as Modbus RTU master over RS-485 | Comms architecture §8 |

---

# PRELIMINARY PHASE — Framework and Principles

## P.1 Scope of the Architecture Effort

**In scope:** the FFD-100 sensor node — firmware, on-board sensing, discrete output interface, Modbus RTU interface, configuration and commissioning, event data model.

**Out of scope:** customer PLC logic, SCADA/HMI screens, network infrastructure beyond the RS-485 terminal block, mechanical mounting design (inherited unchanged), cloud/IIoT gateway (identified as a future extension in §10).

## P.2 Architecture Principles

| ID | Principle | Rationale | Implication |
|----|-----------|-----------|-------------|
| PR-01 | **Hardware invariance** | The board, BOM and enclosure are already qualified and in production | No PCB respin. If a requirement cannot be met in firmware/config, it is escalated, not silently solved with hardware |
| PR-02 | **Interface continuity** | Customers have already integrated the impact sensor's discrete output | The discrete output must be electrically and behaviourally drop-in compatible. Modbus is *additive*, never a prerequisite for the safety-relevant output |
| PR-03 | **Detection independence** | The discrete output must not depend on the health of the Modbus stack | Detection→output path runs in a priority path that cannot be blocked by comms; comms failure must never suppress an output |
| PR-04 | **Deterministic before clever** | Field devices are debugged with a multimeter, not a profiler | Fixed-point arithmetic, static allocation, bounded loops, no dynamic memory, no blocking delays |
| PR-05 | **Configuration over recompilation** | One firmware image across customers reduces variant sprawl | Thresholds, slave ID, baud, latch behaviour are runtime-configurable and persisted in EEPROM |
| PR-06 | **Evidence on every event** | Disputes about "did it actually fall?" are the main support cost | Every event stores a characterisation record (duration, height, impact peak, timestamp) retrievable over Modbus |
| PR-07 | **Fail-evident, not fail-silent** | A dead sensor that looks healthy is worse than one that alarms | Watchdog, periodic self-test, health output, heartbeat register |
| PR-08 | **Maximise reuse of the impact platform** | Cost, schedule and field-proven code | The impact detection module is *reused as a confirmation stage*, not discarded |

## P.3 Governance

| Element | Approach |
|---|---|
| Architecture Board | Product Owner + Firmware Lead + Hardware Lead + Field Application Engineer |
| Change control | Any deviation from PR-01 or PR-02 requires Board approval and a documented ADR |
| Compliance review gates | Phase A sign-off, pre-firmware-freeze review, pre-field-trial review |
| Artefacts under version control | This ADD, Modbus register map, requirements catalogue, ADR log, test protocol |

---

# PHASE A — Architecture Vision

## A.1 Problem Statement

The impact detection sensor tells the customer that *something hit something*. It cannot distinguish an object that was **dropped** from one that was **struck**, and it provides no data about the event beyond a contact closure. Customers running lifting, hoisting, handling and transport operations need to know when an asset (or, in some deployments, a load or tool) has entered free fall — ideally *before or at* the moment of impact — and need the surrounding evidence for incident investigation.

## A.2 Vision Statement

> A field-deployable sensor that detects free fall within a few hundred milliseconds of onset, asserts a discrete output identical in form to the existing impact sensor, and simultaneously publishes event characterisation data (fall duration, estimated drop height, impact severity, device health) over Modbus RTU — built entirely on the qualified impact-sensor hardware platform with firmware changes only.

## A.3 Value Proposition

| Stakeholder | Value |
|---|---|
| Customer operations | Immediate notification of a drop event; automatic stop/interlock capability via existing wiring |
| Customer maintenance | Drop height and impact magnitude indicate whether the asset needs inspection or can continue in service |
| Customer HSE | Timestamped, quantified event record for incident reporting |
| Our product line | New SKU with near-zero hardware NRE; shared production, test and service processes |
| Our support | Self-diagnosis and event history reduce "false alarm" call-outs |

## A.4 Stakeholder Map

| Stakeholder | Concern | Architecture viewpoint that addresses it |
|---|---|---|
| Customer PLC/automation engineer | "How do I wire and read it?" | Interface view (§8), Modbus register map (§6.3) |
| Customer HSE / safety officer | "Can I rely on this to protect people?" | §12 R-01, §A.6 constraint statement |
| Plant maintenance technician | "Is it working? How do I reset it?" | Health & diagnostics view (§7.6), commissioning process (§5.3) |
| Firmware engineer | "What do I build, and what can I reuse?" | Application architecture (§7), gap analysis (§9) |
| Hardware engineer | "Does this need a board change?" | Technology architecture (§8), PR-01 |
| Test / QA | "How do I prove it detects a fall?" | Requirements catalogue (§11), test strategy (§10.4) |
| Product manager | "What is the SKU story?" | Product line view (§9.4) |
| Regulatory / certification | "What claims can we make?" | §12 R-01, R-02 |

## A.5 Business Scenario

**Situation.** An asset instrumented with the sensor is being moved, lifted or handled. A sling fails / a grip slips / a fixing releases.

**Sequence.**
1. The asset loses support. Measured acceleration on all axes collapses toward 0 g.
2. The sensor recognises sustained near-zero-g for longer than the configured free-fall time and enters a candidate-fall state.
3. The asset strikes a surface. A high-g transient occurs — **this is exactly what the existing impact detection algorithm already detects.**
4. The asset comes to rest. Acceleration settles back to ~1 g with low variance, typically at a new orientation.
5. The sensor confirms a genuine fall, latches the discrete output, writes an event record, and updates Modbus registers.
6. Customer PLC reads the discrete input, halts the operation, and (optionally) polls Modbus for the event detail.
7. A technician inspects the asset, clears the latch via the local reset input or a Modbus command, and the sensor returns to armed state.

**Desired outcome.** Fall is annunciated within the configured detection window; the event is characterised sufficiently to decide whether the asset must be quarantined.

## A.6 Constraints

| ID | Constraint |
|---|---|
| C-01 | No hardware changes (PR-01). ATmega328P: 32 KB flash, **2 KB SRAM**, 1 KB EEPROM |
| C-02 | ATmega328P has a **single hardware USART**, shared with the USB bridge — see ADR-004 |
| C-03 | ADXL345 output data rate practically limited to ≤ 400 Hz over I²C with useful headroom; ±16 g range has 3.9 mg/LSB resolution in full-res mode |
| C-04 | Discrete output must remain behaviourally compatible with the impact sensor |
| C-05 | Product is not developed under IEC 61508 / ISO 13849; no SIL or PL claim may be made |
| C-06 | Field wiring is the customer's existing 2-wire (or 4-wire) run plus a new RS-485 pair |

## A.7 Success Criteria

| ID | Criterion | Target |
|---|---|---|
| SC-01 | Detection latency from onset of free fall to output assertion | ≤ configured free-fall time + 50 ms (unconfirmed mode) |
| SC-02 | Missed detection rate for drops ≥ configured minimum height | < 1 % over qualification drop matrix |
| SC-03 | False positive rate under normal handling/vibration profile | < 1 per 30 days per device |
| SC-04 | Drop height estimation accuracy for clean vertical drops 0.3–2.0 m | ± 20 % or ± 5 cm, whichever is greater |
| SC-05 | Modbus response time to FC03/FC04 | < 50 ms at 19200 baud |
| SC-06 | Firmware reuse from impact sensor codebase | ≥ 60 % of modules reused unmodified or with configuration change only |
| SC-07 | Hardware BOM delta vs impact sensor | Zero |

---

# PHASE B — Business Architecture

## B.1 Business Capabilities

```
Asset Event Monitoring
├── Motion Sensing
│   ├── Continuous acceleration acquisition
│   └── Sensor health verification
├── Event Detection
│   ├── Free-fall onset detection          ← NEW
│   ├── Impact detection                   ← REUSED from baseline
│   ├── Post-event rest detection          ← NEW
│   └── Multi-stage event confirmation     ← NEW
├── Event Characterisation                 ← NEW
│   ├── Fall duration measurement
│   ├── Drop height estimation
│   ├── Impact severity quantification
│   └── Orientation change assessment
├── Annunciation
│   ├── Discrete output signalling         ← REUSED
│   ├── Local indication (LED / buzzer)    ← REUSED
│   └── Latch and acknowledgement          ← REUSED
├── Data Publication                       ← NEW
│   ├── Modbus RTU slave service
│   ├── Live measurement exposure
│   └── Event history exposure
└── Device Management
    ├── Configuration and persistence      ← EXTENDED
    ├── Self-diagnosis and watchdog        ← EXTENDED
    └── Commissioning and addressing       ← NEW
```

**Capability heat map:** New capability development concentrates in *Event Detection (free-fall)*, *Event Characterisation* and *Data Publication*. Annunciation is unchanged — this is the core of the reuse argument and the basis of PR-02.

## B.2 Value Stream: Drop Event to Resolution

| Stage | Actor | Activity | Value added | Supporting capability |
|---|---|---|---|---|
| 1. Detect | Sensor | Recognise free-fall signature | Event exists and is known | Free-fall onset detection |
| 2. Confirm | Sensor | Correlate with impact + rest | False alarms suppressed | Multi-stage confirmation |
| 3. Annunciate | Sensor → PLC | Assert discrete output | Operation halted automatically | Discrete output signalling |
| 4. Characterise | Sensor | Compute duration, height, severity | Severity known without manual measurement | Event characterisation |
| 5. Publish | Sensor → PLC/SCADA | Serve registers on request | Data reaches operator screens and historians | Modbus RTU service |
| 6. Investigate | Technician | Inspect asset, read event record | Correct disposition decision | Event history exposure |
| 7. Restore | Technician | Clear latch, re-arm | Sensor returns to service | Latch and acknowledgement |

## B.3 Business Process — Event Handling (Actor Swimlane)

```
ASSET      : [supported] ──▶ [released] ──▶ [falling] ──▶ [impact] ──▶ [at rest]
                                  │             │            │            │
SENSOR     :                  ARMED ──▶ FALL_CANDIDATE ──▶ CONFIRMING ──▶ FALL_LATCHED
                                                │             │            │
                                          (timeout,      (impact seen)  (write event
                                           no impact)                    record)
                                                ▼                          │
                                            ARMED ◀── discard              ▼
                                                                   assert DO1 + LED
                                                                           │
PLC        :                                                    read DI ──▶ stop sequence
                                                                           │
                                                                    poll FC04 registers
                                                                           │
TECHNICIAN :                                                   inspect ──▶ reset (local
                                                                           button or FC06
                                                                           command reg)
                                                                           │
SENSOR     :                                                        ARMED ◀┘
```

## B.4 Business Rules

| ID | Rule |
|---|---|
| BR-01 | A fall is *annunciated* on free-fall detection; it is *confirmed and characterised* on subsequent impact + rest. Annunciation is never delayed waiting for confirmation when the device is in Immediate mode |
| BR-02 | The discrete output latches and remains asserted until explicitly cleared, or until an auto-reset timer expires if auto-reset is enabled |
| BR-03 | A Modbus communication failure never clears, suppresses or delays the discrete output (PR-03) |
| BR-04 | A failed self-test de-asserts the health output and sets the fault bit, but does not suppress fall detection if the accelerometer is still returning plausible data |
| BR-05 | Configuration changes take effect immediately in RAM but are only persisted to EEPROM on an explicit save command |
| BR-06 | The event counter is monotonic and non-resettable except by factory reset, to preserve audit integrity |
| BR-07 | Drop height is reported as an *estimate* and is flagged invalid if the free-fall phase was interrupted or the fall was not clean |

---

# PHASE C (i) — Data Architecture

## C.1 Data Entities

| Entity | Description | Residence | Lifecycle |
|---|---|---|---|
| `AccelSample` | Raw X/Y/Z 16-bit signed reading + implicit timestamp | SRAM (ring buffer) | Volatile, overwritten continuously |
| `MagnitudeSample` | Derived signal vector magnitude, scaled mg | SRAM (ring buffer) | Volatile |
| `DetectionState` | Current state machine state + timers | SRAM | Volatile |
| `FallEvent` | Characterised event record | SRAM (current) + EEPROM (history) | Persistent, circular |
| `DeviceConfig` | Thresholds, timing, comms, behaviour | EEPROM, mirrored in SRAM | Persistent, versioned, CRC-protected |
| `DeviceStatus` | Health flags, uptime, counters | SRAM (counters mirrored to EEPROM periodically) | Mixed |
| `ModbusImage` | Register-view projection of the above | SRAM | Volatile, regenerated on access |

## C.2 `FallEvent` Structure (16 bytes — EEPROM record)

| Offset | Field | Type | Units / Notes |
|---|---|---|---|
| 0 | `event_id` | uint16 | Monotonic |
| 2 | `timestamp_s` | uint32 | Seconds since power-on (see §C.5 on time) |
| 6 | `freefall_ms` | uint16 | Measured near-zero-g duration |
| 8 | `height_cm` | uint16 | Estimated, 0xFFFF = invalid |
| 10 | `impact_peak_mg` | uint16 | Peak SVM during impact window; 0 = no impact detected |
| 12 | `orient_delta_deg` | uint8 | Tilt change pre vs post event |
| 13 | `flags` | uint8 | bit0 confirmed, bit1 impact seen, bit2 rest seen, bit3 height valid, bit4 saturated, bit5 truncated |
| 14 | `crc16` | uint16 | Record integrity |

At 16 bytes, reserving 512 B of the 1 KB EEPROM gives **32 stored events** in a circular log, with the remaining 512 B for `DeviceConfig`, counters and wear-levelling headroom.

## C.3 SRAM Budget — Critical Constraint

The ATmega328P has **2048 bytes** of SRAM total, shared with the stack, the Modbus frame buffers and all library state.

| Consumer | Estimate | Note |
|---|---|---|
| Modbus RTU RX + TX buffers | ~ 300 B | 256-byte ADU each is not affordable; cap at 128 B and restrict max register count per request |
| Modbus register mirror | ~ 100 B | |
| `DeviceConfig` RAM mirror | ~ 60 B | |
| Detection state + timers | ~ 60 B | |
| Current `FallEvent` + previous | ~ 32 B | |
| Stack + ISR frames | ~ 400 B | Keep ISRs minimal |
| Library / runtime overhead | ~ 250 B | |
| **Available for sample buffer** | **~ 800 B** | |

**Architectural decision:** do **not** buffer raw tri-axial samples. Buffering X/Y/Z as int16 costs 6 B/sample — only ~130 samples, i.e. 1.3 s at 100 Hz, leaving no margin. Instead buffer **magnitude only**, as uint16 mg (2 B/sample): 100 Hz × 3 s = 300 samples = 600 B, which fits with margin. Raw tri-axial data is retained only for the small window immediately around the impact peak (e.g. 16 samples = 96 B) where axis information is actually needed. See ADR-003.

## C.4 Data Flow

```
ADXL345 registers
   │ (I²C burst read, 6 bytes, triggered by DATA_READY or timer)
   ▼
AccelSample (X,Y,Z raw LSB)
   │ scale by range → mg
   ▼
Calibrated tri-axial mg ──┬──▶ MagnitudeRingBuffer (uint16 mg)
                          │           │
                          │           ├──▶ Free-fall detector (software confirm)
                          │           ├──▶ Impact detector [REUSED]
                          │           └──▶ Rest detector
                          │
                          └──▶ ImpactWindowBuffer (tri-axial, 16 samples)
                                      │
                                      ▼
                          Orientation / tilt computation
                                      │
                                      ▼
                             DetectionStateMachine
                                      │
                        ┌─────────────┼──────────────┐
                        ▼             ▼              ▼
                  DiscreteOutput  FallEvent    ModbusImage
                                      │
                                      ▼
                                EEPROM event log
```

Parallel low-latency path (PR-03):
```
ADXL345 INT1 (on-chip FREE_FALL) ──▶ MCU external interrupt ──▶ set flag
                                                                  │
                                                          main loop (<10 ms)
                                                                  │
                                                          assert DO1 immediately
```

## C.5 Time and Timestamps

There is no RTC on the baseline hardware (PR-01). Consequently:

- Internal time is **uptime in seconds since power-on**, maintained from a timer tick.
- Events are stamped with uptime.
- The Modbus master may write a "host epoch" value to a holding register at commissioning; the device then reports `host_epoch + uptime` as a derived absolute timestamp, flagged as *host-synchronised* or *uptime-only* in the status word.
- **Do not claim traceable timestamps.** If the customer requires forensic-grade time, the correlation must happen in the PLC/historian, which timestamps the register read.

## C.6 Data Quality Rules

| Rule | Enforcement |
|---|---|
| Config integrity | CRC16 over `DeviceConfig`; on mismatch, load factory defaults and raise config-fault bit |
| Event record integrity | CRC16 per record; corrupt records reported as invalid, not silently skipped |
| Saturation flagging | If any axis reads full-scale during the impact window, set the `saturated` flag; `impact_peak_mg` is then a lower bound |
| Height validity | Height marked invalid if free-fall phase was non-contiguous, exceeded plausible bounds, or the range/ODR configuration was changed mid-event |
| Register consistency | Multi-register 32-bit values are copied into a shadow buffer before a Modbus response is assembled, so a master never reads a torn value |

---

# PHASE C (ii) — Application Architecture

## C7.1 Layered Component Model

```
┌───────────────────────────────────────────────────────────────┐
│ L5  DEVICE MANAGEMENT                                         │
│     Commissioning · Config persistence · Self-test scheduler  │
│     Watchdog supervision · Factory reset                      │
├───────────────────────────────────────────────────────────────┤
│ L4  INTERFACE / PUBLICATION                                   │
│     Modbus RTU Slave · Register Map Service · Event Log Access│
│     Discrete Output Manager [REUSED] · Local HMI [REUSED]     │
├───────────────────────────────────────────────────────────────┤
│ L3  EVENT LOGIC                                               │
│     Detection State Machine · Event Characteriser             │
│     Latch & Acknowledge Manager [REUSED]                      │
├───────────────────────────────────────────────────────────────┤
│ L2  DETECTION ALGORITHMS                                      │
│     Free-Fall Detector [NEW] · Impact Detector [REUSED]       │
│     Rest Detector [NEW] · Orientation Estimator [NEW]         │
├───────────────────────────────────────────────────────────────┤
│ L1  SIGNAL PROCESSING                                         │
│     Scaling & Calibration [REUSED] · SVM Computation          │
│     Ring Buffer Manager · Simple IIR smoother                 │
├───────────────────────────────────────────────────────────────┤
│ L0  HAL / DRIVERS                                             │
│     ADXL345 Driver [REUSED, RECONFIGURED] · I²C · UART        │
│     GPIO · Timer · EEPROM · WDT · External Interrupt          │
└───────────────────────────────────────────────────────────────┘
```

## C7.2 Component Catalogue

| Component | Responsibility | Disposition | Key interfaces |
|---|---|---|---|
| `adxl345_drv` | Register access, configuration, FIFO, self-test, interrupt config | **Reuse, reconfigure** | `init(cfg)`, `read_xyz()`, `read_int_source()`, `self_test()` |
| `calib` | Offset/gain correction, LSB→mg scaling | **Reuse** | `apply(raw)→mg` |
| `svm` | Signal vector magnitude, fixed-point | New (small) | `magnitude(x,y,z)→mg` |
| `ringbuf` | Fixed-size circular magnitude buffer | New | `push()`, `window_stats()` |
| `ff_detect` | Free-fall onset/offset, duration measurement | **New — core deliverable** | `update(mg, dt)→ff_state` |
| `impact_detect` | High-g transient detection, peak capture | **Reuse unmodified** | `update(mg)→impact_event` |
| `rest_detect` | Low-variance settle detection | New (small) | `update(window)→at_rest` |
| `orient_est` | Tilt angle from static gravity vector | New | `tilt(x,y,z)→deg` |
| `fsm` | ARMED / CANDIDATE / CONFIRMING / LATCHED / FAULT | **New — orchestration** | `tick(inputs)→actions` |
| `characteriser` | Duration, height, severity, flags → `FallEvent` | New | `build_event()` |
| `latch_mgr` | Output latch, acknowledge, auto-reset timer | **Reuse** | `set()`, `clear()`, `status()` |
| `dout` | Discrete output driver, opto/relay | **Reuse** | `write(ch, state)` |
| `hmi` | LED patterns, buzzer | **Reuse, extend patterns** | `indicate(state)` |
| `modbus_slave` | RTU framing, CRC, FC dispatch, T3.5 timing | **New** | `poll()` |
| `regmap` | Projection of internal state to registers, command handling | **New** | `read(addr,n)`, `write(addr,vals)` |
| `cfg_store` | EEPROM config load/save, CRC, defaults | **Reuse, extend** | `load()`, `save()`, `defaults()` |
| `evt_log` | Circular EEPROM event log | New | `append()`, `get(idx)` |
| `diag` | Self-test scheduling, health flags, plausibility checks | **Extend** | `run()`, `health()` |
| `sched` | Cooperative non-blocking task loop | **Reuse** | `run()` |

## C7.3 Detection Strategy — The Central Design Decision

Three candidate approaches were evaluated:

| Option | Description | Latency | False positives | MCU load | Verdict |
|---|---|---|---|---|---|
| **1. On-chip only** | Use ADXL345 `THRESH_FF`/`TIME_FF` and the FREE_FALL interrupt exclusively | Lowest | Highest — a toss, a fast lowering or a sharp downward jerk can trip it | Minimal | Insufficient alone |
| **2. Software only** | Stream samples, compute SVM, threshold in firmware | Higher, ODR-dependent | Low with good windowing | Highest — continuous I²C traffic | Wasteful on this MCU |
| **3. Hybrid (SELECTED)** | On-chip interrupt as the *trigger* and immediate annunciation path; software confirms the fall profile and characterises it | Low for annunciation, moderate for confirmation | Lowest — three-stage profile | Moderate, bursty | **Selected** |

**Selected architecture — three-stage fall profile:**

```
      1 g ─────┐                    ╱╲  ← Stage 2: IMPACT (high-g transient)
               │                   ╱  ╲    reuses the existing impact detector
               │                  ╱    ╲
               │                 ╱      ╲
               └────────────────╱        ╲──────────────  ← Stage 3: REST
                                                             (~1 g, low variance,
      ~0 g ─────────────────────                              new orientation)
               └── Stage 1: FREE FALL ──┘
                   all axes < threshold
                   for > TIME_FF
```

This structure is what makes the impact sensor a genuine asset rather than a coincidence of hardware: **Stage 2 is the existing product's entire function, reused as a confirmation stage.**

## C7.4 Detection State Machine

| State | Entry condition | Actions | Exits |
|---|---|---|---|
| `INIT` | Power-on | Load config, init driver, self-test | → `ARMED` (pass) / `FAULT` (fail) |
| `ARMED` | Ready | Monitor FF interrupt + magnitude | → `CANDIDATE` on FF trigger |
| `CANDIDATE` | Free-fall detected | Start duration timer; **if mode = Immediate, assert output now**; buffer magnitudes | → `CONFIRMING` when SVM rises above impact-arm threshold; → `ARMED` (or `LATCHED`, per mode) on timeout without impact |
| `CONFIRMING` | Free-fall ended | Run impact detector over window; capture peak; start rest timer | → `SETTLING` when impact captured or window expires |
| `SETTLING` | Impact processed | Wait for low-variance rest; compute orientation delta | → `LATCHED` on confirmation; → `ARMED` if confirmation required and not obtained |
| `LATCHED` | Fall confirmed | Assert DO1, write event record, update registers, HMI alarm | → `ARMED` on reset (local button / Modbus command / auto-reset timer) |
| `FAULT` | Self-test fail, config CRC fail, sensor unresponsive | De-assert health output, set fault bits, HMI fault pattern | → `ARMED` on recovery + reset |

**Confirmation modes (configurable, `40004`):**

| Mode | Behaviour | Use when |
|---|---|---|
| 0 — Immediate | Latch on free-fall alone | Lowest latency needed; some false positives tolerable |
| 1 — Impact-confirmed | Latch only if impact follows within window | Default. Balances latency and false alarms |
| 2 — Fully confirmed | Latch only on free-fall + impact + rest | Noisy handling environments, alarm fatigue is the problem |
| 3 — Immediate + revoke | Assert immediately; if no impact follows, log as unconfirmed and optionally auto-clear | Where the PLC can tolerate a brief transient signal |

## C7.5 Free-Fall Detector Design

**On-chip configuration (ADXL345):**

| Register | Purpose | Recommended setting | Notes |
|---|---|---|---|
| `THRESH_FF` (0x28) | Free-fall threshold, **62.5 mg/LSB** | 0x07 (≈ 437 mg) starting point; datasheet-recommended band is roughly 300–600 mg | Too low → misses tumbling/dragged falls. Too high → trips on ordinary handling |
| `TIME_FF` (0x29) | Free-fall duration, **5 ms/LSB** | 0x1E (150 ms) starting point; recommended band roughly 100–350 ms | This is the primary sensitivity knob — see height table below |
| `BW_RATE` (0x2C) | Output data rate | 100 Hz | Below ~50 Hz the timing resolution degrades badly |
| `DATA_FORMAT` (0x31) | Range / resolution | **±16 g, full resolution** | Must stay wide enough for the impact stage; full-res keeps 3.9 mg/LSB throughout, so free-fall sensitivity is not sacrificed. See ADR-002 |
| `INT_ENABLE` (0x2E) | Enable FREE_FALL (bit 2) and the impact/tap or activity source used by the baseline | — | |
| `INT_MAP` (0x2F) | Route FREE_FALL → INT1, impact → INT2 | — | Separate pins avoid source-decoding latency |

**Critical semantics:** the ADXL345 asserts free-fall when the acceleration on **all** axes is below `THRESH_FF` continuously for longer than `TIME_FF`. It is a magnitude-collapse detector, not a downward-motion detector — it therefore also fires on genuine weightlessness in any orientation, which is correct for this application.

**Free-fall time vs minimum detectable drop height** (h = ½gt², ideal free fall, no drag):

| `TIME_FF` | Duration | Minimum height before detection | Practical meaning |
|---|---|---|---|
| 0x14 | 100 ms | ~5 cm | Very sensitive; will trip on setting an object down briskly |
| 0x1E | 150 ms | ~11 cm | Reasonable default |
| 0x28 | 200 ms | ~20 cm | Ignores handling, catches real drops |
| 0x3C | 300 ms | ~44 cm | Only substantial falls |
| 0x46 | 350 ms | ~60 cm | Conservative; upper end of the recommended band |

This table is the single most useful thing to put in front of the customer during commissioning: **the free-fall time setting is a direct statement of "how far must it fall before you want to know".**

**Software confirmation layer** (runs on the magnitude ring buffer):
- Recompute SVM over the candidate window; require the fraction of samples below threshold to exceed a configured proportion, which tolerates a brief spike from tumbling that would otherwise reset the on-chip timer.
- Measure true free-fall duration in software from the sample record, rather than trusting the interrupt edge timing alone.
- Reject candidates whose pre-event window shows the device was already in sustained high vibration (a machine-mounted resonance can drive the magnitude low intermittently).

## C7.6 Event Characterisation

| Quantity | Method | Caveats to publish alongside |
|---|---|---|
| Free-fall duration | Software-measured span of sub-threshold samples, ms | Truncated if impact occurs before the sample window closes |
| Drop height | h ≈ ½ · 9.81 · t², reported in cm | **Underestimates** real height, because the first ~`TIME_FF` of the fall is consumed before detection and air drag reduces true free-fall time. Report as an estimate; flag invalid for tumbling or interrupted falls |
| Impact peak | Max SVM in the impact window, mg — **from the reused impact detector** | Flag if any axis saturated at full scale |
| Impact direction | Dominant axis of the peak sample | Only meaningful for a single clean strike |
| Orientation change | Angle between pre-event and post-rest gravity vectors, degrees | Requires a valid rest phase |
| Confirmation flags | Which stages of the profile were observed | The honest quality indicator for the whole record |

## C7.7 Task Scheduling (Cooperative, Non-blocking)

| Task | Period | Priority | Worst-case budget |
|---|---|---|---|
| ISR: FF interrupt (INT1) | Async | Highest | Set flag + capture timer only, < 10 µs |
| ISR: Impact interrupt (INT2) | Async | Highest | Set flag + capture timer only |
| Sample acquisition | 10 ms (100 Hz) | High | ~1 ms (I²C burst) |
| Detection pipeline | 10 ms | High | ~2 ms |
| State machine tick | 10 ms | High | < 0.5 ms |
| Output / latch service | 10 ms | High | < 0.1 ms |
| Modbus poll | Continuous, byte-driven | Medium | Must yield; never spin |
| HMI update | 100 ms | Low | < 0.5 ms |
| Self-test / diagnostics | 60 s | Low | Deferred, must not run during CANDIDATE/CONFIRMING |
| EEPROM write | On event only | Low | Deferred until after output assertion |
| Watchdog kick | Main loop | — | — |

**Rule (PR-03/PR-04):** EEPROM writes and Modbus transactions are never performed inside the detection path. The output is asserted first; persistence and publication follow.

---

# PHASE D — Technology Architecture

## D.1 Physical Architecture

```
        ┌──────────────────────── SENSOR NODE (unchanged PCB) ─────────────────────┐
        │                                                                          │
        │   ┌─────────────┐   I²C (SCL/SDA)   ┌──────────────────┐                 │
        │   │  ADXL345    │◀─────────────────▶│                  │                 │
        │   │ 3-axis      │                   │  Arduino Nano    │                 │
        │   │ accel       │──── INT1 ────────▶│  ATmega328P      │                 │
        │   │             │──── INT2 ────────▶│  16 MHz          │                 │
        │   └─────────────┘                   │  32K flash       │                 │
        │                                     │  2K SRAM         │                 │
        │   ┌─────────────┐                   │  1K EEPROM       │                 │
        │   │ Reset btn   │──────────────────▶│                  │                 │
        │   └─────────────┘                   │                  │                 │
        │   ┌─────────────┐                   │                  │                 │
        │   │ Config DIP  │──────────────────▶│                  │                 │
        │   └─────────────┘                   │                  │                 │
        │                                     │                  │                 │
        │   ┌─────────────┐   opto/relay      │                  │                 │
        │   │ DO1 FALL    │◀──────────────────│                  │                 │
        │   │ DO2 HEALTH  │◀──────────────────│                  │                 │
        │   └─────────────┘                   │                  │                 │
        │                                     │                  │                 │
        │   ┌─────────────┐   UART + DE/RE    │                  │                 │
        │   │ RS-485 xcvr │◀─────────────────▶│                  │                 │
        │   └──────┬──────┘                   └────────┬─────────┘                 │
        │          │                                   │                           │
        │   ┌──────┴──────┐                   ┌────────┴─────────┐                 │
        │   │ Terminal    │                   │ Power reg        │                 │
        │   │ block       │                   │ 12-24 V → 5 V    │                 │
        └───┴──────┬──────┴───────────────────┴────────┬─────────┴─────────────────┘
                   │                                   │
          A / B / GND (RS-485)                   V+ / V- field supply
                   │                                   │
                   ▼                                   ▼
        ┌─────────────────────────────────────────────────────────┐
        │            CUSTOMER SYSTEM (PLC / DCS / SCADA)          │
        │  DI card ◀── DO1 (fall), DO2 (health)                   │
        │  Serial/gateway ◀── Modbus RTU master, RS-485 multidrop │
        └─────────────────────────────────────────────────────────┘
```

## D.2 Pin Allocation **[ASSUMED — CONFIRM against baseline schematic]**

| Pin | Function | Notes |
|---|---|---|
| D0 / D1 | UART RX / TX → RS-485 transceiver | Shared with USB bridge — see ADR-004 |
| D2 | ADXL345 INT1 — FREE_FALL | INT0, hardware external interrupt |
| D3 | ADXL345 INT2 — impact/activity | INT1, hardware external interrupt |
| D4 | RS-485 DE/RE (direction control) | Tie DE and RE together |
| D5 | Buzzer / audible indication | Reused |
| D6 | **DO1 — Fall detected** (opto/relay drive) | Reused output stage |
| D7 | **DO2 — Health / OK** | Reused output stage |
| D8 | Local reset / acknowledge input | Debounced, pulled up |
| D9 | Status LED (green — armed) | |
| D10 | Status LED (red — alarm/fault) | |
| D11–D13 | Spare / SPI if ADXL345 is on SPI in the baseline | Mutually exclusive with A4/A5 use |
| A0 | Supply voltage monitor | Brown-out and undervoltage diagnostics |
| A1 | Config DIP switches (resistor ladder) | Slave ID / baud at commissioning |
| A4 / A5 | I²C SDA / SCL → ADXL345 | If baseline uses I²C |

## D.3 Technology Standards Catalogue

| Layer | Standard / choice | Rationale |
|---|---|---|
| MCU | ATmega328P, 16 MHz, 5 V | Inherited (PR-01) |
| Sensor | ADXL345, ±16 g full-resolution mode | Inherited; on-chip free-fall engine |
| Sensor bus | I²C @ 400 kHz (or SPI @ ≤ 5 MHz) | Inherited |
| Field bus | **Modbus RTU over RS-485 (EIA-485), 2-wire half-duplex** | Universal PLC support; matches customer requirement |
| Default serial | 19200 baud, 8-E-1 | Modbus specification default |
| Supported baud | 9600 / 19200 / 38400 / 57600 / 115200 | 115200 marginal on SoftwareSerial — see ADR-004 |
| Byte order | Big-endian (Modbus native); 32-bit values as high word first | Document explicitly — this is the #1 integration support ticket |
| Discrete output | Inherited opto-isolated / dry contact | PR-02 |
| Config storage | Internal EEPROM with CRC16 | Inherited pattern |
| Firmware language | C / C++ subset, no dynamic allocation | PR-04 |
| Watchdog | AVR WDT, 1 s timeout | PR-07 |

## D.4 ADR — Architecture Decision Records

**ADR-001 — Hybrid detection (on-chip trigger + software confirmation)**
*Status:* Accepted. *Context:* Pure on-chip free-fall detection is fast but false-positive prone; pure software is expensive on a 2 KB / 16 MHz platform. *Decision:* Use the ADXL345 FREE_FALL interrupt as the trigger and immediate annunciation path, with a software three-stage confirmation for latching and characterisation. *Consequence:* Two configurable sensitivity layers must be kept consistent; commissioning documentation must explain both.

**ADR-002 — Retain ±16 g full-resolution range**
*Status:* Accepted. *Context:* Free-fall detection alone would favour ±2 g for maximum sensitivity, but the impact confirmation stage needs headroom, and range switching mid-event is not viable. *Decision:* Operate permanently in ±16 g full-resolution mode, which preserves ~3.9 mg/LSB — far finer than the 62.5 mg/LSB free-fall threshold granularity, so nothing is lost on the free-fall side. *Consequence:* No range switching logic; impact peaks up to 16 g quantified, beyond which the saturation flag applies.

**ADR-003 — Buffer magnitude, not tri-axial samples**
*Status:* Accepted. *Context:* SRAM is 2 KB (§C.3). *Decision:* Maintain a magnitude-only ring buffer for the detection window; retain tri-axial data only for a short window around the impact peak. *Consequence:* Full waveform capture and export are not possible on this platform — explicitly out of scope, deferred to a future MCU (§10.5).

**ADR-004 — Serial port arbitration**
*Status:* **Open — decision required.** *Context:* The ATmega328P has one USART, shared with the USB-serial bridge. Options: (a) Modbus on the hardware UART, accepting that USB debug and field comms cannot coexist; (b) Modbus on `SoftwareSerial`, freeing USB but limiting reliable baud to ≈ 38400 and introducing interrupt latency that can disturb sample timing; (c) hardware UART with a jumper isolating the USB bridge in the field. *Recommendation:* **(c)** — hardware UART for field operation with the USB bridge jumper-isolated, since Modbus timing (T1.5/T3.5 character gaps) is genuinely hard to honour on a software serial port while also servicing a 100 Hz sample loop. This depends on the baseline board's layout and must be confirmed with the hardware lead.

**ADR-005 — Discrete output remains the primary signal**
*Status:* Accepted. *Context:* Customers have existing wiring and interlock logic. *Decision:* Modbus is strictly additive and advisory (PR-02, PR-03). *Consequence:* All safety-relevant sequencing on the customer side stays on the discrete path; Modbus carries characterisation and diagnostics only.

---

# PHASE E — Opportunities and Solutions

## E.1 Gap Analysis: Impact Sensor (Baseline) → Free-Fall Sensor (Target)

| Domain | Baseline | Target | Gap | Action |
|---|---|---|---|---|
| **PCB / BOM / enclosure** | Impact sensor board | Identical | **None** | Reuse as-is |
| **ADXL345 config** | Tap / activity registers, range for impact | + `THRESH_FF`, `TIME_FF`, FREE_FALL interrupt, INT mapping, ODR 100 Hz | Configuration only | Extend `adxl345_drv` init |
| **Driver layer** | Present | Same + interrupt source decoding | Minor | Extend |
| **Calibration / scaling** | Present | Same | None | Reuse |
| **Impact detection** | Present, field-proven | Same, repositioned as confirmation stage | None functionally | Reuse, re-wire into FSM |
| **Free-fall detection** | Absent | Required | **Full gap** | Build `ff_detect` |
| **Rest / settle detection** | Absent | Required for mode 2 | **Full gap** | Build `rest_detect` |
| **Orientation estimation** | Absent | Required for event record | **Full gap** | Build `orient_est` |
| **State machine** | Simple (detect→latch) | Multi-stage with timeouts | **Substantial gap** | Rebuild `fsm` |
| **Event characterisation** | Absent (or minimal) | Duration, height, severity, flags | **Full gap** | Build `characteriser` |
| **Event log** | Absent (assumed) | 32-record circular EEPROM log | **Full gap** | Build `evt_log` |
| **Discrete output** | Present | Identical behaviour | **None** | Reuse |
| **Latch / acknowledge** | Present | Same + Modbus-initiated reset | Minor | Extend |
| **Local HMI** | Present | Additional state patterns | Minor | Extend |
| **Modbus RTU slave** | Absent | Required | **Full gap** | Build `modbus_slave` + `regmap` |
| **RS-485 wiring/termination** | Possibly unpopulated | Required | Check | Confirm transceiver is fitted; if not, PR-01 is breached — escalate |
| **Config persistence** | Present | + new parameters, versioned schema | Minor | Extend |
| **Self-test / diagnostics** | Basic | Scheduled self-test, plausibility, health output | Moderate | Extend `diag` |
| **Commissioning tooling** | Minimal | Slave ID, baud, threshold setting | **Full gap** | Build/document |

**Reuse estimate:** roughly 55–65 % of the existing firmware carries over unmodified or configuration-only, concentrated in the driver, signal-conditioning, impact-detection and output layers. New work concentrates in detection orchestration, characterisation and comms. This meets SC-06 but the margin is thin — confirm against the actual codebase.

## E.2 Work Packages

| WP | Description | Depends on | Effort (rel.) |
|---|---|---|---|
| WP-1 | Baseline code audit; module inventory and reuse confirmation | Access to impact sensor repo | S |
| WP-2 | ADXL345 driver extension: free-fall registers, dual interrupt mapping, self-test | WP-1 | S |
| WP-3 | Signal layer: SVM, magnitude ring buffer, window statistics | WP-1 | S |
| WP-4 | Free-fall detector + software confirmation | WP-2, WP-3 | M |
| WP-5 | Rest detector + orientation estimator | WP-3 | S |
| WP-6 | Detection state machine and mode handling | WP-4, WP-5 | M |
| WP-7 | Event characteriser + EEPROM event log | WP-6 | M |
| WP-8 | Modbus RTU slave + register map service | ADR-004 resolved | **L** |
| WP-9 | Config schema extension, persistence, versioning, migration | WP-7 | S |
| WP-10 | Diagnostics, watchdog, health output, self-test scheduler | WP-2 | S |
| WP-11 | SRAM/flash budget verification and optimisation pass | WP-6, WP-8 | M |
| WP-12 | Drop-rig test harness and qualification matrix | WP-6 | M |
| WP-13 | Threshold tuning against real drop data | WP-12 | M |
| WP-14 | Commissioning guide, register map document, integration note | WP-8, WP-13 | M |
| WP-15 | Field trial with lead customer | All | L |

**Critical path:** WP-1 → WP-2 → WP-4 → WP-6 → WP-12 → WP-13 → WP-15. WP-8 (Modbus) runs in parallel but is the largest single risk to the flash/SRAM budget, so WP-11 must gate firmware freeze.

## E.3 Solution Building Blocks vs Architecture Building Blocks

| Architecture building block | Solution building block | Source |
|---|---|---|
| Motion sensing | ADXL345 + existing driver | Reused |
| Free-fall detection | On-chip FF engine + `ff_detect` | Hybrid: vendor IP + new code |
| Impact detection | Existing impact algorithm | Reused |
| Discrete annunciation | Existing output stage + `latch_mgr` | Reused |
| Data publication | Lightweight Modbus RTU slave | New (consider a proven minimal RTU implementation rather than a heavyweight library — see §12 R-04) |
| Config persistence | Existing EEPROM pattern | Reused, extended |
| Supervision | AVR WDT + `diag` | Extended |

---

# PHASE F — Migration and Implementation Planning

## F.1 Phased Delivery

| Phase | Content | Exit criteria |
|---|---|---|
| **F0 — Baseline** | Audit impact sensor code; stand up build; confirm assumptions A1–A6; resolve ADR-004 | Assumption table cleared; reuse inventory signed off |
| **F1 — Detection core** | WP-2 to WP-6. On-chip FF trigger, software confirmation, FSM, discrete output. **No Modbus.** | Bench drops reliably detected and annunciated; latency meets SC-01 |
| **F2 — Characterisation** | WP-7, WP-9. Event record, height estimate, EEPROM log | Event data matches instrumented drop-rig measurements within SC-04 |
| **F3 — Publication** | WP-8, WP-10. Modbus RTU, register map, diagnostics, health output | Interoperates with at least two independent Modbus masters; SC-05 met |
| **F4 — Hardening** | WP-11, WP-12, WP-13. Budget verification, tuning, EMC/environmental regression | SC-02, SC-03, SC-06, SC-07 met; no watchdog resets over 168 h soak |
| **F5 — Field trial** | WP-14, WP-15 | Lead customer sign-off; support documentation complete |

**Deliberate sequencing rationale:** F1 delivers a working, shippable free-fall sensor using only the discrete output. If Modbus proves to be a bigger integration or resource problem than expected, there is a saleable product at the end of F2 — this is a direct consequence of PR-02 and PR-03.

## F.2 Transition Architecture (end of F2)

A device that is functionally a free-fall sensor with impact-sensor-compatible wiring, full local behaviour and a populated event log, but with no field bus. Retrievable only via the local interface. This is a valid interim product state, not merely a milestone.

## F.3 Migration Considerations for Existing Customers

| Consideration | Approach |
|---|---|
| Existing impact sensors in the field | No forced migration. FFD-100 is a separate SKU |
| Can an installed impact sensor be field-upgraded? | If hardware is truly identical, yes — by firmware reflash. **Confirm the RS-485 transceiver is populated on existing boards**; if it is not, upgraded units are discrete-output-only |
| Wiring changes | Discrete output: none. Modbus: new twisted pair + termination |
| Customer PLC changes | Optional. Existing DI logic continues to work unchanged |
| Configuration migration | New config schema version; loader detects old-version records and applies defaults for new fields rather than rejecting them |

---

# PHASE G — Implementation Governance

## G.1 Compliance Checkpoints

| Gate | Checks |
|---|---|
| **Design review (pre-code)** | Assumptions A1–A6 resolved; ADR-004 closed; pin map confirmed against schematic; register map reviewed by a customer-facing engineer |
| **Code review (per WP)** | No dynamic allocation; no blocking delays in the detection path; ISR bodies minimal; all fixed-point arithmetic range-checked for overflow |
| **Pre-freeze review** | SRAM headroom ≥ 15 % measured at worst case (not computed); flash headroom ≥ 10 %; stack high-water mark instrumented |
| **Pre-trial review** | Full drop matrix passed; EMC regression clean; 168 h soak with no watchdog reset; Modbus conformance against two masters |

## G.2 Architecture Compliance Rules for Implementation

| Rule | Verification |
|---|---|
| Modbus never blocks detection | Timing instrumentation: assert a scope pin around the detection path; confirm jitter under full Modbus load |
| Output asserts before any persistence or comms activity | Code review + scope trace from INT1 edge to DO1 edge |
| Every configurable parameter has a validated range and a defined default | Config schema table reviewed against `regmap` |
| No silent failure paths | Every error branch sets a status bit and drives an HMI state |
| Fixed-point only | Compiler flags to detect float usage; check map file for float library linkage (this alone can blow the flash budget) |

## G.3 Dispensations

Any breach of PR-01 (hardware invariance) or PR-02 (interface continuity) requires: written justification, Board approval, an ADR, and an assessment of the impact on existing field installations.

---

# PHASE H — Architecture Change Management

## H.1 Change Drivers and Response

| Driver | Likely response | Governance path |
|---|---|---|
| Customer needs raw waveform capture | Not feasible on ATmega328P (ADR-003) — platform change | Full ADM cycle |
| Customer needs wireless / IIoT | Gateway external to the sensor; sensor stays a Modbus slave | Phase E increment |
| False positive rate unacceptable at a site | Retune thresholds/mode via config — no firmware change (PR-05) | Field procedure only |
| Customer requires a SIL rating | **New product line under IEC 61508** — not achievable by modifying this architecture | Full ADM cycle, new vision |
| Modbus TCP requested | External RTU/TCP gateway | Change request, no device change |
| ADXL345 obsolescence | Driver-layer abstraction limits blast radius; detection layer written against mg, not LSB | Phase D increment |

## H.2 Architecture Maintenance

- Register map is a **published contract**. Additions go to unused addresses; existing addresses are never repurposed. Version the map and expose the map version in a register.
- Config schema is versioned with forward migration in the loader.
- Every field-observed false positive or missed detection is captured with its event record and fed back into the threshold tuning dataset — this dataset is a long-lived architecture asset, arguably more valuable than the firmware.

---

# CROSS-PHASE ARTEFACTS

## 6.3 Modbus RTU Register Map (Draft Contract)

**Conventions:** all registers 16-bit; 32-bit values occupy two consecutive registers, **high word first**; addresses shown as conventional data-model references, with the zero-based protocol offset in brackets.

### Discrete Inputs (FC02) — read-only status bits

| Ref | Offset | Name | Description |
|---|---|---|---|
| 10001 | 0 | `FALL_LATCHED` | Fall detected and latched |
| 10002 | 1 | `FREEFALL_ACTIVE` | Free-fall currently in progress (live) |
| 10003 | 2 | `DEVICE_HEALTHY` | All self-tests passing |
| 10004 | 3 | `CONFIG_VALID` | Config CRC good |
| 10005 | 4 | `SENSOR_OK` | ADXL345 responding and plausible |
| 10006 | 5 | `EVENT_LOG_FULL` | Circular log has wrapped |
| 10007 | 6 | `IMPACT_CONFIRMED` | Last event included an impact |
| 10008 | 7 | `TIME_SYNCED` | Host epoch has been written |

### Coils (FC01/FC05) — commands

| Ref | Offset | Name | Description |
|---|---|---|---|
| 00001 | 0 | `CLEAR_LATCH` | Write 1 to clear the fall latch and re-arm |
| 00002 | 1 | `RUN_SELFTEST` | Write 1 to trigger an immediate self-test |
| 00003 | 2 | `SAVE_CONFIG` | Write 1 to persist current config to EEPROM |

### Input Registers (FC04) — measurements and event data

| Ref | Offset | Name | Type | Units / Notes |
|---|---|---|---|---|
| 30001 | 0 | `STATUS_WORD` | bitfield | Mirrors discrete inputs + FSM state in high nibble |
| 30002 | 1 | `FSM_STATE` | enum | 0=INIT 1=ARMED 2=CANDIDATE 3=CONFIRMING 4=SETTLING 5=LATCHED 6=FAULT |
| 30003 | 2 | `FAULT_FLAGS` | bitfield | Sensor, config, EEPROM, supply, self-test |
| 30004 | 3 | `EVENT_COUNT` | uint16 | Monotonic total falls |
| 30005 | 4 | `LAST_FF_DURATION` | uint16 | ms |
| 30006 | 5 | `LAST_HEIGHT_EST` | uint16 | cm, 0xFFFF = invalid |
| 30007 | 6 | `LAST_IMPACT_PEAK` | uint16 | mg |
| 30008 | 7 | `LAST_ORIENT_DELTA` | uint16 | degrees |
| 30009 | 8 | `LAST_EVENT_FLAGS` | bitfield | Confirmation stage flags |
| 30010–30011 | 9–10 | `LAST_EVENT_TIME` | uint32 | Uptime seconds at event |
| 30012 | 11 | `ACCEL_X` | int16 | mg, live |
| 30013 | 12 | `ACCEL_Y` | int16 | mg, live |
| 30014 | 13 | `ACCEL_Z` | int16 | mg, live |
| 30015 | 14 | `ACCEL_SVM` | uint16 | mg, live magnitude |
| 30016 | 15 | `TILT_ANGLE` | uint16 | degrees from vertical, live |
| 30017 | 16 | `SUPPLY_MV` | uint16 | mV |
| 30018–30019 | 17–18 | `UPTIME_S` | uint32 | seconds |
| 30020 | 19 | `FW_VERSION` | uint16 | Major (hi byte) / minor (lo byte) |
| 30021 | 20 | `REGMAP_VERSION` | uint16 | Contract version |
| 30022 | 21 | `SELFTEST_RESULT` | bitfield | Per-axis pass/fail |
| 30023 | 22 | `MISSED_SAMPLES` | uint16 | Diagnostic: acquisition overruns |
| 30024 | 23 | `WDT_RESETS` | uint16 | Diagnostic: watchdog reset count |
| 30051–30058 | 50–57 | `EVENT_RECORD[]` | — | Selected historical record, per `EVENT_SELECT` |

### Holding Registers (FC03/FC06/FC16) — configuration and commands

| Ref | Offset | Name | Type | Range | Default |
|---|---|---|---|---|---|
| 40001 | 0 | `FF_THRESHOLD_MG` | uint16 | 190–940 (quantised to 62.5 mg steps) | 437 |
| 40002 | 1 | `FF_TIME_MS` | uint16 | 50–1275 (5 ms steps) | 150 |
| 40003 | 2 | `IMPACT_THRESH_MG` | uint16 | Per baseline impact sensor | Inherit |
| 40004 | 3 | `CONFIRM_MODE` | enum | 0–3 (see §C7.4) | 1 |
| 40005 | 4 | `CONFIRM_WINDOW_MS` | uint16 | 100–5000 | 1500 |
| 40006 | 5 | `REST_WINDOW_MS` | uint16 | 200–5000 | 1000 |
| 40007 | 6 | `LATCH_MODE` | enum | 0=manual, 1=auto-reset | 0 |
| 40008 | 7 | `AUTO_RESET_S` | uint16 | 1–3600 | 60 |
| 40009 | 8 | `OUTPUT_POLARITY` | enum | 0=NO, 1=NC | Inherit from baseline |
| 40010 | 9 | `SAMPLE_RATE_CODE` | enum | ADXL345 `BW_RATE` code | 100 Hz |
| 40011 | 10 | `MODBUS_SLAVE_ID` | uint8 | 1–247 | 1 |
| 40012 | 11 | `BAUD_CODE` | enum | 0=9600 … 4=115200 | 1 (19200) |
| 40013 | 12 | `PARITY_CODE` | enum | 0=none/2stop, 1=even, 2=odd | 1 |
| 40014 | 13 | `EVENT_SELECT` | uint16 | 0 = most recent … 31 | 0 |
| 40015 | 14 | `COMMAND` | enum | 1=clear latch, 2=self-test, 3=save config, 4=factory reset, 5=recalibrate offsets, 6=clear event log | 0 |
| 40016–40017 | 15–16 | `HOST_EPOCH` | uint32 | Unix seconds written by master | 0 |

**Integration notes to publish with the map:**
- Changes to `MODBUS_SLAVE_ID`, `BAUD_CODE` or `PARITY_CODE` take effect only after `SAVE_CONFIG` **and** a power cycle, to avoid orphaning the device mid-session.
- `COMMAND` self-clears to 0 once the action completes; the master should poll it to confirm.
- Reads spanning 30010–30011 or 40016–40017 are served from a shadow copy so no torn value can be returned.
- Maximum registers per request is limited (see §C.3 buffer sizing) — document the actual limit once WP-8 is sized.

## 11. Requirements Catalogue (Extract)

| ID | Requirement | Type | Priority | Traces to | Verification |
|---|---|---|---|---|---|
| FR-01 | Detect sustained near-zero-g on all axes below a configurable threshold for a configurable duration | Functional | Must | A.2, C7.5 | Drop rig |
| FR-02 | Assert a discrete output on fall detection, electrically compatible with the impact sensor | Functional | Must | PR-02, C-04 | Bench + customer DI card |
| FR-03 | Latch the output until explicitly cleared | Functional | Must | BR-02 | Bench |
| FR-04 | Provide local reset via button and remote reset via Modbus | Functional | Must | B.3 | Bench |
| FR-05 | Confirm falls using impact and rest stages per configured mode | Functional | Must | C7.3 | Drop rig + handling profile |
| FR-06 | Measure free-fall duration and estimate drop height | Functional | Must | C7.6 | Instrumented drop rig |
| FR-07 | Quantify impact peak magnitude | Functional | Should | C7.6 | Drop rig |
| FR-08 | Store the last 32 events in non-volatile memory | Functional | Should | C.2 | Power-cycle test |
| FR-09 | Serve Modbus RTU FC01/02/03/04/05/06/16 as a slave | Functional | Must | A.2 | Conformance test |
| FR-10 | Expose live acceleration, magnitude and tilt over Modbus | Functional | Should | 6.3 | Master read test |
| FR-11 | Persist configuration across power cycles with CRC protection | Functional | Must | PR-05 | Power-cycle + corruption injection |
| FR-12 | Run periodic self-test and drive a health output | Functional | Must | PR-07 | Fault injection |
| FR-13 | Recover automatically from firmware hang via watchdog | Functional | Must | PR-07 | Fault injection |
| NFR-01 | Detection latency ≤ `FF_TIME` + 50 ms in Immediate mode | Performance | Must | SC-01 | Scope trace |
| NFR-02 | Missed detection < 1 % for drops ≥ configured minimum | Performance | Must | SC-02 | Drop matrix, ≥ 200 drops |
| NFR-03 | False positives < 1 per 30 days under handling profile | Performance | Must | SC-03 | Extended field trial |
| NFR-04 | Height estimate within ± 20 % or ± 5 cm for 0.3–2.0 m clean drops | Accuracy | Should | SC-04 | Instrumented rig |
| NFR-05 | Modbus response < 50 ms at 19200 baud | Performance | Must | SC-05 | Master timing capture |
| NFR-06 | SRAM headroom ≥ 15 % at worst case | Resource | Must | C-01 | Stack high-water instrumentation |
| NFR-07 | Flash headroom ≥ 10 % | Resource | Must | C-01 | Map file |
| NFR-08 | Modbus activity must not perturb detection timing | Reliability | Must | PR-03 | Scope trace under bus load |
| NFR-09 | Zero BOM change vs impact sensor | Cost | Must | SC-07, PR-01 | BOM diff |
| CON-01 | No SIL/PL claim may be made in any documentation or marketing | Constraint | Must | C-05, R-01 | Document review |

## 12. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| **R-01** | **Product is deployed in a personnel-safety role (fall arrest, man-down) for which it is not qualified.** This architecture uses commercial-grade components with no redundancy, no diverse channel and no development under IEC 61508 / ISO 13849 | Medium | **Severe** | State the limitation explicitly in the datasheet, manual and quotation. Do not market as a safety device. If a customer requires this, it is a new product programme, not a configuration |
| R-02 | Free-fall threshold tuning proves site-specific; one default does not serve all customers | High | Medium | PR-05 runtime configurability; publish the time-vs-height table; provide a commissioning procedure with a guided trial-drop routine |
| R-03 | SRAM exhaustion once Modbus is integrated | **High** | High | ADR-003; WP-11 as a hard gate; measure rather than compute; cap Modbus buffer sizes and max registers per request |
| R-04 | A general-purpose Modbus library consumes disproportionate flash/SRAM | High | High | Evaluate footprint before committing; prefer a minimal purpose-built RTU implementation supporting only the required function codes |
| R-05 | ADR-004 unresolved — serial port conflict blocks WP-8 | High | High | Resolve at F0 gate with the hardware lead; do not start WP-8 until closed |
| R-06 | Existing field boards lack a populated RS-485 transceiver, breaking the field-upgrade story | Medium | Medium | Confirm at F0; if unpopulated, scope the upgrade path as discrete-output-only and communicate clearly |
| R-07 | Tumbling or tethered falls do not produce a clean sub-threshold window and are missed | Medium | High | Software confirmation tolerant of brief spikes (§C7.5); include tumbling and partially-restrained drops in the qualification matrix |
| R-08 | Height estimate is systematically low and customers treat it as measurement rather than estimate | High | Medium | Report with a validity flag; document the bias and its cause; never present it without the caveat |
| R-09 | RS-485 EMI in the plant corrupts Modbus and, if the stack is poorly isolated, disturbs detection | Medium | High | PR-03 architectural separation; proper termination and biasing guidance; EMC regression at F4 |
| R-10 | Interrupt latency from Modbus servicing causes missed samples | Medium | Medium | Minimal ISRs; `MISSED_SAMPLES` diagnostic register exposes the problem in the field rather than hiding it |
| R-11 | Baseline impact code proves less reusable than assumed (A4) | Medium | High | WP-1 audit is the very first activity; revise SC-06 and schedule if the audit contradicts the assumption |
| R-12 | Configuration drift across a fleet leads to inconsistent behaviour and disputed events | Medium | Medium | Expose all config over Modbus for read-back; recommend the customer records a config snapshot at commissioning |

## 13. Test and Qualification Strategy (Outline)

| Test class | Method | Pass criterion |
|---|---|---|
| Free-fall functional | Controlled drop rig, heights 0.1 / 0.2 / 0.3 / 0.5 / 1.0 / 1.5 / 2.0 m onto varied surfaces, ≥ 30 drops each | NFR-02 |
| Orientation independence | Repeat matrix in ≥ 6 device orientations | No orientation-dependent miss rate |
| Tumbling / restrained falls | Drops with induced rotation; drops on a slack tether | Detection or a correctly-flagged unconfirmed record — never a silent miss |
| False-positive profile | Normal handling, transport vibration, hammer strikes to the mounting structure, rapid set-down, lift start/stop | NFR-03 |
| Latency | Scope: INT1 edge → DO1 edge under idle and full Modbus load | NFR-01, NFR-08 |
| Height accuracy | Drop rig with independent height measurement | NFR-04 |
| Modbus conformance | Two independent masters; malformed frames; wrong slave ID; broadcast; bus collision | No lockup, no incorrect response, correct exception codes |
| Resource | Stack high-water marking, map file analysis | NFR-06, NFR-07 |
| Fault injection | Disconnect ADXL345 mid-run; corrupt EEPROM; brown-out; force watchdog | Correct fault state, health output de-asserted, no false latch |
| Soak | 168 h continuous with periodic drops and full bus load | Zero unexplained resets, no counter corruption |
| Environmental / EMC | Regression against the impact sensor's existing qualification | No regression |

---

## 14. Immediate Next Actions

1. **Provide the impact sensor repository and schematic** — WP-1 is on the critical path and every assumption A1–A6 depends on it.
2. **Resolve ADR-004** (serial port arbitration) with the hardware lead. This blocks WP-8.
3. **Confirm R-01**: state definitively whether this product will ever be used to protect people. The answer changes the programme, not just the documentation.
4. **Confirm the RS-485 transceiver is populated** on the existing board (R-06, PR-01).
5. **Confirm the intended application and minimum drop height of interest** — this sets the default `FF_TIME_MS` and hence the entire sensitivity story.
6. Review and freeze the Modbus register map (§6.3) with a customer-facing engineer before any code is written against it.

---

*End of document. Sections marked [ASSUMED — CONFIRM] and ADR-004 must be closed before this architecture is baselined.*
