#!/usr/bin/env bash
#
# Сборка прошивки без PlatformIO / Arduino IDE.
#
# Используется, когда нужно собрать .bin в "чистом" окружении:
#   * кросс-компилятор xtensa-lx106-elf-g++ (GCC 4.8.2, с поддержкой C++),
#   * исходники Arduino-ядра esp8266 версии 2.4.2,
#   * python3 для упаковки eboot + sketch в один .bin.
#
# Переменные окружения:
#   TOOLCHAIN_DIR  - каталог с bin/xtensa-lx106-elf-g++      (обязательно)
#   CORE_DIR       - каталог Arduino-ядра esp8266 (2.4.2)     (обязательно)
#   OUT_DIR        - куда положить результат (по умолчанию firmware)
#   BUILD_DIR      - временный каталог сборки (по умолчанию build)
#
# Пример:
#   TOOLCHAIN_DIR=$HOME/tools/xtensa-lx106-elf \
#   CORE_DIR=$HOME/tools/Arduino-2.4.2 ./tools/build_firmware.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC_DIR="$ROOT/src"
OUT_DIR="${OUT_DIR:-$ROOT/firmware}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

: "${TOOLCHAIN_DIR:?Укажите TOOLCHAIN_DIR (каталог с xtensa-lx106-elf-g++)}"
: "${CORE_DIR:?Укажите CORE_DIR (каталог Arduino-ядра esp8266 2.4.2)}"

CXX="$TOOLCHAIN_DIR/bin/xtensa-lx106-elf-g++"
CC="$TOOLCHAIN_DIR/bin/xtensa-lx106-elf-gcc"
AR="$TOOLCHAIN_DIR/bin/xtensa-lx106-elf-ar"
SIZE="$TOOLCHAIN_DIR/bin/xtensa-lx106-elf-size"

for f in "$CXX" "$CC" "$AR"; do
  [ -x "$f" ] || { echo "Не найден $f" >&2; exit 1; }
done

LIBRARIES="ESP8266WiFi ESP8266WebServer DNSServer EEPROM"

FLASH_MODE="${FLASH_MODE:-dout}"
FLASH_FREQ="${FLASH_FREQ:-40}"
FLASH_SIZE="${FLASH_SIZE:-4M}"
FLASH_LD="${FLASH_LD:-eagle.flash.4m.ld}"

OBJ="$BUILD_DIR/obj"
APP="$BUILD_DIR/app"
rm -rf "$OBJ" "$APP"
mkdir -p "$OBJ" "$APP" "$OUT_DIR"

# --- 1. Генерируем общий linker script (как это делает Arduino при сборке) ---
"$CC" -CC -E -P -DVTABLES_IN_FLASH \
  "$CORE_DIR/tools/sdk/ld/eagle.app.v6.common.ld.h" \
  -o "$CORE_DIR/tools/sdk/ld/eagle.app.v6.common.ld"

# Ядро 3.x резервирует в начале .irom0.text 8 байт под __crc_len/__crc_val -
# именно туда tools/elf2bin.py пишет контрольную сумму образа. В ядре 2.4.2
# такого резерва нет, поэтому добавляем его сами (иначе были бы испорчены
# первые 8 байт кода).
python3 - "$CORE_DIR/tools/sdk/ld/eagle.app.v6.common.ld" <<'PYEOF'
import sys

path = sys.argv[1]
text = open(path).read()
anchor = "  .irom0.text : ALIGN(4)\n  {\n    _irom0_text_start = ABSOLUTE(.);\n"
insert = ("    /* 8 байт под контрольную сумму образа (заполняет elf2bin.py) */\n"
          "    __crc_len = ABSOLUTE(.);\n"
          "    LONG(0x00000000);\n"
          "    __crc_val = ABSOLUTE(.);\n"
          "    LONG(0x00000000);\n")
if "__crc_len" not in text:
    if anchor not in text:
        sys.exit("build_firmware.sh: не найден раздел .irom0.text в linker script")
    text = text.replace(anchor, anchor + insert, 1)
    open(path, "w").write(text)
    print("   linker script: зарезервированы 8 байт под CRC образа")
PYEOF

COMMON_DEFS=(
  -D__ets__ -DICACHE_FLASH -U__STRICT_ANSI__
  -DF_CPU=80000000L -DLWIP_OPEN_SRC
  -DARDUINO=10805 -DARDUINO_ESP8266_GENERIC -DARDUINO_ARCH_ESP8266
  -DARDUINO_BOARD='"ESP8266_GENERIC"'
  -DESP8266 -DF_CRYSTAL=26000000 -DVTABLES_IN_FLASH
)

INCLUDES=(
  -I"$CORE_DIR/cores/esp8266"
  -I"$CORE_DIR/variants/generic"
  -I"$CORE_DIR/tools/sdk/include"
  -I"$CORE_DIR/tools/sdk/lwip/include"
  -I"$CORE_DIR/tools/sdk/libc/xtensa-lx106-elf/include"
)
for lib in $LIBRARIES; do
  INCLUDES+=( -I"$CORE_DIR/libraries/$lib" -I"$CORE_DIR/libraries/$lib/src"
              -I"$CORE_DIR/libraries/$lib/src/include" -I"$CORE_DIR/libraries/$lib/include" )
