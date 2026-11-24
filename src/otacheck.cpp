#include "otacheck.h"

#include <string.h>

static char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Заканчивается ли строка на ".bin" без учёта регистра.
static bool ends_with_bin(const char* s) {
  size_t n = s ? strlen(s) : 0;
  if (n < 4) return false;
  return lower(s[n - 4]) == '.' && lower(s[n - 3]) == 'b' &&
         lower(s[n - 2]) == 'i' && lower(s[n - 1]) == 'n';
}

OtaFileCheck otacheck_size(uint32_t size, uint32_t maxSize) {
  if (size < OTA_FILE_MIN) return OTA_FILE_SMALL;
  if (maxSize > 0 && size > maxSize) return OTA_FILE_BIG;
  return OTA_FILE_OK;
}

OtaFileCheck otacheck_file(const char* filename, uint32_t size, uint32_t maxSize) {
  size_t n = filename ? strlen(filename) : 0;
  if (n == 0) return OTA_FILE_MISSING;
  if (!ends_with_bin(filename)) return OTA_FILE_EXT;
  return otacheck_size(size, maxSize);
}

const char* otacheck_text(OtaFileCheck res) {
  switch (res) {
    case OTA_FILE_OK:      return "";
    case OTA_FILE_MISSING: return "не выбран файл прошивки";
    case OTA_FILE_EXT:     return "нужен файл .bin (.bin с сайта проекта)";
    case OTA_FILE_SMALL:   return "файл слишком маленький - это не прошивка";
    case OTA_FILE_BIG:     return "файл больше свободного места под прошивку";
  }
  return "неизвестная ошибка выбора файла";
}
