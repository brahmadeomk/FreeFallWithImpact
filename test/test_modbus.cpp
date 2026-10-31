/* =====================================================================
   Host-side tests for the SimpleModbusSlave receive path.

   The register suites stub this library out, so until now nothing
   exercised it. These tests compile the REAL SimpleModbusSlave.cpp and
   drive it through whole frames, because the thing that failed in the
   field was not a register semantic -- it was frame assembly.

   What broke on a loaded bus: the old receive loop blocked for
   delayMicroseconds(T1_5) after every byte, drained slower than bytes
   arrived, and treated everything it drained in one pass as ONE frame
   keyed on the first byte. A slave still draining when the next
   request arrived appended that request to a frame carrying somebody
   else's ID, and discarded its own request. The master saw a silent
   disconnection.

   The case that proves the fix is test_our_frame_survives_a_foreign_one.

   Build and run:  test/run_tests.sh
   ===================================================================== */

#include <stdio.h>
#include <string.h>

#include "stubs/Arduino.h"

/* ---- storage the stubs declare extern ---- */
uint8_t  DDRC = 0, PORTC = 0, MCUSR = 0;
uint16_t TCNT1 = 0, TCCR1A = 0, TCCR1B = 0, TIMSK1 = 0;
uint8_t  ADMUX = 0, ADCSRA = 0;
uint16_t ADC = 0;
unsigned long g_millis = 0, g_micros = 0;
HardwareSerial Serial;
StubPinEvent   g_pinLog[STUB_PINLOG_CAP];
int            g_pinLogLen = 0;

/* ---- the sketch side of the library's two callbacks ---- */
static int sensorDataCalls = 0;
static int readCompleteCalls = 0;
static unsigned int lastReadStart = 0, lastReadQty = 0;
void sensorData() { sensorDataCalls++; }
void modbus_read_complete(unsigned int start, unsigned int qty)
{
  readCompleteCalls++; lastReadStart = start; lastReadQty = qty;
}

#define TEST_REGS 76
static unsigned int testRegs[TEST_REGS];

#include "../VRM315_IMLOS_revA/SimpleModbusSlave.cpp"

/* ---------------------------- harness ------------------------------ */
static int failures = 0, checks = 0;

#define CHECK(cond, ...)                                        \
  do {                                                          \
    checks++;                                                   \
    if (!(cond)) {                                              \
      failures++;                                               \
      printf("  FAIL %s:%d  ", __FILE__, __LINE__);             \
      printf(__VA_ARGS__); printf("\n");                        \
    }                                                           \
  } while (0)

#define CHECK_EQ(got, want, label)                              \
  do {                                                          \
    checks++;                                                   \
    long g_ = (long)(got), w_ = (long)(want);                   \
    if (g_ != w_) {                                             \
      failures++;                                               \
      printf("  FAIL %s:%d  %s: got %ld, expected %ld\n",       \
             __FILE__, __LINE__, label, g_, w_);                \
    }                                                           \
  } while (0)

#define SLAVE_ID 71
static const unsigned int T3_5_US = 35000000 / 9600;   /* 3645 */

/* Modbus CRC, independent of the library's own, so a bug in one does
   not hide a bug in the other. */
static unsigned int refCRC(const unsigned char *b, int n)
{
  unsigned int t = 0xFFFF;
  for (int i = 0; i < n; i++) {
    t ^= b[i];
    for (int j = 0; j < 8; j++) t = (t & 1) ? ((t >> 1) ^ 0xA001) : (t >> 1);
  }
  return t;                                  /* low byte first on the wire */
}

static void appendCRC(unsigned char *b, int n)
{
  unsigned int c = refCRC(b, n);
  b[n]     = (unsigned char)(c & 0xFF);
  b[n + 1] = (unsigned char)(c >> 8);
}

/* build a function-3 read request */
static int makeRead(unsigned char *b, unsigned char id,
                    unsigned int start, unsigned int qty)
{
  b[0] = id; b[1] = 3;
  b[2] = start >> 8; b[3] = start & 0xFF;
  b[4] = qty >> 8;   b[5] = qty & 0xFF;
  appendCRC(b, 6);
  return 8;
}

