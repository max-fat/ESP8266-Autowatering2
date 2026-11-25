# Хост-тесты логики прошивки (без железа и без PlatformIO).
#
#   make        - собрать и запустить
#   make clean

CXX      ?= g++
CXXFLAGS := -std=c++11 -O1 -g -Wall -Wextra -Wno-unused-parameter -I.
HDR      := $(wildcard ../../src/*.h) Arduino.h EEPROM.h
SRC      := test_logic.cpp ../../src/storage.cpp ../../src/channels.cpp ../../src/scheduler.cpp ../../src/clock.cpp ../../src/scanlist.cpp ../../src/text.cpp ../../src/transfer.cpp ../../src/webjson.cpp ../../src/otacheck.cpp

# Заголовки тоже в зависимостях: иначе смена FW_VERSION или структуры в
# app_config.h не пересобирает тест, и он проверяет старую прошивку.
test_logic: $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

.PHONY: run clean
run: test_logic
	./test_logic
	@./test_logic --dump-state  | python3 check_json.py state
	@./test_logic --dump-config | python3 check_json.py config
	@./test_logic --dump-export | python3 check_json.py export
	@./test_logic --dump-ota    | python3 check_json.py ota

clean:
	rm -f test_logic
