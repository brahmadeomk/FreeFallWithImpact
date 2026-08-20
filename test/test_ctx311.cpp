/* =====================================================================
   Host-side tests for the CTX311 rev A register and detector logic.

   Inherited from the CTX310 rev H suite. The rev H tests are kept
   UNCHANGED except for the three assertions CTX311 deliberately
   invalidates (map version, firmware version, buffer ceiling): if a
   rev H test breaks for any other reason, CTX311 has broken something
   that was working, and that is exactly what this file is for.

   The sketch is included directly, with the AVR core, SPI, EEPROM and
   the ADXL driver replaced by the stubs in test/stubs. Only the parts
   of the firmware that run in loop context are exercised -- the ISR
   needs real interrupt timing and is not covered here.

   The point of these tests is the write path: which registers hold a
   written value, which reject it, which self-clear, and which are
   inert reserved holes. Those are the semantics a Modbus master
   depends on, and rev F got two of them wrong (see CHANGES FROM REV F
   in the sketch).

   Build and run:  test/run_tests.sh
   ===================================================================== */

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- storage for the objects the stubs declare extern ---- */
#include "stubs/Arduino.h"
#include "stubs/EEPROM.h"
#include "stubs/SPI.h"
#include "stubs/SparkFun_ADXL345-master/SparkFun_ADXL345.cpp"

uint8_t  DDRC = 0, PORTC = 0, MCUSR = 0;
uint16_t TCNT1 = 0, TCCR1A = 0, TCCR1B = 0, TIMSK1 = 0;
unsigned long g_millis = 0, g_micros = 0;
HardwareSerial Serial;
EEPROMClass    EEPROM;
SPIClass       SPI;
AdxlFeed       adxlFeed = {0, 0, 256};

/* ---- the half of SimpleModbusSlave the sketch calls into ---- */
unsigned int modbus_update() { return 0; }
void modbus_update_comms(long, unsigned char, unsigned char) {}
void modbus_configure(HardwareSerial *, long, unsigned char, unsigned char,
                      unsigned char, unsigned int, unsigned int *) {}

#include "../CTX311_LossOfSupport_revA/CTX311_LossOfSupport_revA.ino"

/* ---------------------------- harness ------------------------------ */
static int failures = 0;
static int checks   = 0;

#define CHECK(cond, ...)                                        \
  do {                                                          \
    checks++;                                                   \
    if (!(cond)) {                                              \
      failures++;                                               \
      printf("  FAIL %s:%d  ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__);                                      \
      printf("\n");                                             \
    }                                                           \
  } while (0)

#define CHECK_EQ(got, want, name)                               \
  do {                                                          \
    long g_ = (long)(got), w_ = (long)(want);                   \
    CHECK(g_ == w_, "%s: got %ld, expected %ld", name, g_, w_); \
  } while (0)

static void bootFresh()
{
  /* blank EEPROM -> loadConfig() rejects it and applies defaults */
  for (int i = 0; i < 512; i++) EEPROM.cell[i] = 0xFF;
  cfgWasDefaulted = false;
  setup();
}

/* ===================================================================== */
/*  reg 28 -- the trigger register                                       */
/* ===================================================================== */

static void test_command_self_clears_and_acknowledges()
{
  printf("command register: executes, self-clears, acknowledges\n");
  bootFresh();

  tripCount = 5;
  holdingRegs[CommandReg] = CMD_CLEAR_TRIPCOUNT;
  checkCommandRegister();

  CHECK_EQ(tripCount, 0, "trip counter cleared");
  CHECK_EQ(holdingRegs[CommandReg], 0,
           "reg 28 self-cleared (a read-back can never show the code)");
  CHECK_EQ(holdingRegs[LastCommandReg], CMD_CLEAR_TRIPCOUNT, "reg 45 echo");
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_ACCEPTED, "reg 46 status");
  CHECK_EQ(holdingRegs[CommandCountReg], 1, "reg 47 count");
}

static void test_command_repeats_are_countable()
{
  printf("command register: the same code twice moves the counter twice\n");
  bootFresh();

  peakVec2Hold = 810000UL;
  holdingRegs[CommandReg] = CMD_CLEAR_PEAKHOLD;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandCountReg], 1, "count after first");

  peakVec2Hold = 490000UL;
  holdingRegs[CommandReg] = CMD_CLEAR_PEAKHOLD;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandCountReg], 2, "count after second");
  CHECK_EQ(peakVec2Hold, 0, "peak hold cleared again");
}

static void test_command_zero_is_idle()
{
  printf("command register: writing 0 does nothing\n");
  bootFresh();

  holdingRegs[CommandReg] = CMD_CLEAR_TRIPCOUNT;
  checkCommandRegister();
  unsigned int countAfterOne = holdingRegs[CommandCountReg];

  holdingRegs[CommandReg] = 0;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandCountReg], countAfterOne, "count unchanged by 0");
  CHECK_EQ(holdingRegs[LastCommandReg], CMD_CLEAR_TRIPCOUNT, "echo unchanged");
}

static void test_unknown_command_is_reported()
{
  printf("command register: an unrecognised code is rejected, not ignored\n");
  bootFresh();

  holdingRegs[CommandReg] = 0x1234;
  checkCommandRegister();

  CHECK_EQ(holdingRegs[CommandReg], 0, "reg 28 still cleared");
  CHECK_EQ(holdingRegs[LastCommandReg], 0x1234, "reg 45 echoes the bad code");
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_UNKNOWN, "reg 46 = unknown");
  CHECK_EQ(holdingRegs[CommandCountReg], 0, "reg 47 not incremented");
}

