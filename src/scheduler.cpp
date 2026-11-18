#include "scheduler.h"

#include <Arduino.h>
#include <time.h>

#include "channels.h"
#include "storage.h"

#define TIME_VALID_MIN 1700000000UL  // 14.11.2023 - всё, что меньше, считаем "время не синхронизировано"

bool sched_time_valid() {
  return (uint32_t)time(nullptr) > TIME_VALID_MIN;
}

// Дата base + days дней, но со временем hour:minute (локальное время).
static time_t at_day_time(time_t base, int days, uint8_t hour, uint8_t minute) {
  struct tm tmv;
  localtime_r(&base, &tmv);
  tmv.tm_mday += days;
  tmv.tm_hour = hour;
  tmv.tm_min = minute;
  tmv.tm_sec = 0;
  tmv.tm_isdst = -1;
  return mktime(&tmv);
}

// Ближайшее наступление hour:minute строго после now.
static time_t next_occurrence(time_t now, uint8_t hour, uint8_t minute) {
  time_t cand = at_day_time(now, 0, hour, minute);
  if (cand <= now) cand = at_day_time(now, 1, hour, minute);
  if (cand <= now) cand = now + 60;  // страховка от странностей с TZ
  return cand;
}

static uint8_t task_period(const TaskCfg& t) {
  uint8_t p = t.periodDays;
  if (p < 1) p = 1;
  if (p > TASK_MAX_PERIOD_DAYS) p = TASK_MAX_PERIOD_DAYS;
  return p;
}

// Расчёт ближайшего планового времени, если nextDue ещё не сохранён.
// Если задача уже поливалась (lastRun != 0), период отсчитывается от неё,
// иначе берётся ближайшее наступление указанного времени суток.
static time_t compute_next_due(const TaskCfg& t, time_t now) {
  uint8_t period = task_period(t);
  if (t.lastRun != 0) {
    time_t cand = at_day_time((time_t)t.lastRun, period, t.hour, t.minute);
    uint16_t guard = 0;
    while (cand <= now && guard++ < 20000) {
      cand = at_day_time(cand, period, t.hour, t.minute);
    }
    if (cand > now) return cand;
  }
  return next_occurrence(now, t.hour, t.minute);
}

time_t sched_next_run(const TaskCfg& t) {
  if (!t.used || !t.enabled) return 0;
  if (t.nextDue != 0) return (time_t)t.nextDue;
  if (!sched_time_valid()) return 0;
  return compute_next_due(t, time(nullptr));
}

void sched_task_changed(uint8_t idx) {
  if (idx >= MAX_TASKS) return;
  g_store.tasks[idx].nextDue = 0;
}

uint8_t sched_task_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    if (g_store.tasks[i].used) n++;
  }
  return n;
}

const char* sched_status_str(uint8_t idx) {
  if (idx >= MAX_TASKS) return "free";
  const TaskCfg& t = g_store.tasks[idx];
  if (!t.used) return "free";
  if (!t.enabled) return "off";
  if (channels_is_on(t.channel) && channels_task(t.channel) == (int8_t)idx) return "running";
  if (!sched_time_valid()) return "nosync";
  return "wait";
}

void sched_update() {
  if (!sched_time_valid()) return;

  time_t now = time(nullptr);
  bool dirty = false;

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    TaskCfg& t = g_store.tasks[i];
    if (!t.used || !t.enabled) continue;

    if (t.nextDue == 0) {
      t.nextDue = (uint32_t)compute_next_due(t, now);
      dirty = true;
    }

    time_t due = (time_t)t.nextDue;
    if (now < due) continue;

    uint32_t late = (uint32_t)(now - due);
    uint32_t window = (uint32_t)g_store.cfg.catchupHours * 3600UL;
    // Окно 0 = "поливать строго по расписанию", допускаем минуту на тики.
    bool doRun = (window == 0) ? (late < 60) : (late <= window);

    uint8_t period = task_period(t);
    time_t nd = at_day_time(due, period, t.hour, t.minute);
    uint16_t guard = 0;
    while (nd <= now && guard++ < 20000) {
      nd = at_day_time(nd, period, t.hour, t.minute);
    }
    if (nd <= now) nd = now + (time_t)period * 86400;

    if (doRun) {
      StartResult r = channels_start(t.channel, t.durationSec, (int8_t)i);
      if (r == START_OK) {
        t.lastRun = (uint32_t)now;
        Serial.printf("[sched] task %u: channel %u on for %u s\n", i, t.channel, t.durationSec);
      } else {
        Serial.printf("[sched] task %u skipped (start result %d)\n", i, (int)r);
      }
    } else {
      Serial.printf("[sched] task %u missed by %lu s, rescheduled\n", i, (unsigned long)late);
    }

    t.nextDue = (uint32_t)nd;
    dirty = true;
  }

  if (dirty) storage_save();
}
