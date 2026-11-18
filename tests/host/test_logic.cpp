// Хост-тесты логики прошивки (без железа).
//
//   cd tests/host && make && ./test_logic
//
// Проверяется: хранение/CRC, драйвер помп и датчика, планировщик задач.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "Arduino.h"
#include "EEPROM.h"

#include "../../src/app_config.h"
#include "../../src/channels.h"
#include "../../src/clock.h"
#include "../../src/scheduler.h"
#include "../../src/storage.h"

// --- заглушки, которые ждут модули прошивки ---
uint32_t g_millis = 0;
int      g_pins[17];
int      g_pin_mode[17];
int      g_pullup[17];
uint8_t  g_eeprom[FAKE_EEPROM_SIZE];
int      g_eeprom_commits = 0;
SerialStub Serial;
EEPROMStub EEPROM;

static time_t g_now = 0;
extern "C" time_t time(time_t* t) {
  if (t) *t = g_now;
  return g_now;
}

// --- инфраструктура тестов ---
static int g_fails = 0;
static int g_checks = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
      g_fails++;                                                                \
      printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    g_checks++;                                                                 \
    long _a = (long)(a), _b = (long)(b);                                        \
    if (_a != _b) {                                                             \
      g_fails++;                                                                \
      printf("  FAIL %s:%d  %s == %s  (%ld != %ld)\n", __FILE__, __LINE__, #a,  \
             #b, _a, _b);                                                       \
    }                                                                          \
  } while (0)

static time_t make_time(int year, int mon, int day, int hour, int min) {
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = year - 1900;
  t.tm_mon = mon - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = min;
  t.tm_isdst = -1;
  return mktime(&t);
}

static const char* fmt(time_t t) {
  static char b[32];
  struct tm tv;
  localtime_r(&t, &tv);
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &tv);
  return b;
}

static void reset_all() {
  g_millis = 0;
  for (int i = 0; i < 17; i++) {
    g_pins[i] = -1;       // "висит в воздухе"
    g_pin_mode[i] = 0;
    g_pullup[i] = 0;
  }
  memset(g_eeprom, 0xFF, sizeof(g_eeprom));
  g_eeprom_commits = 0;
  g_now = make_time(2026, 11, 18, 9, 30);
}

// ------------------------------------------------------------------ storage
static void test_storage() {
  printf("[storage]\n");
  reset_all();

  bool valid = storage_begin();
  CHECK_EQ(valid, 0);                       // EEPROM пустой -> умолчания
  CHECK_EQ(g_eeprom_commits, 1);
  CHECK_EQ(g_store.magic, STORE_MAGIC);
  CHECK_EQ(g_store.cfg.sensorPin, DEFAULT_SENSOR_PIN);
  CHECK_EQ(g_store.cfg.ch[0].pin, DEFAULT_PUMP_PIN_0);
  CHECK_EQ(g_store.cfg.ch[3].pin, DEFAULT_PUMP_PIN_3);
  CHECK_EQ(g_store.cfg.maxRunSec, DEFAULT_MAX_RUN_SEC);
  CHECK_EQ(g_store.cfg.tzOffsetMin, DEFAULT_TZ_OFFSET_MIN);  // UTC+3 (МСК)
  CHECK_EQ(g_store.version, STORE_VERSION);
  CHECK(strcmp(g_store.cfg.ntpServer, DEFAULT_NTP_SERVER) == 0);

  // сохранение и повторная загрузка
  strcpy(g_store.cfg.ssid, "HomeNet");
  g_store.tasks[0].used = 1;
  g_store.tasks[0].channel = 2;
  g_store.tasks[0].hour = 7;
  g_store.tasks[0].durationSec = 42;
  g_store.tasks[0].lastRun = (uint32_t)g_now;
  storage_save();
  CHECK_EQ(g_eeprom_commits, 2);

  memset(&g_store, 0, sizeof(g_store));
  CHECK_EQ(storage_begin(), 1);             // данные валидны
  CHECK(strcmp(g_store.cfg.ssid, "HomeNet") == 0);
  CHECK_EQ(g_store.tasks[0].used, 1);
  CHECK_EQ(g_store.tasks[0].durationSec, 42);
  CHECK_EQ(g_store.tasks[0].lastRun, (long)g_now);

  // повреждение данных -> умолчания
  g_eeprom[20] ^= 0xFF;
  memset(&g_store, 0, sizeof(g_store));
  CHECK_EQ(storage_begin(), 0);
  CHECK_EQ(g_store.cfg.ssid[0], 0);

  // сброс к заводским
  strcpy(g_store.cfg.ssid, "X");
  storage_factory_reset();
  CHECK_EQ(g_store.cfg.ssid[0], 0);
  CHECK_EQ(g_store.tasks[0].used, 0);
}

