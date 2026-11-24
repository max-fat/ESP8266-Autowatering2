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
#include "../../src/scanlist.h"
#include "../../src/scheduler.h"
#include "../../src/storage.h"
#include "../../src/text.h"
#include "../../src/transfer.h"
#include "../../src/webjson.h"
#include "../../src/otacheck.h"

// --- заглушки, которые ждут модули прошивки ---
uint32_t g_millis = 0;
int      g_pins[17];
int      g_pin_mode[17];
int      g_pullup[17];
uint8_t  g_eeprom[FAKE_EEPROM_SIZE];
int      g_eeprom_commits = 0;
SerialStub Serial;
EEPROMStub EEPROM;

EspStub ESP = { 30000 };          // «свободная память» для /api/state

/* Заглушки netmgr: сборщик JSON берёт из них состояние Wi-Fi. */
static String g_netIp = "192.168.1.50";
bool   net_connected() { return true; }
bool   net_ap_active() { return false; }
bool   net_wifi_configured() { return true; }
String net_ip() { return g_netIp; }
String net_ap_ip() { return "192.168.4.1"; }
String net_ap_ssid() { return "AutoWater-00A1B2"; }
String net_mode_str() { return "sta"; }
int    net_rssi() { return -55; }

/* Заглушки модуля обновления: webjson_ota() собирает JSON из этих значений.
   Сам ota.cpp (Update/flash/перезагрузка) на хосте не собирается. */
uint8_t  ota_state()      { return 2; }        // OTA_DONE
uint32_t ota_total()      { return 314800; }
uint32_t ota_received()   { return 314800; }
uint32_t ota_max_size()   { return 712704; }
const char* ota_error()   { return ""; }

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
  CHECK_EQ(g_store.cfg.name[0], 0);         // имя модуля не задано
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) CHECK_EQ(g_store.cfg.ch[i].name[0], 0);

  // сохранение и повторная загрузка
  strcpy(g_store.cfg.ssid, "HomeNet");
  strcpy(g_store.cfg.name, "Теплица");
  strcpy(g_store.cfg.ch[0].name, "Орхидеи");
  strcpy(g_store.cfg.ch[2].name, "Томаты");
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
  CHECK(strcmp(g_store.cfg.name, "Теплица") == 0);
  CHECK(strcmp(g_store.cfg.ch[0].name, "Орхидеи") == 0);
  CHECK(strcmp(g_store.cfg.ch[2].name, "Томаты") == 0);
  CHECK_EQ(g_store.cfg.ch[1].name[0], 0);
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
  strcpy(g_store.cfg.name, "Y");
  strcpy(g_store.cfg.ch[1].name, "Z");
  storage_factory_reset();
  CHECK_EQ(g_store.cfg.ssid[0], 0);
  CHECK_EQ(g_store.cfg.name[0], 0);
  CHECK_EQ(g_store.cfg.ch[1].name[0], 0);
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

/* ---------------- сканирование Wi-Fi: список сетей ---------------- */
static ScanNet mk(const char* ssid, int rssi, int sec) {
  ScanNet n;
  memset(&n, 0, sizeof(n));
  strncpy(n.ssid, ssid, SCAN_SSID_MAX);
  n.rssi = (int8_t)rssi;
  n.secured = (uint8_t)sec;
  return n;
}

