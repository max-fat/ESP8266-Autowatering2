/*
 * channels.h - драйвер помп (4 канала) и датчика низкого уровня воды.
 */
#pragma once

#include <stdint.h>
#include "app_config.h"

// Причины выключения канала (для уведомлений в UI).
enum StopReason {
  STOP_REASON_NONE = 0,
  STOP_REASON_TIMER = 1,      // истекло заданное время полива
  STOP_REASON_MANUAL = 2,     // остановлено вручную (кнопка/API)
  STOP_REASON_LOW_WATER = 3   // аварийное отключение: низкий уровень воды
};

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

// Событие последнего выключения канала: seq увеличивается при каждом
// выключении (UI сравнивает значения и показывает уведомление),
// reason - одно из StopReason, lastRunSec - сколько канал реально работал.
uint8_t  channels_stop_seq(uint8_t ch);
uint8_t  channels_stop_reason(uint8_t ch);
uint16_t channels_last_run_sec(uint8_t ch);

bool     channels_is_on(uint8_t ch);
uint32_t channels_remaining_sec(uint8_t ch);  // 0, если выключен
int8_t   channels_task(uint8_t ch);           // id задачи или -1
uint8_t  channels_on_count();

bool sensor_is_low();   // true = "вода на исходе" (с учётом полярности)
int  sensor_raw();      // сырой уровень на пине (0/1)
