/* =====================================================================
   Host-side tests for the CTX310 rev H register logic.

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
#include "stubs/SparkFun_ADXL345-master/mFFT_SparkFun_ADXL345.cpp"

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

#include "../CTX310_Vibration_revH/CTX310_Vibration_revH.ino"

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

  CHECK_EQ(holdingRegs[MapVersionReg], 8, "reg 43 map version");
  CHECK_EQ(holdingRegs[FwVersionReg], (1 << 8) | 8, "reg 42 firmware version");
  CHECK(holdingRegs[BuildDateReg] != 0, "reg 44 build date is populated");

  unsigned int d = holdingRegs[BuildDateReg];
  unsigned int month = (d >> 5) & 0x0F, day = d & 0x1F;
  CHECK(month >= 1 && month <= 12, "build month in range: got %u", month);
  CHECK(day >= 1 && day <= 31, "build day in range: got %u", day);

  /* the map must still fit one function-3 request against the patched
     library (BUFFER_SIZE 128 -> 5 + 2*N <= 128 -> N <= 61) */
  CHECK(HOLDING_REGS_SIZE <= 61,
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

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
