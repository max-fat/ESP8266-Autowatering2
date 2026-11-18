/*
 * ESP8266-Autowatering2 - контроллер автополива растений
 *
 * Точка входа: инициализация хранилища, каналов, сети и веб-интерфейса.
 */
#include <Arduino.h>

#include "app_config.h"
#include "channels.h"
#include "netmgr.h"
#include "scheduler.h"
#include "storage.h"
#include "webui.h"

static uint32_t s_lastSched = 0;

static void print_banner() {
  Serial.println();
  Serial.println(F("======================================"));
  Serial.printf(" %s v%s\n", FW_NAME, FW_VERSION);
  Serial.println(F("======================================"));
}

static void print_config() {
  Serial.printf("Wi-Fi SSID : %s\n", g_store.cfg.ssid[0] ? g_store.cfg.ssid : "(не настроен)");
  Serial.printf("Датчик     : GPIO%u, %s, %s\n", g_store.cfg.sensorPin,
                g_store.cfg.sensorActiveLow ? "низкий=мало воды" : "высокий=мало воды",
                g_store.cfg.sensorPullup ? "pullup" : "no pullup");
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    Serial.printf("Канал %u    : GPIO%u%s\n", i + 1, g_store.cfg.ch[i].pin,
                  g_store.cfg.ch[i].activeLow ? " (инверсия)" : "");
  }
  Serial.printf("Задач      : %u\n", sched_task_count());
}

void setup() {
  Serial.begin(115200);
  delay(50);
  print_banner();

  bool ok = storage_begin();
  Serial.printf("[store] %s\n", ok ? "конфигурация загружена" : "конфигурация сброшена к умолчаниям");
  print_config();

  channels_begin();
  net_begin();
  webui_begin();

  s_lastSched = millis();
  Serial.println(F("[sys] запуск завершён"));
}

void loop() {
  net_update();
  webui_update();
  channels_update();

  uint32_t now = millis();
  if (now - s_lastSched >= 1000) {
    s_lastSched = now;
    sched_update();
  }

  yield();
}
