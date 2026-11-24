#include <Arduino.h>
#include <Updater.h>

#include "ota.h"

#include <stdio.h>
#include <string.h>

#include "channels.h"
#include "otacheck.h"

namespace {

uint8_t  s_state = OTA_IDLE;
char     s_err[64];
uint32_t s_total = 0;
uint32_t s_received = 0;
uint32_t s_restartAt = 0;
bool     s_restart = false;

void setErr(const char* text) {
  s_state = OTA_ERROR;
  s_restart = false;
  snprintf(s_err, sizeof(s_err), "%s", text ? text : "");
  Serial.printf("[ota] ошибка: %s\n", s_err);
}

// Коды Updater -> текст для интерфейса.
const char* update_error_text(uint8_t code) {
  switch (code) {
    case UPDATE_ERROR_WRITE:        return "ошибка записи во flash";
    case UPDATE_ERROR_ERASE:        return "ошибка стирания flash";
    case UPDATE_ERROR_READ:         return "ошибка чтения flash";
    case UPDATE_ERROR_SPACE:        return "не хватает места во flash";
    case UPDATE_ERROR_SIZE:         return "пустой файл прошивки";
    case UPDATE_ERROR_MAGIC_BYTE:   return "это не прошивка ESP8266 (нет сигнатуры 0xE9)";
    case UPDATE_ERROR_BOOTSTRAP:    return "модуль в режиме прошивки по UART - обновление невозможно";
    case UPDATE_ERROR_FLASH_CONFIG: return "не совпадает конфигурация flash";
    case UPDATE_ERROR_MD5:          return "контрольная сумма образа не совпала";
    default:                        return "не удалось записать прошивку";
  }
}

}  // namespace

uint32_t ota_max_size() {
  // Updater пишет новый образ в конец свободной области - до начала SPIFFS.
  return ESP.getFreeSketchSpace();
}

uint8_t ota_state()       { return s_state; }
uint32_t ota_total()      { return s_total; }
uint32_t ota_received()   { return s_received; }
const char* ota_error()   { return s_err; }

bool ota_begin(uint32_t total, const char* filename) {
  s_state = OTA_IDLE;
  s_err[0] = 0;
  s_total = total;
  s_received = 0;
  s_restart = false;

  const uint32_t maxSize = ota_max_size();
  OtaFileCheck chk = otacheck_file(filename, total, maxSize);
  if (chk != OTA_FILE_OK) {
    setErr(otacheck_text(chk));
    return false;
  }

  // Пока идёт запись flash, основной цикл не выполняется - полив мог бы
  // «зависнуть» включённым, поэтому гасим все каналы заранее.
  channels_stop_all();

  if (!Update.begin(total, U_FLASH)) {
    setErr(update_error_text(Update.getError()));
    return false;
  }

  s_state = OTA_RUNNING;
  Serial.printf("[ota] приём прошивки: %lu байт (свободно %lu)\n",
                (unsigned long)total, (unsigned long)maxSize);
  return true;
}

size_t ota_write(const uint8_t* data, size_t len) {
  if (s_state != OTA_RUNNING) return 0;
  size_t written = Update.write((uint8_t*)data, len);
  s_received += written;
  if (written != len) {
    // Внутри Updater уже выставлен код ошибки - сохраняем текст и закрываем приём.
    setErr(update_error_text(Update.getError()));
    Update.end(false);
  }
  return written;
}

bool ota_finish() {
  if (s_state != OTA_RUNNING) return false;

  // evenIfRemaining = true: размер из Content-Length может быть больше файла
  // (границы multipart), и Updater запишет ровно принятые байты.
  if (!Update.end(true)) {
    setErr(update_error_text(Update.getError()));
    return false;
  }

  s_state = OTA_DONE;
  s_restart = true;
  s_restartAt = millis() + 1500;   // даём ответу уйти в браузер
  Serial.printf("[ota] образ принят: %lu байт, md5 %s\n",
                (unsigned long)s_received, Update.md5String().c_str());
  return true;
}

void ota_abort() {
  if (s_state == OTA_RUNNING) Update.end(false);
  setErr("загрузка прервана");
}

void ota_update() {
  if (!s_restart) return;
  if ((int32_t)(millis() - s_restartAt) < 0) return;
  s_restart = false;
  Serial.println(F("[ota] перезагрузка в новую прошивку"));
  Serial.flush();
  ESP.restart();
}
