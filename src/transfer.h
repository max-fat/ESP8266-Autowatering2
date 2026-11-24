/*
 * transfer.h - обмен настройками и задачами (экспорт/импорт).
 *
 * Задача кодируется в одну строку из 8 чисел через запятую:
 *   ch,h,m,dur,pd,en,last,next
 * Такой формат удобно передавать в POST /api/import обычным
 * application/x-www-form-urlencoded — на ESP8266 нет JSON-парсера, а строка
 * из чисел разбирается без выделения памяти (JSON разбирает браузер).
 *
 * Модуль не зависит от Arduino и покрыт host-тестами.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "app_config.h"

#define TRANSFER_TASK_FIELDS 8

/* Закодировать задачу в строку. false = буфер мал. */
bool transfer_task_encode(const TaskCfg& t, char* out, size_t outSize);

/*
 * Разобрать строку задачи. Проверяются диапазоны:
 * канал < NUM_CHANNELS, часы 0..23, минуты 0..59, длительность >= 1,
 * период 1..TASK_MAX_PERIOD_DAYS, флаг активности 0/1.
 * false = строка битая (тогда out не меняется).
 */
bool transfer_task_decode(const char* s, TaskCfg* out);
