#!/usr/bin/env python3
"""Подставляет в docs/index.html версию, размер, MD5 и дату сборки
и кладёт копию прошивки в docs/firmware/ для прошивальщика на странице.

Страница проекта не должна «врать» про версию: цифры берутся из
firmware/build-info.txt, который пишет tools/build_firmware.sh.
Копия .bin нужна, чтобы страница на GitHub Pages могла прошить плату
из браузера, не обращаясь к внешним сайтам.

    python3 tools/update_docs.py           # обновить страницу
    python3 tools/update_docs.py --check   # только проверить (код 1, если разошлось)
"""
import hashlib
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INFO = ROOT / "firmware" / "build-info.txt"
PAGE = ROOT / "docs" / "index.html"
BIN = ROOT / "firmware" / "autowatering-esp8266.bin"
DOCS_BIN = ROOT / "docs" / "firmware" / "autowatering-esp8266.bin"


def file_md5(path):
    digest = hashlib.md5()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sync_bin(info, check_only):
    """Копия прошивки для docs/firmware/ — её скачивает прошивальщик на странице."""
    problems = []
    if not BIN.exists():
        sys.exit("нет %s — сначала соберите прошивку" % BIN)
    if file_md5(BIN) != info["md5"]:
        problems.append("%s не совпадает с md5 из build-info.txt" % BIN)

    if check_only:
        if not DOCS_BIN.exists():
            problems.append("нет %s — запустите python3 tools/update_docs.py" % DOCS_BIN.relative_to(ROOT))
        elif file_md5(DOCS_BIN) != info["md5"]:
            problems.append("%s устарел — запустите python3 tools/update_docs.py"
                            % DOCS_BIN.relative_to(ROOT))
    else:
        DOCS_BIN.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(BIN, DOCS_BIN)
        if file_md5(DOCS_BIN) != info["md5"]:
            problems.append("копия %s получилась битой" % DOCS_BIN.relative_to(ROOT))
    return problems


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

    check_only = "--check" in sys.argv
    problems = sync_bin(info, check_only)

    if check_only:
        if changed:
            problems.insert(0, "docs/index.html расходится со сборкой:")
            problems[1:1] = ["  " + c for c in changed]
        if problems:
            for item in problems:
                print(item)
            return 1
        print("docs/index.html в порядке: %s, %s, md5 %s (docs/firmware в синхроне)"
              % (fields["version"], fields["size"], fields["md5"]))
        return 0

    PAGE.write_text(html, encoding="utf-8")
    if changed:
        print("docs/index.html обновлён:")
        for c in changed:
            print("  " + c)
    else:
        print("docs/index.html уже актуален: %s" % fields["version"])
    if problems:
        for item in problems:
            print("ВНИМАНИЕ: " + item)
        return 1
    print("docs/firmware/autowatering-esp8266.bin: %d байт, md5 %s"
          % (size, fields["md5"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
