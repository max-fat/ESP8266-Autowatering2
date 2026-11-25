#!/usr/bin/env node
/*
 * Сборка прошивальщика для страницы проекта: tools/flasher/src/flasher.js
 * (esptool-js + crypto-js) -> docs/js/flasher.js (один минифицированный файл).
 *
 * Зачем бандл в репозитории: страница — статический HTML на GitHub Pages, без
 * сборки на стороне сервера и без внешних CDN.
 *
 * Зависимости (одноразово):
 *   npm i esptool-js crypto-js esbuild
 *   node tools/build_flasher.js
 */
const { build } = require("esbuild");
const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..");
const OUT = path.join(ROOT, "docs", "js", "flasher.js");

function pkg(name) {
  try {
    return require(`${name}/package.json`);
  } catch (e) {
    return null;
  }
}

(async () => {
  const esptool = pkg("esptool-js");
  const crypto = pkg("crypto-js");
  const banner =
    "/* Прошивальщик ESP8266 для страницы проекта. Собран tools/build_flasher.js из tools/flasher/src/flasher.js.\n" +
    ` * esptool-js${esptool ? " " + esptool.version : ""} (Apache-2.0), crypto-js${crypto ? "" : ""} (MIT). Не редактировать вручную. */`;

  fs.mkdirSync(path.dirname(OUT), { recursive: true });
  await build({
    entryPoints: [path.join(ROOT, "tools", "flasher", "src", "flasher.js")],
    outfile: OUT,
    bundle: true,
    minify: true,
    format: "iife",
    target: "es2019",
    banner: { js: banner },
    legalComments: "none",
    // Зависимости могут лежать не в репозитории, а в отдельной папке:
    //   NODE_PATH=/path/to/node_modules node tools/build_flasher.js
    nodePaths: (process.env.NODE_PATH || "").split(path.delimiter).filter(Boolean),
  });

  const size = fs.statSync(OUT).size;
  console.log(`docs/js/flasher.js: ${(size / 1024).toFixed(0)} КБ`);
})().catch((e) => {
  console.error("не удалось собрать прошивальщик: " + e.message);
  process.exit(1);
});