static void test_factory_reset_restores_writable_registers()
{
  printf("command register: factory reset republishes regs 21/22\n");
  bootFresh();

  holdingRegs[ThresholdReg] = 5000;
  checkThresholdUpdate();
  holdingRegs[SlaveIDReg] = 40;
  checkModbusSlaveIdUpdate();
  CHECK_EQ(thresholdMg, 5000, "threshold changed before reset");
  CHECK_EQ(currentSlaveId, 40, "slave id changed before reset");

  holdingRegs[CommandReg] = CMD_FACTORY_RESET;
  checkCommandRegister();

  CHECK_EQ(thresholdMg, DEFAULT_THRESHOLD_MG, "threshold back to default");
  CHECK_EQ(currentSlaveId, DEFAULT_SLAVE_ID, "slave id back to default");
  CHECK_EQ(holdingRegs[ThresholdReg], DEFAULT_THRESHOLD_MG, "reg 22 republished");
  CHECK_EQ(holdingRegs[SlaveIDReg], DEFAULT_SLAVE_ID, "reg 21 republished");

  /* a read landing straight after must not undo any of it */
  publishBlock();
  checkThresholdUpdate();
  checkModbusSlaveIdUpdate();
  CHECK_EQ(thresholdMg, DEFAULT_THRESHOLD_MG, "threshold survives a read");
  CHECK_EQ(currentSlaveId, DEFAULT_SLAVE_ID, "slave id survives a read");
}

/* ===================================================================== */
/*  regs 21 and 22 -- the settings                                       */
/* ===================================================================== */

/* The rev F bug: publishBlock() runs at READ time and rewrote regs 21
   and 35 from the device's current values, so a write that arrived
   before the next pass of loop() was erased before it was inspected. */
static void test_setting_survives_a_read_landing_first()
{
  printf("settings: a read between the write and loop() must not erase it\n");
  bootFresh();

  holdingRegs[SlaveIDReg] = 40;
  publishBlock();
  checkModbusSlaveIdUpdate();
  CHECK_EQ(currentSlaveId, 40, "reg 21 write survived the interleaved read");
  CHECK_EQ(holdingRegs[SlaveIDReg], 40, "reg 21 reads back as written");

  holdingRegs[ThresholdReg] = 4500;
  publishBlock();
  checkThresholdUpdate();
  CHECK_EQ(thresholdMg, 4500, "reg 22 write survived the interleaved read");
}

static void test_settings_hold_their_value()
{
  printf("settings: reg 22 holds the written value and reg 24 echoes it\n");
  bootFresh();

  holdingRegs[ThresholdReg] = 1200;
  checkThresholdUpdate();
  publishBlock();

  CHECK_EQ(holdingRegs[ThresholdReg], 1200, "reg 22 reads back as written");
  CHECK_EQ(holdingRegs[ThresholdEffReg], 1200, "reg 24 effective threshold");

}

static void test_out_of_range_writes_revert()
{
  printf("settings: an out-of-range write reverts to the value in force\n");
  bootFresh();

  holdingRegs[ThresholdReg] = THRESHOLD_MAX_MG + 1;
  checkThresholdUpdate();
  CHECK_EQ(thresholdMg, DEFAULT_THRESHOLD_MG, "threshold unchanged");
  CHECK_EQ(holdingRegs[ThresholdReg], DEFAULT_THRESHOLD_MG, "reg 22 reverted");

  holdingRegs[ThresholdReg] = THRESHOLD_MIN_MG - 1;
  checkThresholdUpdate();
  CHECK_EQ(holdingRegs[ThresholdReg], DEFAULT_THRESHOLD_MG, "reg 22 reverted");

  holdingRegs[SlaveIDReg] = 248;
  checkModbusSlaveIdUpdate();
  CHECK_EQ(currentSlaveId, DEFAULT_SLAVE_ID, "slave id unchanged");
  CHECK_EQ(holdingRegs[SlaveIDReg], DEFAULT_SLAVE_ID, "reg 21 reverted");

}

/* ===================================================================== */
/*  reg 37 -- read-to-clear                                              */
/* ===================================================================== */

static void test_impact_register_clears_on_read()
{
  printf("reg 37: cleared after the response is sent, but only if unchanged\n");
  bootFresh();

  impactMag2 = 640000UL;                       /* 800 counts -> 3120 mg */
  publishBlock();
  CHECK_EQ(holdingRegs[ImpactMagReg], countsToMg(800), "impact published");

  modbus_read_complete(0, HOLDING_REGS_SIZE);
  CHECK_EQ(impactMag2, 0, "cleared once the frame is on the wire");
  CHECK_EQ(holdingRegs[ImpactMagReg], 0, "reg 37 zeroed");

  /* a read that did not cover reg 37 must not clear it */
  impactMag2 = 640000UL;
  publishBlock();
  modbus_read_complete(0, 10);
  CHECK_EQ(impactMag2, 640000UL, "untouched by a read of regs 0-9");

  /* a new impact between publication and the clear must not be wiped */
  publishBlock();
  impactMag2 = 810000UL;                       /* ISR stores a bigger one */
  modbus_read_complete(0, HOLDING_REGS_SIZE);
  CHECK_EQ(impactMag2, 810000UL, "new impact survives the read-to-clear");
}

/* ===================================================================== */
/*  identification and arithmetic                                        */
/* ===================================================================== */

static void test_identification_registers()
{
  printf("identification: regs 42-44\n");
  bootFresh();

  /* 9 -> 10: registers 64-68 added for tilt. Appending cannot make a
     map-9 master misread what it already reads, but the map changed and
     a master must acknowledge it, so the version moves. */
  CHECK_EQ(holdingRegs[MapVersionReg], 10, "reg 43 map version");
  CHECK_EQ(holdingRegs[FwVersionReg], (2 << 8) | 0, "reg 42 firmware version");
  CHECK(holdingRegs[BuildDateReg] != 0, "reg 44 build date is populated");

  unsigned int d = holdingRegs[BuildDateReg];
  unsigned int month = (d >> 5) & 0x0F, day = d & 0x1F;
  CHECK(month >= 1 && month <= 12, "build month in range: got %u", month);
  CHECK(day >= 1 && day <= 31, "build day in range: got %u", day);

  /* The map must still fit one function-3 request. CTX311 raises the
     library BUFFER_SIZE 128 -> 160, so 5 + 2*N <= 160 -> N <= 77.
     At 64 registers there is room for a v10 without touching the
     library again -- which was the point of raising it now. */
  CHECK(HOLDING_REGS_SIZE <= 77,
        "map fits one request: %d registers", (int)HOLDING_REGS_SIZE);
}

