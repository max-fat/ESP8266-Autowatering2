#include "webui.h"

#include <Arduino.h>
#include <ESP8266WebServer.h>
#include <time.h>

#include "app_config.h"
#include "channels.h"
#include "clock.h"
#include "scheduler.h"
#include "storage.h"
#include "text.h"
#include "transfer.h"
#include "netmgr.h"
#include "page_html.h"

static ESP8266WebServer s_server(80);

/* ------------------------------------------------------------------ */
/*  Вспомогательные функции                                            */
/* ------------------------------------------------------------------ */

static long arg_long(const char* name, long defVal) {
  if (!s_server.hasArg(name)) return defVal;
  String v = s_server.arg(name);
  v.trim();
  if (v.length() == 0) return defVal;
  return v.toInt();
}

static bool arg_bool(const char* name, bool defVal) {
  if (!s_server.hasArg(name)) return defVal;
  String v = s_server.arg(name);
  long n = v.toInt();
  if (v == "true" || v == "on") return true;
  if (v == "false" || v == "off") return false;
  return n != 0;
}

static void send_json(int code, const String& body) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(code, "application/json; charset=utf-8", body);
}

static void send_ok() {
  send_json(200, F("{\"ok\":true}"));
}

static void send_err(int code, const char* msg) {
  String j = F("{\"ok\":false,\"error\":\"");
  j += msg;
  j += F("\"}");
  send_json(code, j);
}

// Причина выключения канала -> строка для UI.
static const char* stop_reason_str(uint8_t reason) {
  switch (reason) {
    case STOP_REASON_TIMER:     return "timer";
    case STOP_REASON_MANUAL:    return "manual";
    case STOP_REASON_LOW_WATER: return "low";
    default:                    return "";
  }
}

static bool pin_valid(int pin) {
  return pin >= 0 && pin <= 16 && !(pin >= 6 && pin <= 11);
}

/* ------------------------------------------------------------------ */
/*  JSON состояния                                                     */
/* ------------------------------------------------------------------ */

