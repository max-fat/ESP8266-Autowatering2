#include "scheduler.h"

#include <Arduino.h>

#include "channels.h"
#include "clock.h"
#include "storage.h"

bool sched_time_valid() {
  return clock_valid();
}

static uint8_t task_period(const TaskCfg& t) {
  uint8_t p = t.periodDays;
  if (p < 1) p = 1;
  if (p > TASK_MAX_PERIOD_DAYS) p = TASK_MAX_PERIOD_DAYS;
  return p;
}

// Ближайшее наступление hour:minute строго после now (в локальной шкале).
static uint32_t next_occurrence(uint32_t now, uint8_t hour, uint8_t minute) {
  uint32_t cand = clock_at_time(now, 0, hour, minute);
  if (cand <= now) cand = clock_at_time(now, 1, hour, minute);
  if (cand <= now) cand = now + 60;  // страховка
  return cand;
}

// Расчёт ближайшего планового времени, если nextDue ещё не сохранён.
// Если задача уже поливалась (lastRun != 0), период отсчитывается от неё,
// иначе берётся ближайшее наступление указанного времени суток.
static uint32_t compute_next_due(const TaskCfg& t, uint32_t now) {
  uint8_t period = task_period(t);
  if (t.lastRun != 0) {
    uint32_t cand = clock_at_time(t.lastRun, period, t.hour, t.minute);
    uint16_t guard = 0;
    while (cand <= now && guard++ < 20000) {
      cand = clock_at_time(cand, period, t.hour, t.minute);
    }
    if (cand > now) return cand;
  }
  return next_occurrence(now, t.hour, t.minute);
}

uint32_t sched_next_run(const TaskCfg& t) {
  if (!t.used || !t.enabled) return 0;
  if (t.nextDue != 0) return t.nextDue;
  if (!clock_valid()) return 0;
  return compute_next_due(t, clock_local());
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
  if (!clock_valid()) return "nosync";
  return "wait";
}

void sched_update() {
  if (!clock_valid()) return;

  uint32_t now = clock_local();
  bool dirty = false;

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    TaskCfg& t = g_store.tasks[i];
    if (!t.used || !t.enabled) continue;

    if (t.nextDue == 0) {
      t.nextDue = compute_next_due(t, now);
      dirty = true;
    }

    uint32_t due = t.nextDue;
    if (now < due) continue;

    uint32_t late = now - due;
    uint32_t window = (uint32_t)g_store.cfg.catchupHours * 3600UL;
    // Окно 0 = "поливать строго по расписанию", допускаем минуту на тики.
    bool doRun = (window == 0) ? (late < 60) : (late <= window);

    uint8_t period = task_period(t);
    uint32_t nd = clock_at_time(due, period, t.hour, t.minute);
    uint16_t guard = 0;
    while (nd <= now && guard++ < 20000) {
      nd = clock_at_time(nd, period, t.hour, t.minute);
    }
    if (nd <= now) nd = now + (uint32_t)period * 86400UL;

    if (doRun) {
      StartResult r = channels_start(t.channel, t.durationSec, (int8_t)i);
      if (r == START_OK) {
        t.lastRun = now;
        Serial.printf("[sched] task %u: channel %u on for %u s\n", i, t.channel, t.durationSec);
      } else {
        Serial.printf("[sched] task %u skipped (start result %d)\n", i, (int)r);
      }
    } else {
      Serial.printf("[sched] task %u missed by %lu s, rescheduled\n", i, (unsigned long)late);
    }

    t.nextDue = nd;
    dirty = true;
  }

  if (dirty) storage_save();
}