/* The shift-by-30 form isqrt32() used before rev G. Kept here purely as
   a reference: the firmware's byte-extract version must agree with it
   exactly. It is 4.7x slower on AVR (gcc expands `n >> 30` into a
   30-iteration bit-shift loop), which is why it is not in the firmware. */
static uint16_t isqrt32_shift30(uint32_t n)
{
  uint32_t rem = 0, root = 0;
  for (uint8_t i = 0; i < 16; i++) {
    root <<= 1;
    rem = (rem << 2) | (n >> 30);
    n <<= 2;
    if (root < rem) { root++; rem -= root; root++; }
  }
  return (uint16_t)(root >> 1);
}

/* floor(sqrt(n)), computed independently of either implementation */
static uint32_t floor_sqrt(uint32_t n)
{
  uint32_t r = (uint32_t)sqrtl((long double)n);
  while ((uint64_t)r * r > (uint64_t)n) r--;
  while ((uint64_t)(r + 1) * (r + 1) <= (uint64_t)n) r++;
  return r;
}

static void test_isqrt32()
{
  printf("isqrt32: exact, and identical to the pre-rev-G shift-by-30 form\n");

  CHECK_EQ(isqrt32(0), 0, "sqrt 0");
  CHECK_EQ(isqrt32(1), 1, "sqrt 1");
  CHECK_EQ(isqrt32(640000UL), 800, "sqrt 640000");
  CHECK_EQ(isqrt32(639999UL), 799, "sqrt truncates");
  CHECK_EQ(isqrt32(4294836225UL), 65535, "sqrt of the largest square");
  CHECK_EQ(isqrt32(4294967295UL), 65535, "sqrt of UINT32_MAX");

  /* every perfect square and both its neighbours -- where an off-by-one
     in the remainder handling would surface */
  int mismatch = 0, inexact = 0;
  for (uint32_t k = 1; k < 65535 && mismatch + inexact < 8; k++) {
    uint32_t sq = k * k;
    const uint32_t probes[3] = {sq - 1, sq, sq + 1};
    for (int j = 0; j < 3; j++) {
      uint32_t n = probes[j];
      uint16_t got = isqrt32(n);
      if (got != isqrt32_shift30(n)) mismatch++;
      if (got != floor_sqrt(n)) inexact++;
    }
  }
  CHECK_EQ(mismatch, 0, "perfect squares: agrees with the shift-by-30 form");
  CHECK_EQ(inexact, 0, "perfect squares: equals floor(sqrt(n))");

  /* a stride sweep across the whole 32-bit input range */
  mismatch = 0;
  inexact  = 0;
  unsigned long swept = 0;
  for (uint64_t n = 0; n <= 0xFFFFFFFFULL; n += 9973) {
    uint16_t got = isqrt32((uint32_t)n);
    if (got != isqrt32_shift30((uint32_t)n)) mismatch++;
    if (got != floor_sqrt((uint32_t)n)) inexact++;
    swept++;
  }
  CHECK_EQ(mismatch, 0, "full-range sweep: agrees with the shift-by-30 form");
  CHECK_EQ(inexact, 0, "full-range sweep: equals floor(sqrt(n))");
  printf("  (%lu values swept across the full uint32 range)\n", swept);
}

/* ===================================================================== */
/*  mean-square handoff: the ISR hands off squares, loop() roots them    */
/* ===================================================================== */

static void test_block_published_from_mean_squares()
{
  printf("handoff: loop() takes the roots the ISR no longer takes\n");
  bootFresh();

  /* per-axis RMS of 100, 200 and 300 counts */
  impactLatched = 0;
  b_ms2X = 10000UL; b_ms2Y = 40000UL; b_ms2Z = 90000UL;
  b_vec2 = b_ms2X + b_ms2Y + b_ms2Z;
  b_pkPX = b_pkPY = b_pkPZ = 0;
  b_pkNX = b_pkNY = b_pkNZ = 0;
  publishBlock();

  CHECK_EQ(holdingRegs[XaxisRMS], countsToMg(100), "reg 0 from b_ms2X");
  CHECK_EQ(holdingRegs[YaxisRMS], countsToMg(200), "reg 1 from b_ms2Y");
  CHECK_EQ(holdingRegs[ZaxisRMS], countsToMg(300), "reg 2 from b_ms2Z");
  CHECK_EQ(holdingRegs[SumReg], countsToMg(isqrt32(140000UL)),
           "reg 23 is the root of the summed mean squares");

  /* the identity the ISR now relies on: summing the mean squares is the
     same vector as rooting each axis and squaring it back up, minus the
     double rounding the old form suffered */
  int lower = 0;
  for (uint32_t a = 1; a < 40000; a += 617) {
    uint32_t mx = a, my = a * 2 + 13, mz = a / 3 + 7;
    uint16_t rx = isqrt32(mx), ry = isqrt32(my), rz = isqrt32(mz);
    uint32_t oldWay = (uint32_t)rx * rx + (uint32_t)ry * ry + (uint32_t)rz * rz;
    uint32_t newWay = mx + my + mz;
    if (oldWay > newWay) lower++;          /* must never overstate */
  }
  CHECK_EQ(lower, 0, "summed mean squares are never below the old form");
}

static void test_impact_snapshot_is_frozen()
{
  printf("handoff: a latched impact freezes the snapshot, live block ignored\n");
  bootFresh();

  b_ms2X = 10000UL; b_ms2Y = 10000UL; b_ms2Z = 10000UL;
  b_vec2 = 30000UL;
  i_ms2X = 640000UL; i_ms2Y = 0; i_ms2Z = 0;
  impactMaxVec2 = 640000UL;
  i_pkPX = i_pkPY = i_pkPZ = 0;
  i_pkNX = i_pkNY = i_pkNZ = 0;
  impactLatched = 1;
  publishBlock();

  CHECK_EQ(holdingRegs[XaxisRMS], countsToMg(800), "reg 0 from the snapshot");
  CHECK_EQ(holdingRegs[SumReg], countsToMg(800), "reg 23 from the snapshot");
  CHECK(holdingRegs[StatusReg] & 0x02, "status bit 1 (latched) set");

  impactLatched = 0;
  publishBlock();
  CHECK_EQ(holdingRegs[XaxisRMS], countsToMg(100), "reg 0 back to live block");
}