static String build_state_json() {
  String j;
  j.reserve(1800);

  bool tv = clock_valid();
  char timeBuf[16];
  char dateBuf[16];
  clock_now_strings(timeBuf, sizeof(timeBuf), dateBuf, sizeof(dateBuf));

  char nameEsc[MODULE_NAME_MAX * 6 + 1];
  text_json_escape(g_store.cfg.name, nameEsc, sizeof(nameEsc));

  j += F("{\"fw\":\"" FW_VERSION "\",\"appName\":\"" FW_NAME "\",\"name\":\"");
  j += nameEsc;                 // имя модуля ("" = не задано, UI покажет "Автополив")
  j += F("\",");
  // Время отдаём строками, посчитанными на устройстве: браузер не должен
  // пересчитывать его в свой часовой пояс.
  j += F("\"now\":");
  j += String((unsigned long)clock_utc());
  j += F(",\"time\":\"");
  j += timeBuf;
  j += F("\",\"date\":\"");
  j += dateBuf;
  j += F("\",\"timeValid\":");
  j += tv ? '1' : '0';
  j += F(",\"timeSrc\":\"");
  j += clock_source();
  j += F("\",\"tz\":");
  j += String((int)g_store.cfg.tzOffsetMin);

  /* Wi-Fi */
  j += F(",\"wifi\":{\"mode\":\"");
  j += net_mode_str();
  j += F("\",\"configured\":");
  j += net_wifi_configured() ? '1' : '0';
  j += F(",\"connected\":");
  j += net_connected() ? '1' : '0';
  j += F(",\"ip\":\"");
  j += net_connected() ? net_ip() : String("");
  j += F("\",\"rssi\":");
  j += String(net_connected() ? net_rssi() : 0);
  j += F(",\"apActive\":");
  j += net_ap_active() ? '1' : '0';
  j += F(",\"apSsid\":\"");
  j += net_ap_ssid();
  j += F("\",\"apIp\":\"");
  j += net_ap_ip();
  j += F("\",\"ssid\":\"");
  j += g_store.cfg.ssid;
  j += F("\"}");

  /* Датчик уровня воды */
  j += F(",\"sensor\":{\"low\":");
  j += sensor_is_low() ? '1' : '0';
  j += F(",\"raw\":");
  j += String(sensor_raw());
  j += F(",\"pin\":");
  j += String((int)g_store.cfg.sensorPin);
  j += F(",\"inv\":");
  j += String((int)g_store.cfg.sensorActiveLow);
  j += F(",\"block\":");
  j += g_store.cfg.blockWhenLow ? '1' : '0';
  j += '}';

  /* Настройки, нужные интерфейсу */
  j += F(",\"limits\":{\"maxRun\":");
  j += String((unsigned)g_store.cfg.maxRunSec);
  j += F(",\"maxTasks\":");
  j += String((int)MAX_TASKS);
  j += F(",\"channels\":");
  j += String((int)NUM_CHANNELS);
  j += '}';

  /* Каналы */
  j += F(",\"ch\":[");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (i) j += ',';
    j += F("{\"i\":");
    j += String((int)i);
    j += F(",\"pin\":");
    j += String((int)g_store.cfg.ch[i].pin);
    j += F(",\"inv\":");
    j += String((int)g_store.cfg.ch[i].activeLow);
    j += F(",\"on\":");
    j += channels_is_on(i) ? '1' : '0';
    j += F(",\"rem\":");
    j += String((unsigned long)channels_remaining_sec(i));
    j += F(",\"task\":");
    j += String((int)channels_task(i));
    // Событие последнего выключения: seq меняется при каждом выключении,
    // rsn - причина (timer/manual/low), run - сколько секунд реально работал.
    j += F(",\"seq\":");
    j += String((int)channels_stop_seq(i));
    j += F(",\"rsn\":\"");
    j += stop_reason_str(channels_stop_reason(i));
    j += F("\",\"run\":");
    j += String((unsigned)channels_last_run_sec(i));
    j += '}';
  }
  j += ']';

  /* Задачи */
  j += F(",\"tasks\":[");
  bool first = true;
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    const TaskCfg& t = g_store.tasks[i];
    if (!t.used) continue;
    if (!first) j += ',';
    first = false;
    j += F("{\"i\":");
    j += String((int)i);
    j += F(",\"on\":");
    j += t.enabled ? '1' : '0';
    j += F(",\"ch\":");
    j += String((int)t.channel);
    j += F(",\"h\":");
    j += String((int)t.hour);
    j += F(",\"m\":");
    j += String((int)t.minute);
    j += F(",\"dur\":");
    j += String((unsigned)t.durationSec);
    j += F(",\"pd\":");
    j += String((int)t.periodDays);
    char lastBuf[24];
    char nextBuf[24];
    clock_format_full(t.lastRun, lastBuf, sizeof(lastBuf));
    clock_format_full(sched_next_run(t), nextBuf, sizeof(nextBuf));
    j += F(",\"lastStr\":\"");
    j += lastBuf;
    j += F("\",\"nextStr\":\"");
    j += nextBuf;
    j += F("\",\"st\":\"");
    j += sched_status_str(i);
    j += F("\"}");
  }
  j += ']';

  j += F(",\"heap\":");
  j += String((unsigned long)ESP.getFreeHeap());
  j += F(",\"uptime\":");
  j += String((unsigned long)(millis() / 1000UL));
  j += '}';
  return j;
}

