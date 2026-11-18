@echo off
rem Прошивка контроллера автополива (Windows, COM7, 921600).
rem Требуется Python + esptool:  pip install esptool
rem Запуск: flash.bat [COM-порт] [скорость]

setlocal
set PORT=%1
if "%PORT%"=="" set PORT=COM7
set BAUD=%2
if "%BAUD%"=="" set BAUD=921600
set BIN=%~dp0..\firmware\autowatering-esp8266.bin

echo ==^> %PORT% @ %BAUD%
esptool --chip esp8266 --port %PORT% --baud %BAUD% write_flash --flash_mode dout --flash_freq 40m --flash_size 4MB 0x00000 "%BIN%"
if errorlevel 1 (
  echo.
  echo Ошибка прошивки. Проверьте порт, драйвер CH340/CP210x и что монитор порта закрыт.
  pause
  exit /b 1
)
echo.
echo Готово. Лог: 115200 бод.
pause
