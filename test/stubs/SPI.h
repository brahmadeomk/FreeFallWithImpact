/* Host-side stand-in for the Arduino SPI library. Test use only. */
#ifndef TEST_STUB_SPI_H
#define TEST_STUB_SPI_H

#include <stdint.h>

#define SPI_MODE3        0x0C
#define SPI_CLOCK_DIV4   0x00

/* A stand-in ADXL345 register file, so the sketch's own SPI framing --
   address byte with the MSB set, then a dummy byte to clock the value
   out -- is exercised rather than mocked away. Tests write the
   registers they care about (0x00 DEVID, 0x2D POWER_CTL). */
extern uint8_t spiRegs[64];

class SPIClass {
public:
  void begin() {}
  void setDataMode(uint8_t) {}
  void setClockDivider(uint8_t) {}

  uint8_t transfer(uint8_t b)
  {
    if (b & 0x80) {            /* address byte of a read */
      pending = b & 0x3F;
      return 0;
    }
    return spiRegs[pending];   /* dummy byte clocks the value back */
  }

private:
  uint8_t pending = 0;
};
extern SPIClass SPI;

#endif