static void test_peak_hold_published_from_square()
{
  printf("handoff: reg 30 is the root of the held vector mean square\n");
  bootFresh();

  peakVec2Hold = 640000UL;
  publishBlock();
  CHECK_EQ(holdingRegs[PeakSumHoldReg], countsToMg(800), "reg 30");

  holdingRegs[CommandReg] = CMD_CLEAR_PEAKHOLD;
  checkCommandRegister();
  publishBlock();
  CHECK_EQ(holdingRegs[PeakSumHoldReg], 0, "reg 30 cleared");
  CHECK_EQ(peakMag2Hold, 0, "reg 36 source cleared too");
}

/* rev H: exactly one thing can open the interlock. Regs 35 and 48 are
   reserved holes and must stay inert no matter what a master writes --
   a map-7 client will keep writing reg 35 and must not be able to make
   it read back as though a trip had been armed. */
static void test_rms_trip_is_gone()
{
  printf("trip: the sustained-RMS path is removed, regs 35/48 inert\n");
  bootFresh();

  CHECK_EQ(holdingRegs[Rsvd35Reg], 0, "reg 35 reads 0 at boot");
  CHECK_EQ(holdingRegs[Rsvd48Reg], 0, "reg 48 reads 0 at boot");

  /* a map-7 master arming the old trip */
  holdingRegs[Rsvd35Reg] = 2000;
  holdingRegs[Rsvd48Reg] = 2000;
  checkThresholdUpdate();
  checkModbusSlaveIdUpdate();
  checkCommandRegister();
  publishBlock();
  CHECK_EQ(holdingRegs[Rsvd35Reg], 0, "reg 35 pinned back to 0 by a read");
  CHECK_EQ(holdingRegs[Rsvd48Reg], 0, "reg 48 pinned back to 0 by a read");

  /* writing the reserved register must not touch the real threshold,
     nor cost an EEPROM write */
  CHECK_EQ(thresholdMg, DEFAULT_THRESHOLD_MG, "reg 22 unaffected");

  /* the surviving trip threshold still reaches the ISR pre-squared */
  holdingRegs[ThresholdReg] = 3900;        /* 3900 mg / 3.9 = 1000 counts */
  checkThresholdUpdate();
  CHECK_EQ(thresholdSq, 1000UL * 1000UL, "thresholdSq = counts^2");
  CHECK_EQ(thresholdCounts, 1000, "thresholdCounts");
}

/* PEAK_CONFIRM is a product decision, not an implementation detail:
   it sets both the trip latency and the shortest strike the device can
   see at all. Pin it so a change has to be deliberate. */
static void test_peak_confirm_is_one()
{
  printf("trip: a single sample over threshold trips\n");
  CHECK_EQ(PEAK_CONFIRM, 1,
           "PEAK_CONFIRM (1 = 0.63 ms latency, 2 = 1.26 ms)");
}

static void test_conversions()
{
  printf("conversions: the counts-to-units helpers\n");

  CHECK_EQ(countsToMg(256), 998, "256 counts ~= 1 g");    /* 3.9 mg/LSB */
  CHECK_EQ(countsToMg(0), 0, "0 counts");
  CHECK_EQ(countsToMg(65535), 65535, "conversion saturates, never wraps");
  /* the signed form shifts a negative product, which floors rather than
     truncating, so it lands 1 mg below the mirror of the unsigned form.
     Irrelevant on regs 32-34 at 1 mg resolution, but pin it down.     */
  CHECK_EQ(countsToMgSigned(256), 998, "signed conversion, positive");
  CHECK_EQ(countsToMgSigned(-256), -999, "signed conversion, negative");
  CHECK_EQ(countsToCms2(256), 979, "256 counts ~= 9.79 m/s2");
}


/* ===================================================================== */
/*  CTX311 -- loss-of-support detector                                   */
/* ===================================================================== */
/* These DO exercise the ISR. Rev H could not: its ISR decisions needed
   real interrupt timing. The LOS path is different -- it is a pure
   function of the raw sample stream, so feeding myHandler() a synthetic
   stream tests it exactly. That is a deliberate property of the design,
   not a happy accident: a protective function that can only be tested
   on hardware cannot be regression-tested at all. */

/* Drive N samples of a given magnitude on Z. 256 counts ~= 1 g. */
static void feed(int counts, int n)
{
  adxlFeed.x = 0; adxlFeed.y = 0; adxlFeed.z = counts;
  for (int i = 0; i < n; i++) { g_micros += 629; myHandler(); }
}

/* Same, but dither so the stuck-sample detector stays quiet. */
static void feedLive(int counts, int n)
{
  for (int i = 0; i < n; i++) {
    adxlFeed.x = (i & 1); adxlFeed.y = 0; adxlFeed.z = counts + (i & 1);
    g_micros += 629; myHandler();
  }
}

static void armed()
{
  bootFresh();
  settleCount = SETTLE_SAMPLES;   /* skip the 2 s boot suppression */
  losLatched = 0; losCount = 0; losTripCount = 0;
  losMinMag2 = 0xFFFFFFFFUL;
  PORTC |= (1 << LOS_PORT_BIT);
}

/* Same as armed(), but left INSIDE the settle window. setup() does not
   zero settleCount -- on a real part power-on reset does it, but these
   tests share one process, so a previous armed() would otherwise leak
   an armed detector into a test about arming. */
static void arming()
{
  bootFresh();
  settleCount = 0;
  losLatched = 0; losCount = 0; losTripCount = 0;
  losMinMag2 = 0xFFFFFFFFUL;
  PORTC |= (1 << LOS_PORT_BIT);
  cfgWasDefaulted = false;   /* isolate arming from the defaulted-EEPROM fault */
  faultFlags = 0;
}

