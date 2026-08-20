/* =====================================================================
   CTX 311  --  Loss-of-Support Monitor for jacked / climbing systems
   Revision A  -  derived from CTX310 rev H, register map version 9

   WHAT THIS DEVICE DOES, AND WHAT IT MUST NOT BE ASKED TO DO
   ----------------------------------------------------------
   The CTX311 drives an ARREST / BRAKE device. That makes it a
   PROTECTIVE FUNCTION, not a monitor with a useful side effect.

   It is built from commercial-grade parts on the CTX310 hardware. It
   has NO redundancy, NO diverse second channel, and was NOT developed
   under IEC 61508 or ISO 13849. It therefore carries NO SIL or PL
   rating and none may be claimed for it, in documentation, in
   marketing, or in a lift plan.

   It is intended as an ADDITIONAL, FASTER channel on top of the
   mechanical fail-safes a jacking or climbing system already has --
   anti-return wedges, pawls, rope grabs. If it is the only thing
   standing between a slip and a fall, the system is under-engineered
   and this firmware does not fix that.

   WHY LOSS-OF-SUPPORT AND NOT FREE FALL
   -------------------------------------
   A jacked or climbing assembly is restrained. The realistic failure
   modes -- a strand slipping, a climbing shoe releasing under load, a
   hydraulic hose bursting -- unload the assembly PARTIALLY. They do
   not produce the sustained near-zero-g of a clean free fall, and a
   strict free-fall detector would sail past most of them.

   So the trip test is a LOSS OF SUPPORT: total acceleration magnitude
   falling below a threshold near 1 g (default 850 mg) for a short
   time (default 25 ms). That fires in roughly a sixth of the time a
   free-fall detector needs, and it catches partial failures.

   Timing is not a tuning preference here. It sizes the arrestor:

       detection time   drop before output   velocity at arrest
            25 ms            0.3 cm               0.25 m/s
            50 ms            1.2 cm               0.49 m/s
           150 ms           11.0 cm               1.47 m/s
           300 ms           44.1 cm               2.94 m/s

   Arrest energy goes with the SQUARE of velocity. Doubling detection
   time quadruples what the arrestor must absorb. Add the arrest
   device's own engagement time on top of every figure above.

   THE FAIL-SAFE DIRECTION IS NOT ESTABLISHED BY THIS FIRMWARE
   -----------------------------------------------------------
   All three outputs are driven ACTIVE LOW: the pin is driven HIGH in
   the safe/healthy state and pulled LOW to trip. On loss of MCU power
   the pins go HIGH IMPEDANCE, not low. Whether that engages the
   arrest device depends on TWO things outside this firmware:

     1. an external pull-down on the output, so a high-Z pin reads low
     2. the arrest device engaging on DE-energisation

   CONFIRM BOTH ON HARDWARE BEFORE THIS IS RELIED ON. If the arrest is
   triggered by energisation instead, the polarity here is backwards
   and OUTPUT_ACTIVE_LOW must be changed -- but note that an
   energise-to-arrest design cannot be made fail-safe by firmware at
   all, and is the wrong architecture for this job.

   DIAGNOSTIC COVERAGE IS THE POINT, NOT THE ALGORITHM
   ---------------------------------------------------
   The dangerous failure of a device like this is not a missed fall.
   It is a dead sensor that looks like a stationary assembly. CTX310
   rev H had no protection against it whatsoever: if the ADXL345 died
   or the SPI bus stuck returning a constant value, the firmware saw a
   perfectly still machine and would never have tripped again.

   Rev A adds, in order of importance:

     1. SAMPLE RATE SUPERVISION. If DATA_READY stops, isrCount falls
        to zero and FAULT_RATE is raised within one second. This is
        the check that catches a dead sensor, a dead SPI bus and a
        detached interrupt line. It is the most important line of
        code in this file.
     2. STUCK-SAMPLE DETECTION. A real accelerometer always dithers by
        at least one LSB. Bit-identical consecutive samples for one
        second means the data path is frozen even though interrupts
        are still arriving.
     3. PLAUSIBILITY. At rest the magnitude must sit in a band around
        1 g. Sustained departure that is not a loss-of-support event
        means the part is mis-scaled or damaged.
     4. WATCHDOG. Rev H included <avr/wdt.h> and then called
        wdt_disable(). A hang left the interlock wherever it was.
     5. BOOT CHECK. Before arming, confirm the part is delivering
        plausible, changing data.

   On any fault the HEALTH output opens. By default a fault ALSO trips
   the loss-of-support output (LosFaultActionReg = 1): if the
   detection channel is dead, the protective function is gone, and
   halting is the safe direction. Set the register to 0 only with a
   documented reason.

   HEALTH is ALSO open for the first SETTLE_SAMPLES (~2 s) after reset,
   while the trip test is suppressed and the device cannot detect
   anything. An unarmed protective device is not a healthy one. This is
   not a fault: it sets nothing in reg 61, never engages the arrest,
   and clears itself. Reg 49 bit 6 is the pollable form of it.
   BEWARE: a PLC that treats health-open as a hard stop will refuse to
   start for ~2 s after a sensor reset. See the arming-window section
   of docs/REGISTER_MAP_CTX311.md.

   NOT IMPLEMENTED, DELIBERATELY: the ADXL345 electrostatic SELF_TEST.
   It deflects the axes, which perturbs the very signal that operates
   the arrest device, so it must never run online. Running it at boot
   needs setSelfTestBit() in the SparkFun driver; that API was not
   confirmed against the vendored copy, so the boot check here uses
   only readAccel(). Add the real self-test once the driver is checked
   -- see bootCheck().

   CHANGES FROM CTX310 REV H
   -------------------------
   1. NEW loss-of-support detector, in the ISR, on RAW magnitude taken
      BEFORE gravity removal. Everything downstream of the DC tracker
      in rev H is AC-coupled and is structurally blind to a sustained
      loss of support. See the ISR.
   2. Impact trip KEPT on PC0, unchanged. Rev H's "one path to the
      output" principle is preserved by giving the new test its OWN
      output (PC1) rather than sharing PC0: each pin still has exactly
      one meaning. Impact remains the better detector for shock-loaded
      partial failures and was not worth removing.
   3. NEW health output on PC2.
   4. THRESHOLD_MIN_MG raised 10 -> 1200. Entering free fall produces
      a ~1000 mg step in the AC path, because the DC tracker still
      holds gravity while the raw signal collapses. Any impact
      threshold below that would have tripped PC0 at the START of a
      fall, before anything was struck. Unreachable in an impact-only
      product; reachable here.
   5. Status reg 25 bit 2 REMOVED (reads 0). HARDWARE_VALIDATION.md
      records it re-latching within seconds of any clear, making it a
      permanent fault light for normal polling. Reg 26 remains the
      diagnostic counter. This is the map revision it was waiting for.
   6. Registers 49-63 added, map version 9. Library BUFFER_SIZE raised
      128 -> 160, so the 64-register map is one function-3 request and
      there is room left for a v10.
   7. Watchdog enabled.

   Everything else -- the DC tracker, block DSP, 1 s RMS, isqrt32, the
   settings/trigger semantics, the command acknowledge path, the
   read-to-clear on reg 37 -- is rev H code, unchanged.

   CTX310 REV H HISTORY FOLLOWS, RETAINED IN FULL
   ==============================================

   CHANGES FROM REV G
   ------------------
   1. SUSTAINED-RMS TRIPPING REMOVED. Rev G could open the interlock two
      ways: the per-sample peak test (reg 22) and a sustained vector-RMS
      test (reg 35). A trip therefore meant either "something hit it" or
      "it has been shaking a while", and nothing in the map said which
      -- reg 25 bit0 is the same bit either way. For an impact monitor
      driving an interlock that ambiguity is a liability, so there is now
      exactly ONE path to the output: reg 22, per sample, 0.63 ms.
      Removed with it: the reg 35 threshold and its reg 48 echo (both
      now RESERVED, always reading 0), the EEPROM field, the validation
      branch in checkThresholdUpdate(), and the ISR comparison.
      Sustained vibration is still MEASURED -- regs 38-41 (1 s RMS) and
      reg 30 (peak vector-RMS hold) are untouched. It just no longer
      operates the output. Trend it on the Pi, where the time constant
      and the hysteresis are visible and adjustable, instead of in an
      ISR where they are neither.
      Regs 35 and 48 are held as reserved holes rather than deleted so
      the rest of the map does not renumber and the existing dashboard
      keeps working. MAP VERSION 8: a master built for map 7 would write
      reg 35 and believe it had armed a trip that no longer exists.

   CHANGES FROM REV F
   ------------------
   A. COMMAND REGISTER IS ACKNOWLEDGED (regs 45-47). Reg 28 is a
      self-clearing TRIGGER, not a setting: checkCommandRegister()
      executes the code and immediately writes 0 back, so a master that
      writes reg 28 and then reads it back ALWAYS sees 0. That is not a
      lost write -- it is the only way the register can work. If reg 28
      held its value, the command would re-execute on every pass of
      loop() (a written CMD_CLEAR_PEAKHOLD would pin regs 30/36 to zero
      for ever). The problem with rev F was that the master had no way
      to prove the command landed. Rev F now publishes:
          reg 45  echo of the last command code received
          reg 46  0 = idle, 1 = accepted, 2 = unknown code
          reg 47  count of accepted commands, wraps at 65535
      Write reg 28, then read reg 47 and check it incremented.
      Contrast reg 22 (threshold), which IS a setting: it holds the
      written value and is echoed back through reg 24.
   B. FIXED: WRITES TO REGS 21 AND 35 COULD BE SILENTLY DISCARDED.
      publishBlock() runs from sensorData(), i.e. inside the function-3
      handler, and rev F rewrote holdingRegs[SlaveIDReg] and
      holdingRegs[RmsThresholdReg] from the device's current values on
      every read. A master write that landed between the write and the
      next pass of loop() was overwritten with the OLD value before
      checkThresholdUpdate()/checkModbusSlaveIdUpdate() ever saw it, so
      the setting silently reverted. publishBlock() no longer touches
      any writable register; the check functions own regs 21, 22, 35 and
      write them back only to REJECT an out-of-range value.
   C. NEW REG 48: effective sustained-RMS threshold, the read-only echo
      of reg 35. Reg 24 already did this for reg 22; reg 35 had no echo,
      so a rejected write was indistinguishable from an accepted one.
   D. ISR WORST CASE CUT FROM ~1200 us TO ~380 us. A field unit reported
      1203 us in reg 19 against a 625 us sample budget -- the ISR was
      still running when the next two samples arrived. Cause: isqrt32()
      cost ~4200 cycles (265 us), not the ~200 cycles its comment
      claimed, because gcc compiles `n >> 30` on AVR into a 30-iteration
      bit-shift loop. The block boundary calls it four times, so every
      16th sample spent ~1050 us on square roots alone. Taking the same
      two bits from the top byte drops the call to ~900 cycles (57 us)
      and the block boundary to ~230 us. Same algorithm, bit-identical
      output, verified over the full uint32 range. See isqrt32().
      Registers 26 and 20 are the ones to watch after this change:
      clear diagnostics (reg 28 = 3) and see how they climb.
   E. NO SQUARE ROOTS IN THE ISR AT ALL. Rev F's block boundary took
      three per-axis roots, squared them back up and rooted the sum --
      four isqrt32 calls. But sqrt(msX)^2 + sqrt(msY)^2 + sqrt(msZ)^2
      IS msX + msY + msZ, so the vector mean square needs no roots, and
      every ISR decision on it (peak hold, the sustained-RMS trip, the
      impact snapshot) is a comparison that works identically on
      squares -- the split reg 36 has always used with peakMag2Hold.
      The ISR now hands loop() mean squares and loop() takes the roots
      for presentation. Measured 517 us worst case on hardware after
      change D; this removes a further ~230 us.
      The decisions STAY in the ISR. Moving them to loop() would have
      been a real fault: loop() misses most blocks while a Modbus read
      is in flight (reg 26 read 3568 on a field unit), so peak hold
      would under-report and sustained trips could be missed outright.
      Two visible consequences, both improvements:
        - regs 23/30/41 read 1-2 counts (~4-8 mg) HIGHER, because the
          old form rounded each axis down before squaring it again.
        - the sustained-RMS trip is up to ~2 counts more sensitive,
          since it now compares squares like the fast path always has.
      Neither changes a unit or a register meaning, so the map version
      stays at 7.
   F. PEAK_CONFIRM 2 -> 1. A single sample over threshold now trips.
      Detection latency 1.26 ms -> 0.63 ms, and the shortest detectable
      strike halves with it -- that second sample was the binding limit
      on how sharp an impact this product could see. The cost is that
      one corrupted sample can trip the output; see PEAK_CONFIRM.
   G. KNOWN-LIMITS NOTE CORRECTED. The 29-register cap was a property of
      the unpatched library's 64-byte BUFFER_SIZE. This build ships the
      patched library at 128 bytes, so the whole 49-register map is one
      function-3 request (response = 5 + 2*49 = 103 bytes).

   CHANGES FROM REV E
   ------------------
   A. 1-SECOND RMS DIVIDES MOVED OUT OF THE ISR. Rev E ran three
      64-bit divisions (__udivdi3) in interrupt context once per
      second. At ~2500-3500 cycles each that is 470-660 us against a
      625 us sample budget -- the ISR could overrun a sample period
      once every second. The ISR now only copies the accumulators
      (24 bytes, ~3 us) and sets a flag; loop() does the divides and
      square roots.
   B. ISR TIMING NOW USES TIMER1, NOT micros(). micros() is unreliable
      inside an ISR because the Timer0 overflow handler cannot run
      while interrupts are disabled -- rev E reported 64,604 us for an
      ISR that plainly could not have taken 103 sample periods.
      Timer1 free-runs at clk/8 (0.5 us per tick) and needs no
      interrupt. Reg 19 is still in MICROSECONDS.
   C. SAMPLE RATE MEASURED AGAINST ACTUAL ELAPSED TIME. Rev E counted
      against an assumed 1000 ms window, so loop overshoot read high
      (observed 1650 for a true ~1600). Now divides by real elapsed ms.
   D. IDENTIFICATION REGISTERS 42-44: firmware version, register-map
      version and build date. The map version is the important one --
      it has changed shape repeatedly (units, redefinitions, read-to-
      clear) and a master using the wrong assumptions logs plausible
      but wrong numbers silently. Have the Pi check reg 43 on connect.
   E. Library: fixed a one-byte buffer overrun in the receive loop
      (frame[BUFFER_SIZE] was written before the overflow flag took
      effect). Present in the original library too.

   CHANGES FROM REV D
   ------------------
   1. Reg 23 is now TRUE TRIAXIAL RMS  sqrt(rx^2+ry^2+rz^2), not the
      L1 sum rx+ry+rz. The old form overstated by up to 1.73x with a
      ratio that depended on how energy split across axes, and it did
      not share geometry with the trip metric, so it could not be used
      to explain a trip. Reg 35 (slow RMS path, removed in rev H)
      used the same.
   2. NEW 1-SECOND RMS, regs 38-41. Per-axis + vector. 10 ms blocks
      keep ~18% inherent jitter (1/sqrt(2N), N=16); a 1 s window cuts
      that to ~1.8%, which is what trending actually needs.
   3. NO INPUT CLAMPING. The full +/-16 g range of the part reaches
      every register untouched. Only the 1 s accumulator ever needed
      bounding (100 x 8192^2 = 6.71e9 overflows uint32), so it is a
      uint64 instead. That accumulation runs once per BLOCK (100/s),
      not per sample, so the cost is ~0.04% CPU and +12 bytes RAM.
      block sqSum (1.07e9) and mag2 (201e6) already fit uint32.
   4. REGISTERS ARE UNSIGNED. holdingRegs is now `unsigned int`, which
      also matches SimpleModbusSlave's `unsigned int*` signature and
      removes the old signedness/volatile cast mismatch. Range is now
      0..65535 mg (65.5 g) at 1 mg resolution.
      EXCEPTION: regs 32-34 (gravity vector) carry a two's-complement
      pattern and MUST be read as signed by the master.
      NOTE: holdingRegs no longer needs `volatile` -- the ISR does not
      touch it; only publishBlock(), from loop context, does.
   5. REG 37 IS READ-TO-CLEAR and no longer self-clears on hold expiry.
      Requires the one-line SimpleModbusSlave patch (see below).
      Compare-and-clear is used so an impact landing between the
      response and the clear is not silently wiped.
   6. DC_SHIFT 10 -> 11. Datasheet says response from 0.5 Hz; at
      DC_SHIFT=10 the tracker corner sat at 0.249 Hz, leaving 0.5 Hz
      down ~0.96 dB. At 11 the corner is 0.124 Hz and 0.5 Hz is down
      0.26 dB, i.e. effectively flat at the published band edge.
   7. Threshold range 10..15000 mg to honour the +/-15 g datasheet
      figure (the part itself is set to +/-16 g for headroom).
   8. FIXED reg 20: loop time was a uint16 in microseconds, but a
      38-register read at 9600 baud takes ~104 ms. It wrapped and
      under-reported by ~2.7x exactly when starvation was worst.
      Now accumulated in uint32 and reported in 100 us UNITS.
   9. Reg 3 renamed TempValue -> OutputState. THIS PRODUCT HAS NO
      TEMPERATURE SENSOR; the index reports interlock state and the
      old name caused it to be logged as a temperature.
  10. sensorData() is DEFINED here and calls publishBlock().
      SimpleModbusSlave calls sensorData() from inside its function-3
      handler, so registers are refreshed at read time.

   REQUIRED LIBRARY PATCH (SimpleModbusSlave)
   ------------------------------------------
     .h   add:   void modbus_read_complete(unsigned int startAddress,
                                           unsigned int quantity);
     .cpp add after  sendPacket(responseFrameSize);  in the function-3
          branch:
                 modbus_read_complete(startingAddress, no_of_registers);
     sendPacket() blocks through flush(), so by the time this runs the
     frame really is on the wire.

   WRITABLE REGISTERS -- TWO KINDS, DIFFERENT SEMANTICS
   ----------------------------------------------------
   SETTINGS  regs 21 and 22. Hold the written value, persist to
       EEPROM, read back as written. An out-of-range write is rejected
       and the register reverts to the value still in force. Verify reg
       22 through its effective-value echo, reg 24; reg 21 reads back
       directly.
   TRIGGERS  reg 28. Executes once and self-clears to 0, so it ALWAYS
       reads back 0. Verify through regs 45-47, or by watching the side
       effect (reg 30/36 -> 0 for CLEAR_PEAKHOLD, reg 31 -> 0 for
       CLEAR_TRIPCOUNT).

   KNOWN LIMITS
   ------------
   - Minimum detectable impact duration = PEAK_CONFIRM sample periods.
     At PEAK_CONFIRM=1 and the measured 1589 Hz ODR that is 0.63 ms
     (it was 1.26 ms at PEAK_CONFIRM=2). A strike shorter than one
     sample period is still missed at ANY amplitude -- one sample is
     the floor and no confirm count can go below it.
   - 0.63 ms is SOFTWARE latency. The ADXL345 internal filter adds
     unspecified group delay. Measure strike-to-pin with a scope.
   - At PEAK_CONFIRM=1 there is no consecutive-sample confirmation, so
     a single corrupted SPI read can trip the output. See PEAK_CONFIRM.
   - A function-3 read is capped by the library's BUFFER_SIZE. The
     patched library shipped here uses 128, giving 61 registers per
     request, so all 49 registers come back in one sweep. Against the
     UNPATCHED 64-byte library the cap is 29 and a full sweep needs
     two requests.

   UNITS
     "g" registers      -> milli-g      (1000 = 1.000 g)   UNSIGNED
     "m_s" registers    -> centi-m/s2   (100  = 1.00 m/s2) UNSIGNED
     regs 32-34         -> milli-g                          SIGNED
     reg 19             -> microseconds
     reg 20             -> 100-microsecond units
   ===================================================================== */

