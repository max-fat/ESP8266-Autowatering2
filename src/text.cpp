#include "text.h"

#include <stdio.h>
#include <string.h>

static bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Сколько байт занимает UTF-8 символ по ведущему байту (0 = мусор). */
static size_t utf8_len(unsigned char c) {
  if ((c & 0x80) == 0x00) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 0;
}

void text_set(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }

  const char* start = src;
  while (*start && is_space(*start)) start++;

  size_t len = strlen(start);
  while (len > 0 && is_space(start[len - 1])) len--;

  const size_t max = dstSize - 1;
  size_t out = 0;
  size_t i = 0;
  while (i < len && out < max) {
    unsigned char c = (unsigned char)start[i];
    size_t need = utf8_len(c);
    if (need == 0) break;                 /* битый байт - дальше не копируем */
    if (i + need > len) break;            /* символ обрезан концом строки */
    if (out + need > max) break;          /* символ не влезает в буфер целиком */
    bool ok = true;
    for (size_t k = 1; k < need; k++) {
      if (((unsigned char)start[i + k] & 0xC0) != 0x80) { ok = false; break; }
    }
    if (!ok) break;
    memcpy(dst + out, start + i, need);
    out += need;
    i += need;
  }

  /* хвостовые пробелы могли остаться внутри уже скопированного */
  while (out > 0 && is_space(dst[out - 1])) out--;
  dst[out] = '\0';
}

void text_json_escape(const char* in, char* out, size_t outSize) {
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
      size_t l = strlen(rep);
      if (o + l + 1 > outSize) break;
      memcpy(out + o, rep, l);
      o += l;
    } else {
      if (o + 2 > outSize) break;
      out[o++] = (char)c;
    }
  }

  out[o] = '\0';
}

size_t text_utf8_len(const char* s) {
  if (!s) return 0;
  size_t n = 0;
  for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
    if ((*p & 0xC0) != 0x80) n++;   /* считаем только ведущие байты */
  }
  return n;
}
