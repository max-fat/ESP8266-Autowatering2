/*
 * netmgr.h - подключение к Wi-Fi, точка доступа для настройки, NTP.
 *
 * Логика:
 *   - если в настройках нет SSID -> поднимается открытая точка доступа
 *     "AutoWater-XXXXXX" с captive portal (страница настройки Wi-Fi);
 *   - если SSID есть, но подключиться не удалось за 25 секунд ->
 *     тоже поднимается точка доступа, чтобы можно было исправить настройки;
 *   - после успешного подключения время синхронизируется по NTP.
 */
#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "scanlist.h"

void   net_begin();
void   net_update();

bool   net_connected();        // есть подключение к роутеру
bool   net_ap_active();        // поднята точка доступа
bool   net_wifi_configured();  // в настройках есть SSID

String net_ip();               // IP в режиме клиента (или 0.0.0.0)
String net_ap_ip();            // IP точки доступа
String net_ap_ssid();
String net_mode_str();         // "sta" | "ap" | "sta+ap" | "boot"
int    net_rssi();

// Перезагрузка через delayMs (чтобы страница успела получить ответ).
void net_request_reboot(uint32_t delayMs = 800);

// Применить новые параметры времени (NTP-сервер / смещение зоны).
void net_apply_time_config();

// Принудительно перезапросить время у NTP.
void net_force_ntp_sync();

/* ---- Сканирование Wi-Fi (для автовыбора SSID) ----
 * Сканирование асинхронное: запрос не блокирует веб-сервер и не рвёт
 * соединение, поэтому страницу можно открывать и в режиме точки доступа.
 *   net_scan_start()          - начать сканирование (если уже идёт - ничего);
 *   net_scan_state()          - -1 идёт, -2 данных нет, >= 0 готово (число сетей);
 *   net_scan_results(&count)  - готовый список (только при state >= 0).
 */
void             net_scan_start();
int8_t           net_scan_state();
const ScanNet*   net_scan_results(uint8_t* count);

#define NET_SCAN_MAX_RESULTS 16   // сколько сетей отдаём в интерфейс