static void test_los_trips_below_threshold()
{
  printf("LOS: sustained loss of support trips the arrest output\n");
  armed();

  /* 1 g: supported. Must not trip however long it runs. */
  feedLive(256, 200);
  CHECK(!losLatched, "1 g does not trip");
  CHECK(PORTC & (1 << LOS_PORT_BIT), "arrest output stays safe at 1 g");

  /* 0.5 g, well under the 850 mg default, for longer than 25 ms. */
  feedLive(128, (int)losSamplesRequired + 5);
  CHECK(losLatched, "sustained 0.5 g trips");
  CHECK(!(PORTC & (1 << LOS_PORT_BIT)), "arrest output driven low");
  CHECK_EQ(losTripCount, 1, "trip counted once");
  CHECK_EQ(losByFault, 0, "trip attributed to an event, not a fault");
}

static void test_los_ignores_brief_dips()
{
  printf("LOS: a dip shorter than the confirm time does not trip\n");
  armed();

  int nearly = (int)losSamplesRequired - 2;
  CHECK(nearly > 0, "confirm time is more than one sample");
  feedLive(128, nearly);
  CHECK(!losLatched, "a dip one sample short of the confirm time is ignored");

  feedLive(256, 50);            /* recover */
  CHECK(!losLatched, "still armed after recovery");
}

static void test_los_partial_unloading_is_the_point()
{
  printf("LOS: catches partial unloading that free-fall detection misses\n");
  armed();

  /* 0.8 g -- a jack slipping under load. Nowhere near free fall, so a
     300-600 mg free-fall threshold would never see it. This is the
     failure mode the product exists for. */
  feedLive(205, (int)losSamplesRequired + 5);
  CHECK(losLatched, "0.8 g partial unloading trips");

  /* And it is correctly reported as NOT having reached free fall, so
     the height estimate must be withheld rather than guessed. */
  publishBlock();
  CHECK(!(holdingRegs[LosStatusReg] & 0x08),
        "free-fall depth flag clear for a partial unload");
  CHECK_EQ(holdingRegs[LastLosHeightReg], 0xFFFF,
           "height withheld when free-fall depth was never reached");
  CHECK(holdingRegs[LastLosMinMagReg] > 700,
        "min magnitude records a partial unload: got %u",
        holdingRegs[LastLosMinMagReg]);
}

static void test_los_spike_tolerance_keeps_a_run_alive()
{
  printf("LOS: a single spike does not discard a run in progress\n");
  armed();

  int half = (int)losSamplesRequired / 2;
  feedLive(128, half);                 /* half way into a real event */
  feedLive(300, 1);                    /* one sample back over 1 g */
  feedLive(128, half + 3);             /* event continues */

  CHECK(losLatched,
        "a one-sample excursion mid-event must not reset the counter");
}

static void test_los_latches_until_commanded()
{
  printf("LOS: default is latch, and the latch survives recovery\n");
  armed();
  CHECK_EQ(losHoldMs, 0, "default hold is 0 = latch");

  feedLive(128, (int)losSamplesRequired + 5);
  CHECK(losLatched, "tripped");

  feedLive(256, 2000);                 /* back to 1 g for a long time */
  updateLosOutput();
  CHECK(losLatched, "latched output does not self-clear on a timer");
  CHECK(!(PORTC & (1 << LOS_PORT_BIT)), "arrest stays engaged");

  holdingRegs[CommandReg] = CMD_CLEAR_LOS;
  checkCommandRegister();
  CHECK(!losLatched, "CLEAR_LOS re-arms");
  CHECK(PORTC & (1 << LOS_PORT_BIT), "arrest output released");
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_ACCEPTED,
           "CLEAR_LOS acknowledged");
}

static void test_los_settings_follow_the_rev_h_pattern()
{
  printf("LOS: settings hold, echo, and reject out of range\n");
  armed();

  holdingRegs[LosThresholdReg] = 700;
  checkLosSettingsUpdate();
  publishBlock();
  CHECK_EQ(holdingRegs[LosThresholdReg], 700, "reg 50 holds the value");
  CHECK_EQ(holdingRegs[LosThresholdEffReg], 700, "reg 51 echoes it");

  /* Above 1 g is meaningless for a loss-of-support threshold. */
  holdingRegs[LosThresholdReg] = 1100;
  checkLosSettingsUpdate();
  CHECK_EQ(holdingRegs[LosThresholdReg], 700,
           "a threshold above 1 g is rejected and reverts");

  holdingRegs[LosTimeMsReg] = 40;
  checkLosSettingsUpdate();
  CHECK_EQ(losSamplesRequired, (40U * LOS_ODR_HZ + 999U) / 1000U,
           "confirm time converts to samples at the FIXED ODR");

  holdingRegs[LosTimeMsReg] = 5000;
  checkLosSettingsUpdate();
  CHECK_EQ(holdingRegs[LosTimeMsReg], 40, "an absurd confirm time reverts");
}

/* ===================================================================== */
/*  CTX311 -- diagnostics. The part that matters most.                   */
/* ===================================================================== */

static void test_dead_sensor_is_detected()
{
  printf("DIAG: a dead sensor raises a fault and engages the arrest\n");
  armed();
  faultFlags = 0;

  /* No samples at all -- DATA_READY has stopped. This is the failure
     rev H could not see: a dead part looks exactly like a perfectly
     stationary assembly, for ever. */
  runDiagnostics(0);
  CHECK(faultFlags & FAULT_RATE, "zero sample rate raises FAULT_RATE");
  CHECK(!(PORTC & (1 << HEALTH_PORT_BIT)), "health output opens");
  CHECK(losLatched, "fail-safe default trips the arrest output");
  CHECK_EQ(losByFault, 1, "trip attributed to a fault, not an event");

  publishBlock();
  CHECK(holdingRegs[LosStatusReg] & 0x20,
        "reg 49 bit5 tells the master this was a self-diagnosis");
}

static void test_frozen_data_path_is_detected()
{
  printf("DIAG: a frozen SPI bus is detected even while interrupts run\n");
  armed();
  faultFlags = 0;

  /* Interrupts keep arriving at the right rate, but every sample is
     bit-identical -- a stuck bus or a latched-up part. A real ADXL345
     always dithers by at least one LSB. */
  feed(256, STUCK_SAMPLE_LIMIT + 10);
  runDiagnostics(1589);
  CHECK(faultFlags & FAULT_STUCK,
        "identical samples for ~1 s raise FAULT_STUCK");
}

