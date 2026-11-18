/*
 * storage.h - энергонезависимое хранение конфигурации и статусов задач.
 *
 * Всё состояние (настройки + задачи, включая время последнего полива)
 * лежит одной структурой Store в секторе эмуляции EEPROM и защищено CRC32.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "app_config.h"

extern Store g_store;

// Читает состояние из EEPROM. При первом старте / повреждении данных
// заполняет умолчания и сразу сохраняет их. Возвращает true, если
// данные в EEPROM были валидны.
bool storage_begin();

// Записывает g_store в EEPROM (с пересчётом CRC).
void storage_save();

// Сброс к заводским настройкам (стирает Wi-Fi и все задачи).
void storage_factory_reset();

// Заполняет g_store умолчаниями (без записи в EEPROM).
void storage_defaults();

uint32_t crc32_buf(const void* data, size_t len);
