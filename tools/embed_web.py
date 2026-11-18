#!/usr/bin/env python3
"""
Упаковка web/index.html в src/page_html.h.

Страница сжимается gzip и попадает в прошивку как PROGMEM-массив.
Так экономится и flash, и RAM ESP8266: HTTP-сервер отдаёт её
с заголовком Content-Encoding: gzip.

Запуск (из корня репозитория):
    python3 tools/embed_web.py
"""
import gzip
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_HTML = os.path.join(ROOT, "web", "index.html")
OUT_H = os.path.join(ROOT, "src", "page_html.h")
CHUNK = 16


def main() -> int:
    with open(SRC_HTML, "rb") as f:
        raw = f.read()

    # mtime=0 -> воспроизводимый результат (одинаковый .bin при сборке)
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    lines = []
    lines.append("/* Автоматически сгенерированный файл. Не редактировать!")
    lines.append(" * Источник: web/index.html")
    lines.append(" * Сборка:   python3 tools/embed_web.py")
    lines.append(" * Размер исходника: %d байт, после gzip: %d байт" % (len(raw), len(packed)))
    lines.append(" */")
    lines.append("#pragma once")
    lines.append("#include <stdint.h>")
    lines.append("#include <stddef.h>")
    lines.append("")
    lines.append("#define INDEX_HTML_RAW_LEN %d" % len(raw))
    lines.append("#define INDEX_HTML_GZ_LEN %d" % len(packed))
    lines.append("")
    lines.append("static const uint8_t INDEX_HTML_GZ[] PROGMEM = {")
    for i in range(0, len(packed), CHUNK):
        chunk = packed[i:i + CHUNK]
        lines.append("  " + ", ".join("0x%02x" % b for b in chunk) + ",")
    lines.append("};")
    lines.append("")

    with open(OUT_H, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))

    print("page_html.h: %d -> %d байт (gzip), записано в %s" % (len(raw), len(packed), OUT_H))
    return 0


if __name__ == "__main__":
    sys.exit(main())
