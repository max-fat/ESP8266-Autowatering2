/*
 * webjson.h - сборка JSON для /api/state, /api/config и /api/export.
 *
 * Вынесено из webui.cpp, чтобы эти структуры можно было проверять
 * host-тестами: ошибка в имени поля (например, потерянный "nm" с именем
 * канала) ломает интерфейс, но никак не проявляется при сборке прошивки.
 *
 * Модуль не знает про HTTP: только данные -> String.
 */
#pragma once

#include <Arduino.h>

String webjson_state();    // GET /api/state - всё, что нужно интерфейсу
String webjson_config();   // GET /api/config - настройки
String webjson_export();   // GET /api/export - файл резервной копии

// Причина выключения канала -> строка для UI ("timer" / "manual" / "low" / "").
const char* stop_reason_str(uint8_t reason);