// ----------------------------------------------------------------- channels
static void test_channels() {
  printf("[channels]\n");
  reset_all();
  storage_begin();
  g_store.cfg.tzOffsetMin = 0;   // тесты не зависят от пояса

  // по умолчанию активный ВЫСОКИЙ уровень: в выключенном состоянии - LOW
  channels_begin();
  CHECK_EQ(g_pins[g_store.cfg.ch[0].pin], LOW);
  CHECK_EQ(g_pin_mode[g_store.cfg.ch[0].pin], OUTPUT);

  // датчик: pullup -> "вода в норме"
  CHECK_EQ(sensor_is_low(), 0);

  // пуск на 30 секунд
  CHECK_EQ(channels_start(0, 30, -1), START_OK);
  CHECK_EQ(g_pins[g_store.cfg.ch[0].pin], HIGH);
  CHECK_EQ(channels_is_on(0), 1);
  CHECK_EQ(channels_remaining_sec(0), 30);

  g_millis += 10000;
  channels_update();
  CHECK_EQ(channels_is_on(0), 1);
  CHECK_EQ(channels_remaining_sec(0), 20);

  g_millis += 20000;
  channels_update();
  CHECK_EQ(channels_is_on(0), 0);
  CHECK_EQ(g_pins[g_store.cfg.ch[0].pin], LOW);

  // ограничение максимальной длительности
  g_store.cfg.maxRunSec = 5;
  CHECK_EQ(channels_start(1, 600, -1), START_OK);
  CHECK_EQ(channels_remaining_sec(1), 5);
  channels_stop(1);
  g_store.cfg.maxRunSec = DEFAULT_MAX_RUN_SEC;

  // нулевая длительность не запускается
  CHECK_EQ(channels_start(0, 0, -1), START_BAD_SECONDS);
  CHECK_EQ(channels_start(9, 10, -1), START_BAD_CHANNEL);

  // низкий уровень воды блокирует пуск (активный низкий у датчика по умолчанию)
  g_pins[g_store.cfg.sensorPin] = LOW;
  for (int i = 0; i < 10; i++) { g_millis += 30; channels_update(); }
  CHECK_EQ(sensor_is_low(), 1);
  CHECK_EQ(channels_start(0, 10, -1), START_LOW_WATER);

  // вода появилась, помпа работает, затем вода кончается -> аварийный стоп
  g_pins[g_store.cfg.sensorPin] = HIGH;
  for (int i = 0; i < 10; i++) { g_millis += 30; channels_update(); }
  CHECK_EQ(sensor_is_low(), 0);
  CHECK_EQ(channels_start(0, 60, 3), START_OK);
  CHECK_EQ(channels_task(0), 3);

  g_pins[g_store.cfg.sensorPin] = LOW;
  for (int i = 0; i < 10; i++) { g_millis += 30; channels_update(); }
  CHECK_EQ(channels_is_on(0), 0);
  CHECK_EQ(g_pins[g_store.cfg.ch[0].pin], LOW);

  // инверсия канала: включение низким уровнем
  g_pins[g_store.cfg.sensorPin] = HIGH;
  g_store.cfg.ch[3].activeLow = 1;
  channels_apply_config();
  CHECK_EQ(g_pins[g_store.cfg.ch[3].pin], HIGH);  // выключено = HIGH
  channels_start(3, 10, -1);
  CHECK_EQ(g_pins[g_store.cfg.ch[3].pin], LOW);   // включено = LOW
  channels_stop(3);
  CHECK_EQ(g_pins[g_store.cfg.ch[3].pin], HIGH);
}

