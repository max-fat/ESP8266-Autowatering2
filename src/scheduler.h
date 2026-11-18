/*
 * scheduler.h - планировщик задач полива.
 *
 * Задача = канал + время суток + длительность + период в днях.
 * В EEPROM для каждой задачи хранится lastRun (время последнего полива -
 * это и есть "статус" задачи) и nextDue (ближайший плановый запуск).
 */
#pragma once

#include <stdint.h>
#include <time.h>
#include "app_config.h"

// Есть ли достоверное время (синхронизация по NTP прошла).
bool sched_time_valid();

// Обработка всех задач. Вызывать из loop().
void sched_update();

// Ближайшее плановое время задачи (0 - задача выключена или время неизвестно).
time_t sched_next_run(const TaskCfg& t);

// Сброс расписания задачи (после изменения её параметров из UI).
void sched_task_changed(uint8_t idx);

// Текстовая строка статуса для UI: off | wait | running | nosync | free
const char* sched_status_str(uint8_t idx);

// Количество занятых слотов задач.
uint8_t sched_task_count();
