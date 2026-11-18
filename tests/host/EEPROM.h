// Хост-заглушка EEPROM (эмуляция сектора во flash через массив).
#pragma once
#include <stdint.h>
#include <string.h>

#define FAKE_EEPROM_SIZE 4096

extern uint8_t g_eeprom[FAKE_EEPROM_SIZE];
extern int     g_eeprom_commits;

class EEPROMStub {
 public:
  bool begin(size_t) { return true; }
  bool commit() { g_eeprom_commits++; return true; }

  template <typename T>
  T& get(int idx, T& t) {
    memcpy(&t, g_eeprom + idx, sizeof(T));
    return t;
  }

  template <typename T>
  const T& put(int idx, const T& t) {
    memcpy(g_eeprom + idx, &t, sizeof(T));
    return t;
  }
};

extern EEPROMStub EEPROM;
