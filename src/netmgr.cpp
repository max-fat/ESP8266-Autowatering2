#include "netmgr.h"

#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <time.h>

#include "storage.h"
#include "scheduler.h"

enum NetState {
  NET_BOOT = 0,   // пытаемся подключиться к сохранённой сети
  NET_STA,        // подключены к роутеру, AP выключена
  NET_AP,         // только точка доступа (Wi-Fi не настроен)
  NET_AP_STA      // точка доступа + попытки подключиться к роутеру
};

static NetState s_state = NET_BOOT;
static uint32_t s_stateStart = 0;
static uint32_t s_lastNtp = 0;
static uint32_t s_lastRetry = 0;      // попытки переподключиться в AP_STA
static uint32_t s_lostAt = 0;         // когда потеряли связь в режиме STA
static bool     s_ntpStarted = false;
static char     s_apSsid[32] = {0};
static DNSServer s_dns;
static bool     s_dnsRunning = false;
// Результаты сканирования Wi-Fi: сырые записи + обработанный список.
static ScanNet  s_scanRaw[NET_SCAN_MAX_RESULTS * 2];
static ScanNet  s_scanList[NET_SCAN_MAX_RESULTS];
static uint8_t  s_scanCount = 0;
static uint32_t s_rebootAt = 0;
static bool     s_rebootPending = false;

#define STA_CONNECT_TIMEOUT_MS  25000UL   // сколько ждём роутер перед подъёмом AP
#define STA_RETRY_MS            30000UL   // период повторных попыток в AP_STA
#define STA_LOST_TIMEOUT_MS     60000UL   // сколько терпим пропадание связи
#define NTP_RESYNC_MS           (6UL * 3600UL * 1000UL)

static void build_ap_ssid() {
  char id[8];
  snprintf(id, sizeof(id), "%06X", ESP.getChipId() & 0xFFFFFF);
  snprintf(s_apSsid, sizeof(s_apSsid), "%s%s", DEFAULT_AP_PREFIX, id);
}

static void start_ntp() {
  // Пояс НЕ передаём ядру: внутри прошивки время всегда UTC, а смещение
  // применяется в src/clock.cpp. Так поведение одинаково на ядрах 2.4.x и 3.x.
  const char* srv = g_store.cfg.ntpServer[0] ? g_store.cfg.ntpServer : DEFAULT_NTP_SERVER;
  configTime(0, 0, srv);
  s_lastNtp = millis();
  s_ntpStarted = true;
  Serial.printf("[net] NTP: %s (UTC), пояс %d мин\n", srv, (int)g_store.cfg.tzOffsetMin);
}

void net_apply_time_config() {
  s_ntpStarted = false;   // пересинхронизировать при следующем тике net_update()
}

void net_force_ntp_sync() {
  s_ntpStarted = false;
}

static void start_ap() {
  WiFi.mode(WIFI_AP);
  build_ap_ssid();
  bool ok = WiFi.softAP(s_apSsid);   // открытая сеть: только настройка, без пароля
  Serial.printf("[net] AP \"%s\" %s, IP %s\n", s_apSsid, ok ? "started" : "FAILED",
                WiFi.softAPIP().toString().c_str());
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());
  s_dnsRunning = true;
}