/* build a function-6 single write */
static int makeWrite(unsigned char *b, unsigned char id,
                     unsigned int reg, unsigned int val)
{
  b[0] = id; b[1] = 6;
  b[2] = reg >> 8; b[3] = reg & 0xFF;
  b[4] = val >> 8; b[5] = val & 0xFF;
  appendCRC(b, 6);
  return 8;
}

/* Put bytes on the wire at 9600 baud, running a loop() pass between
   each one. That is what the firmware really does -- the whole point of
   the non-blocking receive is that loop() keeps turning while a frame
   arrives -- and it means the library timestamps each byte as it lands
   rather than all at once at the end. */
static void wire(const unsigned char *b, int n)
{
  for (int i = 0; i < n; i++) {
    Serial.rxPush(b[i]);
    modbus_update();          /* loop() drains it on the next pass */
    g_micros += 1042;         /* then one character time passes */
  }
}

static void idle(unsigned long us) { g_micros += us; }

/* One loop() pass. The firmware calls modbus_update() once per pass. */
static void pump(int times = 1) { for (int i = 0; i < times; i++) modbus_update(); }

static void reset()
{
  Serial.rxClear(); Serial.txClear();
  memset(testRegs, 0, sizeof testRegs);
  for (unsigned int i = 0; i < TEST_REGS; i++) testRegs[i] = 0x1000 + i;
  modbus_configure(&Serial, 9600, SERIAL_8N1, SLAVE_ID, 2, TEST_REGS, testRegs);
  sensorDataCalls = readCompleteCalls = 0;
  g_pinLogLen = 0;
  g_micros = 1000000;
}

/* =================== the frame-assembly tests ====================== */

static void test_frame_is_not_processed_until_silence()
{
  printf("RX: a frame is held until T3.5 of silence, not acted on early\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);

  wire(req, n);
  pump();
  CHECK_EQ(Serial.txLen, 0, "no reply while the gap is still short");

  idle(T3_5_US / 2);
  pump();
  CHECK_EQ(Serial.txLen, 0, "still nothing half way through the gap");

  idle(T3_5_US);
  pump();
  CHECK(Serial.txLen > 0, "replies once the full gap has elapsed");
  CHECK_EQ(Serial.txBuf[0], SLAVE_ID, "reply carries our slave id");
  CHECK_EQ(Serial.txBuf[1], 3, "reply echoes function 3");
  CHECK_EQ(Serial.txBuf[2], 2, "one register is two bytes");
  CHECK_EQ((Serial.txBuf[3] << 8) | Serial.txBuf[4], testRegs[0],
           "reply carries the register value");
}

static void test_receive_does_not_block()
{
  printf("RX: a part-arrived frame returns immediately, pass after pass\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);

  /* Deliver the request one byte per loop() pass, as a real UART would
     across a running loop. The old code blocked ~1.5 ms per byte here. */
  for (int i = 0; i < n; i++) {
    Serial.rxPush(req[i]);
    unsigned long before = g_micros;
    pump();
    CHECK_EQ(g_micros, before, "modbus_update consumed no time");
    g_micros += 1042;
  }
  CHECK_EQ(Serial.txLen, 0, "no reply yet -- the gap has not elapsed");

  idle(T3_5_US + 100);
  pump();
  CHECK(Serial.txLen > 0, "and the frame is complete once the line goes quiet");
}

static void test_foreign_frame_is_ignored()
{
  printf("RX: a frame for another slave is dropped and costs nothing\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID + 1, 0, 10);

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "we do not answer another slave's request");
  CHECK_EQ(modbus_error_count(), 0, "and it is not counted as an error");
  CHECK_EQ(modbus_request_count(), 0, "nor as a request to us");
  CHECK_EQ(sensorDataCalls, 0, "the sketch is not disturbed at all");
}

static void test_our_frame_survives_a_foreign_one()
{
  printf("RX: our request still lands right behind another slave's traffic\n");
  reset();

  /* This is the field failure. A foreign request, that slave's reply,
     then our own request -- each separated by the gap the Modbus spec
     asks for and nothing more. The old receive loop was still draining
     the earlier bytes when ours arrived, merged them into one frame
     keyed on the foreign id, and threw our request away. */
  unsigned char foreign[8]; int fn = makeRead(foreign, SLAVE_ID + 1, 0, 1);
  unsigned char reply[7] = { SLAVE_ID + 1, 3, 2, 0x12, 0x34, 0, 0 };
  appendCRC(reply, 5);
  unsigned char mine[8]; int mn = makeRead(mine, SLAVE_ID, 5, 2);

  wire(foreign, fn);
  idle(T3_5_US + 100);
  pump();
  wire(reply, 7);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "nothing said so far -- none of it was ours");

  wire(mine, mn);
  idle(T3_5_US + 100);
  pump();

  CHECK(Serial.txLen > 0, "our request IS answered");
  CHECK_EQ(Serial.txBuf[0], SLAVE_ID, "and the reply is ours");
  CHECK_EQ(Serial.txBuf[2], 4, "two registers requested, four bytes returned");
  CHECK_EQ(modbus_request_count(), 1, "exactly one request counted as ours");
}

static void test_frames_without_a_gap_merge_and_are_dropped()
{
  printf("RX: frames with no gap between them merge -- reported, not silent\n");
  reset();

  /* A master that ignores the 3.5-character gap produces one
     unparseable run. That is the master violating the spec, and the
     slave's job is to reject it rather than act on half of it. */
  unsigned char foreign[8]; int fn = makeRead(foreign, SLAVE_ID + 1, 0, 1);
  unsigned char mine[8];    int mn = makeRead(mine, SLAVE_ID, 0, 1);

  wire(foreign, fn);
  wire(mine, mn);                       /* back to back, no gap at all */
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "the merged run is not answered");
  CHECK_EQ(modbus_request_count(), 0, "and not counted as a request");
}

