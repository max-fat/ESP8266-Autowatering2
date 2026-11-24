/*
 * otacheck.h - проверка файла прошивки перед обновлением по Wi-Fi.
 *
 * Правила намеренно живут отдельно от железа: их же повторяет веб-интерфейс,
 * чтобы пользователь увидел ошибку ещё до отправки файла, и они проверяются
 * host-тестами (ESP8266 для этого не нужен).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

// 32 КБ - заведомо меньше любой настоящей прошивки этого проекта (~300 КБ).
#define OTA_FILE_MIN (32u * 1024u)

enum OtaFileCheck {
  OTA_FILE_OK = 0,
  OTA_FILE_MISSING,   // файл не выбран или пустое имя
  OTA_FILE_EXT,       // расширение не .bin
  OTA_FILE_SMALL,     // меньше OTA_FILE_MIN - это не прошивка
  OTA_FILE_BIG        // больше свободного места под второй образ
};

// Полная проверка: имя + размер (size - размер файла, maxSize - что влезает).
OtaFileCheck otacheck_file(const char* filename, uint32_t size, uint32_t maxSize);

// Только размер.
OtaFileCheck otacheck_size(uint32_t size, uint32_t maxSize);

// Текст причины для интерфейса ("" для OTA_FILE_OK).
const char* otacheck_text(OtaFileCheck res);
