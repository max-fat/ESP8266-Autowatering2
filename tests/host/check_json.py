#!/usr/bin/env python3
"""Проверка JSON, который прошивка отдаёт в интерфейс.

Собирается бинарником host-тестов (режимы --dump-state / --dump-config /
--dump-export / --dump-ota / --dump-ident), а разбирается настоящим парсером — так ловится любая ошибка
форматирования, которую не видно при сборке прошивки (запятая, кавычка,
потерянное поле).

    ./test_logic --dump-state | python3 check_json.py state
"""
import json
import sys

FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def check_ident(d):
    """Визитка для сканера (/api/ident): метка приложения и версия."""
    check(d.get("app") == "autowatering", "нет метки app=autowatering")
    check(bool(d.get("fw")), "нет версии fw")
    check(isinstance(d.get("ch"), int) and d["ch"] > 0, "нет числа каналов ch")
    check(isinstance(d.get("name"), str), "нет имени name")
    check(isinstance(d.get("ip"), str), "нет адреса ip")


def check_state(d):
    check(d.get("fw"), "нет поля fw")
    check(d.get("appName") == "ESP8266-Autowatering2", "нет appName")
    check("name" in d, "нет имени модуля")
    check(d.get("time") and d.get("date"), "нет строк времени")
    check(isinstance(d.get("tz"), int), "tz не число")
    for key in ("wifi", "sensor", "limits", "ch", "tasks"):
        check(key in d, "нет секции %s" % key)
    check(isinstance(d["heap"], int) and isinstance(d["uptime"], int), "heap/uptime не числа")
    check(len(d["ch"]) == d["limits"]["channels"], "число каналов не совпадает с limits")

    for ch in d["ch"]:
        # именно на этом поле держится подпись «Канал N · Имя»
        check("nm" in ch, "у канала %s нет поля nm" % ch.get("i"))
        for key in ("i", "pin", "inv", "on", "rem", "task", "seq", "rsn", "run"):
            check(key in ch, "у канала %s нет поля %s" % (ch.get("i"), key))

    for t in d["tasks"]:
        for key in ("i", "on", "ch", "h", "m", "dur", "pd", "lastStr", "nextStr", "st"):
            check(key in t, "у задачи %s нет поля %s" % (t.get("i"), key))

    print("state ok: каналов %d (имена: %s), задач %d"
          % (len(d["ch"]), ", ".join(c["nm"] or "—" for c in d["ch"]), len(d["tasks"])))


def check_config(d):
    for key in ("name", "ssid", "ntp", "tz", "sensorPin", "sensorInv", "sensorPull",
                "blockLow", "maxRun", "catchup", "ch"):
        check(key in d, "нет поля %s" % key)
    for ch in d["ch"]:
        for key in ("pin", "inv", "name"):
            check(key in ch, "у канала в конфиге нет поля %s" % key)
    print("config ok: модуль «%s», каналов %d (%s)"
          % (d.get("name") or "—", len(d["ch"]), ", ".join(c["name"] or "—" for c in d["ch"])))


def check_export(d):
    check(d.get("app") == "ESP8266-Autowatering2", "нет app")
    check(d.get("fw"), "нет fw")
    check(d.get("sv") == 3, "sv должен быть 3")
    cfg = d.get("cfg") or {}
    for key in ("name", "tz", "ntp", "sensorPin", "sensorInv", "sensorPull",
                "blockLow", "maxRun", "catchup", "ch"):
        check(key in cfg, "в экспорте нет cfg.%s" % key)
    for ch in cfg.get("ch", []):
        for key in ("pin", "inv", "name"):
            check(key in ch, "в экспорте у канала нет %s" % key)
    for t in d.get("tasks", []):
        for key in ("ch", "h", "m", "dur", "pd", "en", "last", "next"):
            check(key in t, "в экспорте у задачи нет %s" % key)
    raw = json.dumps(d, ensure_ascii=False)
    check("secret" not in raw and "pass" not in raw, "в экспорте оказался пароль Wi-Fi")
    print("export ok: задач %d, версия формата %s" % (len(d.get("tasks", [])), d.get("sv")))


def check_ota(d):
    for key in ("fw", "state", "stateText", "received", "total", "maxSize", "minSize",
                "freeHeap", "err"):
        check(key in d, "нет поля %s" % key)
    check(isinstance(d.get("state"), int) and 0 <= d["state"] <= 3, "state вне 0..3")
    check(d.get("stateText") in ("idle", "running", "done", "error"),
          "stateText не из набора: %r" % d.get("stateText"))
    for key in ("received", "total", "maxSize", "minSize", "freeHeap"):
        check(isinstance(d.get(key), int) and d[key] >= 0, "%s не число" % key)
    check(d.get("minSize") == 32 * 1024, "minSize должен быть 32 КБ")
    check(isinstance(d.get("err"), str), "err не строка")
    print("ota ok: прошивка %s, состояние %s, свободно %d КБ"
          % (d.get("fw"), d.get("stateText"), d.get("maxSize", 0) // 1024))


def main():
    if len(sys.argv) < 2:
        print("использование: check_json.py state|config|export|ota|ident < json")
        return 2
    what = sys.argv[1]
    raw = sys.stdin.read()
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as e:
        print("JSON НЕ РАЗБИРАЕТСЯ (%s): %s" % (what, e))
        print(raw[:400])
        return 1

    {"state": check_state, "config": check_config, "export": check_export,
     "ota": check_ota, "ident": check_ident}[what](data)
    if FAILS:
        for f in FAILS:
            print("  FAIL " + f)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
