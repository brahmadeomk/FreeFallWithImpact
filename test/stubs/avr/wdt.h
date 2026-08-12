/* Host-side stand-in for <avr/wdt.h>. Test use only. */
#ifndef TEST_STUB_WDT_H
#define TEST_STUB_WDT_H

#define WDTO_250MS 4
#define WDTO_500MS 5

inline void wdt_disable() {}
inline void wdt_enable(int) {}
inline void wdt_reset() {}

#endif