static void test_scanlist() {
  printf("[scan list]\n");

  // 1. Сортировка по уровню сигнала, скрытые сети отброшены
  ScanNet in[5];
  in[0] = mk("Home", -70, 1);
  in[1] = mk("", -40, 0);          // скрытая сеть
  in[2] = mk("Neighbour", -85, 0);
  in[3] = mk("Office", -35, 1);
  ScanNet out[4];
  uint8_t n = scanlist_build(in, 4, out, 4);
  CHECK_EQ(n, 3);
  CHECK(strcmp(out[0].ssid, "Office") == 0);   // самый сильный первым
  CHECK(strcmp(out[1].ssid, "Home") == 0);
  CHECK(strcmp(out[2].ssid, "Neighbour") == 0);

  // 2. Дубликаты SSID склеиваются, остаётся самый сильный сигнал
  ScanNet dup[4];
  dup[0] = mk("Home", -80, 1);
  dup[1] = mk("Home", -50, 0);
  dup[2] = mk("Home", -60, 1);
  dup[3] = mk("Guest", -90, 0);
  n = scanlist_build(dup, 4, out, 4);
  CHECK_EQ(n, 2);
  CHECK(strcmp(out[0].ssid, "Home") == 0);
  CHECK_EQ(out[0].rssi, -50);
  CHECK_EQ(out[0].secured, 1);
  CHECK(strcmp(out[1].ssid, "Guest") == 0);

  // 3. Обрезка списка: слабые сети вытесняются, сильные остаются
  ScanNet many[6];
  many[0] = mk("A", -90, 0);
  many[1] = mk("B", -30, 1);
  many[2] = mk("C", -70, 1);
  many[3] = mk("D", -20, 0);
  many[4] = mk("E", -95, 0);
  many[5] = mk("F", -50, 1);
  n = scanlist_build(many, 6, out, 3);
  CHECK_EQ(n, 3);
  CHECK(strcmp(out[0].ssid, "D") == 0);
  CHECK(strcmp(out[1].ssid, "B") == 0);
  CHECK(strcmp(out[2].ssid, "F") == 0);

  // 4. Пустой вход и нулевой размер выхода
  CHECK_EQ(scanlist_build(NULL, 0, out, 4), 0);
  CHECK_EQ(scanlist_build(in, 4, out, 0), 0);

  // 5. Экранирование для JSON (общий модуль text.*)
  char esc[128];
  text_json_escape("My \"net\" \\ 5G", esc, sizeof(esc));
  CHECK(strcmp(esc, "My \\\"net\\\" \\\\ 5G") == 0);
  text_json_escape("a\tb", esc, sizeof(esc));
  CHECK(strcmp(esc, "a\\tb") == 0);
  text_json_escape("", esc, sizeof(esc));
  CHECK(strcmp(esc, "") == 0);
  // обрезка по размеру буфера: 4 байта -> 3 символа + ноль
  text_json_escape("abcdef", esc, 4);
  CHECK(strcmp(esc, "abc") == 0);
  // кириллица (UTF-8) проходит без изменений
  text_json_escape("Сеть", esc, sizeof(esc));
  CHECK(strcmp(esc, "Сеть") == 0);
}

/* ---------------- пользовательские строки: имена модуля и каналов ---------------- */
static void test_text() {
  printf("[text]\n");

  char buf[16];

  // 1. обычная строка, пробелы по краям срезаются
  text_set(buf, sizeof(buf), "  Теплица  ");
  CHECK(strcmp(buf, "Теплица") == 0);

  // 2. пустая строка и NULL -> пустая строка
  text_set(buf, sizeof(buf), "");
  CHECK_EQ(buf[0], 0);
  text_set(buf, sizeof(buf), NULL);
  CHECK_EQ(buf[0], 0);
  text_set(buf, sizeof(buf), "     ");
  CHECK_EQ(buf[0], 0);

  // 3. обрезка по размеру буфера не рвёт UTF-8: "Орхидеи" = 14 байт
  //    буфер 5 байт -> 4 байта данных = ровно два символа "Ор"
  char small[5];
  text_set(small, sizeof(small), "Орхидеи");
  CHECK(strcmp(small, "\xD0\x9E\xD1\x80") == 0);
  CHECK_EQ(text_utf8_len(small), 2);
  CHECK_EQ(strlen(small), 4);

  // 4. полное имя помещается целиком
  char full[33];
  text_set(full, sizeof(full), "Орхидеи на окне");
  CHECK(strcmp(full, "Орхидеи на окне") == 0);

  // 5. длинное ASCII-имя обрезается по размеру поля
  char longName[9];
  text_set(longName, sizeof(longName), "abcdefghijklmno");
  CHECK(strcmp(longName, "abcdefgh") == 0);

  // 6. битый UTF-8: копирование останавливается на мусоре, без падения
  text_set(buf, sizeof(buf), "Ok\xFF\xFEBad");
  CHECK(strcmp(buf, "Ok") == 0);

  // 7. длина в символах
  CHECK_EQ(text_utf8_len("Канал 1"), 7);
  CHECK_EQ(text_utf8_len(""), 0);
}