#include <SPI.h>
#include <EEPROM.h>
#include <util/atomic.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include "SimpleModbusSlave.h"
#include "SparkFun_ADXL345-master/SparkFun_ADXL345.cpp"

/* ------------------------- configuration --------------------------- */
/* Outputs. All ACTIVE LOW: driven HIGH = safe/healthy, pulled LOW to
   trip. See the fail-safe note in the header -- a high-Z pin on power
   loss only reads low if the board has an external pull-down.        */
#define OUTPUT_PORT_BIT   PC0     /* impact interlock   (rev H, unchanged) */
#define LOS_PORT_BIT      PC1     /* loss of support -> ARREST DEVICE      */
#define HEALTH_PORT_BIT   PC2     /* health: HIGH = healthy                */

#define TxEnablePin       3
#define ADXL_INT1_PIN     2
#define ADXL_CS_PIN       4
#define baud              9600

#define DEFAULT_SLAVE_ID     71
#define DEFAULT_THRESHOLD_MG 3000
/* Raised from 10 mg. Entering free fall steps the AC path by ~1000 mg
   (the DC tracker still holds gravity while the raw signal collapses),
   so any threshold below that trips PC0 at the START of a fall.      */
#define THRESHOLD_MIN_MG     1200
#define THRESHOLD_MAX_MG     15000     /* +/-15 g datasheet figure */
#define TRIP_HOLD_MS         3000UL

/* ------------------- loss-of-support detector ----------------------
   Trips when the RAW vector magnitude -- gravity included, taken
   before the DC tracker -- stays below LOS threshold for LOS time.

   Threshold is a fraction of 1 g, not a near-zero figure. 850 mg
   means "this assembly has lost about 15% of its support". A strict
   free-fall threshold (300-600 mg) would miss a partial slip, which
   on a jacked system is the likely failure, not the unlikely one.

   Time is DELIBERATELY SHORT. See the arrest-energy table in the
   header: this number sizes the arrestor.                           */
#define DEFAULT_LOS_THRESHOLD_MG 850
#define LOS_THRESHOLD_MIN_MG     200
#define LOS_THRESHOLD_MAX_MG     980   /* must stay below 1 g */
#define DEFAULT_LOS_TIME_MS      25
#define LOS_TIME_MIN_MS          5
#define LOS_TIME_MAX_MS          500
#define DEFAULT_LOS_HOLD_MS      0     /* 0 = LATCH until CLEAR_LOS.
                                          An arrest device must not
                                          silently re-arm on a timer. */
#define LOS_HOLD_MAX_MS          600000UL

/* Nominal ODR used to convert LOS time -> samples. FIXED, not taken
   from the measured rate in reg 27: trip timing on a protective
   function must be deterministic, not drift with a live measurement.
   1589 Hz is the figure measured on hardware in rev H
   (HARDWARE_VALIDATION.md); nominal is 1600. The 0.7% error is
   immaterial next to threshold tuning uncertainty.                   */
#define LOS_ODR_HZ 1589U

/* Consecutive-sample confirmation works the OPPOSITE way here to
   PEAK_CONFIRM on the impact path. Impact risks missing an event
   BRIEFER than one sample. Loss of support risks BREAKING a long run:
   a jerk, a guide contact or a partial re-catch pushes one sample
   back over threshold and resets a counter that was 30 samples into a
   real event. So the counter leaks rather than resetting: up to
   LOS_SPIKE_TOLERANCE samples may exceed the threshold across one
   event without discarding the run.
   Set to 0 to get strict consecutive-sample behaviour.              */
#define LOS_SPIKE_TOLERANCE 8

/* ---------------------------- diagnostics --------------------------
   STUCK: a real ADXL345 dithers by >=1 LSB. Bit-identical samples for
   this long means the data path is frozen while interrupts still
   arrive -- the failure that would otherwise look like a perfectly
   stationary assembly for ever.
   RATE: if DATA_READY stops, isrCount goes to zero. This is the check
   that catches a dead part, a dead bus, or a detached INT1.
   PLAUSIBLE: at rest the raw magnitude must sit near 1 g. Checked
   only while NOT in a loss-of-support event, for obvious reasons.  */
#define STUCK_SAMPLE_LIMIT   1589U     /* ~1 s of identical samples */
#define RATE_MIN_HZ          1200U
#define RATE_MAX_HZ          2000U
#define PLAUSIBLE_MIN_MG     500U
#define PLAUSIBLE_MAX_MG     1600U
#define PLAUSIBLE_FAIL_MS    2000UL

/* Fault bits, published in reg 61 */
#define FAULT_RATE        0x0001   /* sample rate outside band       */
#define FAULT_STUCK       0x0002   /* data path frozen               */
#define FAULT_IMPLAUSIBLE 0x0004   /* magnitude not near 1 g at rest */
#define FAULT_CONFIG      0x0008   /* EEPROM defaulted               */
#define FAULT_BOOTCHECK   0x0010   /* boot check failed              */
#define FAULT_WDT_RESET   0x0020   /* sticky: watchdog fired         */

/* NOT every fault costs us the protective function, and treating them
   alike is wrong in both directions.

   DETECTION_LOST means the device can no longer see an event: the
   sample stream has stopped, frozen, or gone implausible. The
   protective function is GONE, so the arrest engages (subject to
   LosFaultActionReg). These are the ones worth tripping on.

   The rest are ADVISORY. A defaulted EEPROM means the thresholds are
   at defaults, not that detection has failed -- and since
   applyDefaults() writes the EEPROM, a factory-fresh unit would
   otherwise trip its arrest device on first power-up for no better
   reason than never having been configured. A past watchdog reset is
   likewise serious enough that an operator must see it, but the
   device is demonstrably running now. Both open the HEALTH output and
   neither touches the arrest.                                      */
#define FAULT_DETECTION_LOST (FAULT_RATE | FAULT_STUCK | \
                              FAULT_IMPLAUSIBLE | FAULT_BOOTCHECK)

/* Boot check state, published in reg 62 */
#define BOOT_PENDING  0
#define BOOT_PASS     1
#define BOOT_FAIL     2

#ifndef ADXL345_INT_DATA_READY_BIT   /* the official driver defines it; the host stub does not */
#define ADXL345_INT_DATA_READY_BIT 7
#endif

/* DSP parameters, sized for 1600 Hz */
#define BLOCK_SIZE      16             /* 10 ms per block */
#define BLOCK_SHIFT     4
#define BLOCKS_PER_SEC  100            /* 100 x 16 = 1600 samples = 1.000 s */

/* Dynamic gravity removal. Orientation changes take seconds, impacts
   milliseconds; a time constant between the two separates them.
   DC_SHIFT=11 -> tau 1.28 s, corner 0.124 Hz (flat at the 0.5 Hz
   datasheet band edge). DC_MAX_INC clamps the per-sample correction so
   a one-sided transient cannot drag the estimate; it always tracks in
   the correct direction, so there is no stuck-forever mode.           */
#define DC_SHIFT     11
#define DC_MAX_INC   256

/* Fast impact path: instantaneous vector magnitude, every sample.
   Number of CONSECUTIVE samples over threshold needed to trip.
   At the measured 1589 Hz ODR one sample period is 629 us:
     CONFIRM=1 -> 0.63 ms, 2 -> 1.26 ms, 3 -> 1.89 ms

   CONFIRM=1: a single sample over threshold trips. This halves the
   detection latency AND halves the shortest detectable strike, which is
   the point -- at CONFIRM=2 anything briefer than 1.26 ms was missed at
   ANY amplitude, and a hard metal-on-metal strike is easily shorter
   than that.

   The trade: a single corrupted sample can now trip the output. Sensor
   noise cannot do it at a sensible threshold -- the ADXL345 at 800 Hz
   bandwidth is ~10-15 mg RMS per axis, so the 3000 mg default sits
   ~200 sigma away -- but a garbled SPI read is not Gaussian and only
   has to happen once. Watch reg 31 (trip count) against reg 37 (impact
   magnitude): a trip whose recorded magnitude is implausible for the
   machine is the signature. Set this back to 2 if that shows up.    */
#define PEAK_CONFIRM 1

#define SETTLE_SAMPLES 3200U           /* 2 s trip suppression at boot */

/* Stillness gate. 30 mg is comfortably above the resting noise of the
   1 s RMS (single mg on a real unit -- see HARDWARE_VALIDATION.md) and
   comfortably below any real movement. */
#define TILT_REST_MG      30U

/* How long it must stay still before the angle is trusted. The DC
   tracker has tau = 1.28 s, so after motion the gravity vector needs
   several tau to settle. 4 s is ~3.1 tau (~95%). Shorter would publish
   an angle the tracker has not finished converging to -- which reads as
   the structure slowly drifting when it is only the filter catching up. */
#define TILT_REST_MS      4000UL

/* Recompute cadence once at rest. The tracker cannot move faster than
   its own time constant, so 1 Hz is already generous. */
#define TILT_UPDATE_MS    1000UL

#define TILT_INVALID      0xFFFFU

/* Reg 65 bits */
#define TILT_ST_REF_SET   0x01
#define TILT_ST_AT_REST   0x02
#define TILT_ST_VALID     0x04

/* ---- supply monitoring (registers 69, 70) --------------------------
   The ATmega328P can measure its own Vcc with no external parts: read
   the internal 1.1 V bandgap with the ADC referenced to AVcc, and
   Vcc = 1.1 * 1024 / reading. The ADC is otherwise unused here -- the
   analog pins are driven as digital outputs and nothing samples them --
   so this costs no hardware and steals nothing.

   1.1 * 1024 * 1000, so the division yields millivolts directly. */
#define VCC_SCALE_MV      1126400UL

/* The bandgap needs time to settle after the ADC is first enabled, and
   the datasheet says to discard the first conversion after a reference
   change. Nothing here ever changes the reference again, so discarding
   a few at boot is enough. */
#define VCC_DISCARD       4
#define VCC_UPDATE_MS     1000UL
#define VCC_INVALID       0xFFFFU


