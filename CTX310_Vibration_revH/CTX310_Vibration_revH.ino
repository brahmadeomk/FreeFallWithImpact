/* =====================================================================
   VRM315 / CTX 310  --  Triaxial Impact & Vibration Monitor
   Revision H  -  1600 Hz ODR, 0.63 ms impact detection

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
#include "SimpleModbusSlave.h"
#include "SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp"

/* ------------------------- configuration --------------------------- */
#define OUTPUT_PORT_BIT   PC0
#define TxEnablePin       3
#define ADXL_INT1_PIN     2
#define ADXL_CS_PIN       4
#define baud              9600

#define DEFAULT_SLAVE_ID     71
#define DEFAULT_THRESHOLD_MG 3000
#define THRESHOLD_MIN_MG     10
#define THRESHOLD_MAX_MG     15000     /* +/-15 g datasheet figure */
#define TRIP_HOLD_MS         3000UL

#define ADXL345_INT_DATA_READY_BIT 7

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
#define FW_VERSION_MAJOR      1
#define FW_VERSION_MINOR      8          /* revision H */
#define FW_VERSION_PACKED     (((FW_VERSION_MAJOR) << 8) | (FW_VERSION_MINOR))
#define REGISTER_MAP_VERSION  8

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
                              bit2 block missed, bit3 cfg defaulted */
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

/* --------------------------- diagnostics ---------------------------- */
volatile uint16_t isrMaxTicks = 0;   /* Timer1 ticks, 0.5 us each */
volatile uint16_t isrCount     = 0;
static   uint32_t maxLoopUs    = 0;    /* uint32: a full read is ~104 ms */

/* ---------------------- threshold (loop -> ISR) --------------------- */
volatile uint16_t thresholdCounts = 0;
volatile uint32_t thresholdSq     = 0;

/* ----------------------------- config ------------------------------- */
const uint16_t CONFIG_MAGIC = 0xA5B2;
const int      EEPROM_ADDR  = 0;

struct Config {
  uint16_t magic;
  uint8_t  slaveId;
  uint16_t thresholdMg;
};

Config   cfg;
uint8_t  currentSlaveId  = DEFAULT_SLAVE_ID;
uint16_t thresholdMg     = DEFAULT_THRESHOLD_MG;
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

/* ==================================================================== */
/*  EEPROM configuration                                                */
/* ==================================================================== */

void writeConfig()
{
  EEPROM.update(EEPROM_ADDR + 0, lowByte(CONFIG_MAGIC));
  EEPROM.update(EEPROM_ADDR + 1, highByte(CONFIG_MAGIC));
  EEPROM.update(EEPROM_ADDR + 2, cfg.slaveId);
  EEPROM.update(EEPROM_ADDR + 5, lowByte(cfg.thresholdMg));
  EEPROM.update(EEPROM_ADDR + 6, highByte(cfg.thresholdMg));
}

void applyDefaults()
{
  cfg.magic          = CONFIG_MAGIC;
  cfg.slaveId        = DEFAULT_SLAVE_ID;
  cfg.thresholdMg    = DEFAULT_THRESHOLD_MG;
  writeConfig();
  currentSlaveId = cfg.slaveId;
  thresholdMg    = cfg.thresholdMg;
  recomputeThresholdCounts();
}

void loadConfig()
{
  cfg.magic       = (uint16_t)EEPROM.read(EEPROM_ADDR + 0) |
                    ((uint16_t)EEPROM.read(EEPROM_ADDR + 1) << 8);
  cfg.slaveId     = EEPROM.read(EEPROM_ADDR + 2);
  cfg.thresholdMg = (uint16_t)EEPROM.read(EEPROM_ADDR + 5) |
                    ((uint16_t)EEPROM.read(EEPROM_ADDR + 6) << 8);

  bool bad = (cfg.magic != CONFIG_MAGIC) ||
             (cfg.slaveId < 1) || (cfg.slaveId > 247) ||
             (cfg.thresholdMg < THRESHOLD_MIN_MG) ||
             (cfg.thresholdMg > THRESHOLD_MAX_MG);

  if (bad) {
    cfgWasDefaulted = true;
    applyDefaults();
  } else {
    currentSlaveId = cfg.slaveId;
    thresholdMg    = cfg.thresholdMg;
    recomputeThresholdCounts();
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
      holdingRegs[SlaveIDReg]      = currentSlaveId;
      holdingRegs[ThresholdReg]    = thresholdMg;
      /* identification registers are compile-time constants and are
         deliberately NOT cleared by a factory reset */
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
  holdingRegs[Rsvd48Reg]          = 0;
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
  if (bm > 0)          st |= 0x04;
  if (cfgWasDefaulted) st |= 0x08;
  holdingRegs[StatusReg] = st;
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

  DDRC  |= (1 << OUTPUT_PORT_BIT);
  PORTC |= (1 << OUTPUT_PORT_BIT);          /* default CLOSED */

  loadConfig();

  modbus_configure(&Serial, baud, SERIAL_8N1, currentSlaveId,
                   TxEnablePin, HOLDING_REGS_SIZE, holdingRegs);

  holdingRegs[SlaveIDReg]         = currentSlaveId;
  holdingRegs[ThresholdReg]       = thresholdMg;
  holdingRegs[ThresholdEffReg]    = thresholdMg;
  holdingRegs[Rsvd35Reg]          = 0;
  holdingRegs[Rsvd48Reg]          = 0;
  holdingRegs[CommandReg]         = 0;
  holdingRegs[LastCommandReg]     = 0;
  holdingRegs[CommandStatusReg]   = CMD_STATUS_IDLE;
  holdingRegs[CommandCountReg]    = 0;
  holdingRegs[ResetCauseReg]      = resetCause;

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
}

void loop()
{
  static unsigned long lastRateMs = 0;
  unsigned long        loopT0     = micros();

  updateOutput();

  update1sRms();

  uint8_t ready;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { ready = blockReady; }
  if (ready) publishBlock();

  checkModbusSlaveIdUpdate();
  checkThresholdUpdate();
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