/* ---------------- обмен настройками и задачами ---------------- */
static void test_transfer() {
  printf("[transfer]\n");

  TaskCfg t;
  memset(&t, 0, sizeof(t));
  t.used = 1;
  t.enabled = 1;
  t.channel = 2;
  t.hour = 19;
  t.minute = 30;
  t.durationSec = 60;
  t.periodDays = 2;
  t.lastRun = 1795000000UL;
  t.nextDue = 1795170000UL;

  // 1. кодирование
  char buf[64];
  CHECK(transfer_task_encode(t, buf, sizeof(buf)) == true);
  CHECK(strcmp(buf, "2,19,30,60,2,1,1795000000,1795170000") == 0);

  // 2. обратный разбор: все поля на месте
  TaskCfg d;
  memset(&d, 0, sizeof(d));
  CHECK(transfer_task_decode(buf, &d) == true);
  CHECK_EQ(d.used, 1);
  CHECK_EQ(d.channel, 2);
  CHECK_EQ(d.hour, 19);
  CHECK_EQ(d.minute, 30);
  CHECK_EQ(d.durationSec, 60);
  CHECK_EQ(d.periodDays, 2);
  CHECK_EQ(d.enabled, 1);
  CHECK_EQ(d.lastRun, 1795000000L);
  CHECK_EQ(d.nextDue, 1795170000L);

  // 3. выключенная задача: en = 0
  t.enabled = 0;
  CHECK(transfer_task_encode(t, buf, sizeof(buf)) == true);
  CHECK(transfer_task_decode(buf, &d) == true);
  CHECK_EQ(d.enabled, 0);

  // 4. пробелы вокруг чисел допустимы
  CHECK(transfer_task_decode(" 0 , 8 , 0 , 30 , 1 , 1 , 0 , 0 ", &d) == true);
  CHECK_EQ(d.hour, 8);

  // 5. битые строки отвергаются
  CHECK(transfer_task_decode("", &d) == false);
  CHECK(transfer_task_decode("1,2,3", &d) == false);
  CHECK(transfer_task_decode("1,2,3,4,5,6,7,8,9", &d) == false);
  CHECK(transfer_task_decode("a,b,c,d,e,f,g,h", &d) == false);
  CHECK(transfer_task_decode("0,0,0,10,1,1,0", &d) == false);       // мало полей
  CHECK(transfer_task_decode("0,0,,10,1,1,0,0", &d) == false);      // пустое поле

  // 6. проверка диапазонов
  CHECK(transfer_task_decode("9,0,0,10,1,1,0,0", &d) == false);     // канал
  CHECK(transfer_task_decode("0,24,0,10,1,1,0,0", &d) == false);    // часы
  CHECK(transfer_task_decode("0,0,60,10,1,1,0,0", &d) == false);    // минуты
  CHECK(transfer_task_decode("0,0,0,0,1,1,0,0", &d) == false);      // длительность 0
  CHECK(transfer_task_decode("0,0,0,10,0,1,0,0", &d) == false);     // период 0
  CHECK(transfer_task_decode("0,0,0,10,61,1,0,0", &d) == false);    // период > 60
  CHECK(transfer_task_decode("0,0,0,10,1,2,0,0", &d) == false);     // флаг активности
  CHECK(transfer_task_decode("0,0,0,10,1,1,-1,0", &d) == false);    // отрицательное

  // 7. при ошибке структура не меняется
  memset(&d, 0, sizeof(d));
  CHECK(transfer_task_decode("bad", &d) == false);
  CHECK_EQ(d.used, 0);

  // 8. слишком маленький буфер при кодировании
  t.enabled = 1;
  char small[10];
  CHECK(transfer_task_encode(t, small, sizeof(small)) == false);
}

/* ---------------- JSON для интерфейса (webjson.*) ---------------- */
static int count_substr(const char* hay, const char* needle) {
  int n = 0;
  const char* p = hay;
  size_t len = strlen(needle);
  while ((p = strstr(p, needle)) != NULL) {
    n++;
    p += len;
  }
  return n;
}