/* ======================= protocol behaviour ======================== */

static void test_bad_crc_is_counted_and_unanswered()
{
  printf("RX: a corrupted frame is counted and never answered\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);
  req[7] ^= 0xFF;                       /* break the CRC */

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "no reply to a frame that failed CRC");
  CHECK_EQ(modbus_error_count(), 1, "counted in register 74");
  CHECK_EQ(modbus_request_count(), 0, "a bad frame is not a served request");
}

static void test_short_frame_is_counted()
{
  printf("RX: a runt frame is counted, not parsed\n");
  reset();
  unsigned char req[4] = { SLAVE_ID, 3, 0, 0 };

  wire(req, 4);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "no reply to a runt");
  CHECK_EQ(modbus_error_count(), 1, "counted in register 74");
}

static void test_oversized_frame_overflows_safely()
{
  printf("RX: a frame longer than the buffer is dropped without damage\n");
  reset();
  unsigned char big[BUFFER_SIZE + 41];
  big[0] = SLAVE_ID;
  for (int i = 1; i < (int)sizeof big; i++) big[i] = (unsigned char)i;
  wire(big, (int)sizeof big);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 0, "no reply");
  CHECK_EQ(modbus_error_count(), 1, "overflow is counted");
  CHECK(frame[BUFFER_SIZE - 1] == frame[BUFFER_SIZE - 1], "no write past frame[]");
}

static void test_write_and_echo()
{
  printf("RX: function 6 writes the register and echoes the request\n");
  reset();
  unsigned char req[8]; int n = makeWrite(req, SLAVE_ID, 4, 0xBEEF);

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(testRegs[4], 0xBEEF, "the register took the value");
  CHECK_EQ(Serial.txLen, 8, "the echo is eight bytes");
  CHECK_EQ(Serial.txBuf[0], SLAVE_ID, "echo carries our id");
  CHECK_EQ(modbus_request_count(), 1, "counted as a served request");
}

static void test_broadcast_is_acted_on_but_silent()
{
  printf("RX: a broadcast write is obeyed and never answered\n");
  reset();
  unsigned char req[8]; int n = makeWrite(req, 0, 6, 0x1234);

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(testRegs[6], 0x1234, "the broadcast write landed");
  CHECK_EQ(Serial.txLen, 0, "and nothing was transmitted");
}