/* ------------------------- IDENTIFICATION --------------------------
   FW_VERSION      what code is running.  Packed major<<8 | minor.
   REGISTER_MAP_VERSION  what the MASTER needs to interpret the data.
       Bump this ONLY when the map changes in a way that would make an
       older master misread the device -- a register redefined, a unit
       changed, a read-to-clear added. A pure bugfix does not bump it.
       The Pi should read reg 43 and refuse to ingest an unexpected
       value rather than silently logging misinterpreted numbers.
   Map history:
       4 = rev D  (mg units, peak trip on reg 22, reg 23 = L1 sum)
       5 = rev E  (unsigned mg, reg 23 = true vector RMS, reg 37
                   read-to-clear, regs 38-41 added, reg 3 renamed)
       6 = rev F  (reg 19 now Timer1-based; +regs 42-44 identification)
       7 = rev G  (+regs 45-47 command acknowledge, +reg 48 effective
                   RMS threshold; regs 21/35 no longer rewritten at
                   read time)
       8 = rev H  (sustained-RMS trip REMOVED; regs 35 and 48 are
                   reserved and always read 0. Writing reg 35 no longer
                   does anything, so a master built for map 7 would
                   believe it had armed a trip that does not exist.)
   BUILD_DATE      derived from __DATE__ at compile time, packed as
       (year-2000)<<9 | month<<5 | day.  Decode on the master:
       year = 2000 + (v >> 9);  month = (v >> 5) & 0x0F;  day = v & 0x1F
   ------------------------------------------------------------------ */
#define FW_VERSION_MAJOR      2
#define FW_VERSION_MINOR      0          /* CTX311 revision A */
#define FW_VERSION_PACKED     (((FW_VERSION_MAJOR) << 8) | (FW_VERSION_MINOR))
#define REGISTER_MAP_VERSION  11

#define BUILD_YEAR  ((__DATE__[7]-'0')*1000 + (__DATE__[8]-'0')*100 + \
                     (__DATE__[9]-'0')*10   + (__DATE__[10]-'0'))
#define BUILD_MONTH (__DATE__[2]=='n' ? (__DATE__[1]=='a' ? 1 : 6) : \
                     __DATE__[2]=='b' ? 2  : \
                     __DATE__[2]=='r' ? (__DATE__[0]=='M' ? 3 : 4) : \
                     __DATE__[2]=='y' ? 5  : \
                     __DATE__[2]=='l' ? 7  : \
                     __DATE__[2]=='g' ? 8  : \
                     __DATE__[2]=='p' ? 9  : \
                     __DATE__[2]=='t' ? 10 : \
                     __DATE__[2]=='v' ? 11 : 12)
#define BUILD_DAY   ((__DATE__[4]==' ' ? 0 : __DATE__[4]-'0')*10 + (__DATE__[5]-'0'))
#define BUILD_DATE_PACKED  ((((BUILD_YEAR)-2000) << 9) | ((BUILD_MONTH) << 5) | (BUILD_DAY))

/* Command register codes */
#define CMD_FACTORY_RESET   0x5A5A
#define CMD_CLEAR_PEAKHOLD  0x0001
#define CMD_CLEAR_TRIPCOUNT 0x0002
#define CMD_CLEAR_DIAG      0x0003
#define CMD_CLEAR_LOS       0x0004     /* clear the LOS latch, re-arm */
#define CMD_CLEAR_LOSCOUNT  0x0005     /* zero reg 54 only */
#define CMD_CLEAR_FAULTS    0x0006     /* clear latched fault bits */
#define CMD_SET_TILT_REF    0x0007     /* capture tilt reference (at rest) */
#define CMD_CLEAR_TILT_REF  0x0008     /* forget the tilt reference       */

/* Command status codes, published in reg 46 */
#define CMD_STATUS_IDLE     0          /* no command since boot */
#define CMD_STATUS_ACCEPTED 1          /* last code was recognised and run */
#define CMD_STATUS_UNKNOWN  2          /* last code was not a CMD_* value */

/* --------------------------- registers ----------------------------- */
enum
{
  XaxisRMS,            /* 0  mg, 10 ms window, AC-coupled */
  YaxisRMS,            /* 1  */
  ZaxisRMS,            /* 2  */
  OutputState,         /* 3  interlock: 1 = closed, 0 = open (tripped)
                              NOTE: this index was previously published as
                              a temperature value. There is NO temperature
                              sensor in this product -- see datasheet note. */
  XaxismodPosP,        /* 4  mg, positive peak in block */
  YaxismodPosP,        /* 5  */
  ZaxismodPosP,        /* 6  */
  XaxisRMS_m_s,        /* 7  centi-m/s2 */
  YaxisRMS_m_s,        /* 8  */
  ZaxisRMS_m_s,        /* 9  */
  XaxismodPosP_m_s,    /* 10 */
  YaxismodPosP_m_s,    /* 11 */
  ZaxismodPosP_m_s,    /* 12 */
  XaxismodNegP,        /* 13 mg, negative peak magnitude */
  YaxismodNegP,        /* 14 */
  ZaxismodNegP,        /* 15 */
  XaxismodNegP_m_s,    /* 16 centi-m/s2 */
  YaxismodNegP_m_s,    /* 17 */
  ZaxismodNegP_m_s,    /* 18 */
  looptime,            /* 19 max ISR time, MICROSECONDS */
  Modbuslooptime,      /* 20 max loop time, 100-US UNITS */
  SlaveIDReg,          /* 21 R/W SETTING 1..247 */
  ThresholdReg,        /* 22 R/W SETTING peak trip, mg, 10..15000 */
  SumReg,              /* 23 TRIAXIAL VECTOR RMS, mg, 10 ms window */
  ThresholdEffReg,     /* 24 peak threshold in force, mg (echo of 22) */
  StatusReg,           /* 25 bit0 tripped, bit1 latched,
                              bit2 REMOVED in map 9 (always 0 -- reg 26
                              is the counter), bit3 cfg defaulted    */
  BlockMissedReg,      /* 26 blocks loop() failed to collect */
  SampleRateReg,       /* 27 measured samples/s (expect ~1600) */
  CommandReg,          /* 28 W TRIGGER: CMD_* codes. SELF-CLEARING --
                              always reads back 0. See regs 45-47. */
  ResetCauseReg,       /* 29 raw MCUSR at boot */
  PeakSumHoldReg,      /* 30 max vector RMS since clear, mg */
  TripCountReg,        /* 31 monotonic trip counter */
  DcXReg,              /* 32 gravity X, mg  *** SIGNED *** */
  DcYReg,              /* 33 gravity Y, mg  *** SIGNED *** */
  DcZReg,              /* 34 gravity Z, mg  *** SIGNED *** */
  Rsvd35Reg,           /* 35 RESERVED, always reads 0. Was the
                              sustained-RMS trip threshold; the second
                              trip path was removed in rev H because two
                              ways to open the interlock made a trip
                              ambiguous. Kept as a hole so the rest of
                              the map does not renumber. */
  PeakMagHoldReg,      /* 36 max instantaneous |a| since clear, mg */
  ImpactMagReg,        /* 37 |a| of last impact, mg -- READ-TO-CLEAR */
  Rms1sXReg,           /* 38 1 s RMS X, mg */
  Rms1sYReg,           /* 39 1 s RMS Y, mg */
  Rms1sZReg,           /* 40 1 s RMS Z, mg */
  Rms1sVecReg,         /* 41 1 s triaxial vector RMS, mg */
  FwVersionReg,        /* 42 firmware version, major<<8 | minor */
  MapVersionReg,       /* 43 register-map version -- CHECK THIS FIRST */
  BuildDateReg,        /* 44 build date, (yr-2000)<<9 | mth<<5 | day */
  LastCommandReg,      /* 45 echo of the last code written to reg 28 */
  CommandStatusReg,    /* 46 CMD_STATUS_* for that code */
  CommandCountReg,     /* 47 accepted commands since boot, wraps */
  Rsvd48Reg,           /* 48 RESERVED, always reads 0. Was the echo
                              of reg 35. */

  /* ---------------- CTX311 additions, map version 9 ----------------
     Appended from 49. Regs 35 and 48 stay reserved and are NOT reused:
     a map-8 master could still write them.                          */
  LosStatusReg,        /* 49 bit0 LOS latched, bit1 LOS active now,
                              bit2 impact followed, bit3 reached
                              free-fall depth, bit4 impact clipped,
                              bit5 tripped by fault not by event    */
  LosThresholdReg,     /* 50 R/W SETTING raw magnitude, mg, 200..980 */
  LosThresholdEffReg,  /* 51 echo of 50 */
  LosTimeMsReg,        /* 52 R/W SETTING confirm time, ms, 5..500 */
  LosTimeMsEffReg,     /* 53 echo of 52 */
  LosCountReg,         /* 54 monotonic LOS trip count */
  LastLosDurationReg,  /* 55 duration below threshold, ms */
  LastLosMinMagReg,    /* 56 minimum raw magnitude during event, mg
                              -- THE discriminator between a real drop
                              and a constrained descent */
  LastLosHeightReg,    /* 57 estimated drop, cm; 0xFFFF = not valid */
  LastLosImpactReg,    /* 58 impact peak following the event, mg */
  LosHoldMsReg,        /* 59 R/W SETTING output hold, ms; 0 = LATCH */
  RawMagReg,           /* 60 live RAW magnitude, mg. DC-INCLUSIVE:
                              ~1000 mg at rest. NOT comparable with
                              reg 23, which is AC-coupled. */
  FaultReg,            /* 61 fault bitfield, FAULT_* */
  BootCheckReg,        /* 62 BOOT_PENDING/PASS/FAIL */
  LosFaultActionReg,   /* 63 R/W SETTING 0 = health output only,
                              1 = a fault also trips LOS. DEFAULT 1. */
  /* ---- tilt, map version 10. MONITORING ONLY, operates nothing ---- */
  TiltAngleReg,        /* 64 tenths of a degree from the reference,
                              0xFFFF = not valid                      */
  TiltStatusReg,       /* 65 bit0 ref set, bit1 at rest, bit2 valid   */
  TiltRefXReg,         /* 66 reference gravity X, mg *** SIGNED ***   */
  TiltRefYReg,         /* 67 reference gravity Y, mg *** SIGNED ***   */
  TiltRefZReg,         /* 68 reference gravity Z, mg *** SIGNED ***   */
  /* ---- supply, map version 11. Logging and troubleshooting only --- */
  VccReg,              /* 69 controller supply now, mV, 0xFFFF = not
                              measured yet                            */
  VccMinReg,           /* 70 lowest supply seen since boot or
                              CLEAR_DIAG, mV. The sag catcher.        */
  HOLDING_REGS_SIZE
};

/* Not volatile: the ISR never touches this array. Unsigned to match
   SimpleModbusSlave's `unsigned int*` parameter exactly.              */
unsigned int holdingRegs[HOLDING_REGS_SIZE];

ADXL345 adxl = ADXL345(ADXL_CS_PIN);

/* ------------------- ISR-private DSP state ------------------------- */
static int32_t  dcAccX = 0, dcAccY = 0, dcAccZ = 0;
static bool     dcSeeded = false;
static uint32_t sqSumX = 0, sqSumY = 0, sqSumZ = 0;
static int16_t  posPX = 0, posPY = 0, posPZ = 0;
static int16_t  negPX = 0, negPY = 0, negPZ = 0;
static uint8_t  blockIdx = 0;

/* 1-second RMS accumulation (block mean-squares) */
/* uint64: 100 blocks x 8192^2 = 6.71e9 overflows uint32. Accumulation
   runs once per BLOCK (100/s), not per sample. No clamp needed -> the
   full +/-16 g range of the part reaches the 1 s RMS untouched.
   The ISR only ACCUMULATES and hands off; the divides and square
   roots are done by loop() -- see rev F change A.                   */
static uint64_t acc1sX = 0, acc1sY = 0, acc1sZ = 0;
static uint8_t  blockCount1s = 0;

/* ---------------- ISR -> loop published results ---------------------
   These are MEAN SQUARES in counts^2, not RMS in counts. The ISR hands
   off squares and loop() takes the roots -- see "no square roots in the
   ISR" below. b_vec2 <= 3 x 8192^2 = 201e6, inside uint32.           */
volatile uint32_t b_ms2X = 0, b_ms2Y = 0, b_ms2Z = 0;
volatile uint16_t b_pkPX = 0, b_pkPY = 0, b_pkPZ = 0;
volatile uint16_t b_pkNX = 0, b_pkNY = 0, b_pkNZ = 0;
volatile uint32_t b_vec2 = 0;                 /* vector mean square */
volatile int16_t  b_dcX = 0, b_dcY = 0, b_dcZ = 0;
/* ISR -> loop handoff of the raw 1 s accumulators */
volatile uint64_t s1AccX = 0, s1AccY = 0, s1AccZ = 0;
volatile uint8_t  sec1Ready = 0;
/* 1 s RMS results, owned entirely by loop() -- no volatile needed */
static uint16_t r1msX = 0, r1msY = 0, r1msZ = 0, r1msV = 0;
volatile uint8_t  blockReady   = 0;
volatile uint16_t blockMissed  = 0;

/* ---------------------- impact snapshot (ISR) ----------------------- */
volatile uint8_t  impactLatched = 0;
volatile uint32_t impactMaxVec2 = 0;          /* vector mean square */
volatile uint32_t i_ms2X = 0, i_ms2Y = 0, i_ms2Z = 0;
volatile uint16_t i_pkPX = 0, i_pkPY = 0, i_pkPZ = 0;
volatile uint16_t i_pkNX = 0, i_pkNY = 0, i_pkNZ = 0;

/* --------------------------- trip state ----------------------------- */
volatile uint8_t       tripped      = 0;
volatile unsigned long tripMs       = 0;
volatile uint16_t      tripCount    = 0;
volatile uint32_t      peakVec2Hold = 0;      /* vector mean square */

/* ------------------------ fast impact path -------------------------- */
volatile uint32_t peakMag2Hold = 0;
volatile uint32_t impactMag2   = 0;    /* persists until READ */
static   uint32_t publishedImpactMag2 = 0;
static   uint8_t  overCount   = 0;
static   uint16_t settleCount = 0;

/* ------------------- loss-of-support path (CTX311) ------------------
   Operates on RAW magnitude, before gravity removal. ~20 bytes.
   No sample buffer is needed: this is a running counter, not a
   windowed statistic.                                              */
volatile uint32_t losThresholdSq     = 0;   /* counts^2 */
volatile uint16_t losSamplesRequired = 0;
volatile uint16_t losCount           = 0;   /* samples below threshold */
volatile uint8_t  losTolerance       = 0;   /* leaky spike allowance */
volatile uint8_t  losActive          = 0;   /* below threshold right now */
volatile uint8_t  losLatched         = 0;
volatile uint8_t  losByFault         = 0;   /* tripped by fault, not event */
volatile uint16_t losTripCount       = 0;
volatile unsigned long losMs         = 0;
volatile uint32_t losMinMag2         = 0xFFFFFFFFUL;
volatile uint16_t losRunSamples      = 0;   /* length of the last run */
volatile uint32_t rawMag2Live        = 0;