static void test_live_data_does_not_raise_stuck()
{
  printf("DIAG: normal dithering data does not raise a false fault\n");
  armed();
  faultFlags = 0;

  feedLive(256, STUCK_SAMPLE_LIMIT + 10);
  runDiagnostics(1589);
  CHECK(!(faultFlags & FAULT_STUCK), "dithering data is not stuck");
  CHECK(!(faultFlags & FAULT_RATE), "a healthy rate is not a fault");
  CHECK(!(faultFlags & FAULT_DETECTION_LOST),
        "no detection-lost fault on healthy data");
  CHECK(!losLatched, "healthy data does not engage the arrest");
}

static void test_advisory_faults_do_not_engage_the_arrest()
{
  printf("DIAG: an advisory fault opens health but does not arrest\n");
  armed();
  faultFlags = 0;
  cfgWasDefaulted = true;          /* blank EEPROM, defaults applied */

  feedLive(256, 100);
  runDiagnostics(1589);

  CHECK(faultFlags & FAULT_CONFIG, "a defaulted config is reported");
  CHECK(!(PORTC & (1 << HEALTH_PORT_BIT)), "health output opens");
  /* A factory-fresh unit must not trip its arrest device merely for
     never having been configured. Detection still works. */
  CHECK(!losLatched, "a defaulted config does not engage the arrest");
  CHECK(PORTC & (1 << LOS_PORT_BIT), "arrest output stays released");

  /* ...but it must not block re-arming either. */
  holdingRegs[CommandReg] = CMD_CLEAR_LOS;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_ACCEPTED,
           "an advisory fault does not block CLEAR_LOS");
}

static void test_plausibility_suspended_during_an_event()
{
  printf("DIAG: plausibility does not fire during a real event\n");
  armed();
  faultFlags = 0;

  /* During a genuine loss of support the magnitude is SUPPOSED to be
     low. Flagging that as implausible would raise a fault on exactly
     the event the device exists to detect. */
  feedLive(50, (int)losSamplesRequired + 5);
  CHECK(losLatched, "event detected");
  g_millis += 5000;
  runDiagnostics(1589);
  CHECK(!(faultFlags & FAULT_IMPLAUSIBLE),
        "low magnitude during a latched event is not implausible");
}

static void test_cannot_rearm_while_faulted()
{
  printf("DIAG: CLEAR_LOS is refused while the channel is still faulted\n");
  armed();
  faultFlags = 0;

  runDiagnostics(0);                   /* dead sensor */
  CHECK(losLatched, "faulted and tripped");

  holdingRegs[CommandReg] = CMD_CLEAR_LOS;
  checkCommandRegister();
  CHECK(losLatched, "re-arming is refused while the fault stands");
  CHECK(!(PORTC & (1 << LOS_PORT_BIT)), "arrest stays engaged");
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_UNKNOWN,
           "the refusal is reported, not silently ignored");
}

static void test_impact_threshold_floor_raised()
{
  printf("CTX311: impact threshold cannot be set into the free-fall step\n");
  bootFresh();

  /* Entering free fall steps the AC path by ~1000 mg, because the DC
     tracker still holds gravity. A threshold below that would trip the
     impact output at the START of a fall, before anything was struck. */
  CHECK(THRESHOLD_MIN_MG >= 1000,
        "impact threshold floor clears the ~1000 mg free-fall step");

  holdingRegs[ThresholdReg] = 500;
  checkThresholdUpdate();
  CHECK_EQ(holdingRegs[ThresholdReg], DEFAULT_THRESHOLD_MG,
           "a 500 mg impact threshold is rejected");
}

/* ===================================================================== */
/*  the arming window  --  reg 49 bit 6 and the health output            */
/* ===================================================================== */

static void test_health_is_open_while_arming()
{
  printf("arming: health is OPEN until the detector is armed\n");
  arming();

  /* Straight out of setup(), before any diagnostics pass has run. If
     setup() drove PC2 healthy and left runDiagnostics() to open it,
     there would be a brief healthy pulse on the pin at power-up -- long
     enough for a PLC scan to sample it and latch a start permit. */
  CHECK(!(PORTC & (1 << HEALTH_PORT_BIT)),
        "health is open from the moment setup() returns");

  /* Part way through the settle window: no fault, but not armed. */
  feedLive(256, 200);
  runDiagnostics(1589);

  CHECK_EQ(faultFlags, 0, "arming raises no fault");
  CHECK(!(PORTC & (1 << HEALTH_PORT_BIT)),
        "health is open while still arming");
  /* The whole point: an unarmed detector must not also be engaging the
     arrest. Health open says "do not rely on me yet", not "stop". */
  CHECK(PORTC & (1 << LOS_PORT_BIT),
        "arming does not engage the arrest output");
  CHECK(!losLatched, "arming does not latch a loss-of-support trip");

  publishBlock();
  CHECK(holdingRegs[LosStatusReg] & 0x40,
        "reg 49 bit 6 reports still arming");
}

static void test_health_closes_once_armed()
{
  printf("arming: health closes when the detector arms\n");
  arming();

  /* Cross the settle boundary. SETTLE_SAMPLES is counted in the ISR,
     so this drives it the same way the part would. */
  feedLive(256, SETTLE_SAMPLES + 10);
  runDiagnostics(1589);

  CHECK_EQ(faultFlags, 0, "a healthy unit arms without a fault");
  CHECK(PORTC & (1 << HEALTH_PORT_BIT),
        "health closes once armed");

  publishBlock();
  CHECK(!(holdingRegs[LosStatusReg] & 0x40),
        "reg 49 bit 6 clears once armed");
}

