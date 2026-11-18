#include "scanlist.h"

#include <stdio.h>
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

void scanlist_json_escape(const char* in, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  size_t o = 0;
  if (!in) {
    out[0] = '\0';
    return;
  }

  for (const char* p = in; *p; p++) {
    unsigned char c = (unsigned char)*p;
    char buf[8];
    const char* rep = NULL;

    switch (c) {
      case '"':  rep = "\\\""; break;
      case '\\': rep = "\\\\"; break;
      case '\n': rep = "\\n";  break;
      case '\r': rep = "\\r";  break;
      case '\t': rep = "\\t";  break;
      default:
        if (c < 0x20) {
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
          rep = buf;
        }
        break;
    }

    if (rep) {
      size_t len = strlen(rep);
      if (o + len + 1 > outSize) break;
      memcpy(out + o, rep, len);
      o += len;
    } else {
      if (o + 2 > outSize) break;
      out[o++] = (char)c;
    }
  }

  out[o] = '\0';
}