/* stuck-sample detection */
volatile int      lastRawX = 0, lastRawY = 0, lastRawZ = 0;
volatile uint16_t stuckCount = 0;
volatile uint8_t  rawSeeded  = 0;

/* --------------------------- diagnostics ---------------------------- */
volatile uint16_t isrMaxTicks = 0;   /* Timer1 ticks, 0.5 us each */
volatile uint16_t isrCount     = 0;
static   uint32_t maxLoopUs    = 0;    /* uint32: a full read is ~104 ms */
static   uint16_t faultFlags   = 0;
static   uint8_t  bootState    = BOOT_PENDING;
static   uint16_t bootSamples  = 0;
static   unsigned long implausibleSinceMs = 0;
static   uint8_t  implausibleRunning = 0;

/* ---------------------- threshold (loop -> ISR) --------------------- */
volatile uint16_t thresholdCounts = 0;
volatile uint32_t thresholdSq     = 0;

/* ----------------------------- config ------------------------------- */
const uint16_t CONFIG_MAGIC = 0xA5B2;
const int      EEPROM_ADDR  = 0;

/* EEPROM layout. Offsets 0-6 are UNCHANGED from rev H (magic, slave
   id, threshold), so a unit upgraded from CTX310 keeps its slave ID
   and impact threshold -- the same courtesy rev H extended across the
   G->H struct change. CTX311 fields start at offset 9.             */
struct Config {
  uint16_t magic;
  uint8_t  slaveId;
  uint16_t thresholdMg;
  uint16_t losThresholdMg;
  uint16_t losTimeMs;
  uint32_t losHoldMs;
  uint8_t  losFaultAction;
  /* Tilt reference, in COUNTS. All three zero means "no reference set"
     -- a real gravity vector can never be zero, so no extra flag byte
     is needed to mark it unset. */
  int16_t  tiltRefX;
  int16_t  tiltRefY;
  int16_t  tiltRefZ;
};

Config   cfg;
uint8_t  currentSlaveId  = DEFAULT_SLAVE_ID;
uint16_t thresholdMg     = DEFAULT_THRESHOLD_MG;
uint16_t losThresholdMg  = DEFAULT_LOS_THRESHOLD_MG;
uint16_t losTimeMs       = DEFAULT_LOS_TIME_MS;
uint32_t losHoldMs       = DEFAULT_LOS_HOLD_MS;
uint8_t  losFaultAction  = 1;

/* Tilt state. Loop context only -- the ISR never touches any of it. */
static int16_t       tiltRefX = 0, tiltRefY = 0, tiltRefZ = 0;
static uint16_t      tiltAngle = TILT_INVALID;
static uint8_t       tiltAtRest = 0;
static unsigned long tiltStillSinceMs = 0;
static unsigned long tiltLastUpdateMs = 0;

/* Supply. Loop context only. */
static uint16_t      vccMv      = VCC_INVALID;
static uint16_t      vccMinMv   = VCC_INVALID;
static uint8_t       vccDiscard = VCC_DISCARD;
static uint8_t       vccStarted = 0;
static unsigned long vccLastMs  = 0;

/* Defined further down, next to the tilt maths, but called from the
   command handler above it. The .ino is compiled directly by the host
   tests, so there is no Arduino auto-prototyping to lean on. */
static bool setTiltReference();
bool     cfgWasDefaulted = false;
uint8_t  resetCause      = 0;

/* forward declarations */
static void publishBlock();

/* ==================================================================== */
/*  helpers                                                             */
/* ==================================================================== */

/* Exact integer sqrt of a 32-bit value, no FP. ~900 cycles, 57 us.
   The old comment here claimed ~200 cycles; the real figure was ~4200.

   DO NOT rewrite the marked line as the more obvious (n >> 30). AVR has
   no barrel shifter, and at -Os gcc turns a 32-bit shift by 30 into a
   30-ITERATION BIT-SHIFT LOOP -- 209 cycles, run 16 times per call, so
   ~3300 of the old 4200 cycles were that one shift. Taking the same two
   bits out of the top byte compiles to mov/swap/lsr/lsr/andi, five
   single-cycle instructions, because gcc turns a shift by a multiple of
   8 into register moves.

   This mattered: the block boundary calls isqrt32 four times, which at
   the old cost was ~1050 us inside an ISR with a 625 us sample budget.
   Field unit read 1203 us in reg 19 against that budget.

   The result is bit-identical to the (n >> 30) form -- verified over the
   full uint32 range in test_registers.cpp.                            */
static uint16_t isqrt32(uint32_t n)
{
  uint32_t rem = 0, root = 0;
  for (uint8_t i = 0; i < 16; i++) {
    root <<= 1;
    rem = (rem << 2) | ((uint8_t)(n >> 24) >> 6);   /* NOT (n >> 30) */
    n <<= 2;
    if (root < rem) { root++; rem -= root; root++; }
  }
  return (uint16_t)(root >> 1);
}

/* counts -> milli-g, UNSIGNED.  x*3.9 ~= (x*3994)>>10  (0.01% error) */
static uint16_t countsToMg(uint16_t counts)
{
  uint32_t mg = ((uint32_t)counts * 3994UL) >> 10;
  return (mg > 65535UL) ? 65535U : (uint16_t)mg;
}

/* signed counts -> milli-g. Stored as a two's-complement pattern;
   the master must read regs 32-34 as SIGNED.                          */
static int16_t countsToMgSigned(int16_t counts)
{
  int32_t mg = ((int32_t)counts * 3994L) >> 10;
  if (mg >  32767L) return  32767;
  if (mg < -32768L) return -32768;
  return (int16_t)mg;
}

/* counts -> centi-m/s2, UNSIGNED.
   counts * 0.0039 g * 9.80665 * 100 ~= (counts*15666)>>12            */
static uint16_t countsToCms2(uint16_t counts)
{
  uint32_t v = ((uint32_t)counts * 15666UL) >> 12;
  return (v > 65535UL) ? 65535U : (uint16_t)v;
}

static void recomputeThresholdCounts()
{
  uint32_t c = ((uint32_t)thresholdMg * 10UL) / 39UL;   /* mg / 3.9 */
  if (c > 65535UL) c = 65535UL;
  if (c < 1)       c = 1;
  uint16_t v  = (uint16_t)c;
  uint32_t sq = (uint32_t)v * (uint32_t)v;

  /* Write atomically. The ISR's read then cannot be torn, because
     loop() is not an interrupt and cannot pre-empt the ISR.          */
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    thresholdCounts = v;
    thresholdSq     = sq;
  }
}

/* LOS threshold and time -> the forms the ISR compares against.
   Time is converted with the FIXED LOS_ODR_HZ, not the measured rate:
   a protective function's trip time must not drift with a live
   measurement. Rounds UP, so the configured time is a floor, never
   less than asked for.                                             */
static void recomputeLosParams()
{
  uint32_t c = ((uint32_t)losThresholdMg * 10UL) / 39UL;   /* mg / 3.9 */
  if (c > 65535UL) c = 65535UL;
  if (c < 1)       c = 1;
  uint32_t sq = c * c;

  uint32_t n = ((uint32_t)losTimeMs * LOS_ODR_HZ + 999UL) / 1000UL;
  if (n < 1)       n = 1;
  if (n > 65535UL) n = 65535UL;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    losThresholdSq     = sq;
    losSamplesRequired = (uint16_t)n;
  }
}

/* ==================================================================== */
/*  EEPROM configuration                                                */
/* ==================================================================== */

/* Offsets 0-6 are rev H's and are written identically, so a CTX310
   unit reflashed to CTX311 keeps its slave ID and impact threshold.
   CTX311 fields live at 9+ and were 0xFF on a rev H unit, which the
   range checks in loadConfig() reject into defaults.               */
void writeConfig()
{
  EEPROM.update(EEPROM_ADDR + 0, lowByte(CONFIG_MAGIC));
  EEPROM.update(EEPROM_ADDR + 1, highByte(CONFIG_MAGIC));
  EEPROM.update(EEPROM_ADDR + 2, cfg.slaveId);
  EEPROM.update(EEPROM_ADDR + 5, lowByte(cfg.thresholdMg));
  EEPROM.update(EEPROM_ADDR + 6, highByte(cfg.thresholdMg));

  EEPROM.update(EEPROM_ADDR +  9, lowByte(cfg.losThresholdMg));
  EEPROM.update(EEPROM_ADDR + 10, highByte(cfg.losThresholdMg));
  EEPROM.update(EEPROM_ADDR + 11, lowByte(cfg.losTimeMs));
  EEPROM.update(EEPROM_ADDR + 12, highByte(cfg.losTimeMs));
  EEPROM.update(EEPROM_ADDR + 13, (uint8_t)(cfg.losHoldMs      ) & 0xFF);
  EEPROM.update(EEPROM_ADDR + 14, (uint8_t)(cfg.losHoldMs >>  8) & 0xFF);
  EEPROM.update(EEPROM_ADDR + 15, (uint8_t)(cfg.losHoldMs >> 16) & 0xFF);
  EEPROM.update(EEPROM_ADDR + 16, (uint8_t)(cfg.losHoldMs >> 24) & 0xFF);
  EEPROM.update(EEPROM_ADDR + 17, cfg.losFaultAction);
  EEPROM.update(EEPROM_ADDR + 19, lowByte((uint16_t)cfg.tiltRefX));
  EEPROM.update(EEPROM_ADDR + 20, highByte((uint16_t)cfg.tiltRefX));
  EEPROM.update(EEPROM_ADDR + 21, lowByte((uint16_t)cfg.tiltRefY));
  EEPROM.update(EEPROM_ADDR + 22, highByte((uint16_t)cfg.tiltRefY));
  EEPROM.update(EEPROM_ADDR + 23, lowByte((uint16_t)cfg.tiltRefZ));
  EEPROM.update(EEPROM_ADDR + 24, highByte((uint16_t)cfg.tiltRefZ));
}

void applyDefaults()
{
  cfg.magic          = CONFIG_MAGIC;
  cfg.slaveId        = DEFAULT_SLAVE_ID;
  cfg.thresholdMg    = DEFAULT_THRESHOLD_MG;
  cfg.losThresholdMg = DEFAULT_LOS_THRESHOLD_MG;
  cfg.losTimeMs      = DEFAULT_LOS_TIME_MS;
  cfg.losHoldMs      = DEFAULT_LOS_HOLD_MS;
  cfg.losFaultAction = 1;                    /* fail to safe */
  /* No tilt reference. It describes where this unit was installed, not
     what the product is, so a factory reset must forget it rather than
     carry a stale one into a different mounting. */
  cfg.tiltRefX = cfg.tiltRefY = cfg.tiltRefZ = 0;
  writeConfig();
  currentSlaveId = cfg.slaveId;
  thresholdMg    = cfg.thresholdMg;
  losThresholdMg = cfg.losThresholdMg;
  losTimeMs      = cfg.losTimeMs;
  losHoldMs      = cfg.losHoldMs;
  losFaultAction = cfg.losFaultAction;
  tiltRefX = cfg.tiltRefX;
  tiltRefY = cfg.tiltRefY;
  tiltRefZ = cfg.tiltRefZ;
  recomputeThresholdCounts();
  recomputeLosParams();
}

void loadConfig()
{
  cfg.magic       = (uint16_t)EEPROM.read(EEPROM_ADDR + 0) |
                    ((uint16_t)EEPROM.read(EEPROM_ADDR + 1) << 8);
  cfg.slaveId     = EEPROM.read(EEPROM_ADDR + 2);
  cfg.thresholdMg = (uint16_t)EEPROM.read(EEPROM_ADDR + 5) |
                    ((uint16_t)EEPROM.read(EEPROM_ADDR + 6) << 8);

  cfg.losThresholdMg = (uint16_t)EEPROM.read(EEPROM_ADDR +  9) |
                       ((uint16_t)EEPROM.read(EEPROM_ADDR + 10) << 8);
  cfg.losTimeMs      = (uint16_t)EEPROM.read(EEPROM_ADDR + 11) |
                       ((uint16_t)EEPROM.read(EEPROM_ADDR + 12) << 8);
  cfg.losHoldMs      = (uint32_t)EEPROM.read(EEPROM_ADDR + 13)        |
                       ((uint32_t)EEPROM.read(EEPROM_ADDR + 14) <<  8) |
                       ((uint32_t)EEPROM.read(EEPROM_ADDR + 15) << 16) |
                       ((uint32_t)EEPROM.read(EEPROM_ADDR + 16) << 24);
  cfg.losFaultAction = EEPROM.read(EEPROM_ADDR + 17);
  cfg.tiltRefX = (int16_t)((uint16_t)EEPROM.read(EEPROM_ADDR + 19) |
                 ((uint16_t)EEPROM.read(EEPROM_ADDR + 20) << 8));
  cfg.tiltRefY = (int16_t)((uint16_t)EEPROM.read(EEPROM_ADDR + 21) |
                 ((uint16_t)EEPROM.read(EEPROM_ADDR + 22) << 8));
  cfg.tiltRefZ = (int16_t)((uint16_t)EEPROM.read(EEPROM_ADDR + 23) |
                 ((uint16_t)EEPROM.read(EEPROM_ADDR + 24) << 8));

  bool bad = (cfg.magic != CONFIG_MAGIC) ||
             (cfg.slaveId < 1) || (cfg.slaveId > 247) ||
             (cfg.thresholdMg < THRESHOLD_MIN_MG) ||
             (cfg.thresholdMg > THRESHOLD_MAX_MG) ||
             (cfg.losThresholdMg < LOS_THRESHOLD_MIN_MG) ||
             (cfg.losThresholdMg > LOS_THRESHOLD_MAX_MG) ||
             (cfg.losTimeMs < LOS_TIME_MIN_MS) ||
             (cfg.losTimeMs > LOS_TIME_MAX_MS) ||
             (cfg.losHoldMs > LOS_HOLD_MAX_MS) ||
             (cfg.losFaultAction > 1);

  if (bad) {
    cfgWasDefaulted = true;
    applyDefaults();
  } else {
    currentSlaveId = cfg.slaveId;
    thresholdMg    = cfg.thresholdMg;
    losThresholdMg = cfg.losThresholdMg;
    losTimeMs      = cfg.losTimeMs;
    losHoldMs      = cfg.losHoldMs;
    losFaultAction = cfg.losFaultAction;
    tiltRefX = cfg.tiltRefX;
    tiltRefY = cfg.tiltRefY;
    tiltRefZ = cfg.tiltRefZ;
    recomputeThresholdCounts();
    recomputeLosParams();
  }
}