done

CFLAGS_BASE=(-Os -g -w -mlongcalls -mtext-section-literals -falign-functions=4
             -ffunction-sections -fdata-sections)
CFLAGS_C=("${CFLAGS_BASE[@]}" -Wpointer-arith -Wno-implicit-function-declaration -std=gnu99)
CFLAGS_CXX=("${CFLAGS_BASE[@]}" -fno-exceptions -fno-rtti -std=c++11)

# Имена объектников сохраняют оригинальное расширение (foo.cpp.o) - от этого
# зависит раскладка секций в linker script (код C/C++ уезжает во flash).
compile_dir() {
  local dir="$1" out="$2"
  mkdir -p "$out"
  ( cd "$dir"
    while IFS= read -r f; do
      local rel="${f#./}"
      local obj="$out/$(echo "$rel" | tr '/' '_').o"
      case "$f" in
        *.c)   "$CC"  "${COMMON_DEFS[@]}" "${CFLAGS_C[@]}"   "${INCLUDES[@]}" -c "$f" -o "$obj" -MMD -MF "${obj%.o}.d" ;;
        *.cpp) "$CXX" "${COMMON_DEFS[@]}" "${CFLAGS_CXX[@]}" "${INCLUDES[@]}" -c "$f" -o "$obj" -MMD -MF "${obj%.o}.d" ;;
        *.S)   "$CC"  "${COMMON_DEFS[@]}" -c -g -x assembler-with-cpp -mlongcalls "${INCLUDES[@]}" "$f" -o "$obj" ;;
      esac
    done < <(find . \( -name '*.c' -o -name '*.cpp' -o -name '*.S' \) -not -path '*/examples/*' | sort)
  )
}

echo "==> ядро Arduino"
compile_dir "$CORE_DIR/cores/esp8266" "$OBJ/core"

echo "==> библиотеки: $LIBRARIES"
for lib in $LIBRARIES; do
  compile_dir "$CORE_DIR/libraries/$lib" "$OBJ/lib_$lib"
done

echo "==> исходники проекта"
compile_dir "$SRC_DIR" "$OBJ/app"

echo "==> архив core+libs"
rm -f "$APP/arduino.a"
"$AR" cru "$APP/arduino.a" "$OBJ"/core/*.o "$OBJ"/lib_*/*.o

echo "==> линковка"
"$CXX" -g -Os -nostdlib -Wl,--no-check-sections -u app_entry -Wl,-static \
  -L"$CORE_DIR/tools/sdk/lib" -L"$CORE_DIR/tools/sdk/ld" \
  -L"$CORE_DIR/tools/sdk/libc/xtensa-lx106-elf/lib" \
  -T"$CORE_DIR/tools/sdk/ld/$FLASH_LD" -Wl,--gc-sections \
  -Wl,-wrap,system_restart_local -Wl,-wrap,spi_flash_read \
  -Wl,-Map -Wl,"$APP/app.map" -o "$APP/app.elf" \
  -Wl,--start-group "$OBJ"/app/*.o "$APP/arduino.a" \
  -lhal -lphy -lpp -lnet80211 -llwip_gcc -lwpa -lcrypto -lmain -lwps -lbearssl -laxtls \
  -lespnow -lsmartconfig -lairkiss -lwpa2 -lstdc++ -lm -lc -lgcc -Wl,--end-group -L"$APP"

"$SIZE" -A "$APP/app.elf" | grep -E "^\.(text|data|rodata|bss|irom0)" || true

echo "==> упаковка в .bin"
python3 "$ROOT/tools/elf2bin.py" \
  --eboot "$CORE_DIR/bootloaders/eboot/eboot.elf" \
  --app "$APP/app.elf" \
  --flash_mode "$FLASH_MODE" --flash_freq "$FLASH_FREQ" --flash_size "$FLASH_SIZE" \
  --path "$TOOLCHAIN_DIR/bin" \
  --out "$OUT_DIR/autowatering-esp8266.bin"

BIN="$OUT_DIR/autowatering-esp8266.bin"
ls -l "$BIN"

# Краткая сводка о сборке - удобно рядом с .bin
{
  echo "firmware : $(basename "$BIN")"
  echo "built    : $(date -u '+%Y-%m-%d %H:%M:%S UTC')"
  echo "version  : $(grep -oP '(?<=FW_VERSION  ")[^"]+' "$SRC_DIR/app_config.h" || echo unknown)"
  echo "size     : $(stat -c%s "$BIN") bytes"
  echo "md5      : $(md5sum "$BIN" | cut -d' ' -f1)"
  echo "flash    : mode=$FLASH_MODE freq=${FLASH_FREQ}MHz size=$FLASH_SIZE ld=$FLASH_LD"
  echo "toolchain: $("$CXX" -dumpversion 2>/dev/null) (xtensa-lx106-elf-g++)"
  echo "core     : ${CORE_DIR##*/}"
} > "$OUT_DIR/build-info.txt"
cat "$OUT_DIR/build-info.txt"
echo "Готово."
