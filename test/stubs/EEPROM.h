/* Host-side stand-in for the AVR EEPROM library. Test use only. */
#ifndef TEST_STUB_EEPROM_H
#define TEST_STUB_EEPROM_H

#include <stdint.h>

class EEPROMClass {
public:
  uint8_t cell[512];
  EEPROMClass() { for (int i = 0; i < 512; i++) cell[i] = 0xFF; }
  uint8_t read(int addr) { return cell[addr & 511]; }
  void    write(int addr, uint8_t v) { cell[addr & 511] = v; }
  void    update(int addr, uint8_t v) { write(addr, v); }
};
extern EEPROMClass EEPROM;

#endif