// ---------------------------------------------------------------- scheduler
static void add_task(uint8_t idx, uint8_t ch, uint8_t hour, uint8_t min,
                     uint16_t dur, uint8_t period) {
  TaskCfg& t = g_store.tasks[idx];
  memset(&t, 0, sizeof(t));
  t.used = 1;
  t.enabled = 1;
  t.channel = ch;
  t.hour = hour;
  t.minute = min;
  t.durationSec = dur;
  t.periodDays = period;
}

static void test_scheduler() {
  printf("[scheduler]\n");
  reset_all();
  storage_begin();
  g_store.cfg.tzOffsetMin = 0;
  channels_begin();

  g_store.cfg.catchupHours = 6;
  add_task(0, 0, 8, 0, 30, 1);

  // 09:30 - первый расчёт: полив уже был "пропущен" сегодня, значит ждём завтра
  g_now = make_time(2026, 11, 18, 9, 30);
  sched_update();
  CHECK_EQ(g_store.tasks[0].nextDue, (long)make_time(2026, 11, 19, 8, 0));
  CHECK_EQ(channels_is_on(0), 0);

  // ровно 08:00 следующего дня -> полив
  g_now = make_time(2026, 11, 19, 8, 0);
  sched_update();
  CHECK_EQ(channels_is_on(0), 1);
  CHECK_EQ(g_store.tasks[0].lastRun, (long)g_now);
  CHECK_EQ(g_store.tasks[0].nextDue, (long)make_time(2026, 11, 20, 8, 0));
  CHECK(strcmp(sched_status_str(0), "running") == 0);

  channels_stop(0);
  CHECK(strcmp(sched_status_str(0), "wait") == 0);

  // период 3 дня
  add_task(1, 1, 7, 15, 20, 3);
  g_store.tasks[1].lastRun = (uint32_t)make_time(2026, 11, 19, 7, 15);
  g_store.tasks[1].nextDue = 0;
  g_now = make_time(2026, 11, 19, 12, 0);
  sched_update();
  CHECK_EQ(g_store.tasks[1].nextDue, (long)make_time(2026, 11, 22, 7, 15));

  // догоняющий полив: 2 часа опоздания при окне 6 часов -> поливаем
  add_task(2, 2, 6, 0, 15, 1);
  g_store.tasks[2].lastRun = (uint32_t)make_time(2026, 11, 18, 6, 0);
  g_store.tasks[2].nextDue = (uint32_t)make_time(2026, 11, 19, 6, 0);
  g_now = make_time(2026, 11, 19, 8, 0);
  sched_update();
  CHECK_EQ(channels_is_on(2), 1);
  CHECK_EQ(g_store.tasks[2].lastRun, (long)g_now);
  CHECK_EQ(g_store.tasks[2].nextDue, (long)make_time(2026, 11, 20, 6, 0));
  channels_stop(2);

  // слишком поздно (8 часов при окне 6) -> пропуск, расписание сдвигается
  add_task(3, 3, 6, 0, 15, 1);
  g_store.tasks[3].lastRun = (uint32_t)make_time(2026, 11, 18, 6, 0);
  g_store.tasks[3].nextDue = (uint32_t)make_time(2026, 11, 19, 6, 0);
  g_now = make_time(2026, 11, 19, 14, 0);
  sched_update();
  CHECK_EQ(channels_is_on(3), 0);
  CHECK_EQ(g_store.tasks[3].lastRun, (long)make_time(2026, 11, 18, 6, 0));
  CHECK_EQ(g_store.tasks[3].nextDue, (long)make_time(2026, 11, 20, 6, 0));

  // catchupHours = 0: строго по расписанию, опоздание на 2 минуты -> пропуск
  g_store.cfg.catchupHours = 0;
  add_task(4, 0, 10, 0, 15, 1);
  g_store.tasks[4].lastRun = (uint32_t)make_time(2026, 11, 18, 10, 0);
  g_store.tasks[4].nextDue = (uint32_t)make_time(2026, 11, 19, 10, 0);
  g_now = make_time(2026, 11, 19, 10, 2);
  sched_update();
  CHECK_EQ(channels_is_on(0), 0);
  CHECK_EQ(g_store.tasks[4].nextDue, (long)make_time(2026, 11, 20, 10, 0));
  g_store.cfg.catchupHours = 6;

  // выключенная задача не поливает
  add_task(5, 1, 9, 0, 10, 1);
  g_store.tasks[5].enabled = 0;
  g_now = make_time(2026, 11, 19, 9, 0);
  sched_update();
  CHECK_EQ(channels_is_on(1), 0);
  CHECK(strcmp(sched_status_str(5), "off") == 0);

  // ручной пуск не меняет статус задачи
  add_task(6, 2, 12, 0, 10, 1);
  g_store.tasks[6].lastRun = (uint32_t)make_time(2026, 11, 18, 12, 0);
  g_store.tasks[6].nextDue = (uint32_t)make_time(2026, 11, 19, 12, 0);
  uint32_t last_before = g_store.tasks[6].lastRun;
  uint32_t next_before = g_store.tasks[6].nextDue;
  channels_start(g_store.tasks[6].channel, g_store.tasks[6].durationSec, 6);
  CHECK_EQ(channels_is_on(2), 1);
  CHECK_EQ(g_store.tasks[6].lastRun, (long)last_before);
  CHECK_EQ(g_store.tasks[6].nextDue, (long)next_before);
  CHECK(strcmp(sched_status_str(6), "running") == 0);
  channels_stop(2);

  // нет синхронизации времени -> задачи не запускаются
  g_now = 1000;
  CHECK_EQ(sched_time_valid(), 0);
  add_task(7, 3, 11, 0, 10, 1);
  g_store.tasks[7].nextDue = 0;
  sched_update();
  CHECK_EQ(channels_is_on(3), 0);
  CHECK(strcmp(sched_status_str(7), "nosync") == 0);

  printf("  (пример: задача 0 nextDue=%s)\n", fmt((time_t)g_store.tasks[0].nextDue));
}