/* ==================================================================== */
/*  Modbus-writable settings                                            */
/* ==================================================================== */
/* These own regs 21 and 22. publishBlock() must never write them:
   it runs at READ time, so refreshing a writable register there
   overwrites a master's pending write before it is ever inspected and
   the setting silently reverts (rev F bug, see change B).             */

void checkModbusSlaveIdUpdate()
{
  unsigned int requested = holdingRegs[SlaveIDReg];
  if (requested == currentSlaveId) return;

  if (requested >= 1 && requested <= 247) {
    currentSlaveId = (uint8_t)requested;
    cfg.slaveId    = currentSlaveId;
    writeConfig();
    modbus_update_comms(baud, SERIAL_8N1, currentSlaveId);
  } else {
    holdingRegs[SlaveIDReg] = currentSlaveId;   /* reject: revert */
  }
}

void checkThresholdUpdate()
{
  unsigned int req = holdingRegs[ThresholdReg];
  if (req != thresholdMg) {
    if (req >= THRESHOLD_MIN_MG && req <= THRESHOLD_MAX_MG) {
      thresholdMg     = (uint16_t)req;
      cfg.thresholdMg = thresholdMg;
      writeConfig();
      recomputeThresholdCounts();
    } else {
      holdingRegs[ThresholdReg] = thresholdMg;  /* reject: revert */
    }
  }
}

/* The three LOS settings follow reg 22's pattern exactly: hold the
   written value, persist, echo through a paired effective-value
   register, revert on an out-of-range write. publishBlock() must
   never touch them -- that was the rev F bug.                      */
void checkLosSettingsUpdate()
{
  unsigned int req = holdingRegs[LosThresholdReg];
  if (req != losThresholdMg) {
    if (req >= LOS_THRESHOLD_MIN_MG && req <= LOS_THRESHOLD_MAX_MG) {
      losThresholdMg     = (uint16_t)req;
      cfg.losThresholdMg = losThresholdMg;
      writeConfig();
      recomputeLosParams();
    } else {
      holdingRegs[LosThresholdReg] = losThresholdMg;   /* reject */
    }
  }

  req = holdingRegs[LosTimeMsReg];
  if (req != losTimeMs) {
    if (req >= LOS_TIME_MIN_MS && req <= LOS_TIME_MAX_MS) {
      losTimeMs     = (uint16_t)req;
      cfg.losTimeMs = losTimeMs;
      writeConfig();
      recomputeLosParams();
    } else {
      holdingRegs[LosTimeMsReg] = losTimeMs;           /* reject */
    }
  }

  /* Hold is a uint32 internally but only 16 bits are exposed, so the
     register range is 0..65535 ms. 0 means LATCH.                  */
  req = holdingRegs[LosHoldMsReg];
  if ((uint32_t)req != losHoldMs) {
    losHoldMs     = (uint32_t)req;
    cfg.losHoldMs = losHoldMs;
    writeConfig();
  }

  req = holdingRegs[LosFaultActionReg];
  if (req != losFaultAction) {
    if (req <= 1) {
      losFaultAction     = (uint8_t)req;
      cfg.losFaultAction = losFaultAction;
      writeConfig();
    } else {
      holdingRegs[LosFaultActionReg] = losFaultAction; /* reject */
    }
  }
}

/* Reg 28 is a TRIGGER, not a setting. It must self-clear: if it held
   its value the command would re-run on every pass of loop(), and a
   single CMD_CLEAR_PEAKHOLD would pin regs 30 and 36 to zero for ever.
   So reg 28 always reads back 0 and a read-back cannot confirm
   anything. Regs 45-47 exist for that: the master writes reg 28, then
   reads reg 47 and checks it moved.                                   */
void checkCommandRegister()
{
  uint16_t cmd = (uint16_t)holdingRegs[CommandReg];
  if (cmd == 0) return;

  uint8_t accepted = 1;

  switch (cmd) {
    case CMD_FACTORY_RESET:
      applyDefaults();
      cfgWasDefaulted = true;
      modbus_update_comms(baud, SERIAL_8N1, currentSlaveId);
      /* publishBlock() no longer refreshes the writable registers, so
         the defaults have to be pushed into them here.               */
      holdingRegs[SlaveIDReg]         = currentSlaveId;
      holdingRegs[ThresholdReg]       = thresholdMg;
      holdingRegs[LosThresholdReg]    = losThresholdMg;
      holdingRegs[LosTimeMsReg]       = losTimeMs;
      holdingRegs[LosHoldMsReg]       = (unsigned int)losHoldMs;
      holdingRegs[LosFaultActionReg]  = losFaultAction;
      /* identification registers are compile-time constants and are
         deliberately NOT cleared by a factory reset */
      break;

    /* Clearing the arrest latch is a deliberate operator act, so it
       goes through the acknowledged command path rather than a
       timer. Note it CANNOT clear a fault-driven trip: if the
       detection channel is still dead, re-arming would be a lie.  */
    case CMD_CLEAR_LOS:
      if ((faultFlags & FAULT_DETECTION_LOST) && losFaultAction) {
        accepted = 0;                    /* refuse while faulted */
      } else {
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          losLatched = 0;
          losByFault = 0;
          losCount   = 0;
          losMinMag2 = 0xFFFFFFFFUL;
          PORTC |= (1 << LOS_PORT_BIT);  /* atomic SBI -- re-arm */
        }
      }
      break;

    /* Monitoring only -- neither of these can affect an output. */
    case CMD_SET_TILT_REF:
      accepted = setTiltReference() ? 1 : 0;
      break;

    case CMD_CLEAR_TILT_REF:
      tiltRefX = tiltRefY = tiltRefZ = 0;
      cfg.tiltRefX = cfg.tiltRefY = cfg.tiltRefZ = 0;
      writeConfig();
      tiltAngle = TILT_INVALID;
      break;

    case CMD_CLEAR_LOSCOUNT:
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { losTripCount = 0; }
      break;

    /* Only clears the LATCHED record. Any fault whose condition is
       still present is re-raised by runDiagnostics() within a
       second, which is the intended behaviour.                    */
    case CMD_CLEAR_FAULTS:
      faultFlags = 0;
      implausibleRunning = 0;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { stuckCount = 0; }
      break;

    case CMD_CLEAR_PEAKHOLD:
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        peakVec2Hold = 0;
        peakMag2Hold = 0;
      }
      break;

    case CMD_CLEAR_TRIPCOUNT:
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { tripCount = 0; }
      break;

    case CMD_CLEAR_DIAG:
      /* The supply minimum is a diagnostic high-water like reg 20 and
         reg 26, so it belongs to the same clear rather than needing a
         command of its own. */
      vccMinMv = vccMv;
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        isrMaxTicks = 0;
        blockMissed  = 0;
      }
      maxLoopUs = 0;
      holdingRegs[Modbuslooptime] = 0;
      break;

    default:
      accepted = 0;
      break;
  }

  /* Acknowledge before clearing the trigger, so the master can never
     observe a zeroed reg 28 without the matching evidence in 45-47. */
  holdingRegs[LastCommandReg]   = cmd;
  holdingRegs[CommandStatusReg] = accepted ? CMD_STATUS_ACCEPTED
                                           : CMD_STATUS_UNKNOWN;
  if (accepted) holdingRegs[CommandCountReg]++;

  holdingRegs[CommandReg] = 0;
}

/* ==================================================================== */
/*  ISR -- capture + integer DSP + trip decision                        */
/* ==================================================================== */

void myHandler()
{
  uint16_t t0 = TCNT1;           /* Timer1 free-runs at 0.5 us/tick */

  int x, y, z;
  adxl.readAccel(&x, &y, &z);      /* also clears DATA_READY */

  /* ============ LOSS OF SUPPORT -- RAW, BEFORE GRAVITY REMOVAL =====
     This MUST sit above the DC tracker. Everything below it is
     AC-coupled through a 0.124 Hz corner and is structurally blind to
     a sustained loss of support, which is a DC event: the whole point
     is that total magnitude falls and STAYS down.

     Range: at +/-16 g full-res, full scale is 4096 counts, so
     x*x <= 16.8e6 and rawMag2 <= 50.3e6. Inside uint32, no clamp
     needed -- the same argument rev H makes for mag2 <= 201e6.

     Cost: COMPUTED FROM A BUILD (avr-gcc 7.3.0 -Os, ATmega328P at
     16 MHz), by disassembling myHandler() in the linked image and
     counting cycles along each path:

        201 cycles / 12.6 us   every supported sample (the sustained
                               cost: this is what runs 1589 times a
                               second)
        205 cycles / 12.8 us   while the spike tolerance is leaking
        300 cycles / 18.8 us   the sample that trips, worst case,
                               including the millis() call

     173 of the common-path cycles are the three squares alone. gcc
     emits them as three calls to __mulhisi3 (41 cycles each with a
     negative operand, 37 with a positive one) because the ATmega328P
     has only an 8x8 multiplier. They cannot be shared with the three
     squares rev H already computes: those square the DC-REMOVED ax,
     ay, az, and this block must square the RAW x, y, z. Different
     operands, so the duplication is real and unavoidable here.

     This REPLACES an earlier estimate of ~60-80 cycles / ~5 us, which
     was low by about 2.5x -- it costed the multiplies as instructions
     rather than as libgcc calls.

     Against the budget: rev H measured 153 us in reg 19 against a
     629 us sample period, so this lands at ~166 us typical and ~172 us
     worst case. Comfortably inside the 250 us target.

     A cycle count is still not a measurement. It excludes interrupt
     entry and exit and whatever the compiler did to the surrounding
     code. CONFIRM ON REG 19 AFTER FLASHING (H-03).                */
  uint32_t rawMag2 = (uint32_t)((int32_t)x * x) +
                     (uint32_t)((int32_t)y * y) +
                     (uint32_t)((int32_t)z * z);
  rawMag2Live = rawMag2;

  /* ---- stuck data path ----
     A live ADXL345 dithers by at least one LSB. Bit-identical samples
     mean the bus or the part is frozen while DATA_READY keeps firing
     -- which would otherwise look exactly like a stationary assembly,
     for ever, with the arrest device never told.                   */
  if (rawSeeded && x == lastRawX && y == lastRawY && z == lastRawZ) {
    if (stuckCount < STUCK_SAMPLE_LIMIT) stuckCount++;
  } else {
    stuckCount = 0;
  }
  lastRawX = x; lastRawY = y; lastRawZ = z;
  rawSeeded = 1;

  if (settleCount < SETTLE_SAMPLES) {
    losCount     = 0;
    losActive    = 0;
  } else if (rawMag2 < losThresholdSq) {
    losActive = 1;
    if (rawMag2 < losMinMag2) losMinMag2 = rawMag2;
    losTolerance = LOS_SPIKE_TOLERANCE;      /* refill on a good sample */
    if (losCount < 0xFFFFU) losCount++;
    if (losCount >= losSamplesRequired && !losLatched) {
      PORTC &= ~(1 << LOS_PORT_BIT);         /* atomic CBI -- ARREST */
      losLatched  = 1;
      losByFault  = 0;
      losTripCount++;
      losMs       = millis();
    }
    losRunSamples = losCount;
  } else if (losTolerance) {
    /* Leaky, not resetting. A jerk, a guide contact or a partial
       re-catch must not discard a run that is already 30 samples into
       a real event. This is the OPPOSITE trade to PEAK_CONFIRM on the
       impact path, and for the opposite reason -- see the header.  */
    losTolerance--;
    if (losCount) {
      if (losCount < 0xFFFFU) losCount++;
      losRunSamples = losCount;
    }
  } else {
    losActive = 0;
    losCount  = 0;
  }
  /* ================================================================ */

  if (!dcSeeded) {
    dcAccX = (int32_t)x << DC_SHIFT;
    dcAccY = (int32_t)y << DC_SHIFT;
    dcAccZ = (int32_t)z << DC_SHIFT;
    dcSeeded = true;
  }

  /* ---- dynamic gravity removal ----
     err is both the tracker error AND the gravity-free AC sample. */
  int32_t eX = (int32_t)x - (dcAccX >> DC_SHIFT);
  int32_t eY = (int32_t)y - (dcAccY >> DC_SHIFT);
  int32_t eZ = (int32_t)z - (dcAccZ >> DC_SHIFT);

  int16_t ax = (int16_t)eX;
  int16_t ay = (int16_t)eY;
  int16_t az = (int16_t)eZ;

  /* slew-clamp the tracker update so a one-sided transient cannot
     drag the gravity estimate. Direction is always correct.        */
  if      (eX >  DC_MAX_INC) eX =  DC_MAX_INC;
  else if (eX < -DC_MAX_INC) eX = -DC_MAX_INC;
  if      (eY >  DC_MAX_INC) eY =  DC_MAX_INC;
  else if (eY < -DC_MAX_INC) eY = -DC_MAX_INC;
  if      (eZ >  DC_MAX_INC) eZ =  DC_MAX_INC;
  else if (eZ < -DC_MAX_INC) eZ = -DC_MAX_INC;

  dcAccX += eX;
  dcAccY += eY;
  dcAccZ += eZ;

  /* sX <= 8192^2 = 67e6; block sum <= 16x = 1.07e9; mag2 <= 3x = 201e6.
     All inside uint32 with no clamping.                            */
  uint32_t sX = (uint32_t)((int32_t)ax * ax);
  uint32_t sY = (uint32_t)((int32_t)ay * ay);
  uint32_t sZ = (uint32_t)((int32_t)az * az);

  sqSumX += sX;
  sqSumY += sY;
  sqSumZ += sZ;

  /* ================= FAST IMPACT PATH -- every sample ==============
     Instantaneous vector magnitude, squared. Reuses the three squares
     above, so the marginal cost is two adds and one compare.        */
  uint32_t mag2 = sX + sY + sZ;
  if (mag2 > peakMag2Hold) peakMag2Hold = mag2;

  if (settleCount < SETTLE_SAMPLES) {
    settleCount++;
    overCount = 0;
  } else if (mag2 > thresholdSq) {
    if (overCount < PEAK_CONFIRM) overCount++;
    if (overCount >= PEAK_CONFIRM) {
      PORTC &= ~(1 << OUTPUT_PORT_BIT);    /* atomic CBI -- OUTPUT OPEN */
      if (!tripped) tripCount++;
      tripped = 1;
      tripMs  = millis();
      if (!impactLatched || mag2 > impactMag2) impactMag2 = mag2;
      impactLatched = 1;
    }
  } else {
    overCount = 0;
  }
  /* ================================================================ */

  if (ax > posPX) posPX = ax;  else if (ax < negPX) negPX = ax;
  if (ay > posPY) posPY = ay;  else if (ay < negPY) negPY = ay;
  if (az > posPZ) posPZ = az;  else if (az < negPZ) negPZ = az;

  isrCount++;

  if (++blockIdx >= BLOCK_SIZE) {
    blockIdx = 0;

    uint32_t msX = sqSumX >> BLOCK_SHIFT;      /* block mean-square */
    uint32_t msY = sqSumY >> BLOCK_SHIFT;
    uint32_t msZ = sqSumZ >> BLOCK_SHIFT;

    /* ---- NO SQUARE ROOTS IN THE ISR ----
       The vector mean square is just the sum of the per-axis mean
       squares:  sqrt(msX)^2 + sqrt(msY)^2 + sqrt(msZ)^2 == msX+msY+msZ.
       Rev F took three roots, squared them again and rooted the sum --
       four isqrt32 calls, ~230 us even after the isqrt32 fix, and a
       double rounding that biased the result LOW by 1-2 counts.
       Everything the ISR has to decide (peak hold, the sustained-RMS
       trip, which block wins the impact snapshot) is a comparison, and
       comparisons work exactly the same on squares. loop() takes the
       roots for presentation only -- the same split reg 36 has always
       used with peakMag2Hold.
       vec2 <= 3 x 8192^2 = 201e6, so uint32 is safe with no clamp.  */
    uint32_t vec2 = msX + msY + msZ;

    uint16_t pX = (uint16_t)posPX, pY = (uint16_t)posPY, pZ = (uint16_t)posPZ;
    uint16_t nX = (uint16_t)(-(int32_t)negPX);
    uint16_t nY = (uint16_t)(-(int32_t)negPY);
    uint16_t nZ = (uint16_t)(-(int32_t)negPZ);

    if (blockReady) blockMissed++;      /* loop did not collect the last */
    b_ms2X = msX; b_ms2Y = msY; b_ms2Z = msZ;
    b_pkPX = pX; b_pkPY = pY; b_pkPZ = pZ;
    b_pkNX = nX; b_pkNY = nY; b_pkNZ = nZ;
    b_vec2 = vec2;
    b_dcX  = (int16_t)(dcAccX >> DC_SHIFT);
    b_dcY  = (int16_t)(dcAccY >> DC_SHIFT);
    b_dcZ  = (int16_t)(dcAccZ >> DC_SHIFT);
    blockReady = 1;

    /* Peak hold stays in the ISR. It must see EVERY block -- loop()
       misses most of them while a Modbus read is in flight (reg 26). */
    if (vec2 > peakVec2Hold) peakVec2Hold = vec2;

    /* ---- 1-second RMS: mean of block mean-squares ----
       uint64 accumulator, so no input clamping is required.          */
    acc1sX += msX;
    acc1sY += msY;
    acc1sZ += msZ;
    if (++blockCount1s >= BLOCKS_PER_SEC) {
      blockCount1s = 0;
      /* hand the raw accumulators to loop(); NO division here.
         Latest-wins if loop() has not collected the previous set. */
      s1AccX = acc1sX; s1AccY = acc1sY; s1AccZ = acc1sZ;
      sec1Ready = 1;
      acc1sX = acc1sY = acc1sZ = 0;
    }

    sqSumX = sqSumY = sqSumZ = 0;
    posPX = posPY = posPZ = 0;
    negPX = negPY = negPZ = 0;

    /* ---- impact characterisation ----
       ONE path opens the interlock: the per-sample peak test above.
       Rev G also tripped on sustained vector RMS, which meant a trip
       could mean either "something hit it" or "it has been shaking for
       a while" with nothing in the map to say which. This block no
       longer decides anything -- it only records WHICH 10 ms block was
       the worst of a latched impact, so regs 0-2, 4-6, 13-15 and 23
       describe the strike rather than an arbitrary later block.     */
    if (impactLatched && vec2 > impactMaxVec2) {
      impactMaxVec2 = vec2;
      i_ms2X = msX; i_ms2Y = msY; i_ms2Z = msZ;
      i_pkPX = pX; i_pkPY = pY; i_pkPZ = pZ;
      i_pkNX = nX; i_pkNY = nY; i_pkNZ = nZ;
    }
  }

  uint16_t dt = TCNT1 - t0;      /* wraps correctly; 32.7 ms span */
  if (dt > isrMaxTicks) isrMaxTicks = dt;
}