static String build_config_json() {
  String j;
  j.reserve(700);
  char nameEsc[MODULE_NAME_MAX * 6 + 1];
  text_json_escape(g_store.cfg.name, nameEsc, sizeof(nameEsc));
  j += F("{\"name\":\"");
  j += nameEsc;
  j += F("\",\"ssid\":\"");
  j += g_store.cfg.ssid;
  j += F("\",\"hasPass\":");
  j += g_store.cfg.pass[0] ? '1' : '0';
  j += F(",\"ntp\":\"");
  j += g_store.cfg.ntpServer;
  j += F("\",\"tz\":");
  j += String((int)g_store.cfg.tzOffsetMin);
  j += F(",\"sensorPin\":");
  j += String((int)g_store.cfg.sensorPin);
  j += F(",\"sensorInv\":");
  j += String((int)g_store.cfg.sensorActiveLow);
  j += F(",\"sensorPull\":");
  j += String((int)g_store.cfg.sensorPullup);
  j += F(",\"blockLow\":");
  j += String((int)g_store.cfg.blockWhenLow);
  j += F(",\"maxRun\":");
  j += String((unsigned)g_store.cfg.maxRunSec);
  j += F(",\"catchup\":");
  j += String((int)g_store.cfg.catchupHours);
  j += F(",\"ch\":[");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (i) j += ',';
    j += F("{\"pin\":");
    j += String((int)g_store.cfg.ch[i].pin);
    j += F(",\"inv\":");
    j += String((int)g_store.cfg.ch[i].activeLow);
    char nmEsc[CHANNEL_NAME_MAX * 6 + 1];
    text_json_escape(g_store.cfg.ch[i].name, nmEsc, sizeof(nmEsc));
    j += F(",\"name\":\"");
    j += nmEsc;
    j += F("\"}");
  }
  j += F("]}");
  return j;
}

/* ------------------------------------------------------------------ */
/*  Обработчики                                                        */
/* ------------------------------------------------------------------ */

