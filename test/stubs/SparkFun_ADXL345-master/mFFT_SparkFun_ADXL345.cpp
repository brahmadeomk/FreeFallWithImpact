/* Host-side stand-in for the SparkFun ADXL345 driver the sketch pulls in
   by relative path. Test use only -- readAccel() replays whatever the
   test pushed into adxlFeed. */
#ifndef TEST_STUB_ADXL345
#define TEST_STUB_ADXL345

#include <stdint.h>

#define ADXL345_INT1_PIN 0
#define ADXL345_INT2_PIN 1
#define ADXL345_BW_800   0x0E

struct AdxlFeed { int x, y, z; };
extern AdxlFeed adxlFeed;

class ADXL345 {
public:
  ADXL345(int) {}
  void powerOn() {}
  void setRangeSetting(int) {}
  void setSpiBit(int) {}
  void set_bw(uint8_t) {}
  void setFullResBit(int) {}
  void setInterrupt(uint8_t, bool) {}
  void setInterruptMapping(uint8_t, uint8_t) {}
  uint8_t getInterruptSource() { return 0; }
  void readAccel(int *x, int *y, int *z)
  {
    *x = adxlFeed.x; *y = adxlFeed.y; *z = adxlFeed.z;
  }
};

#endif
