# Хост-тесты логики прошивки (без железа и без PlatformIO).
#
#   make        - собрать и запустить
#   make clean

CXX      ?= g++
CXXFLAGS := -std=c++11 -O1 -g -Wall -Wextra -Wno-unused-parameter -I.
SRC      := test_logic.cpp ../../src/storage.cpp ../../src/channels.cpp ../../src/scheduler.cpp

test_logic: $(SRC) Arduino.h EEPROM.h
	$(CXX) $(CXXFLAGS) $(SRC) -o $@

.PHONY: run clean
run: test_logic
	./test_logic

clean:
	rm -f test_logic