static void handle_index() {
  s_server.sendHeader("Content-Encoding", "gzip");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send_P(200, "text/html; charset=utf-8", (PGM_P)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

static void handle_state() {
  send_json(200, build_state_json());
}

static void handle_config_get() {
  send_json(200, build_config_json());
}

static void handle_task_save() {
  long id = arg_long("id", -1);
  int8_t slot = -1;

  if (id >= 0 && id < MAX_TASKS && g_store.tasks[id].used) {
    slot = (int8_t)id;
  } else {
    for (uint8_t i = 0; i < MAX_TASKS; i++) {
      if (!g_store.tasks[i].used) { slot = (int8_t)i; break; }
    }
    if (slot < 0) { send_err(400, "no_free_slot"); return; }
  }

  long ch = arg_long("ch", 0);
  long hour = arg_long("hour", 8);
  long minute = arg_long("min", 0);
  long dur = arg_long("dur", 30);
  long period = arg_long("period", 1);
  bool enabled = arg_bool("enabled", true);

  if (ch < 0 || ch >= NUM_CHANNELS) { send_err(400, "bad_channel"); return; }
  if (hour < 0 || hour > 23) { send_err(400, "bad_hour"); return; }
  if (minute < 0 || minute > 59) { send_err(400, "bad_minute"); return; }

  uint16_t maxDur = g_store.cfg.maxRunSec ? g_store.cfg.maxRunSec : DEFAULT_MAX_RUN_SEC;
  if (maxDur > TASK_MAX_DURATION_SEC) maxDur = TASK_MAX_DURATION_SEC;
  if (dur < TASK_MIN_DURATION_SEC) dur = TASK_MIN_DURATION_SEC;
  if (dur > maxDur) dur = maxDur;
  if (period < 1) period = 1;
  if (period > TASK_MAX_PERIOD_DAYS) period = TASK_MAX_PERIOD_DAYS;

  TaskCfg& t = g_store.tasks[slot];
  bool wasUsed = t.used != 0;
  t.used = 1;
  t.enabled = enabled ? 1 : 0;
  t.channel = (uint8_t)ch;
  t.hour = (uint8_t)hour;
  t.minute = (uint8_t)minute;
  t.durationSec = (uint16_t)dur;
  t.periodDays = (uint8_t)period;
  if (!wasUsed) {
    t.lastRun = 0;
    t.nextDue = 0;
  } else {
    t.nextDue = 0;  // время/период могли измениться - пересчитаем расписание
  }

  storage_save();
  send_ok();
}

static void handle_task_delete() {
  long id = arg_long("id", -1);
  if (id < 0 || id >= MAX_TASKS || !g_store.tasks[id].used) {
    send_err(400, "bad_id");
    return;
  }
  if (channels_is_on(g_store.tasks[id].channel) &&
      channels_task(g_store.tasks[id].channel) == (int8_t)id) {
    channels_stop(g_store.tasks[id].channel);
  }
  memset(&g_store.tasks[id], 0, sizeof(TaskCfg));
  storage_save();
  send_ok();
}

static void handle_task_run() {
  long id = arg_long("id", -1);
  if (id < 0 || id >= MAX_TASKS || !g_store.tasks[id].used) {
    send_err(400, "bad_id");
    return;
  }
  TaskCfg& t = g_store.tasks[id];
  // Ручной пуск: включаем поток на настроенную длительность, статус задачи
  // (lastRun/nextDue) при этом НЕ меняется.
  StartResult r = channels_start(t.channel, t.durationSec, (int8_t)id);
  if (r == START_LOW_WATER) { send_err(409, "low_water"); return; }
  if (r != START_OK) { send_err(400, "start_failed"); return; }
  send_ok();
}

static void handle_channel() {
  long ch = arg_long("ch", -1);
  long sec = arg_long("sec", 0);
  if (ch < 0 || ch >= NUM_CHANNELS) { send_err(400, "bad_channel"); return; }

  if (sec <= 0) {
    channels_stop((uint8_t)ch);
    send_ok();
    return;
  }
  if (sec > 65535) sec = 65535;
  StartResult r = channels_start((uint8_t)ch, (uint16_t)sec, -1);
  if (r == START_LOW_WATER) { send_err(409, "low_water"); return; }
  if (r != START_OK) { send_err(400, "start_failed"); return; }
  send_ok();
}

static const char* read_settings_from_args(Settings& c) {
  long maxRun = arg_long("maxRun", c.maxRunSec);
  if (maxRun < 1) maxRun = 1;
  if (maxRun > 65535) maxRun = 65535;

  long catchup = arg_long("catchup", c.catchupHours);
  if (catchup < 0) catchup = 0;
  if (catchup > 24) catchup = 24;

  long tz = arg_long("tz", c.tzOffsetMin);
  if (tz < -720) tz = -720;
  if (tz > 840) tz = 840;

  long sensorPin = arg_long("sensorPin", c.sensorPin);
  if (!pin_valid(sensorPin)) return "bad_sensor_pin";

  uint8_t newPins[NUM_CHANNELS];
  uint8_t newInv[NUM_CHANNELS];
  char newNames[NUM_CHANNELS][CHANNEL_NAME_MAX + 1];

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    char key[16];
    snprintf(key, sizeof(key), "ch%u_pin", (unsigned)i);
    long p = arg_long(key, c.ch[i].pin);
    if (!pin_valid(p)) return "bad_channel_pin";
    newPins[i] = (uint8_t)p;

    snprintf(key, sizeof(key), "ch%u_inv", (unsigned)i);
    newInv[i] = arg_bool(key, c.ch[i].activeLow) ? 1 : 0;

    memcpy(newNames[i], c.ch[i].name, sizeof(newNames[i]));
    snprintf(key, sizeof(key), "ch%u_name", (unsigned)i);
    if (s_server.hasArg(key)) {
      String nm = s_server.arg(key);
      text_set(newNames[i], sizeof(newNames[i]), nm.c_str());
    }
  }

  // проверка на дубликаты пинов
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (newPins[i] == (uint8_t)sensorPin) return "pin_conflict_sensor";
    for (uint8_t k = i + 1; k < NUM_CHANNELS; k++) {
      if (newPins[i] == newPins[k]) return "pin_conflict";
    }
  }

  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    c.ch[i].pin = newPins[i];
    c.ch[i].activeLow = newInv[i];
    memcpy(c.ch[i].name, newNames[i], sizeof(c.ch[i].name));
  }

  if (s_server.hasArg("name")) {
    String nm = s_server.arg("name");
    text_set(c.name, sizeof(c.name), nm.c_str());
  }

  c.sensorPin = (uint8_t)sensorPin;
  c.sensorActiveLow = arg_bool("sensorInv", c.sensorActiveLow) ? 1 : 0;
  c.sensorPullup = arg_bool("sensorPull", c.sensorPullup) ? 1 : 0;
  c.blockWhenLow = arg_bool("blockLow", c.blockWhenLow) ? 1 : 0;
  c.maxRunSec = (uint16_t)maxRun;
  c.catchupHours = (uint8_t)catchup;
  c.tzOffsetMin = (int16_t)tz;

  if (s_server.hasArg("ntp")) {
    String ntp = s_server.arg("ntp");
    ntp.trim();
    if (ntp.length() == 0) ntp = DEFAULT_NTP_SERVER;
    if (ntp.length() > 32) ntp = ntp.substring(0, 32);
    strncpy(c.ntpServer, ntp.c_str(), sizeof(c.ntpServer) - 1);
    c.ntpServer[sizeof(c.ntpServer) - 1] = '\0';
  }

  return NULL;
}