static void json_sample() {
  reset_all();
  storage_begin();
  strcpy(g_store.cfg.name, "Теплица");
  strcpy(g_store.cfg.ch[0].name, "Орхидея");
  g_store.cfg.ch[1].name[0] = 0;
  strcpy(g_store.cfg.ch[2].name, "Томаты");
  strcpy(g_store.cfg.ch[3].name, "Газон");
  strcpy(g_store.cfg.ssid, "HomeNet");
  strcpy(g_store.cfg.pass, "secret123");
  g_store.tasks[0].used = 1;
  g_store.tasks[0].enabled = 1;
  g_store.tasks[0].channel = 2;
  g_store.tasks[0].hour = 8;
  g_store.tasks[0].minute = 30;
  g_store.tasks[0].durationSec = 45;
  g_store.tasks[0].periodDays = 2;
  g_now = (time_t)1795000000UL;
  g_millis = 90000;
}

static void test_webjson() {
  printf("[web json]\n");
  json_sample();

  // 1. имя канала обязано быть в /api/state — на нём держится подпись
  //    «Канал 1 · Орхидея» на вкладках «Каналы» и «Задачи»
  String st = webjson_state();
  const char* s = st.c_str();
  CHECK(strstr(s, "\"nm\":\"Орхидея\"") != NULL);
  CHECK(strstr(s, "\"nm\":\"\"") != NULL);          // канал без имени
  CHECK_EQ(count_substr(s, "\"nm\":"), NUM_CHANNELS);
  CHECK(strstr(s, "\"name\":\"Теплица\"") != NULL);
  CHECK(strstr(s, "\"appName\":\"ESP8266-Autowatering2\"") != NULL);
  // Версия в состоянии обязана совпадать с прошивкой (иначе UI врёт).
  String fwField = String("\"fw\":\"") + FW_VERSION + "\"";
  CHECK(strstr(s, fwField.c_str()) != NULL);
  CHECK(strstr(s, "\"limits\":{") != NULL);
  CHECK(strstr(s, "\"tasks\":[") != NULL);
  CHECK_EQ(count_substr(s, "\"seq\":"), NUM_CHANNELS);
  CHECK(strstr(s, "\"heap\":30000") != NULL);
  CHECK(strstr(s, "\"uptime\":90") != NULL);

  // 2. кавычки в имени экранируются, JSON не ломается
  strcpy(g_store.cfg.ch[1].name, "Мамин \"угол\"");
  String st2 = webjson_state();
  CHECK(strstr(st2.c_str(), "\"nm\":\"Мамин \\\"угол\\\"\"") != NULL);

  // 3. /api/config отдаёт имена для полей настроек
  String cfg = webjson_config();
  CHECK(strstr(cfg.c_str(), "\"name\":\"Орхидея\"") != NULL);
  CHECK(strstr(cfg.c_str(), "\"ssid\":\"HomeNet\"") != NULL);

  // 4. экспорт: версия формата, задачи, и НИКАКОГО пароля
  String ex = webjson_export();
  const char* e = ex.c_str();
  CHECK(strstr(e, "\"app\":\"ESP8266-Autowatering2\"") != NULL);
  CHECK(strstr(e, "\"sv\":3") != NULL);
  CHECK(strstr(e, "\"cfg\":{") != NULL);
  CHECK(strstr(e, "\"tasks\":[") != NULL);
  CHECK(strstr(e, "\"dur\":45") != NULL);
  CHECK(strstr(e, "\"pd\":2") != NULL);
  CHECK(strstr(e, "\"name\":\"Газон\"") != NULL);
  CHECK(strstr(e, "secret123") == NULL);          // Wi-Fi не выгружается
  CHECK(strstr(e, "HomeNet") == NULL);
}

