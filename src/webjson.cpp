#include "webjson.h"

#include "app_config.h"
#include "channels.h"
#include "clock.h"
#include "scheduler.h"
#include "storage.h"
#include "netmgr.h"
#include "text.h"
#include "ota.h"
#include "otacheck.h"

const char* ota_state_str(uint8_t state) {
  switch (state) {
    case OTA_RUNNING: return "running";
    case OTA_DONE:    return "done";
    case OTA_ERROR:   return "error";
    default:          return "idle";
  }
}

const char* stop_reason_str(uint8_t reason) {
  switch (reason) {
    case STOP_REASON_TIMER:     return "timer";
    case STOP_REASON_MANUAL:    return "manual";
    case STOP_REASON_LOW_WATER: return "low";
    default:                    return "";
  }
}

/*
 * Визитка модуля — самая короткая из всех ручек (см. webjson_ident ниже,
 * её читает сканер соседних модулей на странице проекта и в интерфейсе).
 * Идентификатор "app" нужен, чтобы сканер не путал чужие сервера в сети
 * с автополивом: поле проверяется на стороне браузера.
 */
String webjson_ident() {
  String j;
  j.reserve(140);

  char nameEsc[MODULE_NAME_MAX * 6 + 1];
  text_json_escape(g_store.cfg.name, nameEsc, sizeof(nameEsc));

  j += F("{\"app\":\"autowatering\",\"fw\":\"" FW_VERSION "\",\"name\":\"");
  j += nameEsc;
  j += F("\",\"ch\":");
  j += String((int)NUM_CHANNELS);
  j += F(",\"ip\":\"");
  j += net_connected() ? net_ip() : net_ap_ip();
  j += F("\"}");
  return j;
}

String webjson_state() {
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
    // Имя канала: интерфейс выводит его рядом с номером ("Канал 1 · Орхидеи").
    char nmEsc[CHANNEL_NAME_MAX * 6 + 1];
    text_json_escape(g_store.cfg.ch[i].name, nmEsc, sizeof(nmEsc));
    j += F(",\"nm\":\"");
    j += nmEsc;
    j += F("\"}");
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

String webjson_config() {
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

String webjson_export() {
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

  return j;
}

/* Состояние обновления по Wi-Fi: что показывает карточка «Обновление прошивки». */
String webjson_ota() {
  char errEsc[160];
  text_json_escape(ota_error(), errEsc, sizeof(errEsc));

  String j = F("{\"fw\":\"" FW_VERSION "\",\"state\":");
  j += String((int)ota_state());
  j += F(",\"stateText\":\"");
  j += ota_state_str(ota_state());
  j += F("\",\"received\":");
  j += String((unsigned long)ota_received());
  j += F(",\"total\":");
  j += String((unsigned long)ota_total());
  j += F(",\"maxSize\":");
  j += String((unsigned long)ota_max_size());
  j += F(",\"minSize\":");
  j += String((unsigned long)OTA_FILE_MIN);
  j += F(",\"freeHeap\":");
  j += String((unsigned long)ESP.getFreeHeap());
  j += F(",\"err\":\"");
  j += errEsc;
  j += F("\"}");
  return j;
}
