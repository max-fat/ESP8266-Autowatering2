#include "channels.h"

#include <Arduino.h>

#include "storage.h"

struct ChannelRuntime {
  bool     on;
  uint32_t startedMs;
  uint32_t durationMs;
  int8_t   taskId;
  // Событие последнего выключения - по нему UI показывает уведомление
  // ("полив завершён", "остановлен вручную", "низкий уровень воды").
  uint8_t  stopSeq;
  uint8_t  stopReason;
  uint16_t lastRunSec;
};

static ChannelRuntime s_ch[NUM_CHANNELS];

// --- состояние датчика уровня (с антидребезгом) ---
static int      s_sensorRaw = 1;
static int      s_sensorStable = 1;
static uint8_t  s_sensorCnt = 0;
static uint32_t s_sensorLastPoll = 0;

static bool valid_ch(uint8_t ch) { return ch < NUM_CHANNELS; }

static uint8_t off_level(uint8_t ch) {
  return g_store.cfg.ch[ch].activeLow ? HIGH : LOW;
}
static uint8_t on_level(uint8_t ch) {
  return g_store.cfg.ch[ch].activeLow ? LOW : HIGH;
}

static void write_channel(uint8_t ch, bool on) {
  digitalWrite(g_store.cfg.ch[ch].pin, on ? on_level(ch) : off_level(ch));
}

static void stop_internal(uint8_t ch, uint8_t reason) {
  if (!valid_ch(ch) || !s_ch[ch].on) return;
  s_ch[ch].on = false;
  s_ch[ch].taskId = -1;
  s_ch[ch].stopSeq++;
  s_ch[ch].stopReason = reason;
  uint32_t elapsedSec = (millis() - s_ch[ch].startedMs) / 1000UL;
  if (elapsedSec > 0xFFFF) elapsedSec = 0xFFFF;
  s_ch[ch].lastRunSec = (uint16_t)elapsedSec;
  write_channel(ch, false);
}

static void setup_channel_pin(uint8_t ch) {
  uint8_t pin = g_store.cfg.ch[ch].pin;
  // Сначала выставляем "выключено", потом переводим в выход - без щелчка реле.
  digitalWrite(pin, off_level(ch));
  pinMode(pin, OUTPUT);
  write_channel(ch, false);
}

static void setup_sensor_pin() {
  uint8_t pin = g_store.cfg.sensorPin;
  // GPIO16 не имеет внутренней подтяжки - для него работает только INPUT.
  if (g_store.cfg.sensorPullup && pin != 16) {
    pinMode(pin, INPUT_PULLUP);
  } else {
    pinMode(pin, INPUT);
  }
}

void channels_begin() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    s_ch[i].on = false;
    s_ch[i].startedMs = 0;
    s_ch[i].durationMs = 0;
    s_ch[i].taskId = -1;
    s_ch[i].stopSeq = 0;
    s_ch[i].stopReason = STOP_REASON_NONE;
    s_ch[i].lastRunSec = 0;
    setup_channel_pin(i);
  }
  setup_sensor_pin();
  s_sensorRaw = digitalRead(g_store.cfg.sensorPin);
  s_sensorStable = s_sensorRaw;
  s_sensorCnt = 0;
}

void channels_apply_config() {
  channels_stop_all();
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    setup_channel_pin(i);
  }
  setup_sensor_pin();
  s_sensorRaw = digitalRead(g_store.cfg.sensorPin);
  s_sensorStable = s_sensorRaw;
  s_sensorCnt = 0;
}

int sensor_raw() {
  return s_sensorRaw;
}

bool sensor_is_low() {
  int v = s_sensorStable;
  return g_store.cfg.sensorActiveLow ? (v == LOW) : (v == HIGH);
}

uint8_t channels_on_count() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) if (s_ch[i].on) n++;
  return n;
}

bool channels_is_on(uint8_t ch) {
  return valid_ch(ch) && s_ch[ch].on;
}

int8_t channels_task(uint8_t ch) {
  return valid_ch(ch) ? s_ch[ch].taskId : -1;
}

uint32_t channels_remaining_sec(uint8_t ch) {
  if (!valid_ch(ch) || !s_ch[ch].on) return 0;
  uint32_t elapsed = millis() - s_ch[ch].startedMs;
  if (elapsed >= s_ch[ch].durationMs) return 0;
  return (s_ch[ch].durationMs - elapsed + 999) / 1000;
}

StartResult channels_start(uint8_t ch, uint16_t sec, int8_t taskId) {
  if (!valid_ch(ch)) return START_BAD_CHANNEL;
  if (sec == 0) return START_BAD_SECONDS;

  uint16_t maxRun = g_store.cfg.maxRunSec;
  if (maxRun == 0) maxRun = DEFAULT_MAX_RUN_SEC;
  if (sec > maxRun) sec = maxRun;

  if (g_store.cfg.blockWhenLow && sensor_is_low()) {
    return START_LOW_WATER;
  }

  s_ch[ch].on = true;
  s_ch[ch].startedMs = millis();
  s_ch[ch].durationMs = (uint32_t)sec * 1000UL;
  s_ch[ch].taskId = taskId;
  write_channel(ch, true);
  return START_OK;
}

void channels_stop(uint8_t ch) {
  stop_internal(ch, STOP_REASON_MANUAL);
}

void channels_stop_all() {
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) channels_stop(i);
}

uint8_t channels_stop_seq(uint8_t ch) {
  return valid_ch(ch) ? s_ch[ch].stopSeq : (uint8_t)0;
}

uint8_t channels_stop_reason(uint8_t ch) {
  return valid_ch(ch) ? s_ch[ch].stopReason : (uint8_t)STOP_REASON_NONE;
}

uint16_t channels_last_run_sec(uint8_t ch) {
  return valid_ch(ch) ? s_ch[ch].lastRunSec : 0;
}

void channels_update() {
  uint32_t now = millis();

  // 1. Антидребезг датчика уровня (опрос каждые 20 мс, 5 совпадений подряд).
  if (now - s_sensorLastPoll >= 20) {
    s_sensorLastPoll = now;
    int v = digitalRead(g_store.cfg.sensorPin);
    if (v != s_sensorRaw) {
      s_sensorRaw = v;
      s_sensorCnt = 0;
    } else if (s_sensorCnt < 5) {
      s_sensorCnt++;
      if (s_sensorCnt == 5) s_sensorStable = v;
    }
  }

  // 2. Аварийное отключение при низком уровне воды.
  if (g_store.cfg.blockWhenLow && sensor_is_low()) {
    for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
      stop_internal(i, STOP_REASON_LOW_WATER);
    }
  }

  // 3. Окончание полива по времени.
  for (uint8_t i = 0; i < NUM_CHANNELS; i++) {
    if (!s_ch[i].on) continue;
    if ((uint32_t)(now - s_ch[i].startedMs) >= s_ch[i].durationMs) {
      stop_internal(i, STOP_REASON_TIMER);
    }
  }
}