/* ==================================================================== */
/*  diagnostics  --  the part that actually matters                     */
/* ==================================================================== */
/* The dangerous failure of this device is NOT a missed event. It is a
   dead sensor that looks like a stationary assembly: nothing trips,
   nothing complains, and the arrest device is never told. Rev H had
   no protection against that at all. These checks are what make the
   difference, and the rate check is the most important of them.    */

static void runDiagnostics(uint16_t measuredRate)
{
  /* ---- 1. sample rate ----
     If DATA_READY stops -- dead part, dead SPI bus, detached INT1 --
     isrCount goes to zero and this is the ONLY thing that notices. */
  if (measuredRate < RATE_MIN_HZ || measuredRate > RATE_MAX_HZ) {
    faultFlags |= FAULT_RATE;
  }

  /* ---- 2. frozen data path ---- */
  uint16_t sc;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { sc = stuckCount; }
  if (sc >= STUCK_SAMPLE_LIMIT) faultFlags |= FAULT_STUCK;

  /* ---- 3. plausibility ----
     Only meaningful at rest: during a genuine loss of support the
     magnitude is SUPPOSED to be low, so the check is suspended
     whenever the LOS path is active or latched.                    */
  uint32_t rm2;
  uint8_t  act, lat;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    rm2 = rawMag2Live; act = losActive; lat = losLatched;
  }
  uint16_t rawMg = countsToMg(isqrt32(rm2));

  if (act || lat || settleCount < SETTLE_SAMPLES) {
    implausibleRunning = 0;
  } else if (rawMg < PLAUSIBLE_MIN_MG || rawMg > PLAUSIBLE_MAX_MG) {
    if (!implausibleRunning) {
      implausibleRunning = 1;
      implausibleSinceMs = millis();
    } else if (millis() - implausibleSinceMs >= PLAUSIBLE_FAIL_MS) {
      faultFlags |= FAULT_IMPLAUSIBLE;
    }
  } else {
    implausibleRunning = 0;
  }

  /* ---- 4. config ---- */
  if (cfgWasDefaulted) faultFlags |= FAULT_CONFIG;

  /* ---- act on it ----
     Health opens on any fault. If configured to fail safe (default),
     a fault ALSO trips the arrest output: a dead detection channel
     means the protective function is gone, and halting is the safe
     direction. losByFault records WHY, so the master can tell a
     genuine event from a self-diagnosis.

     Health ALSO opens while the detector is still arming. For the first
     SETTLE_SAMPLES the trip test is suppressed, so the device cannot
     operate the arrest at all -- and a protective function that cannot
     act is not a healthy one. Closing PC2 through that window would
     tell a PLC the assembly is protected during the one window in which
     it provably is not.

     Arming is NOT a fault: it never engages the arrest, it sets no
     flag in register 61, and it clears itself. That is why the arrest
     branch below stays keyed on faultFlags rather than on this test.
     Register 49 bit 6 is the pollable form of the distinction.     */
  uint16_t sCount;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { sCount = settleCount; }
  bool arming = (sCount < SETTLE_SAMPLES);

  if (faultFlags || arming) {
    PORTC &= ~(1 << HEALTH_PORT_BIT);
    if (faultFlags && losFaultAction && (faultFlags & FAULT_DETECTION_LOST)) {
      ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (!losLatched) {
          PORTC &= ~(1 << LOS_PORT_BIT);
          losLatched = 1;
          losByFault = 1;
        }
      }
    }
  } else {
    PORTC |= (1 << HEALTH_PORT_BIT);
  }
}

/* Boot check. Runs during the SETTLE window, before the detector is
   armed, so it cannot disturb a live protective function.

   NOT a real self-test. The ADXL345 has an electrostatic SELF_TEST
   bit that deflects the axes, which is a far stronger check -- but it
   perturbs the signal that operates the arrest device, so it can only
   ever run here, at boot, and it needs setSelfTestBit() in the
   SparkFun driver. That API was not confirmed against the vendored
   copy, so this uses readAccel() alone: confirm the part delivers
   plausible, CHANGING data before arming.                          */
static void bootCheck()
{
  if (bootState != BOOT_PENDING) return;

  uint32_t rm2;
  uint16_t sc, cnt;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    rm2 = rawMag2Live; sc = stuckCount; cnt = isrCount;
  }
  if (cnt == 0) return;                    /* no samples yet */

  if (++bootSamples < 200) return;         /* let it settle first */

  uint16_t rawMg = countsToMg(isqrt32(rm2));
  bool ok = (rawMg >= PLAUSIBLE_MIN_MG) &&
            (rawMg <= PLAUSIBLE_MAX_MG) &&
            (sc < 100);                    /* data is moving */

  bootState = ok ? BOOT_PASS : BOOT_FAIL;
  if (!ok) faultFlags |= FAULT_BOOTCHECK;
}

/* ==================================================================== */
/*  loss-of-support output  --  hold or latch                           */
/* ==================================================================== */
/* Default is LATCH (losHoldMs == 0). An arrest device must not
   silently re-arm on a timer while the assembly is still hanging on
   whatever caught it; clearing is an operator act through
   CMD_CLEAR_LOS. A timed hold is offered for installations whose PLC
   sequences the reset itself.
   A fault-driven trip NEVER auto-clears, whatever the hold is set to. */

static void updateLosOutput()
{
  uint8_t lat, byFault;
  unsigned long t;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lat = losLatched; byFault = losByFault; t = losMs;
  }
  if (!lat || byFault) return;
  if (losHoldMs == 0) return;              /* latched until commanded */

  if (millis() - t >= losHoldMs) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      if (losLatched && !losByFault &&
          (millis() - losMs >= losHoldMs) && !losActive) {
        losLatched = 0;
        losCount   = 0;
        losMinMag2 = 0xFFFFFFFFUL;
        PORTC |= (1 << LOS_PORT_BIT);      /* atomic SBI */
      }
    }
  }
}

/* ==================================================================== */
/*  output state machine -- loop() may only CLOSE the output            */
/* ==================================================================== */

static void updateOutput()
{
  uint8_t t;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { t = tripped; }
  if (!t) return;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    /* re-check inside the atomic section: the ISR may have just
       re-tripped, in which case tripMs has moved.
       NOTE impactMag2 is deliberately NOT cleared here -- reg 37 is
       read-to-clear and must survive the hold expiry.               */
    if (tripped && (millis() - tripMs >= TRIP_HOLD_MS)) {
      tripped       = 0;
      impactLatched = 0;
      impactMaxVec2 = 0;
      overCount     = 0;
      PORTC |= (1 << OUTPUT_PORT_BIT);       /* atomic SBI */
    }
  }
}

/* ==================================================================== */
/*  stack high-water instrumentation  --  DEBUG BUILDS ONLY             */
/* ==================================================================== */
/* Build with -DCTX311_STACK_DEBUG. It is OUT of a release build and
   must stay out: it reports through a RESERVED register, which a
   release image must never do.

   Why it exists. docs/RESOURCE_BUDGET.md can say how much SRAM is
   statically free (1006 B) but not how much of that the stack actually
   eats. The deepest path is this ISR firing on top of loop() inside a
   Modbus response, and no amount of reading the source settles it --
   the answer is a number a running unit has to give you.

   Method: paint the whole free region with a known byte before main(),
   then count how many bytes are still wearing that byte. The lowest
   count ever observed is the high-water mark. Flash this for the H-05
   soak and read it at the end; a release build goes back on afterwards.

   The reported figure is BYTES STILL UNTOUCHED -- minimum free SRAM.
   Small is bad. If it approaches zero the stack has reached .bss and
   the numbers in RESOURCE_BUDGET.md stop meaning anything.

   WHERE IT REPORTS, and why that is not a new register. T-02 says not
   to add one, because the map is a contract. So this borrows register
   48, a reserved hole that always reads 0 in a release build. Reg 48
   rather than reg 35 deliberately: in map 8 reg 35 was WRITABLE (the
   old sustained-RMS threshold) and a legacy master could still write
   it, which would fight the debug value on the wire. Reg 48 was only
   ever the read-only echo of 35, so nothing out there writes it.

   Borrowing a hole also costs no diagnostic. The soak needs registers
   19, 26, 27, 31 and 54 to stay meaningful, and overloading any of
   those to carry this number would spoil the run it exists to serve. */
#ifdef CTX311_STACK_DEBUG

#define STACK_CANARY 0xC5

#ifdef __AVR__

extern uint8_t _end;        /* end of .bss -- linker-provided */
extern uint8_t __stack;     /* top of RAM   -- linker-provided */

/* Runs from .init1: before .bss is cleared, before main(), and before
   anything has used the stack. naked, and written in assembler,
   because a C body would place its own locals on the very stack it is
   painting and then paint over them.

   Paints [_end, __stack) -- the top byte is left alone so nothing that
   the startup code may already have placed at the stack pointer is
   disturbed. */
void stackPaint(void) __attribute__((naked, used, section(".init1")));
void stackPaint(void)
{
  __asm volatile (
    "    ldi r30, lo8(_end)"    "\n"
    "    ldi r31, hi8(_end)"    "\n"
    "    ldi r24, %[canary]"    "\n"
    "    ldi r25, hi8(__stack)" "\n"
    "    rjmp 2f"               "\n"
    "1:  st   Z+, r24"          "\n"
    "2:  cpi  r30, lo8(__stack)""\n"
    "    cpc  r31, r25"         "\n"
    "    brlo 1b"               "\n"
    :: [canary] "M" (STACK_CANARY)
  );
}

/* Counts canary bytes still standing, from _end upward. Stops at the
   first byte the stack has touched, so this is the minimum free SRAM
   ever seen since reset, not the free SRAM right now. */
static uint16_t stackUnusedBytes(void)
{
  const uint8_t *p = &_end;
  uint16_t       n = 0;

  while (p < &__stack && *p == STACK_CANARY) { p++; n++; }
  return n;
}

#else   /* host test build: there is no AVR stack to paint */
static uint16_t stackUnusedBytes(void) { return 0; }
#endif  /* __AVR__ */

#endif  /* CTX311_STACK_DEBUG */


/* ==================================================================== */
/*  tilt  --  MONITORING ONLY, never protective                         */
/* ==================================================================== */
/* Tilt needs no new sensing. The DC tracker already follows gravity --
   it has to, because everything below it is AC-coupled -- and a gravity
   vector IS a tilt measurement. This turns that vector into an angle
   against a stored reference and publishes it. Nothing here touches an
   output, sets a fault, or is reachable from the arrest path.

   READ THIS BEFORE USING IT FOR ANYTHING:

   1. VALID AT REST ONLY. An accelerometer cannot separate tilt from
      linear acceleration -- they are the same measurement. While the
      assembly is jacked, falling, or vibrating, a "tilt" derived from
      it is meaningless. So the angle is updated ONLY after the 1 s
      AC-coupled vector RMS has stayed below TILT_REST_MG continuously
      for TILT_REST_MS, and it HOLDS its last value the rest of the
      time. A held value is the last trustworthy reading, not the
      current attitude.

   2. IT IS NOT A PROTECTIVE FUNCTION. It operates nothing. Trend it,
      alarm on it in the PLC if you like, but the arrest path is
      registers 49-63 and this is deliberately not part of it. Wiring
      tilt into a safety decision would be a new claim this device
      cannot support.

   3. NO YAW. Rotation about the gravity vector does not move the
      gravity vector, so it is invisible. Two axes, never three.       */

