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

/* ------------------------------------------------------------------
 * Минимальная замена Arduino String: ровно те операции, которые
 * используются в src/webjson.cpp. Нужна, чтобы JSON состояния/настроек
 * можно было собирать и проверять на хосте (без железа).
 * ------------------------------------------------------------------ */
#include <string>

class String {
 public:
  String() {}
  String(const char* s) : s_(s ? s : "") {}
  String(const std::string& s) : s_(s) {}
  explicit String(char c) { s_.assign(1, c); }
  explicit String(int v) { s_ = std::to_string(v); }
  explicit String(unsigned v) { s_ = std::to_string(v); }
  explicit String(long v) { s_ = std::to_string(v); }
  explicit String(unsigned long v) { s_ = std::to_string(v); }
  explicit String(uint8_t v) { s_ = std::to_string((unsigned)v); }

  String& operator+=(const String& o) { s_ += o.s_; return *this; }
  String& operator+=(const char* p) { if (p) s_ += p; return *this; }
  String& operator+=(char c) { s_ += c; return *this; }
  String& operator+=(int v) { s_ += std::to_string(v); return *this; }
  String& operator+=(unsigned v) { s_ += std::to_string(v); return *this; }
  String& operator+=(unsigned long v) { s_ += std::to_string(v); return *this; }

  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  void reserve(size_t n) { s_.reserve(n); }
  void trim();
  String substring(size_t from) const { return String(s_.substr(from)); }
  char operator[](size_t i) const { return s_[i]; }
  long toInt() const { return strtol(s_.c_str(), NULL, 10); }
  bool equals(const char* p) const { return p && s_ == p; }
  bool operator==(const char* p) const { return p && s_ == p; }
  bool operator!=(const char* p) const { return !(*this == p); }
  const std::string& str() const { return s_; }

 private:
  std::string s_;
};

inline String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
inline String operator+(const String& a, const char* b) { String r(a); r += b; return r; }

inline void String::trim() {
  size_t b = s_.find_first_not_of(" \t\r\n");
  if (b == std::string::npos) { s_.clear(); return; }
  size_t e = s_.find_last_not_of(" \t\r\n");
  s_ = s_.substr(b, e - b + 1);
}

/* F() на хосте — просто строка. */
#define F(x) (x)

/* ESP — только то, что читает интерфейс (свободная память). */
struct EspStub {
  uint32_t heapFree;
  uint32_t getFreeHeap() const { return heapFree; }
  uint32_t getChipId() const { return 0x00A1B2C3; }
};
extern EspStub ESP;
