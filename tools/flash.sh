#!/usr/bin/env bash
#
# Прошивка готового .bin через esptool.
#
#   ./tools/flash.sh                 # COM7, 921600 (по умолчанию)
#   ./tools/flash.sh /dev/ttyUSB0 460800
#
# Скрипт требует установленного esptool:
#   pip install esptool
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${1:-COM7}"
BAUD="${2:-921600}"
BIN="${3:-$ROOT/firmware/autowatering-esp8266.bin}"

if [ ! -f "$BIN" ]; then
  echo "Не найден файл прошивки: $BIN" >&2
  exit 1
fi

# esptool v4 (esptool.py) и v5 (esptool) используют разный синтаксис команд.
ESPTOOL=""
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL="esptool.py"
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL="esptool"
elif python3 -c "import esptool" >/dev/null 2>&1; then
  ESPTOOL="python3 -m esptool"
else
  echo "esptool не найден. Установите: pip install esptool" >&2
  exit 1
fi

echo "==> $ESPTOOL на $PORT @ $BAUD"
$ESPTOOL --chip esp8266 --port "$PORT" --baud "$BAUD" \
  write_flash --flash_mode dout --flash_freq 40m --flash_size 4MB \
  0x00000 "$BIN"

echo "==> готово. Лог: 115200 бод, команда: pio device monitor  (или screen/minicom)"
