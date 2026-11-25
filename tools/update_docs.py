#!/usr/bin/env python3
"""Подставляет в docs/index.html версию, размер, MD5 и дату сборки.

Страница проекта не должна «врать» про версию: цифры берутся из
firmware/build-info.txt, который пишет tools/build_firmware.sh.

    python3 tools/update_docs.py           # обновить страницу
    python3 tools/update_docs.py --check   # только проверить (код 1, если разошлось)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INFO = ROOT / "firmware" / "build-info.txt"
PAGE = ROOT / "docs" / "index.html"


def read_info():
    if not INFO.exists():
        sys.exit("нет %s — сначала соберите прошивку" % INFO)
    data = {}
    for line in INFO.read_text(encoding="utf-8").splitlines():
        if ":" in line:
            key, val = line.split(":", 1)
            data[key.strip()] = val.strip()
    return data


def human_size(num):
    kb = int(num) / 1024.0
    return "%d КБ" % round(kb)


def main():
    info = read_info()
    size = int(info["size"].split()[0])   # "322784 bytes"
    fields = {
        "version": info["version"],
        "size": human_size(size),
        "md5": info["md5"],
        "date": info["built"].split(" ")[0],  # 2026-11-24 13:17:49 UTC -> 2026-11-24
    }

    html = PAGE.read_text(encoding="utf-8")
    changed = []
    for key, value in fields.items():
        pattern = re.compile(r'(<(\w+)[^>]*data-fw="%s"[^>]*>)([^<]*)(</\2>)' % re.escape(key))
        def repl(m):
            if m.group(3) != value:
                changed.append("%s: %s -> %s" % (key, m.group(3), value))
            return m.group(1) + value + m.group(4)
        html = pattern.sub(repl, html)

    if "--check" in sys.argv:
        if changed:
            print("docs/index.html расходится со сборкой:")
            for c in changed:
                print("  " + c)
            return 1
        print("docs/index.html в порядке: %s, %s, md5 %s" % (fields["version"], fields["size"], fields["md5"]))
        return 0

    PAGE.write_text(html, encoding="utf-8")
    if changed:
        print("docs/index.html обновлён:")
        for c in changed:
            print("  " + c)
    else:
        print("docs/index.html уже актуален: %s" % fields["version"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
