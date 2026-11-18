// Хост-заглушка Arduino для тестов логики прошивки.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define PROGMEM

extern uint32_t g_millis;
extern int      g_pins[17];
extern int      g_pin_mode[17];
extern int      g_pullup[17];

inline uint32_t millis() { return g_millis; }
inline void delay(uint32_t ms) { g_millis += ms; }
inline void pinMode(uint8_t pin, uint8_t mode) {
  g_pin_mode[pin] = mode;
  g_pullup[pin] = (mode == INPUT_PULLUP) ? 1 : 0;
}
inline void digitalWrite(uint8_t pin, uint8_t val) { g_pins[pin] = val; }
inline int  digitalRead(uint8_t pin) {
  int v = g_pins[pin];
  // если включена подтяжка и пин "висит" (-1) - читаем HIGH
  if (v < 0) v = (g_pullup[pin] ? HIGH : LOW);
  return v;
}
inline void yield() {}

struct SerialStub {
  void begin(unsigned long) {}
  void println(const char*) {}
  void println() {}
  template <typename... A> void printf(const char*, A...) {}
};
extern SerialStub Serial;
