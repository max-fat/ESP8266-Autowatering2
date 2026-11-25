#!/usr/bin/env node
/*
 * Скриншоты интерфейса для страницы проекта (docs/index.html).
 *
 * Берётся настоящий web/index.html, подменяется только сеть (fetch), поэтому
 * на картинках ровно то, что видит пользователь — без «нарисованных» макетов.
 * Пересобрать после правок интерфейса:
 *
 *   npm i puppeteer-core @sparticuz/chromium     # одноразово, ~80 МБ
 *   node tools/screenshots.js
 *
 * Если в системе есть свой Chrome/Chromium, его путь можно указать в CHROME:
 *   CHROME=/usr/bin/chromium node tools/screenshots.js
 *
 * Готовые файлы кладутся в docs/img/*.png (viewport 400x860, deviceScaleFactor 2).
 */
const fs = require('fs');
const path = require('path');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'docs', 'img');
const PAGE = fs.readFileSync(path.join(ROOT, 'web', 'index.html'), 'utf8');

/* ---------- данные для макетов ---------- */
function stationState(over) {
  return Object.assign({
    fw: '1.6.1', appName: 'ESP8266-Autowatering2', name: 'Теплица',
    now: 1795000000, time: '08:12:40', date: '24.11.2026', timeValid: 1, timeSrc: 'ntp', tz: 180,
    wifi: {
      mode: 'sta', configured: 1, connected: 1, ip: '192.168.1.50', rssi: -58,
      apActive: 0, apSsid: 'AutoWater-00A1B2', apIp: '192.168.4.1', ssid: 'HomeNet'
    },
    sensor: { low: 0, raw: 0, pin: 13 },
    limits: { channels: 4, maxRun: 600, maxTasks: 8 },
    ch: [
      { i: 0, pin: 4,  inv: 0, on: 0, rem: 0,  task: 0,    seq: 1, rsn: 'timer', run: 30,  nm: 'Орхидеи' },
      { i: 1, pin: 5,  inv: 0, on: 1, rem: 42, task: 1,    seq: 2, rsn: 'timer', run: 120, nm: 'Томаты' },
      { i: 2, pin: 12, inv: 0, on: 0, rem: 0,  task: 2,    seq: 1, rsn: 'timer', run: 600, nm: 'Огурцы' },
      { i: 3, pin: 14, inv: 0, on: 0, rem: 0,  task: -1,   seq: 0, rsn: '',      run: 0,   nm: 'Газон' }
    ],
    tasks: [
      { i: 0, on: 1, ch: 0, h: 8,  m: 0,  dur: 30,  pd: 1, lastStr: '24.11 08:00', nextStr: '25.11 08:00', st: 'wait' },
      { i: 1, on: 1, ch: 1, h: 8,  m: 12, dur: 120, pd: 1, lastStr: '24.11 08:12', nextStr: '25.11 08:12', st: 'running' },
      { i: 2, on: 1, ch: 2, h: 19, m: 30, dur: 600, pd: 2, lastStr: '23.11 19:30', nextStr: '25.11 19:30', st: 'wait' },
      { i: 3, on: 0, ch: 3, h: 6,  m: 15, dur: 300, pd: 1, lastStr: '',            nextStr: '',            st: 'off' }
    ],
    heap: 28744, uptime: 11540
  }, over || {});
}

function stationConfig(over) {
  return Object.assign({
    name: 'Теплица', ssid: 'HomeNet', ntp: 'pool.ntp.org', tz: 180,
    sensorPin: 13, sensorInv: 1, sensorPull: 1, blockLow: 1, maxRun: 600, catchup: 6,
    ch: [
      { pin: 4,  inv: 0, name: 'Орхидеи' },
      { pin: 5,  inv: 0, name: 'Томаты' },
      { pin: 12, inv: 0, name: 'Огурцы' },
      { pin: 14, inv: 0, name: 'Газон' }
    ]
  }, over || {});
}

const AP_STATE = stationState({
  name: '', timeSrc: 'none', time: '--:--:--', date: 'нет времени', timeValid: 0,
  wifi: {
    mode: 'ap', configured: 0, connected: 0, ip: '—', rssi: 0,
    apActive: 1, apSsid: 'AutoWater-00A1B2', apIp: '192.168.4.1', ssid: ''
  }
});

const SCAN = {
  state: 'done', count: 4,
  nets: [
    { ssid: 'HomeNet', rssi: -52, enc: 1 },
    { ssid: 'TP-LINK_2G', rssi: -71, enc: 1 },
    { ssid: 'MTS_Router', rssi: -80, enc: 1 },
    { ssid: 'Guest', rssi: -88, enc: 0 }
  ]
};

/* ---------- страница с подменённой сетью ---------- */
function buildPage(state, cfg, extra) {
  const mock = `
  <script>
  (function(){
    window.confirm = () => true;
    const STATE = ${JSON.stringify(state)};
    const CFG = ${JSON.stringify(cfg)};
    const SCAN = ${JSON.stringify(SCAN)};
    window.fetch = async function(u){
      u = String(u);
      if (u.indexOf('/api/state') === 0)  return { ok: true, json: async () => STATE };
      if (u.indexOf('/api/config') === 0) return { ok: true, json: async () => CFG };
      if (u.indexOf('/api/scan') === 0)   return { ok: true, json: async () => SCAN };
      if (u.indexOf('/api/ota') === 0)    return { ok: true, json: async () => ({
        fw: STATE.fw, state: 0, stateText: 'idle', received: 0, total: 0,
        maxSize: 724992, minSize: 32768, freeHeap: STATE.heap, err: '' }) };
      return { ok: true, json: async () => ({ ok: true }) };
    };
    ${extra || ''}
  })();
  </script>`;
  return PAGE.replace('<body>', '<body>\n' + mock);
}