static void test_illegal_address_gets_an_exception()
{
  printf("RX: a read past the map returns an exception, not silence\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, TEST_REGS + 5, 1);

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(Serial.txLen, 5, "exception frames are five bytes");
  CHECK_EQ(Serial.txBuf[1], 3 | 0x80, "function byte has the exception bit");
  CHECK_EQ(Serial.txBuf[2], 2, "exception 2, illegal data address");
  CHECK_EQ(modbus_error_count(), 1, "counted in register 74");
}

static void test_read_complete_hook_fires_after_the_frame()
{
  printf("RX: the read-to-clear hook fires once, with the range read\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 9, 3);

  wire(req, n);
  idle(T3_5_US + 100);
  pump();

  CHECK_EQ(sensorDataCalls, 1, "sensorData() called before the frame is built");
  CHECK_EQ(readCompleteCalls, 1, "hook called once");
  CHECK_EQ(lastReadStart, 9, "hook told the start address");
  CHECK_EQ(lastReadQty, 3, "hook told the quantity");
}

static void test_driver_enable_brackets_the_frame()
{
  printf("TX: the RS-485 driver is asserted before byte 0 and released after the last\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);
  wire(req, n);
  idle(T3_5_US + 100);
  g_pinLogLen = 0;                      /* only the reply interests us */
  pump();

  CHECK(Serial.txLen > 0, "a reply was sent");

  int hi = -1, lo = -1;
  for (int i = 0; i < g_pinLogLen; i++) {
    if (g_pinLog[i].pin != 2) continue; /* TxEnablePin, set in reset() */
    if (g_pinLog[i].val == HIGH && hi < 0) hi = i;
    if (g_pinLog[i].val == LOW) lo = i;
  }
  CHECK(hi >= 0, "the driver was enabled");
  CHECK(lo >= 0, "the driver was released again");
  CHECK(hi < lo, "enabled before it was released");
  if (hi >= 0)
    CHECK_EQ(g_pinLog[hi].txLenAtTime, 0,
             "enabled before a single byte went out");
  if (lo >= 0)
    CHECK_EQ(g_pinLog[lo].txLenAtTime, Serial.txLen,
             "released only after the whole frame went out");
}

static void test_reply_arrives_in_the_pass_that_completes_the_frame()
{
  printf("TX: the reply goes out on the same pass that sees the gap\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);
  wire(req, n);
  idle(T3_5_US + 100);

  /* The pre-transmit T3_5 that used to sit in sendPacket() is gone, so
     nothing stands between detecting the end of the request and putting
     the reply on the wire. One loop() pass does the lot. */
  pump();
  CHECK_EQ(Serial.txLen, 7, "one-register reply, complete, in one pass");
}

static void test_diagnostics_reset()
{
  printf("RX: the counters clear on command\n");
  reset();
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);
  wire(req, n); idle(T3_5_US + 100); pump();
  CHECK_EQ(modbus_request_count(), 1, "one request served");

  modbus_reset_diagnostics();
  CHECK_EQ(modbus_request_count(), 0, "requests cleared");
  CHECK_EQ(modbus_error_count(), 0, "errors cleared");
}

static void test_counters_saturate_rather_than_wrap_to_zero()
{
  printf("RX: the request counter saturates instead of wrapping\n");
  reset();
  requestCount = 0xFFFE;
  unsigned char req[8]; int n = makeRead(req, SLAVE_ID, 0, 1);
  for (int i = 0; i < 4; i++) {
    Serial.txClear();
    wire(req, n); idle(T3_5_US + 100); pump();
  }
  CHECK_EQ(modbus_request_count(), 0xFFFF,
           "holds at full scale rather than rolling through zero");
}

int main(void)
{
  printf("SimpleModbusSlave receive path\n\n");

  test_frame_is_not_processed_until_silence();
  test_receive_does_not_block();
  test_foreign_frame_is_ignored();
  test_our_frame_survives_a_foreign_one();
  test_frames_without_a_gap_merge_and_are_dropped();
  test_bad_crc_is_counted_and_unanswered();
  test_short_frame_is_counted();
  test_oversized_frame_overflows_safely();
  test_write_and_echo();
  test_broadcast_is_acted_on_but_silent();
  test_illegal_address_gets_an_exception();
  test_read_complete_hook_fires_after_the_frame();
  test_driver_enable_brackets_the_frame();
  test_reply_arrives_in_the_pass_that_completes_the_frame();
  test_diagnostics_reset();
  test_counters_saturate_rather_than_wrap_to_zero();

  printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
