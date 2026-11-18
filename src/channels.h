/*
 * channels.h - драйвер помп (4 канала) и датчика низкого уровня воды.
 */
#pragma once

#include <stdint.h>
#include "app_config.h"

enum StartResult {
  START_OK = 0,
  START_LOW_WATER = 1,     // датчик сообщает о низком уровне воды
  START_BAD_CHANNEL = 2,
  START_BAD_SECONDS = 3
};

// Настраивает пины помп и датчика по текущим настройкам.
void channels_begin();

// Повторно применяет настройки пинов/полярности/датчика (без перезагрузки).
void channels_apply_config();

// Вызывать в loop(): отрабатывает таймауты полива и аварийное отключение
// по датчику низкого уровня воды.
void channels_update();

// Запуск канала на sec секунд. taskId >= 0 - запуск задачей (для индикации),
// taskId < 0 - ручной запуск.
StartResult channels_start(uint8_t ch, uint16_t sec, int8_t taskId);

void channels_stop(uint8_t ch);
void channels_stop_all();

bool     channels_is_on(uint8_t ch);
uint32_t channels_remaining_sec(uint8_t ch);  // 0, если выключен
int8_t   channels_task(uint8_t ch);           // id задачи или -1
uint8_t  channels_on_count();

bool sensor_is_low();   // true = "вода на исходе" (с учётом полярности)
int  sensor_raw();      // сырой уровень на пине (0/1)