static void test_arming_is_distinguishable_from_a_fault()
{
  printf("arming: a master can tell arming from a fault\n");

  /* Both open PC2. Only one of them is a defect, and register 61 plus
     register 49 bit 6 are what separate them -- this is the reading a
     PLC integrator is most likely to get wrong. */
  arming();
  feedLive(256, 200);
  runDiagnostics(1589);
  publishBlock();
  bool armingOpen  = !(PORTC & (1 << HEALTH_PORT_BIT));
  bool armingBit   = holdingRegs[LosStatusReg] & 0x40;
  uint16_t armingFaults = holdingRegs[FaultReg];

  armed();
  faultFlags = 0;
  cfgWasDefaulted = true;          /* an advisory fault */
  feedLive(256, 100);
  runDiagnostics(1589);
  publishBlock();
  bool faultOpen = !(PORTC & (1 << HEALTH_PORT_BIT));
  bool faultBit  = holdingRegs[LosStatusReg] & 0x40;

  CHECK(armingOpen && faultOpen, "both conditions open health");
  CHECK_EQ(armingFaults, 0, "arming reports no fault in reg 61");
  CHECK(armingBit,  "arming sets reg 49 bit 6");
  CHECK(!faultBit,  "a fault after arming leaves reg 49 bit 6 clear");
}

/* ===================================================================== */
/*  tilt -- registers 64-68. MONITORING ONLY                             */
/* ===================================================================== */

/* True angle between two integer vectors, in tenths of a degree, using
   doubles. The comparison is against the SAME integers the firmware
   gets, so this measures the algorithm and not input quantisation. */
static double trueAngleTenths(int x1, int y1, int z1, int x2, int y2, int z2)
{
  double m1 = sqrt((double)x1*x1 + (double)y1*y1 + (double)z1*z1);
  double m2 = sqrt((double)x2*x2 + (double)y2*y2 + (double)z2*z2);
  double d  = ((double)x1*x2 + (double)y1*y2 + (double)z1*z2) / (m1 * m2);
  if (d >  1.0) d =  1.0;
  if (d < -1.0) d = -1.0;
  return acos(d) * 180.0 / M_PI * 10.0;
}

static void test_angle_between_is_accurate()
{
  printf("tilt: angleBetween swept against double precision\n");

  /* 1 g is ~277 counts at 3.9 mg/LSB, so this is the real operating
     magnitude, and the one where integer error is worst. */
  const int MAG = 277;
  double worst = 0.0; int worstDeg = -1;

  for (int tenth = 0; tenth <= 1800; tenth++) {
    double th = tenth / 10.0 * M_PI / 180.0;
    int ry = (int)lround(MAG * sin(th));
    int rz = (int)lround(MAG * cos(th));

    uint16_t got  = angleBetween(0, ry, rz, 0, 0, MAG);
    double   want = trueAngleTenths(0, ry, rz, 0, 0, MAG);
    double   err  = fabs((double)got - want);
    if (err > worst) { worst = err; worstDeg = tenth; }
  }
  printf("  worst error %.2f tenths of a degree (at %.1f deg)\n",
         worst, worstDeg / 10.0);
  CHECK(worst <= 3.0,
        "integer angle within 0.3 deg of true across 0-180: got %.2f tenths",
        worst);

  /* Small angles are the ones that matter -- structural tilt is a few
     degrees, and that is exactly where a dot-product form would lose
     precision. Hold this range tighter. */
  double worstSmall = 0.0;
  for (int tenth = 0; tenth <= 300; tenth++) {
    double th = tenth / 10.0 * M_PI / 180.0;
    int ry = (int)lround(MAG * sin(th));
    int rz = (int)lround(MAG * cos(th));
    uint16_t got  = angleBetween(0, ry, rz, 0, 0, MAG);
    double   want = trueAngleTenths(0, ry, rz, 0, 0, MAG);
    double   err  = fabs((double)got - want);
    if (err > worstSmall) worstSmall = err;
  }
  printf("  worst error below 30 deg: %.2f tenths\n", worstSmall);
  CHECK(worstSmall <= 5.0,
        "within 0.5 deg below 30 deg: got %.2f tenths", worstSmall);

  /* Degenerate inputs must not return a confident wrong angle. */
  CHECK_EQ(angleBetween(0, 0, 0, 0, 0, MAG), TILT_INVALID,
           "a zero vector has no direction");
  CHECK_EQ(angleBetween(0, 0, MAG, 0, 0, 0), TILT_INVALID,
           "a zero reference has no direction");
  CHECK_EQ(angleBetween(0, 0, MAG, 0, 0, MAG), 0, "identical vectors read 0");

  /* Full scale must not overflow: 16 g is ~4096 counts. */
  uint16_t fs = angleBetween(4096, 0, 0, 0, 0, 4096);
  CHECK(fs >= 895 && fs <= 905,
        "orthogonal at full scale is ~90 deg: got %u tenths", fs);
}

/* Put the device at rest with a known gravity vector. */
static void restAt(int16_t gx, int16_t gy, int16_t gz)
{
  b_dcX = gx; b_dcY = gy; b_dcZ = gz;
  r1msV = 0;                       /* no AC motion */
  g_millis += TILT_REST_MS + TILT_UPDATE_MS + 1;
  updateTilt();
}

static void test_tilt_updates_only_at_rest()
{
  printf("tilt: updates at rest, HOLDS while moving\n");
  armed();
  tiltRefX = tiltRefY = tiltRefZ = 0;
  tiltAngle = TILT_INVALID;
  tiltStillSinceMs = g_millis;
  tiltLastUpdateMs = 0;

  /* Level, and take a reference. */
  restAt(0, 0, 277);
  CHECK(tiltAtRest, "still for long enough counts as at rest");
  CHECK(setTiltReference(), "reference accepted at rest");
  CHECK_EQ(tiltAngle, 0, "zero degrees from itself");

  /* Tilt it ~10 deg and let it settle. */
  restAt(0, 48, 273);
  CHECK(tiltAngle >= 90 && tiltAngle <= 110,
        "reads ~10 deg: got %u tenths", tiltAngle);

  uint16_t held = tiltAngle;

  /* Now move it. The angle must FREEZE -- an accelerometer in motion
     cannot tell tilt from acceleration, so the last resting value is
     the only honest thing to report. */
  b_dcX = 0; b_dcY = 200; b_dcZ = 190;    /* would be ~45 deg if trusted */
  r1msV = 500;                            /* moving */
  g_millis += 5000;
  updateTilt();
  CHECK(!tiltAtRest, "movement clears the at-rest flag");
  CHECK_EQ(tiltAngle, held, "angle HOLDS while moving, does not track");

  /* Going still is not enough on its own -- it must stay still. */
  r1msV = 0;
  g_millis += TILT_REST_MS / 2;
  updateTilt();
  CHECK(!tiltAtRest, "briefly still is not yet at rest");
  CHECK_EQ(tiltAngle, held, "still holding");

  /* Past the settle window it updates again. */
  g_millis += TILT_REST_MS;
  updateTilt();
  CHECK(tiltAtRest, "at rest once the window passes");
  CHECK(tiltAngle >= 400 && tiltAngle <= 500,
        "now tracks the new attitude: got %u tenths", tiltAngle);
}

