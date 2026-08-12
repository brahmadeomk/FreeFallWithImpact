/* Host-side stand-in for the AVR core, used only by test/test_registers.cpp
   so the sketch's register logic can be compiled and exercised on a PC.
   Nothing here is used by the firmware build. */
#ifndef TEST_STUB_ARDUINO_H
#define TEST_STUB_ARDUINO_H

#include <stdint.h>
#include <stddef.h>

typedef uint8_t byte;

/* ---- fake I/O registers ---- */
extern uint8_t  DDRC, PORTC, MCUSR;
extern uint16_t TCNT1, TCCR1A, TCCR1B, TIMSK1;

#define PC0 0
#define PC1 1
#define PC2 2

/* Timer1 prescaler bits */
#define CS10 0
#define CS11 1
#define CS12 2

/* ---- time ---- */
extern unsigned long g_millis;
extern unsigned long g_micros;
inline unsigned long millis() { return g_millis; }
inline unsigned long micros() { return g_micros; }

/* ---- pins / interrupts ---- */
#define INPUT  0
#define OUTPUT 1
#define RISING 3
inline void pinMode(uint8_t, uint8_t) {}
inline int  digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int, void (*)(), int) {}

#define lowByte(w)  ((uint8_t)((w) & 0xFF))
#define highByte(w) ((uint8_t)(((w) >> 8) & 0xFF))

/* ---- serial ---- */
#define SERIAL_8N1 0x06
class HardwareSerial {
public:
  void begin(long) {}
  void begin(long, uint8_t) {}
  int  available() { return 0; }
  int  read() { return -1; }
  void write(uint8_t) {}
  void flush() {}
};
extern HardwareSerial Serial;

inline void delayMicroseconds(unsigned int) {}
inline void digitalWrite(uint8_t, uint8_t) {}

#endif