/* ---------- запуск браузера ---------- */
async function launch() {
  const puppeteer = require('puppeteer-core');
  const opts = { headless: true, args: ['--no-sandbox', '--disable-gpu', '--force-color-profile=srgb', '--hide-scrollbars'] };
  if (process.env.CHROME) {
    opts.executablePath = process.env.CHROME;
  } else {
    const chromium = require('@sparticuz/chromium').default;
    opts.executablePath = await chromium.executablePath();
    opts.args.push(...chromium.args);
    // В комплекте с пакетом идут NSS/NSPR-библиотеки — без них chromium не стартует.
    opts.env = { ...process.env, LD_LIBRARY_PATH: process.env.LD_LIBRARY_PATH || '' };
  }
  return puppeteer.launch(opts);
}

/* ---------- сценарии ---------- */
const SHOTS = [
  {
    file: 'tasks.png', tab: 'tasks',
    title: 'Задачи: время, длительность, период'
  },
  {
    file: 'channels.png', tab: 'channels',
    title: 'Каналы: ручной пуск и остановка'
  },
  {
    file: 'settings.png', tab: 'settings',
    title: 'Настройки: модуль, Wi-Fi, помпы'
  },
  {
    file: 'ota.png', tab: 'settings', scrollTo: '#otaFile', upload: true,
    title: 'Обновление прошивки по Wi-Fi'
  },
  {
    file: 'ap.png', tab: 'settings', scrollTo: '#s_ssid', action: 'scan',
    state: AP_STATE, cfg: stationConfig({ name: '', ssid: '' }),
    title: 'Первый запуск: точка доступа и выбор сети'
  },
  {
    file: 'ota-card.png', tab: 'settings', upload: true, element: '#otaFile',
    title: 'Карточка обновления (крупным планом)'
  },
  {
    file: 'hero.png', tab: 'tasks', wide: true,
    title: 'Общий вид (десктоп)'
  }
];

(async () => {
  fs.mkdirSync(OUT, { recursive: true });
  const browser = await launch();

  for (const shot of SHOTS) {
    // Своя страница на каждый кадр: setContent в одной вкладке переиспользует
    // глобальную область, и интерфейс падает на повторном объявлении let.
    const page = await browser.newPage();
    const state = shot.state || stationState();
    const cfg = shot.cfg || stationConfig();
    await page.setViewport(shot.wide
      ? { width: 1000, height: 900, deviceScaleFactor: 2 }
      : { width: 400, height: 700, deviceScaleFactor: 2 });

    await page.setContent(buildPage(state, cfg), { waitUntil: 'domcontentloaded' });
    // Ждём, пока интерфейс отрисует данные (карточки каналов и задачи).
    await page.waitForFunction(() =>
      document.querySelectorAll('#chGrid .ch').length > 0 &&
      document.querySelectorAll('#tasksList .task').length > 0);
    await page.evaluate(() => document.fonts && document.fonts.ready);
    await new Promise(r => setTimeout(r, 250));
    await page.evaluate(tab => window.selectTab(tab, false), shot.tab);

    if (shot.upload) {
      // Показываем карточку так, как её видит человек, уже выбравший файл.
      const tmp = path.join(require('os').tmpdir(), 'autowatering-esp8266.bin');
      fs.writeFileSync(tmp, Buffer.alloc(324 * 1024, 0xE9));
      const input = await page.$('#otaFile');
      await input.uploadFile(tmp);
      await page.evaluate(() => document.getElementById('otaFile')
        .dispatchEvent(new Event('change', { bubbles: true })));
      await new Promise(r => setTimeout(r, 120));
    }

    if (shot.action === 'scan') {
      await page.click('#btnScan');
      await page.waitForFunction(() => document.querySelectorAll('#scanList .scanrow').length > 1);
    }

    if (shot.scrollTo) {
      await page.evaluate(sel => {
        const el = document.querySelector(sel);
        if (el) el.scrollIntoView({ block: 'center' });
      }, shot.scrollTo);
      await new Promise(r => setTimeout(r, 150));
    }

    if (shot.element) {
      // Кадр одного блока: поднимаемся по DOM до ближайшей .card.
      const handle = await page.evaluateHandle(sel => {
        const el = document.querySelector(sel);
        return el ? el.closest('.card') || el : null;
      }, shot.element);
      const el = handle.asElement();
      if (el) {
        await el.screenshot({ path: path.join(OUT, shot.file) });
      } else {
        await page.screenshot({ path: path.join(OUT, shot.file) });
      }
    } else {
      await page.screenshot({ path: path.join(OUT, shot.file) });
    }
    console.log('  ' + shot.file + '  — ' + shot.title);
    await page.close();
  }

  await browser.close();
  console.log('готово: ' + OUT);
})().catch(e => {
  console.error('не удалось снять скриншоты: ' + e.message);
  process.exit(1);
});
