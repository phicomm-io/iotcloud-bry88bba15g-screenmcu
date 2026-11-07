/* Run against tools/preview.py; reuse the shared cached Playwright package. */
const assert = require('node:assert/strict');
const {chromium} = require(process.env.PLAYWRIGHT_PACKAGE || 'playwright');
(async () => {
  const browser = await chromium.launch({headless: true});
  try {
    const page = await browser.newPage({viewport: {width: 700, height: 1240}});
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto('http://127.0.0.1:8874');
    const ready = (n, demo) => page.waitForFunction(({n, demo}) => {
      const image = document.querySelector('#screen');
      const url = new URL(image.src);
      return image.complete && image.naturalWidth === 480 &&
        url.searchParams.get('page') === String(n) &&
        url.searchParams.get('demo') === String(demo);
    }, {n, demo});
    await ready(0, 0);
    await page.selectOption('#mode', '1'); await ready(0, 1);
    for (let n = 1; n < 5; n++) {
      await page.click('#next'); await ready(n, 1);
    }
    await page.click('#next'); await ready(0, 1);
    await page.click('#screen'); await ready(1, 1);
    await page.dblclick('#screen'); await ready(0, 1);
    await page.keyboard.press('ArrowLeft'); await ready(4, 1);
    await page.keyboard.press('Home'); await ready(0, 1);
    for (let kind = 0; kind < 5; kind++) {
      await page.selectOption('#widget', String(kind)); await ready(3, 1);
      await page.waitForFunction(kind => new URL(document.querySelector('#screen').src).searchParams.get('widget') === String(kind), kind);
    }
    await page.screenshot({path: 'artifacts/browser-preview.png', fullPage: true});
    await page.selectOption('#mode', '0'); await ready(3, 0);
    assert.deepEqual(errors, []);
    console.log('Browser: five pages, click/double click, keyboard, source states and five widget types passed');
  } finally { await browser.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
