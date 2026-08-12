/* Host-side stand-in for the Arduino SPI library. Test use only. */
#ifndef TEST_STUB_SPI_H
#define TEST_STUB_SPI_H

#include <stdint.h>

#define SPI_MODE3        0x0C
#define SPI_CLOCK_DIV4   0x00

class SPIClass {
public:
  void begin() {}
  void setDataMode(uint8_t) {}
  void setClockDivider(uint8_t) {}
  uint8_t transfer(uint8_t) { return 0; }
};
extern SPIClass SPI;

#endif
