/*
 * clock.h - единая точка работы со временем.
 *
 * Внутри всё считается в "локальных секундах эпохи" (wall clock): это обычные
 * unix-секунды, к которым уже прибавлено смещение часового пояса из настроек.
 * Такой подход не зависит от того, как ядро Arduino трактует time()/localtime()
 * (в 2.4.2 time() отдаёт локальное время, в 3.x - UTC), и не зависит от newlib TZ.
 *
 * Источники времени:
 *   1) NTP (приоритет, как только синхронизация прошла);
 *   2) время, переданное с телефона через веб-интерфейс (живёт до перезагрузки);
 *   3) нет времени -> задачи не выполняются.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#define CLOCK_TIME_VALID_MIN 1700000000UL /* 14.11.2023 */

// Есть ли достоверное время.
bool clock_valid();

// Текущее время: UTC и "локальные секунды эпохи" (UTC + смещение пояса из настроек).
uint32_t clock_utc();
uint32_t clock_local();

// Смещение пояса в секундах (из настроек).
int32_t clock_tz_sec();

// Источник времени: "ntp" | "manual" | "none".
const char* clock_source();

// Установить время с телефона. utcSeconds - unix-время UTC (Date.now()/1000).
void clock_set_from_phone(uint32_t utcSeconds);

// Забыть время, установленное с телефона (сброс к заводским настройкам).
void clock_clear_manual();

/* ---- арифметика в локальной шкале (без DST и без календарных сюрпризов) ---- */
uint32_t clock_day_start(uint32_t localEpoch);                       // 00:00 текущих суток
uint32_t clock_at_time(uint32_t localEpoch, int days, uint8_t hour, uint8_t minute);
uint32_t clock_add_minutes(uint32_t localEpoch, int minutes);

/* ---- форматирование ---- */
// "18.11 08:00" (без года) или "—" при 0.
void clock_format_short(uint32_t localEpoch, char* buf, size_t n);
// "18.11.2026 08:00" или "—".
void clock_format_full(uint32_t localEpoch, char* buf, size_t n);
// Текущие время/дата устройства: "08:13:20" / "18.11.2026".
void clock_now_strings(char* timeBuf, size_t timeLen, char* dateBuf, size_t dateLen);