static void handle_config_save() {
  Settings c = g_store.cfg;
  const char* err = read_settings_from_args(c);
  if (err) { send_err(400, err); return; }
  g_store.cfg = c;

  // Ограничение длительности задач новым предохранителем
  uint16_t maxDur = c.maxRunSec ? c.maxRunSec : DEFAULT_MAX_RUN_SEC;
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    if (!g_store.tasks[i].used) continue;
    if (g_store.tasks[i].durationSec > maxDur) g_store.tasks[i].durationSec = maxDur;
  }

  storage_save();
  channels_apply_config();
  net_apply_time_config();
  send_ok();
}

/* Экспорт настроек и задач: обычный JSON, файл скачивает браузер.
 * Wi-Fi (ssid/пароль) намеренно не выгружается. */
static void handle_export() {
  char escName[MODULE_NAME_MAX * 6 + 1];
  char escNtp[64];
  char timeBuf[16];
  char dateBuf[16];
  clock_now_strings(timeBuf, sizeof(timeBuf), dateBuf, sizeof(dateBuf));

  String j;
  j.reserve(1800);
  j += F("{\"app\":\"" FW_NAME "\",\"fw\":\"" FW_VERSION "\",\"sv\":");
  j += String((int)STORE_VERSION);
  j += F(",\"exported\":\"");
  j += dateBuf;
  j += ' ';
  j += timeBuf;
  j += F("\",\"cfg\":{");

  text_json_escape(g_store.cfg.name, escName, sizeof(escName));
  j += F("\"name\":\"");
  j += escName;
  j += F("\",\"tz\":");
  j += String((int)g_store.cfg.tzOffsetMin);
  text_json_escape(g_store.cfg.ntpServer, escNtp, sizeof(escNtp));
  j += F(",\"ntp\":\"");
  j += escNtp;
  j += F("\",\"sensorPin\":");
  j += String((int)g_store.cfg.sensorPin);
  j += F(",\"sensorInv\":");
  j += String((int)g_store.cfg.sensorActiveLow);
  j += F(",\"sensorPull\":");
  j += String((int)g_store.cfg.sensorPullup);
  j += F(",\"blockLow\":");
  j += String((int)g_store.cfg.blockWhenLow);
  j += F(",\"maxRun\":");
  j += String((unsigned)g_store.cfg.maxRunSec);
  j += F(",\"catchup\":");
  j += String((int)g_store.cfg.catchupHours);

  j += F(",\"ch\":[");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (i) j += ',';
    text_json_escape(g_store.cfg.ch[i].name, escName, sizeof(escName));
    j += F("{\"pin\":");
    j += String((int)g_store.cfg.ch[i].pin);
    j += F(",\"inv\":");
    j += String((int)g_store.cfg.ch[i].activeLow);
    j += F(",\"name\":\"");
    j += escName;
    j += F("\"}");
  }
  j += F("]},\"tasks\":[");

  bool first = true;
  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    const TaskCfg& t = g_store.tasks[i];
    if (!t.used) continue;
    if (!first) j += ',';
    first = false;
    j += F("{\"ch\":");
    j += String((int)t.channel);
    j += F(",\"h\":");
    j += String((int)t.hour);
    j += F(",\"m\":");
    j += String((int)t.minute);
    j += F(",\"dur\":");
    j += String((unsigned)t.durationSec);
    j += F(",\"pd\":");
    j += String((int)t.periodDays);
    j += F(",\"en\":");
    j += String((int)t.enabled);
    j += F(",\"last\":");
    j += String((unsigned long)t.lastRun);
    j += F(",\"next\":");
    j += String((unsigned long)t.nextDue);
    j += '}';
  }
  j += F("]}");

  s_server.sendHeader("Cache-Control", "no-store");
  s_server.sendHeader("Content-Disposition", "attachment; filename=\"autowatering-settings.json\"");
  s_server.send(200, "application/json; charset=utf-8", j);
}

