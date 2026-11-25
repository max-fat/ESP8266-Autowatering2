/*
 * Прошивка ESP8266 из браузера: Web Serial + esptool-js.
 *
 * Этот файл — исходник, в репозитории лежит собранный docs/js/flasher.js
 * (esbuild бандлит esptool-js и crypto-js в один файл, см. tools/build_flasher.js).
 *
 * Наружу отдаётся window.EspFlasher:
 *   isSupported()            — есть ли Web Serial (Chrome/Edge/Opera, https)
 *   md5(bytes)               — контрольная сумма файла прошивки (hex)
 *   requestPort()            — попросить у браузера порт (нужен жест пользователя!)
 *   flash({data, port, onLog, onProgress, onChip}) — прошить, port опционален
 *
 * Порт лучше запрашивать сразу по нажатию кнопки: браузер даёт доступ к USB
 * только пока «свежо» действие пользователя, а скачивание 300 КБ по медленной
 * сети этот запас съедает. Поэтому страница сначала получает port, потом файл.
 */
import { ESPLoader, Transport } from "esptool-js";
import CryptoJS from "crypto-js";

/* Наша прошивка собрана под 4 МБ flash (раскладка eagle.flash.4m.ld). */
const REQUIRED_FLASH = "4MB";
const REQUIRED_FLASH_BYTES = 4 * 1024 * 1024;
const FLASH_ADDRESS = 0x00000;             // образ содержит eboot + скетч
const FLASH_MODE = "dout";
const FLASH_FREQ = "40m";

const FLASH_SIZES = {
  "256KB": 256 * 1024,
  "512KB": 512 * 1024,
  "1MB": 1024 * 1024,
  "2MB": 2 * 1024 * 1024,
  "4MB": 4 * 1024 * 1024,
  "8MB": 8 * 1024 * 1024,
  "16MB": 16 * 1024 * 1024,
};

function md5(bytes) {
  return CryptoJS.MD5(CryptoJS.lib.WordArray.create(bytes)).toString();
}

function isSupported() {
  return typeof navigator !== "undefined" && !!navigator.serial;
}

function requestPort() {
  if (!isSupported()) {
    throw new Error("браузер не поддерживает Web Serial — нужен Chrome, Edge или Opera");
  }
  return navigator.serial.requestPort();
}

async function flash({ data, port, baudrate = 921600, onLog = () => {}, onProgress = () => {}, onChip = () => {} }) {
  if (!isSupported()) {
    throw new Error("браузер не поддерживает Web Serial — нужен Chrome, Edge или Opera");
  }
  if (!data || !data.length) throw new Error("нет данных прошивки");

  const device = port || (onLog("Запрос доступа к USB-порту…"), await navigator.serial.requestPort());

  const transport = new Transport(device, false);
  const terminal = {
    clean() {},
    writeLine(line) { if (line) onLog(String(line)); },
    write(line) { if (line) onLog(String(line)); },
  };
  const loader = new ESPLoader({ transport, baudrate, terminal });

  let ok = false;
  try {
    onLog("Подключение к загрузчику (если плата не отвечает — зажмите BOOT/FLASH и нажмите кнопку ещё раз)…");
    const chip = await loader.main("default_reset");
    onChip(chip);
    onLog("Определён чип: " + chip);

    // Проверяем размер flash: на 1–2 МБ наша прошивка не запустится.
    let detected = null;
    try {
      detected = await loader.detectFlashSize();
    } catch (e) {
      onLog("Не удалось определить размер flash, продолжаем на 4 МБ.");
    }
    if (detected) {
      const bytes = FLASH_SIZES[detected];
      onLog("Flash: " + detected);
      if (bytes && bytes < REQUIRED_FLASH_BYTES) {
        throw new Error("у платы " + detected + " flash, а прошивке нужно 4 МБ");
      }
    }

    onLog("Стирание и запись " + Math.round(data.length / 1024) + " КБ по адресу 0x00000…");
    await loader.writeFlash({
      fileArray: [{ data, address: FLASH_ADDRESS }],
      flashMode: FLASH_MODE,
      flashFreq: FLASH_FREQ,
      flashSize: REQUIRED_FLASH,
      eraseAll: false,
      compress: true,
      reportProgress: (fileIndex, written, total) => onProgress(written, total),
      calculateMD5Hash: (image) => md5(image),
    });

    onLog("Готово: образ записан и проверен по MD5.");
    ok = true;
    return { chip, flashSize: detected };
  } finally {
    try {
      // После прошивки — обычная загрузка, а не режим загрузчика.
      await loader.after(ok ? "hard_reset" : "no_reset");
    } catch (e) {
      /* плату могли отключить — это уже не важно */
    }
    try {
      await transport.disconnect();
    } catch (e) {
      /* порт мог быть уже закрыт */
    }
  }
}

window.EspFlasher = { isSupported, flash, md5, requestPort };
