#include "storage.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

Store g_store;

uint32_t crc32_buf(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t crc = 0xFFFFFFFFUL;
  while (len--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; i++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
  }
  return ~crc;
}

static void copy_str(char* dst, size_t dstSize, const char* src) {
  if (!src) src = "";
  strncpy(dst, src, dstSize - 1);
  dst[dstSize - 1] = '\0';
}

void storage_defaults() {
  memset(&g_store, 0, sizeof(g_store));
  g_store.magic = STORE_MAGIC;
  g_store.version = STORE_VERSION;

  Settings& c = g_store.cfg;
  c.ssid[0] = '\0';
  c.pass[0] = '\0';
  copy_str(c.ntpServer, sizeof(c.ntpServer), DEFAULT_NTP_SERVER);
  c.tzOffsetMin     = DEFAULT_TZ_OFFSET_MIN;
  c.sensorPin       = DEFAULT_SENSOR_PIN;
  c.sensorActiveLow = 1;   // геркон/поплавок замыкается на GND при низком уровне
  c.sensorPullup    = 1;
  c.blockWhenLow    = 1;
  c.catchupHours    = DEFAULT_CATCHUP_HOURS;
  c.maxRunSec       = DEFAULT_MAX_RUN_SEC;

  const uint8_t pins[NUM_CHANNELS] = {
    DEFAULT_PUMP_PIN_0, DEFAULT_PUMP_PIN_1, DEFAULT_PUMP_PIN_2, DEFAULT_PUMP_PIN_3
  };
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    g_store.cfg.ch[i].pin = pins[i];
    g_store.cfg.ch[i].activeLow = 0;  // по умолчанию активный ВЫСОКИЙ уровень:
                                      // при старте ESP на пинах 0 В -> помпы выключены
    g_store.cfg.ch[i].reserved = 0;
  }

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    memset(&g_store.tasks[i], 0, sizeof(TaskCfg));
  }
  g_store.crc = 0;
}

bool storage_begin() {
  EEPROM.begin(EEPROM_BYTES);
  EEPROM.get(0, g_store);

  bool ok = (g_store.magic == STORE_MAGIC) && (g_store.version == STORE_VERSION);
  if (ok) {
    uint32_t crc = g_store.crc;
    g_store.crc = 0;
    ok = (crc32_buf(&g_store, sizeof(g_store)) == crc);
    g_store.crc = crc;
  }

  if (!ok) {
    storage_defaults();
    storage_save();
    return false;
  }
  return true;
}

void storage_save() {
  g_store.magic = STORE_MAGIC;
  g_store.version = STORE_VERSION;
  g_store.crc = 0;
  g_store.crc = crc32_buf(&g_store, sizeof(g_store));
  EEPROM.put(0, g_store);
  EEPROM.commit();
}

void storage_factory_reset() {
  storage_defaults();
  storage_save();
}
