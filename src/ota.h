/*
 * ota.h - обновление прошивки по Wi-Fi через веб-интерфейс.
 *
 * Как это работает: браузер отправляет .bin на POST /api/ota, прошивка пишет
 * его во свободную область flash (её размер отдаёт ota_max_size()), а на
 * перезагрузке встроенный загрузчик eboot копирует принятый образ в начало
 * flash и запускает его. Кабель и esptool не нужны; настройки и задачи остаются.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

enum OtaState {
  OTA_IDLE = 0,     // обновление не начиналось
  OTA_RUNNING = 1,  // идёт приём файла
  OTA_DONE = 2,     // образ принят, модуль перезагрузится
  OTA_ERROR = 3     // ошибка (текст в ota_error())
};

// Сколько байт влезает во второй образ (свободно между прошивкой и SPIFFS).
uint32_t ota_max_size();

// Начать приём. total - размер файла (верхняя оценка, см. Updater::begin).
// Останавливает полив: на время записи flash основной цикл не выполняется.
bool ota_begin(uint32_t total, const char* filename);

// Принять очередную порцию данных. Возвращает число записанных байт.
size_t ota_write(const uint8_t* data, size_t len);

// Завершить приём и записать команду загрузчику. После успеха будет
// перезагрузка (её выполняет ota_update() из loop - чтобы ответ успел уйти).
bool ota_finish();

// Отменить обновление (обрыв связи, ошибка, отказ пользователя).
void ota_abort();

void ota_update();

uint8_t  ota_state();
uint32_t ota_total();
uint32_t ota_received();
const char* ota_error();   // "" если ошибки нет
