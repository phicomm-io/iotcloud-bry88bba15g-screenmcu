/* Standalone prototype acceptance. Reuses Playwright from the shared environment.
 * PLAYWRIGHT_PACKAGE=/path/to/playwright node tools/ui_preview_browser.cjs
 * PREVIEW_URL defaults to the nginx subdirectory, not a development root URL.
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { chromium } = require(process.env.PLAYWRIGHT_PACKAGE || 'playwright');
const ROOT = path.resolve(__dirname, '..');
const OUTPUT = path.join(ROOT, 'artifacts/ui-preview');
const URL = process.env.PREVIEW_URL || 'http://127.0.0.1/dakin_lvgl/';
const checks = [];
const check = (name, detail = {}) => { checks.push({ name, ...detail }); console.log(`PASS ${name}`); };

async function main() {
  fs.mkdirSync(OUTPUT, { recursive: true });
  const browser = await chromium.launch({ headless: true });
  const errors = [], failedRequests = [], mutations = [], externalRequests = [];
  try {
    const context = await browser.newContext({ viewport: { width: 1360, height: 1060 }, timezoneId: 'America/Los_Angeles', locale: 'zh-CN' });
    const page = await context.newPage();
    page.on('pageerror', error => errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error') errors.push(message.text()); });
    page.on('response', response => { if (response.status() >= 400) failedRequests.push({ url: response.url(), status: response.status() }); });
    page.on('request', request => {
      if (request.method() !== 'GET') mutations.push(request.url());
      if (!request.url().startsWith(URL) && !request.url().startsWith('data:')) externalRequests.push(request.url());
    });
    await page.goto(URL);
    await page.waitForFunction(() => window.previewSnapshot);
    const snapshot = () => page.evaluate(() => window.previewSnapshot);
    const screen = page.locator('#device-screen');
    const selectPage = id => page.locator(`[data-page-button="${id}"]`).click();
    const shot = name => page.screenshot({ path: path.join(OUTPUT, `${name}.png`), fullPage: true });
    const screenShot = name => screen.screenshot({ path: path.join(OUTPUT, `${name}.png`) });
    async function layout(label) {
      const failures = await page.evaluate(() => {
        const failures = [];
        if (document.documentElement.scrollWidth > innerWidth + 1) failures.push('horizontal document overflow');
        const screen = document.querySelector('#device-screen'), bounds = screen.getBoundingClientRect(), scale = bounds.width / 480;
        const rail = screen.querySelector('.screen-page-rail'), segments = [...rail.children], bar = rail.getBoundingClientRect();
        const snap = window.previewSnapshot;
        if (Math.abs(bar.top - bounds.top) > 1 || Math.abs(bar.left - bounds.left) > 1 || Math.abs(bar.width / scale - 480) > 1 || Math.abs(bar.height / scale - (snap.appearance.pageRailHeight??12)) > 1) failures.push('page rail does not cover the top edge');
        if (segments.length !== snap.pages.length || rail.querySelectorAll('.active').length !== 1 || rail.querySelector('.active')?.dataset.pageSegment !== snap.display.page) failures.push('page rail count or active page');
        segments.forEach((element, index) => {
          const rect = element.getBoundingClientRect(), expected = element.classList.contains('active') ? .9 : .2;
          if (Math.abs(rect.width / scale - 480 / segments.length) > 1 || Math.abs(rect.left - (index ? segments[index - 1].getBoundingClientRect().right : bounds.left)) > 1) failures.push('unequal or separated page segments');
          if (Number(getComputedStyle(element).opacity) !== expected) failures.push('page rail opacity');
        });
        if (screen.querySelector('.screen-dots')) failures.push('obsolete bottom pager');
        for (const element of document.querySelectorAll('.screen-page > *')) {
          const rect = element.getBoundingClientRect();
          if ((rect.bottom - bounds.top) / scale > 838.5) failures.push(`${element.className} bottom=${((rect.bottom - bounds.top) / scale).toFixed(1)}`);
          if (rect.right > bounds.right + 1 || rect.left < bounds.left - 1) failures.push(`${element.className} horizontal overflow`);
        }
        for (const element of document.querySelectorAll('.metric-value, .metric-unit, .chart-number, .chart-strip-value, .legend-row, .widget-time')) {
          if (element.scrollWidth > element.clientWidth + 1) failures.push(`${element.className}: text overflow`);
        }
        for (const widget of document.querySelectorAll('.ui-widget')) {
          if (widget.scrollHeight > widget.clientHeight + 1) failures.push(`${widget.className}: vertical content overflow`);
          for (const element of widget.querySelectorAll('.metric-value, .metric-heading, .metric-unit, .chart-strip-meta, .chart-strip-value, .widget-time, .widget-date, .legend-row, .chart-svg, .weather-days, .reminder-list, .widget-condition, .widget-reminder-title')) {
            const rect = element.getBoundingClientRect(), outer = widget.getBoundingClientRect();
            if (rect.bottom > outer.bottom + 1 || rect.top < outer.top - 1 || rect.left < outer.left - 1 || rect.right > outer.right + 1) failures.push(`${element.className.baseVal || element.className}: clipped by widget`);
          }
        }
        for (const svg of document.querySelectorAll('svg.chart-svg')) for (const text of svg.querySelectorAll('text')) {
          const box = text.getBBox();
          if (box.x < -1 || box.x + box.width > svg.viewBox.baseVal.width + 1) failures.push(`chart label ${text.textContent} outside viewBox`);
        }
        return failures;
      });
      if (failures.length) await shot(`failure-${label}`);
      assert.deepEqual(failures, [], label);
    }
    await shot('desktop-clock');
    assert.equal((await snapshot()).display.page, 'clock');
    assert.equal(await page.locator('.widget-time').textContent(), '18:42');
    assert.equal(await page.locator('.widget-time').evaluate(e => parseFloat(getComputedStyle(e).fontSize)), 144);
    await layout('desktop-initial');
    check('static nginx subdirectory loads with correct device wall time in a different host timezone');
    const geometry = await page.locator('.screen-widget-grid').evaluate(element => {
      const rect = element.getBoundingClientRect(), screen = document.querySelector('#device-screen').getBoundingClientRect(), scale = screen.width / 480;
      return [rect.left - screen.left, rect.top - screen.top, rect.width, rect.height].map(n => Math.round(n / scale));
    });
    assert.deepEqual(geometry, [16, 66, 448, 772]);
    check('LVGL-aligned 16px margins, 8px grid gaps and full-width selectable translucent top page rail');

    await page.click('#tab-display');
    for (const height of [6, 12, 20]) {
      await page.selectOption('#page-rail-height', String(height));
      assert.equal((await snapshot()).appearance.pageRailHeight, height);
      await layout(`page-rail-${height}`);
      await page.reload(); await page.waitForFunction(() => window.previewSnapshot);
      assert.equal((await snapshot()).appearance.pageRailHeight, height);
      await layout(`page-rail-${height}-reloaded`);
      await screenShot(`page-rail-${height}`);
    }
    await page.click('#tab-display');
    await page.selectOption('#page-rail-height', '12');
    check('all three page rail heights render and survive a saved reload');

    for (const state of ['normal', 'missing', 'stale', 'extreme']) {
      await page.selectOption('#data-state', state);
      for (const id of ['clock', 'air', 'trend', 'weather', 'reminders', 'external']) {
        await selectPage(id); await layout(`${state}-${id}`);
        if (state === 'normal') await screenShot(`screen-${id}`);
      }
    }
    await page.selectOption('#data-state', 'normal');
    for (const kind of ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut']) {
      await page.selectOption('#chart-type', kind);
      for (const range of ['1', '6', '24']) {
        await page.selectOption('#chart-range', range);
        for (const state of ['normal', 'missing', 'stale', 'extreme']) {
          await page.selectOption('#data-state', state); await layout(`chart-${kind}-${range}-${state}`);
        }
      }
      await page.selectOption('#data-state', 'normal');
      await screenShot(`chart-${kind}`);
    }
    await shot('desktop-stacked-area');
    check('six pages across four data states, seven chart types across three ranges and four states fit the screen');

    for (const kind of ['value', 'text', 'chart']) {
      await page.selectOption('#external-type', kind);
      await page.selectOption('#data-state', 'extreme'); await layout(`external-${kind}-long`);
    }
    await page.selectOption('#data-state', 'normal');
    await page.selectOption('#external-type', 'value');
    await selectPage('clock'); await screen.click();
    await page.waitForFunction(() => window.previewSnapshot.display.page === 'air');
    await screen.dblclick();
    await page.waitForFunction(() => window.previewSnapshot.display.page === 'clock');
    await screen.focus(); await page.keyboard.press('ArrowLeft');
    assert.equal((await snapshot()).display.page, 'external');
    await page.keyboard.press('Home'); assert.equal((await snapshot()).display.page, 'clock');
    check('external components, single click, double click, keyboard and home');

    await page.click('[data-size="physical"]');
    const before = await screen.boundingBox();
    assert.ok(Math.abs(before.width - 35 * 96 / 25.4) < 1);
    assert.ok(Math.abs(before.width / before.height - 480 / 854) < .001);
    await page.locator('#calibration-scale').fill('130');
    const after = await screen.boundingBox(); assert.ok(Math.abs(after.width / before.width - 1.3) < .01);
    await shot('desktop-physical-size');
    await page.click('[data-size="large"]');
    check('physical reference, calibration and aspect ratio');

    await selectPage('weather');
    await page.click('#tab-sleep');
    await page.click('#advance-half-hour');
    let snap = await snapshot(); assert.equal(snap.display.sleeping, true); assert.equal(snap.display.page, 'clock');
    await page.click('#simulate-touch');
    snap = await snapshot(); assert.equal(snap.display.sleeping, false); assert.equal(snap.page, 'weather');
    await page.click('#simulate-touch'); assert.equal((await snapshot()).page, 'reminders');
    await page.selectOption('#power-source', 'battery'); await page.click('#advance-minute');
    assert.equal((await snapshot()).display.sleeping, true);
    await page.click('#simulate-touch');
    await page.selectOption('#idle-battery', '0'); await page.click('#advance-half-hour');
    assert.equal((await snapshot()).display.sleeping, false);
    await page.selectOption('#idle-external', 'custom'); await page.fill('#custom-external', '17');
    await page.click('#save-policy'); await page.reload(); await page.waitForFunction(() => window.previewSnapshot);
    snap = await snapshot(); assert.equal(snap.policy.idle.externalMinutes, 17); assert.equal(snap.policy.idle.batteryMinutes, 0);
    check('idle timeouts by power source, never, custom minutes, first-touch restoration and saved reload');

    await page.click('#tab-sleep');
    await page.selectOption('#idle-external', '5');
    await selectPage('air');
    await page.click('#jump-night');
    snap = await snapshot(); assert.equal(snap.display.reason, 'plan'); assert.equal(snap.display.page, 'clock'); assert.equal(snap.display.brightness, 8);
    await page.locator('[data-plan-id="night"] [data-edit="page"]').selectOption('weather');
    assert.equal((await snapshot()).display.page, 'weather');
    await page.locator('[data-plan-id="night"] [data-edit="brightness"]').fill('20');
    assert.equal((await snapshot()).display.brightness, 20);
    await page.locator('[data-plan-id="night"] [data-edit="action"]').selectOption('off');
    assert.equal((await snapshot()).display.brightness, 0);
    assert.equal(await page.locator('#screen-content').evaluate(e => getComputedStyle(e).visibility), 'hidden');
    await page.click('#simulate-touch');
    snap = await snapshot(); assert.equal(snap.display.page, 'air'); assert.equal(snap.display.temporaryWake, true);
    await page.click('#advance-minute'); assert.equal((await snapshot()).display.sleeping, false);
    await page.click('#advance-half-hour'); assert.equal((await snapshot()).display.sleeping, true);
    await page.locator('[data-plan-id="night"] [data-edit="action"]').selectOption('dim');
    await shot('desktop-sleep-policy');
    check('schedule-selected brightness and page, true off, temporary wake and scheduled re-entry');

    await page.click('#add-plan');
    let cards = page.locator('.schedule-card'); assert.equal(await cards.count(), 3);
    await cards.last().locator('[data-edit="name"]').fill('重叠示例');
    await cards.last().locator('[data-edit="brightness"]').fill('35');
    await cards.last().locator('[data-edit="page"]').selectOption('external');
    await cards.last().locator('[data-plan-action="up"]').click();
    await page.locator('.schedule-card').nth(1).locator('[data-plan-action="up"]').click();
    snap = await snapshot(); assert.equal(snap.display.planName, '重叠示例'); assert.equal(snap.display.brightness, 35); assert.equal(snap.display.page, 'external');
    await page.locator('.schedule-card').first().locator('[data-plan-action="remove"]').click();
    assert.equal((await snapshot()).display.planName, '夜间');
    const night = page.locator('[data-plan-id="night"]');
    await night.locator('[data-edit="end"]').fill('23:00'); await page.locator('#tab-sleep').click();
    assert.equal(await page.locator('#save-policy').isDisabled(), true);
    await night.locator('[data-edit="end"]').fill('07:00'); await page.locator('#tab-sleep').click();
    assert.equal(await page.locator('#save-policy').isDisabled(), false);
    for (const day of [1, 2, 3, 4, 5, 6, 0]) await night.locator(`[data-day="${day}"]`).click();
    assert.equal(await page.locator('#save-policy').isDisabled(), true);
    await night.locator('[data-day="5"]').click();
    await page.locator('#simulated-time').fill('2026-11-07T06:59'); await page.locator('#simulated-time').blur();
    assert.equal((await snapshot()).display.planName, '夜间');
    await page.selectOption('#idle-external', '0'); await page.click('#advance-minute');
    snap = await snapshot(); assert.equal(snap.display.sleeping, false); assert.equal(snap.display.page, 'air');
    await page.locator('#simulated-time').fill('2026-11-13T23:00'); await page.locator('#simulated-time').blur();
    assert.equal((await snapshot()).display.reason, 'plan');
    await page.click('#simulate-touch'); await page.click('#advance-minute');
    assert.equal((await snapshot()).display.sleeping, true);
    check('plan creation, ordering, deletion, invalid intervals, weekdays, overnight end, next occurrence and one-minute fallback');

    await page.click('#tab-display'); await page.click('#reset-preview');
    assert.equal((await snapshot()).policy.awakeBrightness, 85);
    for (const [width, height] of [[320, 850], [390, 844], [768, 1024], [1360, 1060]]) {
      await page.setViewportSize({ width, height }); await layout(`viewport-${width}`);
      await selectPage('clock'); await shot(`viewport-${width}`);
    }
    await page.setViewportSize({ width: 390, height: 844 });
    await page.click('#tab-sleep'); await shot('mobile-sleep-policy');
    await page.click('#add-plan');
    await page.locator('.schedule-card').last().locator('[data-edit="name"]').fill('<img src=x onerror=alert(1)>');
    await page.locator('.schedule-card').last().locator('[data-plan-action="up"]').click();
    assert.equal(await page.locator('.schedule-card img').count(), 0);
    check('desktop/tablet/mobile including 320px, responsive settings and escaped plan text');

    await page.setViewportSize({ width: 1360, height: 1060 });
    await page.click('#tab-display'); await page.click('#reset-preview');
    await page.click('#add-page');
    let snapWidgets = await snapshot(); const mixedId = snapWidgets.page;
    await page.fill('#page-name', '空气与日常');
    for (const kind of ['clock', 'weather', 'metric', 'reminders', 'text']) await page.click(`[data-add-widget="${kind}"]`);
    await page.fill('#widget-text', '晚安，\n明天见。');
    const mixedWidgets = (await snapshot()).pages.find(p => p.id === mixedId).widgets;
    const mixedMetric = mixedWidgets.find(w => w.type === 'metric');
    await page.locator(`[data-widget-id="${mixedMetric.id}"]`).click();
    await page.selectOption('#widget-binding', 'temp');
    await page.fill('#widget-title', '<img onerror=x>');
    assert.equal(await page.locator('.widget-label img, #page-nav img').count(), 0);
    await page.fill('#widget-title', '温度');
    await page.selectOption('#widget-w', '3');
    assert.equal((await snapshot()).pages.find(p => p.id === mixedId).widgets.find(w => w.id === mixedMetric.id).w, 2);
    assert.match(await page.locator('#layout-message').textContent(), /重叠|边界/);
    await page.click('[data-add-widget="chart"]');
    assert.equal((await snapshot()).pages.find(p => p.id === mixedId).widgets.length, 5);
    assert.match(await page.locator('#layout-message').textContent(), /空间不足/);
    await page.fill('#widget-title', '温度');
    await shot('desktop-widgets');
    await page.click('#edit-layout');
    for (const dataState of ['normal', 'missing', 'stale', 'extreme']) {
      await page.click('#tab-display'); await page.selectOption('#data-state', dataState); await layout(`mixed-widgets-${dataState}`);
    }
    await page.selectOption('#data-state', 'normal'); await screenShot('screen-combination');
    await shot('desktop-combination');
    check('clock, weather, sensor, reminder and text composition; bindings, escaped titles and collision rejection');

    await page.click('#add-page');
    const chartPageId = (await snapshot()).page;
    assert.equal(await page.locator('#add-page').isDisabled(), true);
    await page.fill('#page-name', '时钟与图表');
    await page.click('[data-add-widget="clock"]'); await page.click('[data-add-widget="chart"]');
    for (const kind of ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut']) {
      await page.click('#tab-widgets'); await page.selectOption('#widget-chartType', kind);
      for (const dataState of ['normal', 'missing', 'stale', 'extreme']) {
        await page.click('#tab-display'); await page.selectOption('#data-state', dataState); await layout(`combined-chart-${kind}-${dataState}`);
      }
    }
    await page.selectOption('#data-state', 'normal'); await page.click('#tab-widgets');
    await page.selectOption('#widget-chartType', 'stacked-area');
    await page.click('#edit-layout'); await screenShot('screen-clock-chart');
    await page.click('#edit-layout');
    await page.click('#remove-widget');
    await page.click('[data-add-widget="metric"]');
    let widgetPage = (await snapshot()).pages.find(p => p.id === chartPageId);
    const clockId = widgetPage.widgets.find(w => w.type === 'clock').id, metricId = widgetPage.widgets.find(w => w.type === 'metric').id;
    await page.locator(`[data-widget-id="${clockId}"]`).click(); await page.selectOption('#widget-w', '2');
    const metricLocator = page.locator(`[data-widget-id="${metricId}"]`);
    let box = await metricLocator.boundingBox(), scale = (await screen.boundingBox()).width / 480;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2); await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 228 * scale, box.y + box.height / 2 - 260 * scale, { steps: 10 }); await page.mouse.up();
    let moved = (await snapshot()).pages.find(p => p.id === chartPageId).widgets.find(w => w.id === metricId);
    assert.deepEqual([moved.x, moved.y], [2, 0]);
    await screen.focus(); await page.keyboard.press('ArrowDown'); await page.keyboard.press('ArrowUp'); await page.keyboard.press('ArrowLeft');
    moved = (await snapshot()).pages.find(p => p.id === chartPageId).widgets.find(w => w.id === metricId);
    assert.deepEqual([moved.x, moved.y], [2, 0]);
    const grip = await metricLocator.locator('.resize-grip').boundingBox();
    await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2); await page.mouse.down();
    await page.mouse.move(grip.x + grip.width / 2, grip.y + grip.height / 2 + 260 * scale, { steps: 10 }); await page.mouse.up();
    assert.equal((await snapshot()).pages.find(p => p.id === chartPageId).widgets.find(w => w.id === metricId).h, 4);
    await page.click('#undo-layout');
    assert.equal((await snapshot()).pages.find(p => p.id === chartPageId).widgets.find(w => w.id === metricId).h, 2);
    // A drag which returns to its origin must not retain its last nonzero position.
    box = await metricLocator.boundingBox();
    const start = { x: box.x + box.width / 2, y: box.y + box.height / 2 };
    await page.mouse.move(start.x, start.y); await page.mouse.down();
    await page.mouse.move(start.x, start.y + 130 * scale, { steps: 5 }); await page.mouse.move(start.x, start.y, { steps: 5 }); await page.mouse.up();
    assert.equal((await snapshot()).pages.find(p => p.id === chartPageId).widgets.find(w => w.id === metricId).y, 0);
    await page.click('#edit-layout');
    check('minimum-size clock with seven charts, page limit, pointer drag, keyboard movement, resize, undo and return-to-origin');

    await page.click('#tab-sleep'); await page.selectOption('#idle-page', mixedId); await page.click('#save-policy');
    await selectPage('clock'); await page.click('#advance-half-hour');
    assert.equal((await snapshot()).display.page, mixedId);
    await page.click('#simulate-touch'); assert.equal((await snapshot()).display.page, 'clock');
    await page.reload(); await page.waitForFunction(() => window.previewSnapshot);
    snapWidgets = await snapshot();
    assert.equal(snapWidgets.pages.length, 8); assert.equal(snapWidgets.pages.find(p => p.id === mixedId).widgets.length, 5);
    assert.equal(snapWidgets.policy.idle.page, mixedId);
    await selectPage(mixedId); await page.click('#tab-widgets');
    for (const width of [320, 390, 768]) { await page.setViewportSize({ width, height: 900 }); await layout(`widgets-viewport-${width}`); }
    await page.setViewportSize({ width: 390, height: 844 }); await shot('mobile-widgets');
    await page.click('#remove-page'); assert.equal((await snapshot()).policy.idle.page, 'current');
    await page.click('#undo-layout'); assert.equal((await snapshot()).pages.length, 8);
    check('composed sleep page, layout persistence, responsive widget inspector, deletion fallback and undo');

    await page.setViewportSize({ width: 1360, height: 1060 });
    await page.click('#tab-display'); await page.click('#reset-preview'); await page.click('#add-page');
    const responsiveId = (await snapshot()).page;
    await page.fill('#page-name', '自适应组件'); await page.click('[data-add-widget="metric"]');
    const typography = () => page.locator('.metric-content').evaluate(element => {
      const label = element.querySelector('.widget-label'), unit = element.querySelector('.metric-unit'), value = element.querySelector('.metric-value');
      return { label: parseFloat(getComputedStyle(label).fontSize), unit: unit && parseFloat(getComputedStyle(unit).fontSize), value: parseFloat(getComputedStyle(value).fontSize) };
    });
    const baseType = await typography();
    assert.ok(baseType.value > 64); assert.ok(baseType.label > baseType.unit);
    const baseHeader = await page.locator('.metric-heading').evaluate(element => {
      const title = element.querySelector('.widget-label').getBoundingClientRect(), unit = element.querySelector('.metric-unit').getBoundingClientRect();
      return title.bottom > unit.top && unit.bottom > title.top && unit.left >= title.right;
    });
    assert.equal(baseHeader, true);
    for (const [w, h] of [[3, 3], [4, 4]]) {
      await page.click(`[data-widget-size="${w},${h}"]`);
      const scaled = await typography();
      assert.ok(scaled.value > baseType.value);
      assert.ok(Math.abs(scaled.value / baseType.value - scaled.label / baseType.label) < .03);
      await layout(`metric-square-${w}`);
    }
    await page.click('[data-widget-size="2,2"]');
    assert.equal((await typography()).value, baseType.value);
    const resizingMetric = page.locator('.ui-widget.widget-metric'), resizeGrip = await resizingMetric.locator('.resize-grip').boundingBox();
    const metricScale = (await screen.boundingBox()).width / 480;
    await page.mouse.move(resizeGrip.x + resizeGrip.width / 2, resizeGrip.y + resizeGrip.height / 2); await page.mouse.down();
    await page.mouse.move(resizeGrip.x + resizeGrip.width / 2 + 228 * metricScale, resizeGrip.y + resizeGrip.height / 2 + 260 * metricScale, { steps: 10 });
    assert.ok((await typography()).value > baseType.value * 1.9);
    await page.mouse.up(); await layout('live-metric-resize');
    await page.click('#edit-layout'); await screenShot('metric-square-large'); await page.click('#edit-layout');
    for (const [w, h] of [[2, 2], [3, 3], [4, 4], [4, 1], [4, 2]]) {
      await page.click('#tab-widgets'); await page.click(`[data-widget-size="${w},${h}"]`);
      for (const binding of ['co2', 'pm', 'temp', 'hum', 'tvoc', 'hcho', 'power']) {
        await page.click('#tab-widgets'); await page.selectOption('#widget-binding', binding);
        for (const dataState of ['normal', 'missing', 'stale', 'extreme']) {
          await page.click('#tab-display'); await page.selectOption('#data-state', dataState); await layout(`responsive-metric-${w}-${h}-${binding}-${dataState}`);
        }
      }
    }
    await page.selectOption('#data-state', 'normal'); await page.click('#tab-widgets'); await page.selectOption('#widget-binding', 'co2');
    const rowGeometry = await page.locator('.metric-content').evaluate(element => {
      const title = element.querySelector('.widget-label').getBoundingClientRect(), unit = element.querySelector('.metric-unit').getBoundingClientRect(), number = element.querySelector('.metric-value').getBoundingClientRect();
      return unit.top > title.bottom && number.left > title.right && number.left > unit.right;
    });
    assert.equal(rowGeometry, true);
    await page.click('#edit-layout'); await screenShot('metric-full-row'); await page.click('#edit-layout');
    await page.click('[data-widget-size="4,1"]');
    await page.click('#edit-layout'); await screenShot('metric-slim-row'); await page.click('#edit-layout');
    await page.click('[data-widget-size="2,2"]');
    check('measurement title and unit hierarchy, proportional square scaling, live resize and full-width rows across all bindings and data states');

    await page.click('#add-page'); const stripPageId = (await snapshot()).page;
    await page.fill('#page-name', '紧凑图表'); await page.click('[data-add-widget="clock"]'); await page.click('[data-add-widget="chart"]');
    let compactPage = (await snapshot()).pages.find(p => p.id === stripPageId);
    assert.deepEqual([compactPage.widgets.at(-1).w, compactPage.widgets.at(-1).h], [4, 2]);
    for (const kind of ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut']) {
      await page.click('#tab-widgets'); await page.selectOption('#widget-chartType', kind);
      for (const range of ['1', '6', '24']) {
        await page.selectOption('#widget-range', range);
        for (const dataState of ['normal', 'missing', 'stale', 'extreme']) {
          await page.click('#tab-display'); await page.selectOption('#data-state', dataState); await layout(`strip-${kind}-${range}-${dataState}`);
        }
        await page.click('#tab-widgets');
      }
    }
    await page.click('#tab-display'); await page.selectOption('#data-state', 'normal'); await page.click('#tab-widgets'); await page.selectOption('#widget-chartType', 'stacked-area');
    const stripGeometry = await page.locator('.chart-strip').evaluate(element => {
      const meta = element.querySelector('.chart-strip-meta').getBoundingClientRect(), plot = element.querySelector('.strip-plot').getBoundingClientRect(), canvas = document.querySelector('#device-screen').getBoundingClientRect();
      return { left: meta.width / canvas.width * 480, right: plot.width / canvas.width * 480, separate: meta.right < plot.left };
    });
    assert.ok(stripGeometry.separate); assert.ok(Math.abs(stripGeometry.left - 96) < 1); assert.ok(Math.abs(stripGeometry.right - 324) < 1);
    await page.click('[data-widget-size="4,4"]'); assert.equal(await page.locator('.chart-strip').count(), 0);
    await page.click('[data-widget-size="4,2"]'); assert.equal(await page.locator('.chart-strip').count(), 1);
    await page.click('[data-add-widget="metric"]'); await page.selectOption('#widget-binding', 'temp');
    await page.click('[data-add-widget="metric"]'); await page.selectOption('#widget-binding', 'hum');
    await page.click('#edit-layout'); await screenShot('screen-compact-chart'); await shot('desktop-responsive-layout');
    for (const width of [320, 390, 768]) { await page.setViewportSize({ width, height: 900 }); await layout(`responsive-strip-viewport-${width}`); }
    await page.setViewportSize({ width: 390, height: 844 }); await shot('mobile-compact-chart');
    await page.reload(); await page.waitForFunction(() => window.previewSnapshot); await selectPage(stripPageId);
    assert.equal(await page.locator('.chart-strip').count(), 1);
    compactPage = (await snapshot()).pages.find(p => p.id === stripPageId);
    assert.equal(compactPage.widgets.find(w => w.type === 'chart').h, 2);
    check('two-row four-column charts with one-column summary across seven chart types, three ranges, all data states, resizing and saved reload');

    await page.setViewportSize({ width: 1360, height: 1060 });
    await page.click('#tab-display'); await page.click('#reset-preview');
    const { THEMES } = await import(path.join(ROOT, 'simulator/ui-preview/appearance.js'));
    const rgb = hex => `rgb(${[1, 3, 5].map(start => parseInt(hex.slice(start, start + 2), 16)).join(', ')})`;
    const computed = (selector, property = 'color') => page.locator(selector).first().evaluate((element, prop) => getComputedStyle(element)[prop], property);
    const setColor = (selector, value) => page.locator(selector).evaluate((element, color) => {
      element.value = color; element.dispatchEvent(new Event('input', { bubbles: true })); element.dispatchEvent(new Event('change', { bubbles: true }));
    }, value);
    for (const [id, theme] of Object.entries(THEMES)) {
      await page.click(`[data-theme="${id}"]`);
      assert.equal((await snapshot()).appearance.theme, id);
      assert.equal(await computed('#screen-content', 'backgroundColor'), rgb(theme.colors.canvas));
      assert.equal(await computed('[data-widget-id="clock-co2"] .widget-label'), rgb(theme.colors.title));
      assert.equal(await computed('[data-widget-id="clock-co2"] .metric-value'), rgb(theme.colors.value));
      assert.equal(await computed('[data-widget-id="clock-co2"] .metric-unit'), rgb(theme.colors.unit));
      assert.equal(await computed('.screen-page-rail .active', 'backgroundColor'), rgb(theme.colors.accent));
      assert.equal(await computed('.screen-page-rail i:not(.active)', 'backgroundColor'), rgb(theme.colors.unit));
      await layout(`theme-${id}`); await screenShot(`theme-${id}`);
    }
    await page.selectOption('#chart-type', 'multi'); await page.click('[data-theme="ocean"]');
    assert.equal(await computed('.legend-swatch', 'backgroundColor'), rgb(THEMES.ocean.colors.chartA));
    await page.locator('.appearance-details summary').click();
    assert.equal(await computed('#theme-color-value', 'paddingLeft'), '2px');
    await setColor('#theme-color-chartA', '#f2b77e');
    assert.equal(await computed('.legend-swatch', 'backgroundColor'), rgb('#f2b77e'));
    await setColor('#theme-color-accent', '#e9a6ce');
    assert.equal(await computed('.screen-page-rail .active', 'backgroundColor'), rgb('#e9a6ce'));
    await selectPage('clock'); await setColor('#theme-color-value', '#ffe2b2');
    assert.equal(await computed('.metric-value'), rgb('#ffe2b2'));
    await page.click('[data-theme="amber"]');
    assert.deepEqual((await snapshot()).appearance.colors, {});
    assert.equal(await computed('.metric-value'), rgb(THEMES.amber.colors.value));
    await page.click('[data-theme="ocean"]');
    await page.locator('.appearance-details summary').click();
    check('six complete theme groups update screen, title, reading, unit and chart colors; group editing and replacement reset inherited palette');

    await page.click('#edit-layout'); await page.locator('[data-widget-id="clock-co2"]').click();
    for (const [role, color] of Object.entries({ background: '#322b43', title: '#f5d7b8', value: '#ffd28a', unit: '#abc7e1' })) await setColor(`#widget-color-${role}`, color);
    await page.locator('#widget-opacity').fill('65');
    assert.equal(await computed('[data-widget-id="clock-co2"] .widget-label'), rgb('#f5d7b8'));
    assert.equal(await computed('[data-widget-id="clock-co2"] .metric-value'), rgb('#ffd28a'));
    assert.equal(await computed('[data-widget-id="clock-co2"] .metric-unit'), rgb('#abc7e1'));
    assert.equal(await computed('[data-widget-id="clock-pm"] .metric-value'), rgb(THEMES.ocean.colors.value));
    await page.click('#tab-display'); await page.click('[data-theme="plum"]');
    assert.equal(await computed('[data-widget-id="clock-co2"] .metric-value'), rgb('#ffd28a'));
    assert.equal(await computed('[data-widget-id="clock-pm"] .metric-value'), rgb(THEMES.plum.colors.value));
    await page.click('#tab-widgets'); await page.click('[data-widget-color-reset="unit"]');
    assert.equal(await computed('[data-widget-id="clock-co2"] .metric-unit'), rgb(THEMES.plum.colors.unit));
    await page.click('#reset-widget-colors');
    assert.equal((await snapshot()).pages[0].widgets[1].appearance, undefined);
    await setColor('#widget-color-value', '#ffd28a'); await setColor('#widget-color-background', '#322b43');
    await page.locator('#widget-opacity').fill('65');
    await page.click('#tab-display'); await page.selectOption('#external-type', 'text');
    await page.locator('[data-widget-id="external-main"]').click();
    await page.click('#tab-widgets'); await setColor('#widget-color-body', '#f1d4a1');
    assert.equal(await computed('.widget-text-body'), rgb('#f1d4a1'));
    for (const [id, selector, role] of [['clock', '.widget-time', 'value'], ['weather', '.weather-condition', 'body'], ['reminders', '.reminder-card p', 'body'], ['trend', '.legend-name', 'body']]) {
      await selectPage(id); await page.locator('.ui-widget').first().click();
      await setColor(`#widget-color-${role}`, '#dcbdea'); assert.equal(await computed(selector), rgb('#dcbdea'));
    }
    await selectPage('clock'); await page.locator('[data-widget-id="clock-co2"]').click();
    await page.locator('#widget-color-value').scrollIntoViewIfNeeded();
    const stickyScreen = await screen.boundingBox();
    assert.ok(stickyScreen.y >= 0 && stickyScreen.y + stickyScreen.height <= 1060);
    await shot('desktop-component-colors');
    check('independent component background and text groups, neighboring inheritance, theme replacement, individual/all resets and every widget type');

    await page.click('#tab-display');
    for (const id of ['mountains', 'aurora', 'dunes']) {
      await page.click(`[data-wallpaper="${id}"]`); await page.waitForLoadState('networkidle');
      assert.equal(await page.locator('#screen-content').evaluate(e => e.classList.contains('wallpaper-active')), true);
      assert.match(await computed('#screen-content', 'backgroundImage'), new RegExp(`${id}\\.svg`));
      await layout(`wallpaper-${id}`); await screenShot(`wallpaper-${id}`);
    }
    await page.click('[data-theme="ocean"]'); await page.click('[data-wallpaper="mountains"]');
    await page.click('[data-theme="paper"]'); await layout('paper-wallpaper'); await screenShot('wallpaper-paper'); await page.click('[data-theme="ocean"]');
    await page.locator('#card-opacity').fill('62'); await page.locator('#wallpaper-dim').fill('35');
    const glass = await page.evaluate(() => {
      const card = document.querySelector('[data-widget-id="clock-co2"]'), inherited = document.querySelector('[data-widget-id="clock-pm"]');
      return { custom: getComputedStyle(card).getPropertyValue('--widget-opacity').trim(), inherited: getComputedStyle(inherited).getPropertyValue('--widget-opacity').trim(),
        textOpacity: getComputedStyle(card.querySelector('.metric-value')).opacity, edge: getComputedStyle(card, '::after').boxShadow,
        dim: getComputedStyle(document.querySelector('#screen-content'), '::before').opacity };
    });
    assert.equal(glass.custom, '65%'); assert.equal(glass.inherited, '62%'); assert.equal(glass.textOpacity, '1'); assert.notEqual(glass.edge, 'none'); assert.equal(glass.dim, '0.35');
    await page.click('#edit-layout'); await screenShot('screen-wallpaper-glass'); await shot('desktop-theme-wallpaper');
    for (const width of [320, 390, 768]) { await page.setViewportSize({ width, height: 900 }); await layout(`glass-viewport-${width}`); }
    await page.setViewportSize({ width: 390, height: 844 }); await shot('mobile-theme-wallpaper');
    await page.locator('#wallpaper-file').setInputFiles({ name: '本地壁纸.png', mimeType: 'image/png', buffer: fs.readFileSync(path.join(OUTPUT, 'screen-clock.png')) });
    await page.waitForFunction(() => window.previewSnapshot.appearance.wallpaper === 'custom');
    let savedAppearance = (await snapshot()).appearance;
    assert.match(savedAppearance.customWallpaper.src, /^data:image\/webp;base64,/);
    assert.equal(savedAppearance.customWallpaper.name, '本地壁纸.png');
    await page.reload(); await page.waitForFunction(() => window.previewSnapshot);
    assert.deepEqual((await snapshot()).appearance, savedAppearance);
    assert.equal((await snapshot()).pages[0].widgets[1].appearance.value, '#ffd28a');
    assert.equal(await computed('[data-widget-id="clock-co2"] .metric-value'), rgb('#ffd28a'));
    await page.locator('#wallpaper-file').setInputFiles({ name: 'bad.png', mimeType: 'image/png', buffer: Buffer.from('invalid image') });
    await page.waitForFunction(() => document.querySelector('#appearance-message').classList.contains('error'));
    assert.deepEqual((await snapshot()).appearance, savedAppearance);
    await page.click('#remove-wallpaper'); assert.equal((await snapshot()).appearance.customWallpaper, null);
    assert.equal((await snapshot()).appearance.wallpaper, 'none');
    assert.equal(await computed('#screen-content', 'backgroundImage'), 'none');
    await page.click('[data-theme="paper"]'); await page.click('[data-wallpaper="mountains"]');
    await page.uncheck('#simulate-brightness'); await page.click('#tab-sleep'); await page.selectOption('#idle-action', 'off');
    await page.click('#advance-half-hour');
    assert.equal((await snapshot()).display.brightness, 0);
    assert.equal(await computed('#device-screen', 'backgroundColor'), 'rgb(0, 0, 0)');
    assert.equal(await computed('#screen-content', 'visibility'), 'hidden');
    await page.click('#simulate-touch');
    assert.equal(await computed('#device-screen', 'backgroundColor'), rgb(THEMES.paper.colors.canvas));
    check('wallpaper presets, translucent background and edges, opaque text, opacity, responsive layout, local upload/reload/removal and true off with a light theme');

    await page.click('#tab-display'); await page.click('#reset-preview');
    await page.click('#tab-widgets');
    while ((await snapshot()).pages.length > 1) await page.click('#remove-page');
    await page.fill('#page-name', '用于检查标题截断的十六字长页面名称');
    for (let count = 1; count <= 8; count++) {
      await layout(`page-rail-${count}`);
      if ((await snapshot()).editing) await page.click('#edit-layout');
      const beforePage = await snapshot(), pageIndex = beforePage.pages.findIndex(p => p.id === beforePage.page);
      await screen.focus(); await page.keyboard.press('ArrowRight'); await layout(`page-rail-wrap-${count}`);
      assert.equal((await snapshot()).display.page, beforePage.pages[(pageIndex + 1) % count].id);
      if (count < 8) await page.click('#add-page');
    }
    await page.reload(); await page.waitForFunction(() => window.previewSnapshot); await layout('page-rail-reload-eight');
    check('one to eight equal page segments, wraparound, title truncation, page creation/deletion and persisted layouts');

    await page.evaluate(() => localStorage.setItem('bry88bba15g.ui-preview.v1', '{bad json'));
    await page.evaluate(() => localStorage.setItem('bry88bba15g.ui-layout.v1', '{bad json'));
    await page.evaluate(() => localStorage.setItem('bry88bba15g.ui-appearance.v1', '{bad json'));
    await page.reload(); await page.waitForFunction(() => window.previewSnapshot);
    assert.equal((await snapshot()).policy.plans[0].id, 'night');
    assert.equal((await snapshot()).pages.length, 6);
    assert.equal((await snapshot()).appearance.theme, 'forest');
    assert.match(await page.locator('#appearance-message').textContent(), /恢复默认/);
    const deniedContext = await browser.newContext({ viewport: { width: 390, height: 844 } });
    await deniedContext.addInitScript(() => Object.defineProperty(window, 'localStorage', { get() { throw new DOMException('Blocked', 'SecurityError'); } }));
    const deniedPage = await deniedContext.newPage();
    deniedPage.on('pageerror', error => errors.push(error.message));
    await deniedPage.goto(URL); await deniedPage.waitForFunction(() => window.previewSnapshot);
    await deniedPage.click('#tab-sleep'); await deniedPage.click('#save-policy');
    assert.match(await deniedPage.locator('#policy-message').textContent(), /存储/);
    await deniedPage.click('#add-page'); await deniedPage.click('[data-add-widget="clock"]');
    assert.match(await deniedPage.locator('#layout-message').textContent(), /存储/);
    await deniedPage.click('#tab-display'); await deniedPage.click('[data-theme="ocean"]');
    assert.match(await deniedPage.locator('#appearance-message').textContent(), /存储/);
    await deniedContext.close();
    check('corrupt local settings recovery and disabled localStorage');

    assert.deepEqual(errors, []); assert.deepEqual(failedRequests, []); assert.deepEqual(mutations, []); assert.deepEqual(externalRequests, []);
    check('no console/page errors, failed assets, external requests or write requests');
    fs.writeFileSync(path.join(OUTPUT, 'browser-results.json'), JSON.stringify({ url: URL, checkedAt: new Date().toISOString(), browser: browser.version(), checks, errors, failedRequests, mutations, externalRequests }, null, 2) + '\n');
  } finally { await browser.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
