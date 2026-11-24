#include "scanlist.h"

#include <string.h>

static int find_ssid(const ScanNet* list, uint8_t count, const char* ssid) {
  for (uint8_t i = 0; i < count; i++) {
    if (strncmp(list[i].ssid, ssid, SCAN_SSID_MAX + 1) == 0) return (int)i;
  }
  return -1;
}

static ScanNet make_item(const ScanNet& src) {
  ScanNet item;
  memset(&item, 0, sizeof(item));
  strncpy(item.ssid, src.ssid, SCAN_SSID_MAX);
  item.ssid[SCAN_SSID_MAX] = '\0';
  item.rssi = src.rssi;
  item.secured = src.secured ? 1 : 0;
  return item;
}

uint8_t scanlist_build(const ScanNet* in, uint8_t count, ScanNet* out, uint8_t maxOut) {
  if (!out || maxOut == 0) return 0;
  if (!in) count = 0;

  uint8_t n = 0;
  for (uint8_t i = 0; i < count; i++) {
    if (in[i].ssid[0] == '\0') continue;          /* скрытая сеть - имени нет */

    int idx = find_ssid(out, n, in[i].ssid);
    if (idx >= 0) {
      if (in[i].rssi > out[idx].rssi) out[idx].rssi = in[i].rssi;
      if (in[i].secured) out[idx].secured = 1;
      continue;
    }

    ScanNet item = make_item(in[i]);
    if (n < maxOut) {
      out[n++] = item;
      continue;
    }

    /* Места нет: вытесняем самую слабую запись, если новая сильнее. */
    uint8_t weakest = 0;
    for (uint8_t k = 1; k < n; k++) {
      if (out[k].rssi < out[weakest].rssi) weakest = k;
    }
    if (item.rssi > out[weakest].rssi) out[weakest] = item;
  }

  /* Сортировка вставками по убыванию сигнала (список короткий). */
  for (uint8_t i = 1; i < n; i++) {
    ScanNet key = out[i];
    int j = (int)i - 1;
    while (j >= 0 && out[j].rssi < key.rssi) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = key;
  }

  return n;
}