// --------------------------------------------------------------------- clock
static void test_clock() {
  printf("[clock]\n");
  reset_all();
  storage_begin();

  g_now = 0;  // NTP ещё не синхронизирован
  CHECK_EQ(clock_valid(), 0);
  CHECK(strcmp(clock_source(), "none") == 0);

  uint32_t utc = (uint32_t)make_time(2026, 11, 18, 9, 30);
  clock_set_from_phone(utc);
  CHECK_EQ(clock_valid(), 1);
  CHECK(strcmp(clock_source(), "manual") == 0);
  CHECK_EQ(clock_utc(), utc);
  CHECK_EQ(clock_local(), utc + 3 * 3600);  // пояс по умолчанию UTC+3

  // ручное время "идёт" от millis()
  g_millis += 65000;
  CHECK_EQ(clock_utc(), utc + 65);

  char buf[24];
  clock_format_full(clock_local(), buf, sizeof(buf));
  CHECK(strcmp(buf, "18.11.2026 12:31") == 0);

  // как только появился NTP - он приоритетнее
  g_now = make_time(2026, 11, 18, 10, 0);
  CHECK(strcmp(clock_source(), "ntp") == 0);
  CHECK_EQ(clock_utc(), (long)g_now);

  // смена пояса меняет локальную шкалу
  g_store.cfg.tzOffsetMin = 0;
  CHECK_EQ(clock_local(), (long)g_now);
  g_store.cfg.tzOffsetMin = 180;

  // часовой пояс/время форматируются в формате UTC±HH:MM на стороне UI (JS),
  // здесь проверяем только арифметику суток
  uint32_t local = clock_local();
  CHECK_EQ(clock_at_time(local, 1, 8, 0), clock_day_start(local) + 86400 + 8 * 3600);
}

