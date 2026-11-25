/*
 * ESP8266-Autowatering2 - контроллер автополива растений
 *
 * app_config.h - общие константы, умолчания и структуры хранилища.
 *
 * Проект: 4 канала (помпы) + дискретный датчик низкого уровня воды.
 * Целевая платформа: ESP8266 (NodeMCU / Wemos D1 mini, 4 МБ flash).
 */
#pragma once

#include <stdint.h>

#define FW_NAME     "ESP8266-Autowatering2"
#define FW_VERSION  "1.6.1"

/* ---------- Ограничения ---------- */
#define NUM_CHANNELS      4       // число помп (каналов)
#define MAX_TASKS         8       // максимум задач полива
#define STORE_MAGIC       0x41574532UL   /* "AWE2" */
#define STORE_VERSION     3   /* изменения формата = сброс к умолчаниям */
#define EEPROM_BYTES      1024    // размер эмулированного EEPROM под наше хранилище

/* ---------- Умолчания для пинов ----------
 * Выбраны пины, которые при старте ESP8266 находятся в НИЗКОМ уровне
 * (GPIO4/5/12/13/14/16). Это гарантирует, что реле не "дёрнутся"
 * при включении питания.
 *   GPIO4  = D2     GPIO5  = D1
 *   GPIO12 = D6     GPIO14 = D5
 *   GPIO13 = D7     (датчик уровня, подтяжка к +3.3 В внутри)
 */
#define DEFAULT_PUMP_PIN_0   4
#define DEFAULT_PUMP_PIN_1   5
#define DEFAULT_PUMP_PIN_2   12
#define DEFAULT_PUMP_PIN_3   14
#define DEFAULT_SENSOR_PIN   13

/* ---------- Умолчания параметров ---------- */
#define DEFAULT_MAX_RUN_SEC      600    // предохранитель: максимум 10 минут на канал
#define DEFAULT_CATCHUP_HOURS    6      // сколько часов после планового времени ещё можно полить
#define DEFAULT_TZ_OFFSET_MIN    180    // UTC+3 (МСК) - см. настройки
#define DEFAULT_NTP_SERVER       "pool.ntp.org"
#define DEFAULT_AP_PREFIX        "AutoWater-"

/* ---------- Имена (модуль и каналы) ----------
 * Хранятся в UTF-8. Пустая строка = имя по умолчанию: для модуля это
 * "Автополив" (рисует интерфейс), для канала - "Канал N".
 */
#define MODULE_NAME_MAX   32      // байт под имя модуля (без завершающего нуля)
#define CHANNEL_NAME_MAX  32      // байт под имя канала

#define TASK_MIN_DURATION_SEC    1
#define TASK_MAX_DURATION_SEC    3600
#define TASK_MAX_PERIOD_DAYS     60

/* ---------- Структуры, которые лежат в EEPROM ---------- */
struct ChannelCfg {
  uint8_t pin;         // номер GPIO
  uint8_t activeLow;   // 1 = помпа включена при НИЗКОМ уровне на пине (реле-модуль)
  char    name[CHANNEL_NAME_MAX + 1];  // что поливает канал ("Орхидеи", "Томаты")
};

struct TaskCfg {
  uint8_t  used;         // слот занят
  uint8_t  enabled;      // задача активна
  uint8_t  channel;      // 0..NUM_CHANNELS-1
  uint8_t  hour;         // время старта, часы (0..23) по местному времени
  uint8_t  minute;       // время старта, минуты (0..59)
  uint8_t  periodDays;   // период повторения в днях (1..TASK_MAX_PERIOD_DAYS)
  uint16_t durationSec;  // длительность полива, сек
  uint32_t lastRun;      // локальные секунды эпохи последнего полива (0 = ещё не было)
  uint32_t nextDue;      // локальные секунды эпохи следующего полива (0 = пересчитать)
};

struct Settings {
  char     name[MODULE_NAME_MAX + 1];  // имя модуля ("Теплица", "Балкон")
  char     ssid[33];
  char     pass[65];
  char     ntpServer[33];
  int16_t  tzOffsetMin;      // смещение от UTC в минутах (180 = UTC+3, МСК)
  uint8_t  sensorPin;
  uint8_t  sensorActiveLow;  // 1 = НИЗКИЙ уровень сигнала означает "вода на исходе"
  uint8_t  sensorPullup;     // 1 = включить внутреннюю подтяжку к +3.3 В
  uint8_t  blockWhenLow;     // 1 = запрещать полив при низком уровне воды
  uint8_t  catchupHours;     // окно "догоняющего" полива, часов (0 = не догонять)
  uint16_t maxRunSec;        // максимум секунд на одно включение канала
  ChannelCfg ch[NUM_CHANNELS]; // пины/полярность помп
};

struct Store {
  uint32_t  magic;
  uint16_t  version;
  uint16_t  reserved;
  Settings  cfg;
  TaskCfg   tasks[MAX_TASKS];
  uint32_t  crc;
};