/* ---------------- проверка файла прошивки (otacheck.*) ---------------- */
static void test_otacheck() {
  printf("[ota file check]\n");
  const uint32_t maxSize = 700u * 1024u;

  CHECK(otacheck_file("autowatering-esp8266.bin", 314800, maxSize) == OTA_FILE_OK);
  CHECK(otacheck_file("FIRMWARE.BIN", 314800, maxSize) == OTA_FILE_OK);
  CHECK(otacheck_file("a.Bin", 314800, maxSize) == OTA_FILE_OK);
  CHECK(otacheck_file("fw.bin.txt", 314800, maxSize) == OTA_FILE_EXT);
  CHECK(otacheck_file("firmware", 314800, maxSize) == OTA_FILE_EXT);
  CHECK(otacheck_file("bin", 314800, maxSize) == OTA_FILE_EXT);
  CHECK(otacheck_file("", 314800, maxSize) == OTA_FILE_MISSING);
  CHECK(otacheck_file(NULL, 314800, maxSize) == OTA_FILE_MISSING);
  // Путь, который присылает браузер (Chrome отдаёт только имя, но бывает и путь).
  CHECK(otacheck_file("C:\\build\\autowatering-esp8266.bin", 314800, maxSize) == OTA_FILE_OK);

  CHECK(otacheck_size(OTA_FILE_MIN, maxSize) == OTA_FILE_OK);
  CHECK(otacheck_size(OTA_FILE_MIN - 1, maxSize) == OTA_FILE_SMALL);
  CHECK(otacheck_size(0, maxSize) == OTA_FILE_SMALL);
  CHECK(otacheck_size(maxSize, maxSize) == OTA_FILE_OK);
  CHECK(otacheck_size(maxSize + 1, maxSize) == OTA_FILE_BIG);
  // Неизвестный лимит (0) не должен блокировать обновление.
  CHECK(otacheck_size(900000, 0) == OTA_FILE_OK);

  CHECK(strcmp(otacheck_text(OTA_FILE_OK), "") == 0);
  CHECK(strlen(otacheck_text(OTA_FILE_EXT)) > 0);
  CHECK(strlen(otacheck_text(OTA_FILE_BIG)) > 0);
  CHECK(strlen(otacheck_text(OTA_FILE_SMALL)) > 0);
  CHECK(strlen(otacheck_text(OTA_FILE_MISSING)) > 0);
}

/* ---------------- геометрия OTA для нашей раскладки flash ----------------
 * Раскладка eagle.flash.4m.ld: _SPIFFS_start = 0x40300000, значит область под
 * прошивки - первые 1 МБ. Updater пишет новый образ в её конец:
 *   start = 0x100000 - roundup(Content-Length)
 * и требует start >= roundup(текущая прошивка), иначе затрёт работающий код.
 * Тест берёт настоящий собранный .bin: если прошивка вырастет настолько, что
 * обновление по Wi-Fi перестанет помещаться, это будет видно здесь.
 */
static void test_ota_geometry() {
  printf("[ota geometry]\n");
  const uint32_t updateEnd = 0x100000;   // до _SPIFFS_start

  FILE* f = fopen("../../firmware/autowatering-esp8266.bin", "rb");
  CHECK(f != NULL);
  uint32_t fwSize = 0;
  if (f) {
    fseek(f, 0, SEEK_END);
    fwSize = (uint32_t)ftell(f);
    fclose(f);
  }
  CHECK(fwSize > 0);

  uint32_t sketchRounded = (fwSize + 0xFFF) & ~0xFFFu;
  uint32_t incoming = fwSize + 512;                // Content-Length = файл + multipart
  uint32_t incomingRounded = (incoming + 0xFFF) & ~0xFFFu;
  uint32_t startAddress = updateEnd - incomingRounded;

  CHECK(startAddress >= sketchRounded);            // второй образ не залезет на текущий
  CHECK(incoming <= updateEnd - sketchRounded);    // и влезает в свободное место
  CHECK(updateEnd - sketchRounded > OTA_FILE_MIN); // запас не выродился
  printf("  прошивка %lu КБ, под второй образ %lu КБ\n",
         (unsigned long)(fwSize / 1024), (unsigned long)((updateEnd - sketchRounded) / 1024));
}

int main(int argc, char** argv) {
  setenv("TZ", "UTC", 1);
  tzset();

  // Режимы для проверки JSON настоящим парсером (tests/host/check_json.py).
  // Ничего лишнего в stdout: только JSON.
  if (argc > 1) {
    json_sample();
    String out;
    if (strcmp(argv[1], "--dump-state") == 0)       out = webjson_state();
    else if (strcmp(argv[1], "--dump-config") == 0) out = webjson_config();
    else if (strcmp(argv[1], "--dump-export") == 0) out = webjson_export();
    else if (strcmp(argv[1], "--dump-ota") == 0)    out = webjson_ota();
    printf("%s\n", out.c_str());
    return 0;
  }

  test_storage();
  test_channels();
  test_scheduler();
  test_clock();
  test_local_schedule();
  test_stop_events();
  test_scanlist();
  test_text();
  test_transfer();
  test_webjson();
  test_otacheck();
  test_ota_geometry();

  printf("\n%d проверок, ошибок: %d\n", g_checks, g_fails);
  return g_fails ? 1 : 0;
}