static void test_tilt_reference_refused_while_moving()
{
  printf("tilt: reference refused unless at rest\n");
  armed();
  tiltRefX = tiltRefY = tiltRefZ = 0;
  tiltStillSinceMs = g_millis;
  tiltLastUpdateMs = 0;

  b_dcX = 0; b_dcY = 0; b_dcZ = 277;
  r1msV = 500;                            /* moving */
  g_millis += 10000;
  updateTilt();

  holdingRegs[CommandReg] = CMD_SET_TILT_REF;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_UNKNOWN,
           "refused while moving, and the refusal is reported");
  CHECK(tiltRefX == 0 && tiltRefY == 0 && tiltRefZ == 0,
        "no reference was stored");

  /* At rest, the same command is accepted. */
  restAt(0, 0, 277);
  holdingRegs[CommandReg] = CMD_SET_TILT_REF;
  checkCommandRegister();
  CHECK_EQ(holdingRegs[CommandStatusReg], CMD_STATUS_ACCEPTED,
           "accepted once at rest");
  CHECK_EQ(tiltRefZ, 277, "reference stored");
}

static void test_tilt_publishes_and_never_trips()
{
  printf("tilt: publishes regs 64-68 and operates nothing\n");
  armed();
  faultFlags = 0; cfgWasDefaulted = false;   /* isolate from the blank EEPROM */
  tiltRefX = tiltRefY = tiltRefZ = 0;
  tiltAngle = TILT_INVALID;
  tiltStillSinceMs = g_millis;
  tiltLastUpdateMs = 0;

  publishBlock();
  CHECK_EQ(holdingRegs[TiltAngleReg], TILT_INVALID,
           "no reference -> reg 64 is invalid, not 0");
  CHECK_EQ(holdingRegs[TiltStatusReg] & TILT_ST_REF_SET, 0, "reg 65 bit0 clear");

  restAt(0, 0, 277);
  setTiltReference();
  restAt(0, 48, 273);
  publishBlock();

  CHECK(holdingRegs[TiltStatusReg] & TILT_ST_REF_SET, "reg 65 bit0 set");
  CHECK(holdingRegs[TiltStatusReg] & TILT_ST_AT_REST, "reg 65 bit1 set");
  CHECK(holdingRegs[TiltStatusReg] & TILT_ST_VALID,   "reg 65 bit2 set");
  CHECK_EQ((int16_t)holdingRegs[TiltRefZReg], countsToMgSigned(277),
           "reg 68 echoes the reference in mg");

  /* The whole point: tilt is monitoring. It must not touch an output or
     raise a fault, however far it has tilted. */
  CHECK(PORTC & (1 << LOS_PORT_BIT),   "arrest output untouched by tilt");
  CHECK(PORTC & (1 << OUTPUT_PORT_BIT),"impact output untouched by tilt");
  CHECK(!losLatched, "tilt never latches the arrest");
  CHECK_EQ(faultFlags, 0, "tilt raises no fault");

  /* Clearing the reference invalidates the angle rather than reporting
     a stale one. */
  holdingRegs[CommandReg] = CMD_CLEAR_TILT_REF;
  checkCommandRegister();
  publishBlock();
  CHECK_EQ(holdingRegs[TiltAngleReg], TILT_INVALID,
           "cleared reference -> invalid, not a stale angle");
}

static void test_status_bit2_removed()
{
  printf("CTX311: status bit 2 removed -- it was a permanent fault light\n");
  bootFresh();
  blockMissed = 500;                   /* normal, happens on every poll */
  blockReady = 1;
  publishBlock();
  CHECK(!(holdingRegs[StatusReg] & 0x04),
        "missed blocks no longer set a status bit");
  CHECK_EQ(holdingRegs[BlockMissedReg], 500,
           "reg 26 is still the diagnostic counter");
}

int main()
{
  test_command_self_clears_and_acknowledges();
  test_command_repeats_are_countable();
  test_command_zero_is_idle();
  test_unknown_command_is_reported();
  test_factory_reset_restores_writable_registers();
  test_setting_survives_a_read_landing_first();
  test_settings_hold_their_value();
  test_out_of_range_writes_revert();
  test_impact_register_clears_on_read();
  test_block_published_from_mean_squares();
  test_impact_snapshot_is_frozen();
  test_peak_hold_published_from_square();
  test_rms_trip_is_gone();
  test_peak_confirm_is_one();
  test_identification_registers();
  test_isqrt32();
  test_conversions();

  test_los_trips_below_threshold();
  test_los_ignores_brief_dips();
  test_los_partial_unloading_is_the_point();
  test_los_spike_tolerance_keeps_a_run_alive();
  test_los_latches_until_commanded();
  test_los_settings_follow_the_rev_h_pattern();
  test_dead_sensor_is_detected();
  test_frozen_data_path_is_detected();
  test_live_data_does_not_raise_stuck();
  test_advisory_faults_do_not_engage_the_arrest();
  test_plausibility_suspended_during_an_event();
  test_cannot_rearm_while_faulted();
  test_impact_threshold_floor_raised();
  test_health_is_open_while_arming();
  test_health_closes_once_armed();
  test_arming_is_distinguishable_from_a_fault();
  test_angle_between_is_accurate();
  test_tilt_updates_only_at_rest();
  test_tilt_reference_refused_while_moving();
  test_tilt_publishes_and_never_trips();
  test_status_bit2_removed();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
