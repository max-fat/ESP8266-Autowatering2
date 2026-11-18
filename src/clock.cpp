#include "clock.h"

#include <Arduino.h>
#include <time.h>

#include "storage.h"

// Ручное время с телефона: база в UTC и отметка millis() в момент установки.
static uint32_t s_manualUtc = 0;
static uint32_t s_manualSetMs = 0;

// Ядро отдаёт время по NTP. configTime() вызывается с нулевым поясом (см. netmgr),
// поэтому time() - это UTC в обоих поддерживаемых ядрах.
static uint32_t s_ntp_utc() {
  uint32_t t = (uint32_t)time(nullptr);
  return (t > CLOCK_TIME_VALID_MIN) ? t : 0;
}

int32_t clock_tz_sec() {
  return (int32_t)g_store.cfg.tzOffsetMin * 60L;
}

uint32_t clock_utc() {
  uint32_t ntp = s_ntp_utc();
  if (ntp) return ntp;
  if (s_manualUtc) return s_manualUtc + (millis() - s_manualSetMs) / 1000UL;
  return 0;
}

uint32_t clock_local() {
  uint32_t utc = clock_utc();
  if (!utc) return 0;
  return (uint32_t)((int64_t)utc + clock_tz_sec());
}

bool clock_valid() {
  return clock_utc() != 0;
}

const char* clock_source() {
  if (s_ntp_utc()) return "ntp";
  if (s_manualUtc) return "manual";
  return "none";
}

void clock_clear_manual() {
  s_manualUtc = 0;
  s_manualSetMs = 0;
}

void clock_set_from_phone(uint32_t utcSeconds) {
  if (utcSeconds < CLOCK_TIME_VALID_MIN) return;
  // Если NTP уже есть - он точнее, ручную установку игнорируем.
  if (s_ntp_utc()) return;
  s_manualUtc = utcSeconds;
  s_manualSetMs = millis();
  Serial.printf("[clock] время с телефона: %lu (UTC)\n", (unsigned long)utcSeconds);
}

uint32_t clock_day_start(uint32_t localEpoch) {
  return (localEpoch / 86400UL) * 86400UL;
}

uint32_t clock_at_time(uint32_t localEpoch, int days, uint8_t hour, uint8_t minute) {
  int64_t dayIndex = (int64_t)(localEpoch / 86400UL) + days;
  return (uint32_t)(dayIndex * 86400LL + (int32_t)hour * 3600L + (int32_t)minute * 60L);
}

uint32_t clock_add_minutes(uint32_t localEpoch, int minutes) {
  return (uint32_t)((int64_t)localEpoch + (int64_t)minutes * 60LL);
}

static void fill_tm(uint32_t localEpoch, struct tm* tmv) {
  time_t t = (time_t)localEpoch;
  gmtime_r(&t, tmv);  // локальная шкала уже сдвинута, поэтому UTC-разбор корректен
}

void clock_format_short(uint32_t localEpoch, char* buf, size_t n) {
  if (!localEpoch) {
    strncpy(buf, "—", n);
    if (n) buf[n - 1] = '\0';
    return;
  }
  struct tm tmv;
  fill_tm(localEpoch, &tmv);
  snprintf(buf, n, "%02d.%02d %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_hour, tmv.tm_min);
}

void clock_format_full(uint32_t localEpoch, char* buf, size_t n) {
  if (!localEpoch) {
    strncpy(buf, "—", n);
    if (n) buf[n - 1] = '\0';
    return;
  }
  struct tm tmv;
  fill_tm(localEpoch, &tmv);
  snprintf(buf, n, "%02d.%02d.%04d %02d:%02d", tmv.tm_mday, tmv.tm_mon + 1,
           tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min);
}

void clock_now_strings(char* timeBuf, size_t timeLen, char* dateBuf, size_t dateLen) {
  uint32_t now = clock_local();
  if (!now) {
    snprintf(timeBuf, timeLen, "--:--:--");
    snprintf(dateBuf, dateLen, "нет времени");
    return;
  }
  struct tm tmv;
  fill_tm(now, &tmv);
  snprintf(timeBuf, timeLen, "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  snprintf(dateBuf, dateLen, "%02d.%02d.%04d", tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_year + 1900);
}