/*
 * Импорт настроек и задач.
 * Тело — обычная форма: name, tz, ntp, sensorPin, ..., chN_pin/chN_inv/chN_name,
 * t0..t7 = "ch,h,m,dur,pd,en,last,next" (см. transfer.h), status=1 - переносить
 * статус задач (иначе расписание начнётся заново).
 * Всё проверяется до применения: при ошибке конфигурация не меняется.
 */
static void handle_import() {
  Settings c = g_store.cfg;
  const char* err = read_settings_from_args(c);
  if (err) { send_err(400, err); return; }

  const bool withStatus = arg_bool("status", false);
  const uint16_t maxDur = c.maxRunSec ? c.maxRunSec : DEFAULT_MAX_RUN_SEC;

  TaskCfg imported[MAX_TASKS];
  memset(imported, 0, sizeof(imported));
  uint8_t count = 0;

  for (uint8_t i = 0; i < MAX_TASKS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "t%u", (unsigned)i);
    if (!s_server.hasArg(key)) continue;

    String v = s_server.arg(key);
    TaskCfg t;
    if (!transfer_task_decode(v.c_str(), &t)) { send_err(400, "bad_task"); return; }
    if (t.durationSec > maxDur) { send_err(400, "bad_task"); return; }
    if (!withStatus) {
      t.lastRun = 0;
      t.nextDue = 0;      // расписание пересчитается от текущего времени
    }
    imported[i] = t;
    count++;
  }

  g_store.cfg = c;
  memcpy(g_store.tasks, imported, sizeof(imported));
  storage_save();
  channels_apply_config();
  net_apply_time_config();

  String j = F("{\"ok\":true,\"tasks\":");
  j += String((int)count);
  j += F(",\"channels\":");
  j += String((int)NUM_CHANNELS);
  j += '}';
  send_json(200, j);
}

// Установка времени с телефона (устройство без RTC: пока нет NTP, время
// можно быстро "привезти" из браузера).
static void handle_time_set() {
  long nowUtc = arg_long("now", 0);
  if (nowUtc < (long)CLOCK_TIME_VALID_MIN) {
    send_err(400, "bad_time");
    return;
  }
  clock_set_from_phone((uint32_t)nowUtc);

  // Если браузер прислал свой пояс - сохраняем его в настройки.
  if (s_server.hasArg("tz")) {
    long tz = arg_long("tz", g_store.cfg.tzOffsetMin);
    if (tz < -720) tz = -720;
    if (tz > 840) tz = 840;
    g_store.cfg.tzOffsetMin = (int16_t)tz;
    storage_save();
  }

  char timeBuf[16];
  char dateBuf[16];
  clock_now_strings(timeBuf, sizeof(timeBuf), dateBuf, sizeof(dateBuf));
  char tzbuf[24];
  char sign = g_store.cfg.tzOffsetMin < 0 ? '-' : '+';
  int32_t absMin = g_store.cfg.tzOffsetMin < 0 ? -g_store.cfg.tzOffsetMin : g_store.cfg.tzOffsetMin;
  snprintf(tzbuf, sizeof(tzbuf), "UTC%c%02d:%02d", sign, (int)(absMin / 60), (int)(absMin % 60));

  String j = F("{\"ok\":true,\"time\":\"");
  j += timeBuf;
  j += F("\",\"date\":\"");
  j += dateBuf;
  j += F("\",\"tz\":\"");
  j += tzbuf;
  j += F("\",\"src\":\"");
  j += clock_source();
  j += F("\"}");
  send_json(200, j);
}

/* Сканирование Wi-Fi для автовыбора SSID.
 *   GET /api/scan        - состояние: running | done | idle и список сетей;
 *   GET /api/scan?start=1 - запустить новое сканирование и сразу ответить.
 * Сканирование асинхронное, поэтому страница опрашивает /api/scan, пока не
 * получит done (обычно 2-5 секунд).
 */