static void stop_ap() {
  if (s_dnsRunning) {
    s_dns.stop();
    s_dnsRunning = false;
  }
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

void net_begin() {
  // Креды храним только в своём EEPROM - SDK-шное хранилище отключаем.
  WiFi.persistent(false);
  WiFi.setAutoConnect(false);
  WiFi.setAutoReconnect(false);
  build_ap_ssid();

  s_stateStart = millis();

  if (!net_wifi_configured()) {
    Serial.println("[net] Wi-Fi не настроен - поднимаю точку доступа");
    start_ap();
    s_state = NET_AP;
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(g_store.cfg.ssid, g_store.cfg.pass);
  Serial.printf("[net] подключаюсь к \"%s\"...\n", g_store.cfg.ssid);
  s_state = NET_BOOT;
}

bool net_wifi_configured() {
  return g_store.cfg.ssid[0] != '\0';
}

bool net_connected() {
  return WiFi.status() == WL_CONNECTED;
}

bool net_ap_active() {
  return s_state == NET_AP || s_state == NET_AP_STA;
}

String net_ip() {
  return WiFi.localIP().toString();
}

String net_ap_ip() {
  return WiFi.softAPIP().toString();
}

String net_ap_ssid() {
  return String(s_apSsid);
}

int net_rssi() {
  return WiFi.RSSI();
}

String net_mode_str() {
  switch (s_state) {
    case NET_STA:    return "sta";
    case NET_AP:     return "ap";
    case NET_AP_STA: return "sta+ap";
    default:         return "boot";
  }
}

void net_request_reboot(uint32_t delayMs) {
  s_rebootPending = true;
  s_rebootAt = millis() + delayMs;
}

void net_scan_start() {
  // Уже идёт - второй раз не запускаем (иначе SDK вернёт ошибку).
  if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
  WiFi.scanDelete();
  s_scanCount = 0;
  // async = true: сканирование идёт в фоне, точка доступа и соединение живы.
  WiFi.scanNetworks(true /*async*/, true /*show_hidden*/);
  Serial.println("[net] сканирование Wi-Fi запущено");
}

int8_t net_scan_state() {
  return WiFi.scanComplete();   // -1 идёт, -2 нет данных, иначе число сетей
}

static void scan_collect(uint8_t found) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < found && n < (uint8_t)(sizeof(s_scanRaw) / sizeof(s_scanRaw[0])); i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;            // скрытая сеть: имени нет
    ScanNet& e = s_scanRaw[n++];
    memset(&e, 0, sizeof(e));
    strncpy(e.ssid, ssid.c_str(), SCAN_SSID_MAX);
    e.ssid[SCAN_SSID_MAX] = '\0';
    e.rssi = (int8_t)WiFi.RSSI(i);
    e.secured = (WiFi.encryptionType(i) != ENC_TYPE_NONE) ? 1 : 0;
  }
  s_scanCount = scanlist_build(s_scanRaw, n, s_scanList, NET_SCAN_MAX_RESULTS);
  Serial.printf("[net] найдено сетей: %u (отдано %u)\n", (unsigned)n, (unsigned)s_scanCount);
}

const ScanNet* net_scan_results(uint8_t* count) {
  int8_t found = WiFi.scanComplete();
  if (found < 0) {
    if (count) *count = 0;
    return NULL;
  }
  scan_collect((uint8_t)found);
  if (count) *count = s_scanCount;
  return s_scanList;
}

void net_update() {
  uint32_t now = millis();

  if (s_rebootPending && (int32_t)(now - s_rebootAt) >= 0) {
    Serial.println("[net] перезагрузка по запросу");
    delay(50);
    ESP.restart();
  }

  if (s_dnsRunning) {
    s_dns.processNextRequest();
  }

  switch (s_state) {
    case NET_BOOT:
      if (net_connected()) {
        Serial.printf("[net] подключено, IP %s\n", WiFi.localIP().toString().c_str());
        WiFi.setAutoReconnect(true);
        s_state = NET_STA;
        s_stateStart = now;
        s_ntpStarted = false;
      } else if (now - s_stateStart > STA_CONNECT_TIMEOUT_MS) {
        Serial.println("[net] не удалось подключиться - поднимаю точку доступа");
        WiFi.mode(WIFI_AP_STA);
        build_ap_ssid();
        WiFi.softAP(s_apSsid);
        s_dns.setErrorReplyCode(DNSReplyCode::NoError);
        s_dns.start(53, "*", WiFi.softAPIP());
        s_dnsRunning = true;
        s_state = NET_AP_STA;
        s_stateStart = now;
        s_lastRetry = now;
      }
      break;

    case NET_STA:
      if (!net_connected()) {
        if (s_lostAt == 0) s_lostAt = now;
        if (now - s_lostAt > STA_LOST_TIMEOUT_MS) {
          Serial.println("[net] связь потеряна - поднимаю точку доступа");
          WiFi.mode(WIFI_AP_STA);
          build_ap_ssid();
          WiFi.softAP(s_apSsid);
          s_dns.setErrorReplyCode(DNSReplyCode::NoError);
          s_dns.start(53, "*", WiFi.softAPIP());
          s_dnsRunning = true;
          s_state = NET_AP_STA;
          s_stateStart = now;
          s_lastRetry = now;
        }
      } else {
        s_lostAt = 0;
      }
      break;

    case NET_AP_STA:
      if (net_connected()) {
        Serial.printf("[net] подключено, IP %s - выключаю точку доступа\n",
                      WiFi.localIP().toString().c_str());
        stop_ap();
        WiFi.setAutoReconnect(true);
        s_state = NET_STA;
        s_stateStart = now;
        s_ntpStarted = false;
      } else if (now - s_lastRetry > STA_RETRY_MS) {
        s_lastRetry = now;
        if (net_wifi_configured()) {
          WiFi.begin(g_store.cfg.ssid, g_store.cfg.pass);
        }
      }
      break;

    case NET_AP:
    default:
      break;
  }

  if (net_connected()) {
    if (!s_ntpStarted || (now - s_lastNtp > NTP_RESYNC_MS)) {
      start_ntp();
    }
  }
}
