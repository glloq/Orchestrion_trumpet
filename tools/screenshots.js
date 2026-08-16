/*
 * screenshots.js — regenerate img/screenshots/*.png from the real interface.
 *
 * The UI falls back to its in-memory mock backend when no device answers (see
 * data/js/api.js), so opening index.html from file:// gives a complete,
 * deterministic demo instrument — an ESP32-S3 running the STANDARD preset with
 * two servos and one solenoid — and every screenshot in the documentation is
 * taken from it. No board is needed to regenerate them.
 *
 * Usage (Playwright is NOT a project dependency — install it wherever you like):
 *
 *   npm i playwright && npx playwright install chromium
 *   node tools/screenshots.js
 *
 * Set CHROMIUM_PATH to use an already-installed Chromium instead.
 *
 * Keep the file names stable: they are referenced from README.md and from
 * docs/WEB_UI.md.
 */
'use strict';

const { chromium } = require('playwright');
const path = require('path');
const fs = require('fs');

const ROOT = path.resolve(__dirname, '..');
const OUT = path.join(ROOT, 'img', 'screenshots');
const URL = 'file://' + path.join(ROOT, 'data', 'index.html');
const WIDTH = 1360;
const SCALE = 1.5;   // crisp text without enormous PNGs

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  fs.mkdirSync(OUT, { recursive: true });

  const browser = await chromium.launch(
    process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {});
  const page = await browser.newPage({
    viewport: { width: WIDTH, height: 900 },
    deviceScaleFactor: SCALE,
    colorScheme: 'light'
  });
  const errors = [];
  page.on('pageerror', (e) => errors.push(String(e)));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });

  await page.goto(URL);
  await page.waitForSelector('.nav-item');
  await sleep(700);

  // `fit` grows the viewport to the view's real height before a full-page shot,
  // so the images carry no band of empty background. Modal shots pass
  // fit:false: the overlay is viewport-sized by design.
  async function shot(name, opts) {
    const fit = !opts || opts.fit !== false;
    await sleep(350);
    if (fit) {
      const height = await page.evaluate(() => {
        const view = document.getElementById('view');
        const box = view && view.getBoundingClientRect();
        return Math.ceil(Math.max(box ? box.bottom + window.scrollY + 24 : 0, 620));
      });
      await page.setViewportSize({ width: WIDTH, height: Math.min(height, 4200) });
      await sleep(250);
    }
    await page.screenshot({ path: path.join(OUT, name + '.png'), fullPage: fit });
    await page.setViewportSize({ width: WIDTH, height: 900 });
    console.log('  ' + name + '.png');
  }

  const view = async (id) => { await page.evaluate((v) => App.navigate(v), id); await sleep(600); };
  const tab = async (label) => {
    await page.click('.modal-tab:text-is("' + label + '")');
    await sleep(700);
  };
  // Opens a disclosure and brings it to the top of the modal body, otherwise
  // the shot shows the section header with its content still below the fold.
  async function unfold(label) {
    const toggle = page.locator('.disclosure-toggle', { hasText: label }).first();
    if (!(await toggle.count())) return;
    if ((await toggle.getAttribute('aria-expanded')) === 'false') {
      await toggle.click();
      await sleep(700);
    }
    await toggle.evaluate((node) => {
      const body = node.closest('.modal-body');
      if (body) body.scrollTop += node.getBoundingClientRect().top - body.getBoundingClientRect().top - 8;
    });
    await sleep(500);
  }

  console.log('Play');
  await view('play');
  // Sound a note so the trumpet, the keyboard and the meters are all alive.
  await page.evaluate(() => WS.noteOn(64, 96, 1));
  await sleep(700);
  await shot('play');

  console.log('Configure');
  await view('configure');
  await shot('configure');
  // The fingering editor is folded away by default; open it for its own shot.
  await page.locator('.disclosure-toggle', { hasText: 'Fingering table' }).first().click();
  await sleep(900);
  await page.locator('.disclosure-toggle', { hasText: 'Fingering table' })
            .first().evaluate((n) => {
              n.scrollIntoView({ block: 'start' });
              window.scrollBy(0, -70);
            });
  await sleep(400);
  await page.screenshot({ path: path.join(OUT, 'configure-fingering.png') });
  console.log('  configure-fingering.png');

  console.log('Sound Lab');
  await view('soundlab');
  await sleep(500);
  await shot('soundlab-quick');
  await page.click('.modal-tab:text-is("Voicing")');
  await sleep(700);
  await shot('soundlab-voicing');
  await page.click('.modal-tab:text-is("Expert")');
  await sleep(700);
  await shot('soundlab-expert');

  console.log('Wiring');
  await view('wiring');
  await shot('wiring');
  // Declare a few protections so the page shows both states.
  await page.evaluate(() => {
    const d = App.config().electrical;
    d.separateSupply = 'yes';
    d.flyback = 'yes';
    d.logicMosfet = 'yes';
    d.estop = 'no';
    App.render();
  });
  await sleep(600);
  await shot('wiring-electrical');

  console.log('Settings');
  await view('play');
  await page.evaluate(() => App.openSettings('Device'));
  await sleep(800);
  await shot('settings-device', { fit: false });
  // The network picker, with the scan results in place.
  await page.click('.readout .btn');
  await sleep(1600);
  await shot('settings-network', { fit: false });

  await tab('MIDI');
  await shot('settings-midi', { fit: false });
  await unfold('Routing matrix');
  await shot('settings-routing', { fit: false });

  await tab('Audio');
  await shot('settings-audio', { fit: false });
  await unfold('Chamber, cone and leadpipe');
  await shot('settings-acoustic', { fit: false });
  // The derived block sits at the bottom of that disclosure.
  await page.evaluate(() => {
    const body = document.querySelector('.modal-body');
    body.scrollTop = body.scrollHeight;
  });
  await sleep(500);
  await shot('settings-acoustic-derived', { fit: false });
  await unfold('I²S / I²C pins and DMA');
  await shot('settings-pins', { fit: false });

  await tab('Pistons');
  await shot('settings-pistons', { fit: false });
  await page.evaluate(() => {
    const body = document.querySelector('.modal-body');
    body.scrollTop = body.scrollHeight;
  });
  await sleep(500);
  await shot('settings-valve-sync', { fit: false });

  await tab('Diagnostics');
  await shot('settings-diagnostics', { fit: false });
  // The MIDI monitor lives at the bottom of the same tab.
  await page.evaluate(() => {
    const body = document.querySelector('.modal-body');
    body.scrollTop = body.scrollHeight;
  });
  await sleep(600);
  await shot('settings-midi-monitor', { fit: false });

  await tab('Firmware');
  await shot('settings-firmware', { fit: false });

  console.log('Wizard');
  await page.evaluate(() => App.closeModal());
  await sleep(300);
  await page.evaluate(() => App.openWizard(0));
  await sleep(800);
  await shot('wizard-board', { fit: false });
  await page.evaluate(() => App.gotoWizardStep(4));
  await sleep(700);
  await shot('wizard-valves', { fit: false });
  await page.evaluate(() => App.gotoWizardStep(6));
  await sleep(900);
  await shot('wizard-validation', { fit: false });
  await page.evaluate(() => App.closeModal());

  console.log('Mobile');
  await page.setViewportSize({ width: 390, height: 844 });
  await view('play');
  await sleep(600);
  await page.screenshot({ path: path.join(OUT, 'mobile-play.png') });
  console.log('  mobile-play.png');

  await browser.close();

  if (errors.length) {
    console.log('\nPage errors:');
    errors.slice(0, 20).forEach((e) => console.log('  ' + e));
    process.exitCode = 1;
  } else {
    console.log('\nNo page error.');
  }
})();