static void handle_scan() {
  int8_t st = net_scan_state();

  if (s_server.hasArg("start") && s_server.arg("start") != "0") {
    if (st != WIFI_SCAN_RUNNING) {
      net_scan_start();
      st = net_scan_state();
    }
  }

  String j;
  j.reserve(1400);
  j += F("{\"ok\":true,\"state\":\"");

  const ScanNet* nets = NULL;
  uint8_t count = 0;

  if (st == WIFI_SCAN_RUNNING) {
    j += F("running");
  } else if (st == WIFI_SCAN_FAILED) {
    j += F("failed");               // SDK не смог запустить сканирование
  } else if (st < 0) {
    j += F("idle");                 // ещё не сканировали
  } else {
    j += F("done");
    nets = net_scan_results(&count);
  }

  j += F("\",\"count\":");
  j += String((int)count);
  j += F(",\"nets\":[");

  char esc[SCAN_SSID_MAX * 6 + 1];
  for (uint8_t i = 0; i < count; i++) {
    if (i) j += ',';
    text_json_escape(nets[i].ssid, esc, sizeof(esc));
    j += F("{\"ssid\":\"");
    j += esc;
    j += F("\",\"rssi\":");
    j += String((int)nets[i].rssi);
    j += F(",\"enc\":");
    j += nets[i].secured ? '1' : '0';
    j += '}';
  }
  j += F("]}");
  send_json(200, j);
}

static void handle_wifi_save() {
  String ssid = s_server.hasArg("ssid") ? s_server.arg("ssid") : String("");
  String pass = s_server.hasArg("pass") ? s_server.arg("pass") : String("");
  ssid.trim();
  if (ssid.length() == 0) { send_err(400, "empty_ssid"); return; }
  if (ssid.length() > 32) ssid = ssid.substring(0, 32);
  if (pass.length() > 64) pass = pass.substring(0, 64);

  strncpy(g_store.cfg.ssid, ssid.c_str(), sizeof(g_store.cfg.ssid) - 1);
  g_store.cfg.ssid[sizeof(g_store.cfg.ssid) - 1] = '\0';
  strncpy(g_store.cfg.pass, pass.c_str(), sizeof(g_store.cfg.pass) - 1);
  g_store.cfg.pass[sizeof(g_store.cfg.pass) - 1] = '\0';

  storage_save();
  send_ok();
  net_request_reboot(1200);
}

static void handle_factory_reset() {
  send_ok();
  storage_factory_reset();
  clock_clear_manual();
  net_request_reboot(1200);
}

static void handle_reboot() {
  send_ok();
  net_request_reboot(800);
}

static void handle_not_found() {
  String uri = s_server.uri();
  if (uri.startsWith("/api/")) {
    send_err(404, "not_found");
    return;
  }
  // Captive portal: любые адреса (включая проверки подключения Android/iOS)
  // отдаём как главную страницу.
  handle_index();
}

void webui_begin() {
  s_server.on("/", HTTP_GET, handle_index);
  s_server.on("/index.html", HTTP_GET, handle_index);
  s_server.on("/favicon.ico", HTTP_GET, []() { s_server.send(204, "text/plain", ""); });

  s_server.on("/api/state", HTTP_GET, handle_state);
  s_server.on("/api/config", HTTP_GET, handle_config_get);
  s_server.on("/api/config", HTTP_POST, handle_config_save);
  s_server.on("/api/task", HTTP_POST, handle_task_save);
  s_server.on("/api/task/del", HTTP_POST, handle_task_delete);
  s_server.on("/api/task/run", HTTP_POST, handle_task_run);
  s_server.on("/api/ch", HTTP_POST, handle_channel);
  s_server.on("/api/time", HTTP_POST, handle_time_set);
  s_server.on("/api/scan", HTTP_GET, handle_scan);
  s_server.on("/api/wifi", HTTP_POST, handle_wifi_save);
  s_server.on("/api/export", HTTP_GET, handle_export);
  s_server.on("/api/import", HTTP_POST, handle_import);
  s_server.on("/api/factory", HTTP_POST, handle_factory_reset);
  s_server.on("/api/reboot", HTTP_POST, handle_reboot);

  s_server.onNotFound(handle_not_found);
  s_server.begin();
  Serial.println("[web] сервер запущен на порту 80");
}

void webui_update() {
  s_server.handleClient();
}