/* 1024 * sin(theta) for theta = 0..90 degrees, one entry per degree.
   In flash, not SRAM: 182 bytes of .bss would be 18% of what is free.

   Used BACKWARDS -- given a chord we search for the angle. Indexing by
   angle rather than by sine is what keeps the accuracy uniform: asin is
   near-vertical approaching 90 degrees, so a table indexed by sine
   would interpolate very badly exactly there. */
static const uint16_t tiltSinTable[91] PROGMEM = {
     0,   18,   36,   54,   71,   89,  107,  125,
   143,  160,  178,  195,  213,  230,  248,  265,
   282,  299,  316,  333,  350,  367,  384,  400,
   416,  433,  449,  465,  481,  496,  512,  527,
   543,  558,  573,  587,  602,  616,  630,  644,
   658,  672,  685,  698,  711,  724,  737,  749,
   761,  773,  784,  796,  807,  818,  828,  839,
   849,  859,  868,  878,  887,  896,  904,  912,
   920,  928,  935,  943,  949,  956,  962,  968,
   974,  979,  984,  989,  994,  998, 1002, 1005,
  1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023,
  1023, 1024, 1024
};

/* Angle between two vectors, in TENTHS OF A DEGREE, integer only.

   Method: normalise both to length 1024, then take the chord between
   the normalised tips. chord = 2 * 1024 * sin(theta/2), so a lookup of
   chord/2 in the sine table gives theta/2 directly.

   The chord form is used rather than the more obvious dot-product
   arccos because it stays well conditioned at SMALL angles, which is
   the whole point here -- structural tilt is a few degrees, not ninety.
   A dot product loses precision exactly where this needs it: near zero,
   cos(theta) is flat, so a one-count error in the dot swamps the angle.

   Returns TILT_INVALID if either vector is too short to have a
   direction. */
static uint16_t angleBetween(int16_t ax, int16_t ay, int16_t az,
                             int16_t bx, int16_t by, int16_t bz)
{
  uint32_t ma2 = (uint32_t)((int32_t)ax * ax) +
                 (uint32_t)((int32_t)ay * ay) +
                 (uint32_t)((int32_t)az * az);
  uint32_t mb2 = (uint32_t)((int32_t)bx * bx) +
                 (uint32_t)((int32_t)by * by) +
                 (uint32_t)((int32_t)bz * bz);

  uint16_t ma = isqrt32(ma2);
  uint16_t mb = isqrt32(mb2);
  if (ma < 32 || mb < 32) return TILT_INVALID;   /* no usable direction */

  /* Normalise to 1024. Divides are fine here: this is loop() context,
     at most once a second, and never the ISR. */
  int16_t nax = (int16_t)(((int32_t)ax * 1024L) / ma);
  int16_t nay = (int16_t)(((int32_t)ay * 1024L) / ma);
  int16_t naz = (int16_t)(((int32_t)az * 1024L) / ma);
  int16_t nbx = (int16_t)(((int32_t)bx * 1024L) / mb);
  int16_t nby = (int16_t)(((int32_t)by * 1024L) / mb);
  int16_t nbz = (int16_t)(((int32_t)bz * 1024L) / mb);

  /* The chord is well conditioned for SMALL angles and badly conditioned
     near 180 degrees, where sin(theta/2) flattens out against 90 -- the
     exact mirror of the dot-product form's weakness. So past 90 degrees,
     measure to the OPPOSITE of the reference instead and subtract from
     180. Both halves of the range are then computed in the half where
     the chord is sharp. Without this, error near 175 degrees is ~4.8
     degrees; with it, the whole range holds under 0.2.

     The switch is at half > 724, which is 1024*sin(45) -- theta = 90. */
  uint8_t  supplement = 0;
  int32_t dx = (int32_t)nax - nbx;
  int32_t dy = (int32_t)nay - nby;
  int32_t dz = (int32_t)naz - nbz;
  uint32_t chord2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy) +
                    (uint32_t)(dz * dz);
  uint32_t half = isqrt32(chord2) >> 1;          /* 1024 * sin(theta/2) */

  if (half > 724U) {
    supplement = 1;
    dx = (int32_t)nax + nbx;                     /* distance to -b */
    dy = (int32_t)nay + nby;
    dz = (int32_t)naz + nbz;
    chord2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy) + (uint32_t)(dz * dz);
    half = isqrt32(chord2) >> 1;
  }
  if (half > 1024U) half = 1024U;

  /* Search the table for the bracketing degree, then interpolate. The
     table is monotonic, so a linear scan of 91 entries is bounded and
     obvious; a binary search would save microseconds nobody is short
     of at 1 Hz. */
  uint8_t d = 0;
  while (d < 90 && (uint32_t)pgm_read_word(&tiltSinTable[d + 1]) <= half) d++;

  /* Interpolate straight into tenths of the FULL angle, not tenths of
     the half angle. Scaling by 20 rather than 10 is what makes odd
     tenths reachable at all -- doubling a half-angle in tenths can only
     ever produce even ones, which was costing a tenth of a degree for
     nothing. */
  uint32_t lo = pgm_read_word(&tiltSinTable[d]);
  uint32_t frac = 0;
  if (d < 90) {
    uint32_t hi = pgm_read_word(&tiltSinTable[d + 1]);
    if (hi > lo) frac = ((half - lo) * 20UL) / (hi - lo);   /* 0..20 */
  }
  uint16_t ang = (uint16_t)(((uint32_t)d * 20UL) + frac);
  return supplement ? (uint16_t)(1800U - ang) : ang;
}

/* Called every loop(). Does almost nothing most of the time: the gate
   is cheap and the maths only runs at 1 Hz once genuinely at rest. */
static void updateTilt()
{
  unsigned long now = millis();

  /* Motion gate. r1msV is the 1 s AC-coupled vector RMS -- the same
     quantity register 41 publishes -- which sits at a few mg at rest
     and lifts immediately on any real movement. Using the 1 s figure
     rather than an instantaneous one is deliberate: a single quiet
     sample during motion must not look like rest. */
  if (countsToMg(r1msV) > TILT_REST_MG) {
    tiltStillSinceMs = now;      /* restart the stillness timer */
    tiltAtRest = 0;
    return;                      /* angle HOLDS its last value */
  }

  if (now - tiltStillSinceMs < TILT_REST_MS) {
    tiltAtRest = 0;              /* still enough, but not for long enough */
    return;
  }
  tiltAtRest = 1;

  if (now - tiltLastUpdateMs < TILT_UPDATE_MS) return;
  tiltLastUpdateMs = now;

  if (tiltRefX == 0 && tiltRefY == 0 && tiltRefZ == 0) {
    tiltAngle = TILT_INVALID;    /* no reference to measure against */
    return;
  }

  int16_t gx, gy, gz;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { gx = b_dcX; gy = b_dcY; gz = b_dcZ; }

  tiltAngle = angleBetween(gx, gy, gz, tiltRefX, tiltRefY, tiltRefZ);
}

/* Capture the current gravity vector as the reference.

   REFUSED unless the device is at rest by the same test the angle uses.
   A reference taken while moving is a wrong baseline that every later
   reading is measured against, so it is worth refusing rather than
   silently storing rubbish. */
static bool setTiltReference()
{
  if (!tiltAtRest) return false;

  int16_t gx, gy, gz;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { gx = b_dcX; gy = b_dcY; gz = b_dcZ; }

  /* A zero vector would read back as "no reference". It cannot happen
     with a working sensor, but refuse it rather than store a state that
     means something else. */
  if (gx == 0 && gy == 0 && gz == 0) return false;

  tiltRefX = gx; tiltRefY = gy; tiltRefZ = gz;
  cfg.tiltRefX = gx; cfg.tiltRefY = gy; cfg.tiltRefZ = gz;
  writeConfig();
  tiltAngle = 0;                 /* by definition, zero from itself */
  return true;
}

/* Supply measurement. NON-BLOCKING by construction: invariant 8 forbids
   blocking delays, and a 13-cycle conversion at 125 kHz is ~104 us that
   there is no reason to sit and wait for.

   The ADC is set up once and left alone. ADMUX never changes after that,
   so there is no reference-settling penalty on any conversion but the
   first few, and those are discarded. Each pass either starts a
   conversion or collects one; it never waits for one.

   WHAT THIS IS AND IS NOT. It is a real measurement of the rail the
   controller is running on, which is what you want for logging and for
   finding a supply that sags under load. It is NOT accurate in absolute
   terms: the bandgap is untrimmed and spec'd 1.0-1.2 V, so the reading
   can be out by several percent unit to unit. Trend it, compare a unit
   against itself, and calibrate per unit if an absolute number ever
   matters. See docs/REGISTER_MAP_CTX311.md.

   It also cannot see a FAST sag. At 1 Hz a droop lasting milliseconds --
   a relay pulling in, an inrush -- is invisible. That is what the
   brown-out detector in register 29 catches, in hardware, and the two
   are complementary rather than redundant. */
static void updateVcc()
{
  if (!vccStarted) {
    /* AVcc as reference, channel 14 = the 1.1 V bandgap. No pin is
       involved, so nothing else on the board is disturbed. */
    ADMUX  = (1 << REFS0) | 0x0E;
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
    ADCSRA |= (1 << ADSC);
    vccStarted = 1;
    return;
  }

  if (ADCSRA & (1 << ADSC)) return;      /* still converting */

  uint16_t raw = ADC;

  if (vccDiscard) {                      /* bandgap still settling */
    vccDiscard--;
    ADCSRA |= (1 << ADSC);
    return;
  }

  if (raw != 0) {
    uint32_t mv = VCC_SCALE_MV / raw;
    vccMv = (mv > 65534UL) ? 65534U : (uint16_t)mv;
    /* The minimum is the point of this. A supply that is fine when you
       poll it and dips under load looks healthy in reg 69 and shows up
       here. */
    if (vccMinMv == VCC_INVALID || vccMv < vccMinMv) vccMinMv = vccMv;
  }

  /* Pace the next one. Between ticks the ADC simply sits idle. */
  unsigned long now = millis();
  if (now - vccLastMs >= VCC_UPDATE_MS) {
    vccLastMs = now;
    ADCSRA |= (1 << ADSC);
  }
}

/* ==================================================================== */
/*  register publication                                                */
/* ==================================================================== */
/* Runs from sensorData(), i.e. inside the function-3 handler, so this
   is READ-TIME code. It publishes measurements and read-only echoes
   ONLY. Writable registers (21, 22, 28) are owned by the check
   functions in loop context and must not be touched here. Regs 35 and
   48 are reserved and pinned to 0 here -- they are no longer writable,
   so a stray write must not be able to make them read back non-zero. */

static void publishBlock()
{
  uint16_t rx, ry, rz, px, py, pz, nx, ny, nz, sc;
  uint8_t  latched, isTripped;
  uint16_t ph, tc, bm, im;
  int16_t  dx, dy, dz;
  uint32_t pm2, im2;
  uint32_t ms2X, ms2Y, ms2Z, vec2, pkVec2;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    latched   = impactLatched;
    isTripped = tripped;
    dx = b_dcX; dy = b_dcY; dz = b_dcZ;

    if (latched) {                 /* freeze the impact snapshot */
      ms2X = i_ms2X; ms2Y = i_ms2Y; ms2Z = i_ms2Z;
      px = i_pkPX; py = i_pkPY; pz = i_pkPZ;
      nx = i_pkNX; ny = i_pkNY; nz = i_pkNZ;
      vec2 = impactMaxVec2;
    } else {                       /* live block */
      ms2X = b_ms2X; ms2Y = b_ms2Y; ms2Z = b_ms2Z;
      px = b_pkPX; py = b_pkPY; pz = b_pkPZ;
      nx = b_pkNX; ny = b_pkNY; nz = b_pkNZ;
      vec2 = b_vec2;
    }

    pkVec2 = peakVec2Hold;
    pm2 = peakMag2Hold;
    im2 = impactMag2;
    tc  = tripCount;
    bm  = blockMissed;
    im  = isrMaxTicks;
    blockReady = 0;
  }

  publishedImpactMag2 = im2;     /* what the master is about to read */

  /* The square roots live HERE, outside the ISR. Six isqrt32 calls at
     ~57 us is ~340 us of loop time, against a Modbus read that already
     takes ~126 ms -- it does not matter here, and it was the single
     biggest thing in the ISR.                                        */
  rx = isqrt32(ms2X);
  ry = isqrt32(ms2Y);
  rz = isqrt32(ms2Z);
  sc = isqrt32(vec2);
  ph = isqrt32(pkVec2);

  holdingRegs[XaxisRMS] = countsToMg(rx);
  holdingRegs[YaxisRMS] = countsToMg(ry);
  holdingRegs[ZaxisRMS] = countsToMg(rz);

  holdingRegs[XaxismodPosP] = countsToMg(px);
  holdingRegs[YaxismodPosP] = countsToMg(py);
  holdingRegs[ZaxismodPosP] = countsToMg(pz);

  holdingRegs[XaxismodNegP] = countsToMg(nx);
  holdingRegs[YaxismodNegP] = countsToMg(ny);
  holdingRegs[ZaxismodNegP] = countsToMg(nz);

  holdingRegs[XaxisRMS_m_s] = countsToCms2(rx);
  holdingRegs[YaxisRMS_m_s] = countsToCms2(ry);
  holdingRegs[ZaxisRMS_m_s] = countsToCms2(rz);

  holdingRegs[XaxismodPosP_m_s] = countsToCms2(px);
  holdingRegs[YaxismodPosP_m_s] = countsToCms2(py);
  holdingRegs[ZaxismodPosP_m_s] = countsToCms2(pz);

  holdingRegs[XaxismodNegP_m_s] = countsToCms2(nx);
  holdingRegs[YaxismodNegP_m_s] = countsToCms2(ny);
  holdingRegs[ZaxismodNegP_m_s] = countsToCms2(nz);

  holdingRegs[OutputState]        = isTripped ? 0 : 1;
  holdingRegs[SumReg]             = countsToMg(sc);
  holdingRegs[ThresholdEffReg]    = thresholdMg;
  /* reserved holes: pinned to 0 on every read so a stray write from a
     map-7 master cannot stick and look like an armed trip */
  holdingRegs[Rsvd35Reg]          = 0;
#ifdef CTX311_STACK_DEBUG
  /* DEBUG BUILD ONLY -- reg 48 carries minimum free SRAM in bytes
     instead of 0. See the stack instrumentation block above. A release
     image must publish 0 here. */
  holdingRegs[Rsvd48Reg]          = stackUnusedBytes();
#else
  holdingRegs[Rsvd48Reg]          = 0;
