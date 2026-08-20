/* Host-side stand-in for <avr/pgmspace.h>. Test use only.

   On AVR, PROGMEM keeps a table in flash and pgm_read_word() fetches it
   with LPM. On the host there is one address space, so PROGMEM is empty
   and the read is a plain dereference. */
#ifndef TEST_STUB_PGMSPACE_H
#define TEST_STUB_PGMSPACE_H

#include <stdint.h>

#define PROGMEM
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_byte(addr) (*(const uint8_t  *)(addr))

#endif