// ------------------------------------------ расписание в местном времени
static void test_local_schedule() {
  printf("[local schedule]\n");
  reset_all();
  storage_begin();
  g_store.cfg.tzOffsetMin = 180;   // UTC+3
  channels_begin();

  // 09:00 по Москве (06:00 UTC), задача на 08:00 -> следующий пуск завтра в 08:00
  g_now = make_time(2026, 11, 18, 6, 0);
  add_task(0, 0, 8, 0, 30, 1);
  sched_update();

  char buf[24];
  clock_format_full(g_store.tasks[0].nextDue, buf, sizeof(buf));
  CHECK(strcmp(buf, "19.11.2026 08:00") == 0);   // не 11:00 - баг был в поясах

  // наступает 08:00 по Москве (05:00 UTC) - полив
  g_now = make_time(2026, 11, 19, 5, 0);
  sched_update();
  CHECK_EQ(channels_is_on(0), 1);
  clock_format_full(g_store.tasks[0].lastRun, buf, sizeof(buf));
  CHECK(strcmp(buf, "19.11.2026 08:00") == 0);

  // и следующий пуск через сутки в то же местное время
  clock_format_full(g_store.tasks[0].nextDue, buf, sizeof(buf));
  CHECK(strcmp(buf, "20.11.2026 08:00") == 0);
  channels_stop(0);

  // без времени (ни NTP, ни телефона) задачи не запускаются
  g_now = 0;
  clock_clear_manual();
  CHECK_EQ(sched_time_valid(), 0);
  sched_update();               // не должно ничего запустить/сломать
  CHECK_EQ(channels_is_on(0), 0);
}

// ------------------------------------------------ события выключения каналов
static void test_stop_events() {
  printf("[stop events]\n");
  reset_all();
  storage_begin();
  g_store.cfg.tzOffsetMin = 0;
  channels_begin();

  CHECK_EQ(channels_stop_seq(0), 0);
  CHECK_EQ(channels_stop_reason(0), STOP_REASON_NONE);

  // 1. истекло заданное время полива -> reason=timer
  CHECK_EQ(channels_start(0, 10, -1), START_OK);
  g_millis += 10000;
  channels_update();
  CHECK_EQ(channels_is_on(0), 0);
  CHECK_EQ(channels_stop_seq(0), 1);
  CHECK_EQ(channels_stop_reason(0), STOP_REASON_TIMER);
  CHECK_EQ(channels_last_run_sec(0), 10);

  // 2. ручная остановка -> reason=manual + фактическая длительность
  CHECK_EQ(channels_start(0, 60, -1), START_OK);
  g_millis += 4000;
  channels_stop(0);
  CHECK_EQ(channels_stop_seq(0), 2);
  CHECK_EQ(channels_stop_reason(0), STOP_REASON_MANUAL);
  CHECK_EQ(channels_last_run_sec(0), 4);

  // 3. повторный стоп уже выключенного канала событие не создаёт
  channels_stop(0);
  CHECK_EQ(channels_stop_seq(0), 2);

  // 4. аварийный стоп по низкому уровню воды -> reason=low
  CHECK_EQ(channels_start(1, 60, -1), START_OK);
  g_pins[g_store.cfg.sensorPin] = LOW;
  for (int i = 0; i < 10; i++) { g_millis += 30; channels_update(); }
  CHECK_EQ(channels_is_on(1), 0);
  CHECK_EQ(channels_stop_seq(1), 1);
  CHECK_EQ(channels_stop_reason(1), STOP_REASON_LOW_WATER);

  // 5. счётчики каналов независимы
  CHECK_EQ(channels_stop_seq(2), 0);
  CHECK_EQ(channels_stop_reason(2), STOP_REASON_NONE);
}

int main() {
  setenv("TZ", "UTC", 1);
  tzset();

  test_storage();
  test_channels();
  test_scheduler();
  test_clock();
  test_local_schedule();
  test_stop_events();

  printf("\n%d проверок, ошибок: %d\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