#endif
  holdingRegs[PeakSumHoldReg]     = countsToMg(ph);
  holdingRegs[TripCountReg]       = tc;
  holdingRegs[BlockMissedReg]     = bm;
  holdingRegs[looptime]           = im >> 1;   /* ticks -> microseconds */

  /* two's-complement pattern; master reads regs 32-34 as SIGNED */
  holdingRegs[DcXReg] = (unsigned int)(uint16_t)countsToMgSigned(dx);
  holdingRegs[DcYReg] = (unsigned int)(uint16_t)countsToMgSigned(dy);
  holdingRegs[DcZReg] = (unsigned int)(uint16_t)countsToMgSigned(dz);

  holdingRegs[PeakMagHoldReg] = countsToMg(isqrt32(pm2));
  holdingRegs[ImpactMagReg]   = countsToMg(isqrt32(im2));

  /* loop-owned; computed in update1sRms() */
  holdingRegs[Rms1sXReg]   = countsToMg(r1msX);
  holdingRegs[Rms1sYReg]   = countsToMg(r1msY);
  holdingRegs[Rms1sZReg]   = countsToMg(r1msZ);
  holdingRegs[Rms1sVecReg] = countsToMg(r1msV);

  uint16_t st = 0;
  if (isTripped)       st |= 0x01;
  if (latched)         st |= 0x02;
  /* bit 2 (blocks missed) REMOVED in map 9. It re-latched within
     seconds of any clear -- blocks are missed on every poll by
     construction -- so on a dashboard it was a permanent fault light
     for entirely normal behaviour. Reg 26 is still the counter.    */
  if (cfgWasDefaulted) st |= 0x08;
  holdingRegs[StatusReg] = st;

  /* ---------------- CTX311 registers, map version 9 --------------- */
  uint8_t  lLat, lAct, lFault;
  uint16_t lCount, lRun, sCount;
  uint32_t lMin2, rawLive;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    lLat   = losLatched;
    lAct   = losActive;
    lFault = losByFault;
    lCount = losTripCount;
    lRun   = losRunSamples;
    lMin2  = losMinMag2;
    rawLive = rawMag2Live;
    sCount = settleCount;
  }

  uint16_t minMg = (lMin2 == 0xFFFFFFFFUL) ? 0
                                           : countsToMg(isqrt32(lMin2));
  /* samples -> ms using the SAME fixed ODR the trip test uses, so the
     reported duration is consistent with the configured trip time. */
  uint32_t durMs = ((uint32_t)lRun * 1000UL) / LOS_ODR_HZ;
  if (durMs > 65535UL) durMs = 65535UL;

  /* Height is only meaningful if the assembly actually reached
     something close to free fall. A partial slip that never went
     below ~300 mg was arrested or guided the whole way down, and
     h = 1/2 g t^2 would badly overstate it. Report invalid instead
     of reporting a confident wrong number.                        */
  uint16_t heightCm = 0xFFFFU;
  bool reachedFreeFall = (minMg != 0) && (minMg < 300);
  if (reachedFreeFall && durMs > 0) {
    /* h(cm) = 0.5 * 981 * t^2, t in s  ->  (490.5 * ms^2) / 1e6 */
    uint32_t h = ((uint32_t)durMs * durMs) / 2039UL;   /* 1e6/490.5 */
    heightCm = (h > 65534UL) ? 65534U : (uint16_t)h;
  }

  uint16_t lst = 0;
  if (lLat)                        lst |= 0x01;
  if (lAct)                        lst |= 0x02;
  if (latched)                     lst |= 0x04;  /* impact followed */
  if (reachedFreeFall)             lst |= 0x08;
  if (holdingRegs[PeakMagHoldReg] >= 26000U) lst |= 0x10;  /* clipped */
  if (lFault)                      lst |= 0x20;
  /* bit6 says the detector is NOT yet armed. It is the positive form of
     what PC2 going open during settle means, so a master that polls
     rather than watching the pin can tell "still arming" from "faulted"
     -- both open the health output, but only one of them is a defect. */
  if (sCount < SETTLE_SAMPLES)     lst |= 0x40;

  holdingRegs[LosStatusReg]       = lst;
  holdingRegs[LosThresholdEffReg] = losThresholdMg;
  holdingRegs[LosTimeMsEffReg]    = losTimeMs;
  holdingRegs[LosCountReg]        = lCount;
  holdingRegs[LastLosDurationReg] = (unsigned int)durMs;
  holdingRegs[LastLosMinMagReg]   = minMg;
  holdingRegs[LastLosHeightReg]   = heightCm;
  holdingRegs[LastLosImpactReg]   = holdingRegs[ImpactMagReg];
  holdingRegs[RawMagReg]          = countsToMg(isqrt32(rawLive));
  holdingRegs[FaultReg]           = faultFlags;
  holdingRegs[BootCheckReg]       = bootState;

  {
    uint8_t ts = 0;
    if (!(tiltRefX == 0 && tiltRefY == 0 && tiltRefZ == 0)) ts |= TILT_ST_REF_SET;
    if (tiltAtRest)                  ts |= TILT_ST_AT_REST;
    if (tiltAngle != TILT_INVALID)   ts |= TILT_ST_VALID;
    holdingRegs[TiltAngleReg]  = tiltAngle;
    holdingRegs[TiltStatusReg] = ts;
    holdingRegs[TiltRefXReg] = (unsigned int)(uint16_t)countsToMgSigned(tiltRefX);
    holdingRegs[TiltRefYReg] = (unsigned int)(uint16_t)countsToMgSigned(tiltRefY);
    holdingRegs[TiltRefZReg] = (unsigned int)(uint16_t)countsToMgSigned(tiltRefZ);
  }

  holdingRegs[VccReg]    = vccMv;
  holdingRegs[VccMinReg] = vccMinMv;
}

/* 1-second RMS. Runs in loop context: the three 64-bit divisions cost
   ~500 us, which is fine here but would overrun a 625 us sample period
   if left in the ISR (rev E did exactly that).                        */
static void update1sRms()
{
  uint8_t  ready;
  uint64_t ax_, ay_, az_;

  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    ready = sec1Ready;
    if (ready) {
      ax_ = s1AccX; ay_ = s1AccY; az_ = s1AccZ;
      sec1Ready = 0;
    }
  }
  if (!ready) return;

  r1msX = isqrt32((uint32_t)(ax_ / BLOCKS_PER_SEC));
  r1msY = isqrt32((uint32_t)(ay_ / BLOCKS_PER_SEC));
  r1msZ = isqrt32((uint32_t)(az_ / BLOCKS_PER_SEC));
  /* from the accumulators, not from the three rounded roots -- same
     identity used at the block boundary, and it avoids the double
     rounding that biased the old form low by a count or two.        */
  r1msV = isqrt32((uint32_t)((ax_ + ay_ + az_) / BLOCKS_PER_SEC));
}

/* Called by SimpleModbusSlave from inside its function-3 handler, so
   registers are refreshed at read time.                               */
void sensorData()
{
  publishBlock();
}

/* Called by the patched SimpleModbusSlave AFTER the response frame has
   been transmitted. Reg 37 is read-to-clear.
   Compare-and-clear: if the ISR stored a NEW impact between publication
   and this call, impactMag2 will differ from what was sent, and we must
   not wipe it -- it goes out on the next read instead.                */
void modbus_read_complete(unsigned int startAddress, unsigned int quantity)
{
  if (ImpactMagReg < startAddress ||
      ImpactMagReg >= (startAddress + quantity)) return;

  uint8_t cleared = 0;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (impactMag2 == publishedImpactMag2) {
      impactMag2 = 0;
      cleared = 1;
    }
  }
  if (cleared) holdingRegs[ImpactMagReg] = 0;
}

/* ==================================================================== */
/*  setup / loop                                                        */
/* ==================================================================== */

void setup()
{
  resetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();

  Serial.begin(baud);

  /* All three outputs driven. The two trip outputs start SAFE.
     Note what this cannot do: on power loss these pins go high
     impedance, not low. The fail-safe direction depends on an
     external pull-down and on the arrest device engaging when
     de-energised. See the header -- confirm on hardware.

     HEALTH starts OPEN, not healthy. settleCount is zero here, so the
     detector is not armed and runDiagnostics() would open PC2 on its
     first pass anyway. Driving it HIGH first would put a brief
     "healthy" pulse on the pin during the one window in which the
     device is provably not protecting anything -- short, but long
     enough for a PLC scan to sample it and latch a start permit. */
  DDRC  |= (1 << OUTPUT_PORT_BIT) | (1 << LOS_PORT_BIT) |
           (1 << HEALTH_PORT_BIT);
  PORTC |=  (1 << OUTPUT_PORT_BIT) | (1 << LOS_PORT_BIT);
  PORTC &= ~(1 << HEALTH_PORT_BIT);

  loadConfig();

  modbus_configure(&Serial, baud, SERIAL_8N1, currentSlaveId,
                   TxEnablePin, HOLDING_REGS_SIZE, holdingRegs);

  holdingRegs[SlaveIDReg]         = currentSlaveId;
  holdingRegs[ThresholdReg]       = thresholdMg;
  holdingRegs[ThresholdEffReg]    = thresholdMg;
  holdingRegs[Rsvd35Reg]          = 0;
  holdingRegs[Rsvd48Reg]          = 0;
  holdingRegs[TiltAngleReg]       = TILT_INVALID;
  holdingRegs[TiltStatusReg]      = 0;
  holdingRegs[VccReg]             = VCC_INVALID;
  holdingRegs[VccMinReg]          = VCC_INVALID;
  holdingRegs[CommandReg]         = 0;
  holdingRegs[LastCommandReg]     = 0;
  holdingRegs[CommandStatusReg]   = CMD_STATUS_IDLE;
  holdingRegs[CommandCountReg]    = 0;
  holdingRegs[ResetCauseReg]      = resetCause;

  holdingRegs[LosThresholdReg]    = losThresholdMg;
  holdingRegs[LosThresholdEffReg] = losThresholdMg;
  holdingRegs[LosTimeMsReg]       = losTimeMs;
  holdingRegs[LosTimeMsEffReg]    = losTimeMs;
  holdingRegs[LosHoldMsReg]       = (unsigned int)losHoldMs;
  holdingRegs[LosFaultActionReg]  = losFaultAction;
  holdingRegs[LosStatusReg]       = 0;
  holdingRegs[BootCheckReg]       = BOOT_PENDING;

  /* WDRF in MCUSR means the previous run was killed by the watchdog.
     Sticky: it survives until CLEAR_FAULTS, because a device that
     silently reboots under an arrest device is something an operator
     must be told about, not something that scrolls past.          */
  if (resetCause & (1 << 3)) faultFlags |= FAULT_WDT_RESET;

  /* Identification: written once, never touched again. A master that
     gets an ILLEGAL DATA ADDRESS exception reading reg 42-44 is talking
     to firmware older than rev F.                                     */
  holdingRegs[FwVersionReg]  = FW_VERSION_PACKED;
  holdingRegs[MapVersionReg] = REGISTER_MAP_VERSION;
  holdingRegs[BuildDateReg]  = BUILD_DATE_PACKED;

  /* Timer1 free-running, used only to time the ISR. micros() cannot be
     trusted inside an ISR because the Timer0 overflow handler is blocked
     while interrupts are disabled. clk/8 -> 0.5 us per tick, wraps every
     32.77 ms. Timer1 is otherwise unused here (core uses Timer0 for
     millis and Timer2 for tone; Servo is not used).                   */
  TCCR1A = 0;
  TCCR1B = (1 << CS11);
  TIMSK1 = 0;
  TCNT1  = 0;

  SPI.begin();
  SPI.setDataMode(SPI_MODE3);               /* CPOL=1, CPHA=1 */
  SPI.setClockDivider(SPI_CLOCK_DIV4);      /* 4 MHz; >=2 MHz required
                                               at 1600 Hz, max 5 MHz  */

  adxl.powerOn();
  adxl.setRangeSetting(16);                 /* headroom above the
                                               +/-15 g spec figure    */
  adxl.setSpiBit(0);                        /* 4-wire SPI */
  adxl.set_bw(ADXL345_BW_800);              /* 1600 Hz ODR -> 800 Hz BW,
                                               matching the datasheet */
  adxl.setFullResBit(1);                    /* 3.9 mg/LSB (LSB pinned
                                               to 0 at this ODR ->
                                               7.8 mg step)           */

  adxl.setInterrupt(ADXL345_INT_DATA_READY_BIT, true);
  adxl.setInterruptMapping(ADXL345_INT_DATA_READY_BIT, ADXL345_INT1_PIN);
  adxl.getInterruptSource();                /* clear stale latch once */

  pinMode(ADXL_INT1_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(ADXL_INT1_PIN), myHandler, RISING);

  /* Rev H included <avr/wdt.h> and then called wdt_disable(), so a
     hang left the interlock wherever it happened to be. Under an
     arrest device that is not acceptable.

     500 ms, not shorter: sendPacket() blocks through flush(), and a
     full 64-register response at 9600 baud occupies the line for
     ~140 ms. A 250 ms watchdog would be at risk of firing during a
     legitimate long poll. 500 ms leaves ~3.5x margin.            */
  wdt_enable(WDTO_500MS);
}

void loop()
{
  static unsigned long lastRateMs = 0;
  unsigned long        loopT0     = micros();

  wdt_reset();

  updateOutput();
  updateLosOutput();
  bootCheck();

  update1sRms();
  updateTilt();
  updateVcc();

  uint8_t ready;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ready = blockReady; }
  if (ready) publishBlock();

  checkModbusSlaveIdUpdate();
  checkThresholdUpdate();
  checkLosSettingsUpdate();
  checkCommandRegister();

  /* Measure against ACTUAL elapsed time. Rev E assumed a 1000 ms window,
     so loop overshoot inflated the figure (observed 1650 for ~1600).  */
  unsigned long nowMs = millis();
  if (nowMs - lastRateMs >= 1000) {
    uint16_t c;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { c = isrCount; isrCount = 0; }
    unsigned long elapsed = nowMs - lastRateMs;
    lastRateMs = nowMs;
    uint32_t rate = ((uint32_t)c * 1000UL) / elapsed;
    holdingRegs[SampleRateReg] = (rate > 65535UL) ? 65535U : (unsigned int)rate;

    /* Diagnostics ride the same 1 Hz tick, because the rate check --
       the one that catches a dead sensor -- needs exactly this
       measurement. Worst-case detection of a dead part is therefore
       ~1 s, which is the figure to quote, not "immediate".        */
    runDiagnostics((rate > 65535UL) ? 65535U : (uint16_t)rate);
  }

  modbus_update();

  /* uint32: a 49-register read at 9600 baud is ~130 ms, which would
     wrap a uint16 microsecond counter. Reported in 100 us units.     */
  uint32_t dt = micros() - loopT0;
  if (dt > maxLoopUs) {
    maxLoopUs = dt;
    uint32_t u = maxLoopUs / 100UL;
    holdingRegs[Modbuslooptime] = (u > 65535UL) ? 65535U : (unsigned int)u;
  }
}
