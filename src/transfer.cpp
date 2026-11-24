#include "transfer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool transfer_task_encode(const TaskCfg& t, char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  int n = snprintf(out, outSize, "%u,%u,%u,%u,%u,%u,%lu,%lu",
                   (unsigned)t.channel, (unsigned)t.hour, (unsigned)t.minute,
                   (unsigned)t.durationSec, (unsigned)t.periodDays,
                   (unsigned)(t.enabled ? 1 : 0),
                   (unsigned long)t.lastRun, (unsigned long)t.nextDue);
  if (n < 0 || (size_t)n >= outSize) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool transfer_task_decode(const char* s, TaskCfg* out) {
  if (!s || !out) return false;

  unsigned long v[TRANSFER_TASK_FIELDS];
  int field = 0;
  const char* p = s;

  while (field < TRANSFER_TASK_FIELDS) {
    while (*p == ' ' || *p == '\t') p++;
    if (*p < '0' || *p > '9') return false;   /* пустое, нечисловое или отрицательное */

    unsigned long val = 0;
    while (*p >= '0' && *p <= '9') {
      if (val > 429496729UL) return false;    /* защита от переполнения 32 бит */
      val = val * 10 + (unsigned long)(*p - '0');
      p++;
    }
    v[field++] = val;

    while (*p == ' ' || *p == '\t') p++;
    if (field < TRANSFER_TASK_FIELDS) {
      if (*p != ',') return false;            /* полей меньше, чем нужно */
      p++;
    } else if (*p != '\0') {
      return false;                           /* лишние данные после задачи */
    }
  }

  if (v[0] >= NUM_CHANNELS) return false;
  if (v[1] > 23) return false;
  if (v[2] > 59) return false;
  if (v[3] < TASK_MIN_DURATION_SEC || v[3] > TASK_MAX_DURATION_SEC) return false;
  if (v[4] < 1 || v[4] > TASK_MAX_PERIOD_DAYS) return false;
  if (v[5] > 1) return false;

  TaskCfg t;
  memset(&t, 0, sizeof(t));
  t.used = 1;
  t.enabled = (uint8_t)v[5];
  t.channel = (uint8_t)v[0];
  t.hour = (uint8_t)v[1];
  t.minute = (uint8_t)v[2];
  t.durationSec = (uint16_t)v[3];
  t.periodDays = (uint8_t)v[4];
  t.lastRun = (uint32_t)v[6];
  t.nextDue = (uint32_t)v[7];

  *out = t;
  return true;
}
