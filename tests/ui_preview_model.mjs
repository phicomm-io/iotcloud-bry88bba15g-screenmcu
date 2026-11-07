import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { SCREEN, GRID, PAGE_RAIL, GRID_TOP, GRID_WIDTH, GRID_HEIGHT, COLUMN_PITCH, ROW_PITCH, widgetRect } from '../simulator/ui-preview/geometry.js';
import {
  defaultPolicy, wallTime, timeInput, activePlan, evaluateDisplay, validatePolicy,
  savePolicy, loadPolicy, chartFixture, MINUTE, STORAGE_KEY,
} from '../simulator/ui-preview/model.js';
import { lineSegments, chartMarkup, chartReading } from '../simulator/ui-preview/charts.js';
import { LAYOUT_KEY, makeWidget, defaultLayouts, fits, findSpace, validateLayouts, loadLayouts, saveLayouts } from '../simulator/ui-preview/layout.js';
import {
  APPEARANCE_KEY, THEMES, defaultAppearance, resolveTheme, surfaceOpacity, widgetAppearanceStyle,
  validateAppearance, loadAppearance, saveAppearance,
} from '../simulator/ui-preview/appearance.js';

const time = value => wallTime(`2026-11-${value}`);
const base = (policy, now, rest = {}) => ({ policy, now, lastActivity: now, supply: 'external', page: 'air', ...rest });

test('page rail sizes persist locally and old appearance documents retain the new default',()=>{
  const a=defaultAppearance(),data=new Map(),storage={getItem:k=>data.get(k),setItem:(k,v)=>data.set(k,v)};
  assert.equal(a.pageRailHeight,12);
  for(const n of [6,12,20]){a.pageRailHeight=n;assert.equal(saveAppearance(storage,a),null);assert.equal(loadAppearance(storage).appearance.pageRailHeight,n);}
  delete a.pageRailHeight;assert.equal(validateAppearance(a),true);
  a.pageRailHeight=7;assert.equal(validateAppearance(a),false);
});

test('browser geometry matches the LVGL contract and expands saved logical layouts without migration', () => {
  const native = readFileSync(new URL('../include/ui_layout.h', import.meta.url), 'utf8');
  const values = { UI_MARGIN: GRID.margin, UI_HEADER_HEIGHT: GRID.headerHeight, UI_HEADER_GAP: GRID.headerGap,
    UI_GRID_COLUMNS: GRID.columns, UI_GRID_ROWS: GRID.rows, UI_GRID_GAP: GRID.gap,
    UI_PAGE_RAIL_HEIGHT: PAGE_RAIL.height, UI_PAGE_RAIL_ACTIVE_OPACITY: PAGE_RAIL.activeOpacity,
    UI_PAGE_RAIL_IDLE_OPACITY: PAGE_RAIL.idleOpacity };
  for (const [name, value] of Object.entries(values)) assert.match(native, new RegExp(`#define ${name} ${value}\\b`));
  assert.deepEqual([GRID_TOP, GRID_WIDTH, GRID_HEIGHT, COLUMN_PITCH, ROW_PITCH], [66, 448, 772, 114, 130]);
  assert.deepEqual(widgetRect({ w: 2, h: 2 }), { x: 16, y: 66, width: 220, height: 252 });
  assert.deepEqual(widgetRect({ x: 2, y: 4, w: 2, h: 2 }), { x: 244, y: 586, width: 220, height: 252 });
  const pages = defaultLayouts(), saved = structuredClone(pages);
  for (const page of pages) for (const widget of page.widgets) {
    const rect = widgetRect(widget);
    assert.ok(rect.x >= GRID.margin && rect.y >= GRID_TOP);
    assert.ok(rect.x + rect.width <= SCREEN.width - GRID.margin);
    assert.ok(rect.y + rect.height <= SCREEN.height - GRID.margin);
  }
  assert.deepEqual(pages, saved);
  assert.equal(validateLayouts(pages), true);
});

test('device wall time is validated and independent of host timezone', () => {
  const t = time('06T23:00');
  assert.equal(timeInput(t), '2026-11-06T23:00');
  assert.equal(new Date(t).getUTCDay(), 5);
  assert.ok(Number.isNaN(wallTime('2026-02-30T10:00')));
  assert.ok(Number.isNaN(wallTime('2026-11-06T24:00')));
});
test('overnight schedule uses the selected start weekday and exclusive end', () => {
  const p = defaultPolicy(); p.plans[0].days = [5];
  assert.equal(activePlan(p.plans, time('06T22:59')), null);
  assert.equal(activePlan(p.plans, time('06T23:00')).occurrence, 'night:2026-11-06');
  assert.equal(activePlan(p.plans, time('07T06:59')).occurrence, 'night:2026-11-06');
  assert.equal(activePlan(p.plans, time('07T07:00')), null);
  assert.equal(activePlan(p.plans, time('07T23:00')), null);
});
test('same-day boundaries and zero-length intervals', () => {
  const p = defaultPolicy(); p.plans[0].enabled = false; p.plans[1].enabled = true;
  assert.equal(activePlan(p.plans, time('06T12:29')), null);
  assert.equal(activePlan(p.plans, time('06T12:30')).plan.id, 'afternoon');
  assert.equal(activePlan(p.plans, time('06T13:30')), null);
  p.plans[1].end = p.plans[1].start;
  assert.ok(validatePolicy(p)); assert.equal(activePlan(p.plans, time('06T12:30')), null);
});
test('overlapping schedules follow list order; disabled schedules are skipped', () => {
  const p = defaultPolicy(), other = { ...p.plans[0], id: 'other', brightness: 33, page: 'weather' };
  p.plans.unshift(other);
  assert.equal(evaluateDisplay(base(p, time('06T23:00'))).brightness, 33);
  p.plans.reverse();
  assert.equal(evaluateDisplay(base(p, time('06T23:00'))).brightness, 8);
  p.plans[1].enabled = false;
  assert.equal(evaluateDisplay(base(p, time('06T23:00'))).page, 'weather');
});
test('power-dependent idle timeout, exact boundary and never', () => {
  const p = defaultPolicy(), start = time('06T18:00');
  assert.equal(evaluateDisplay(base(p, start + MINUTE, { lastActivity: start })).sleeping, false);
  assert.equal(evaluateDisplay(base(p, start + MINUTE, { lastActivity: start, supply: 'battery' })).sleeping, true);
  assert.equal(evaluateDisplay(base(p, start + 5 * MINUTE, { lastActivity: start })).reason, 'idle');
  p.idle.externalMinutes = 0;
  assert.equal(evaluateDisplay(base(p, start + 100 * MINUTE, { lastActivity: start })).sleeping, false);
});
test('sleep page, keep-current page, off mode and plan override', () => {
  const p = defaultPolicy(), now = time('06T23:00');
  p.idle.page = 'external'; p.idle.brightness = 90;
  const result = evaluateDisplay(base(p, now, { lastActivity: now - 10 * MINUTE }));
  assert.equal(result.brightness, 8); assert.equal(result.page, 'clock');
  p.plans[0].page = 'current'; p.plans[0].action = 'off';
  const off = evaluateDisplay(base(p, now));
  assert.equal(off.page, 'air'); assert.equal(off.brightness, 0); assert.equal(off.sleeping, true);
});
test('temporary wake restores original page, expires at idle timeout, and does not survive another occurrence', () => {
  const p = defaultPolicy(), now = time('06T23:30'), occurrence = activePlan(p.plans, now).occurrence;
  const wake = evaluateDisplay(base(p, now, { wakeOccurrence: occurrence }));
  assert.equal(wake.page, 'air'); assert.equal(wake.brightness, 85); assert.equal(wake.temporaryWake, true);
  const again = evaluateDisplay(base(p, now + 5 * MINUTE, { lastActivity: now, wakeOccurrence: occurrence }));
  assert.equal(again.sleeping, true); assert.equal(again.page, 'clock');
  assert.equal(evaluateDisplay(base(p, time('07T23:30'), { wakeOccurrence: occurrence })).sleeping, true);
});
test('never-idle still allows a one-minute temporary wake within a schedule', () => {
  const p = defaultPolicy(); p.idle.externalMinutes = 0;
  const now = time('06T23:30'), occurrence = activePlan(p.plans, now).occurrence;
  assert.equal(evaluateDisplay(base(p, now + MINUTE - 1, { lastActivity: now, wakeOccurrence: occurrence })).sleeping, false);
  assert.equal(evaluateDisplay(base(p, now + MINUTE, { lastActivity: now, wakeOccurrence: occurrence })).sleeping, true);
});
test('schedule end restores normal display unless idle remains expired', () => {
  const p = defaultPolicy(), now = time('07T07:00');
  assert.equal(evaluateDisplay(base(p, now)).sleeping, false);
  assert.equal(evaluateDisplay(base(p, now, { lastActivity: now - 8 * MINUTE })).reason, 'idle');
});
test('a higher-priority incoming schedule cancels an earlier temporary wake', () => {
  const p = defaultPolicy(), now = time('06T23:30');
  p.plans.unshift({ ...p.plans[0], id: 'incoming', start: '23:31', brightness: 19 });
  const occurrence = activePlan(p.plans, now).occurrence;
  const result = evaluateDisplay(base(p, now + MINUTE, { lastActivity: now, wakeOccurrence: occurrence }));
  assert.equal(result.sleeping, true); assert.equal(result.brightness, 19);
});
test('local persistence round trip, invalid and unavailable storage', () => {
  const memory = new Map(), storage = { getItem: key => memory.get(key) ?? null, setItem: (key, value) => memory.set(key, value) };
  const p = defaultPolicy(); p.idle.page = 'weather'; p.idle.externalMinutes = 17;
  assert.equal(savePolicy(storage, p), null); assert.deepEqual(loadPolicy(storage).policy, p);
  memory.set(STORAGE_KEY, '{invalid'); assert.equal(loadPolicy(storage).recovered, true);
  memory.set(STORAGE_KEY, JSON.stringify({ ...p, plans: null })); assert.equal(loadPolicy(storage).recovered, true);
  const denied = { getItem() { throw new Error('denied'); }, setItem() { throw new Error('denied'); } };
  assert.equal(loadPolicy(denied).recovered, true); assert.match(savePolicy(denied, p), /存储/);
});
test('invalid enabled days, invalid percentages, times, pages, duplicate IDs and fractional timeout are rejected', () => {
  for (const edit of [p => { p.plans[0].days = []; }, p => { p.plans[0].brightness = 101; }, p => { p.plans[0].start = '24:00'; }, p => { p.plans[0].page = '<script>'; }, p => { p.plans.push(p.plans[0]); }, p => { p.idle.externalMinutes = 1.2; }]) {
    const p = defaultPolicy(); edit(p); assert.ok(validatePolicy(p));
  }
});
test('chart gaps produce separate line runs and do not turn into zero samples', () => {
  const points = lineSegments([1, 2, null, 8, 9, null], n => n, n => n);
  assert.deepEqual(points, [[[0, 1], [1, 2]], [[3, 8], [4, 9]]]);
  const fixture = chartFixture('line', '24', 'normal');
  assert.ok(fixture.series[0].values.includes(null));
  assert.equal((chartMarkup(fixture, time('06T18:42')).match(/data-series="0"/g) || []).length, 2);
});
test('all chart types and ranges render finite coordinates, missing state and comparable stack units', () => {
  for (const kind of ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut']) for (const range of ['1', '6', '24']) for (const state of ['normal', 'missing', 'stale', 'extreme']) {
    const fixture = chartFixture(kind, range, state), html = chartMarkup(fixture, time('06T18:42'));
    assert.doesNotMatch(html, /NaN|Infinity|undefined/);
    if (state === 'missing') assert.match(html, /暂无数据/);
    else if (kind.startsWith('stacked')) {
      assert.equal(new Set(fixture.series.map(s => s.unit)).size, 1);
      assert.equal((html.match(/data-series="2"/g) || []).length, kind === 'stacked-area' ? 1 : fixture.n);
    }
    if (kind === 'dual') assert.equal(fixture.series[1].axis, 'right');
  }
});

test('clock, chart and mixed widgets share a collision-free grid with readable minimum sizes', () => {
  assert.equal(validateLayouts(defaultLayouts()), true);
  const clock = makeWidget('clock', 'clock'), page = { id: 'custom-mixed', title: '组合', widgets: [clock] };
  const chart = findSpace(page, makeWidget('chart', 'chart'));
  assert.deepEqual([chart.x, chart.y, chart.w, chart.h], [0, 2, 4, 2]);
  page.widgets.push(chart);
  const metric = findSpace(page, makeWidget('metric', 'metric'));
  assert.deepEqual([metric.x, metric.y], [0, 4]);
  page.widgets.push(metric);
  assert.equal(fits(page, { ...chart, y: 1 }), false);
  assert.equal(fits(page, { ...chart, w: 2 }), false);
  assert.equal(fits(page, { ...chart, h: 1 }), false);
  assert.equal(fits(page, { ...chart, y: 3 }), false);
  assert.equal(fits(page, { ...chart, x: .5 }), false);
  assert.equal(validateLayouts([page]), true);
});
test('compact weather, clock and half-width readings fit one row without changing other minimums', () => {
  const metric = makeWidget('metric', 'metric'), page = { widgets: [] };
  assert.equal(fits(page, { ...metric, h: 1 }), true);
  assert.equal(fits(page, { ...metric, w: 3, h: 1 }), false);
  assert.equal(fits(page, { ...metric, w: 4, h: 1 }), true);
  for (const type of ['clock', 'weather']) {
    assert.equal(fits(page, { ...makeWidget(type, type), w: 4, h: 1 }), true);
    assert.equal(fits(page, { ...makeWidget(type, type), w: 2, h: 1 }), false);
  }
  for (const type of ['text', 'chart', 'reminders']) assert.equal(fits(page, { ...makeWidget(type, type), w: 4, h: 1 }), false);
  page.widgets.push({ ...metric, w: 4, h: 1, y: 5 });
  assert.equal(fits(page, { ...metric, w: 4, h: 2, y: 5 }), false);
});
test('compact charts preserve totals, units, missing values and line gaps', () => {
  const now = time('06T18:42');
  for (const kind of ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut']) {
    const fixture = chartFixture(kind, '24', 'extreme'), reading = chartReading(fixture, true);
    const html = chartMarkup(fixture, now, { compact: true, strip: true, width: 299, height: 164 });
    assert.doesNotMatch(html, /NaN|Infinity|undefined/);
    assert.equal(reading.unit, fixture.unit);
    if (kind === 'line') assert.equal((html.match(/data-series="0"/g) || []).length, 2);
    if (kind === 'dual') { assert.match(html, /温度 °C/); assert.match(html, /湿度 %/); }
    const missing = chartFixture(kind, '24', 'missing');
    assert.equal(chartReading(missing, true).value, '--');
    assert.match(chartMarkup(missing, now, { compact: true, strip: true, width: 299, height: 164 }), /暂无数据/);
  }
  const stacked = chartFixture('stacked-area', '24', 'extreme');
  const total = stacked.series.reduce((sum, series) => sum + series.values.at(-1), 0);
  assert.equal(chartReading(stacked, true).value, `${Number((total / 1000).toFixed(1))}k`);
});
test('moving and resizing ignore only the same widget and retain neighboring boundaries', () => {
  const a = makeWidget('metric', 'a'), b = { ...makeWidget('weather', 'b'), x: 2 };
  const page = { widgets: [a, b] };
  assert.equal(fits(page, { ...a, y: 2 }), true);
  assert.equal(fits(page, { ...a, x: 1 }), false);
  assert.equal(fits(page, { ...a, w: 3 }), false);
  assert.equal(fits(page, { ...a, w: 1 }), false);
  assert.equal(fits(page, { ...a, h: 4 }), true);
});
test('layout persistence restores composition and rejects collisions, duplicated IDs and excessive totals', () => {
  const memory = new Map(), storage = { getItem: key => memory.get(key) ?? null, setItem: (key, value) => memory.set(key, value) };
  const pages = defaultLayouts();
  pages.push({ id: 'custom-night', title: '夜间组合', widgets: [makeWidget('clock', 'custom-clock')] });
  assert.equal(saveLayouts(storage, pages), null);
  assert.deepEqual(loadLayouts(storage).pages, pages);
  pages.at(-1).widgets.push(makeWidget('metric', 'overlap'));
  assert.match(saveLayouts(storage, pages), /无效/);
  memory.set(LAYOUT_KEY, JSON.stringify({ version: 1, pages }));
  assert.equal(loadLayouts(storage).recovered, true);
  const duplicate = defaultLayouts(); duplicate[1].widgets[0].id = duplicate[0].widgets[0].id;
  assert.equal(validateLayouts(duplicate), false);
  const many = Array.from({ length: 8 }, (_, p) => ({ id: `custom-${p}`, title: '页面', widgets: Array.from({ length: 6 }, (_, i) => ({ ...makeWidget('metric', `m-${p}-${i}`), x: i % 2 * 2, y: Math.floor(i / 2) * 2 })) }));
  assert.equal(validateLayouts(many), false);
  many.splice(6); many[5].widgets.splice(2);
  assert.equal(validateLayouts(many), true);
  const denied = { getItem() { throw new Error('denied'); }, setItem() { throw new Error('denied'); } };
  assert.equal(loadLayouts(denied).recovered, true);
  assert.match(saveLayouts(denied, defaultLayouts()), /存储/);
});
test('a custom widget page can be selected for sleep, with missing references falling back to the current page', () => {
  const policy = defaultPolicy(), pages = defaultLayouts();
  pages.push({ id: 'custom-night', title: '夜间', widgets: [] });
  policy.plans[0].page = 'custom-night';
  assert.equal(validatePolicy(policy), null);
  assert.equal(evaluateDisplay({ ...base(policy, time('06T23:00')), pages }).page, 'custom-night');
  pages.pop();
  assert.equal(evaluateDisplay({ ...base(policy, time('06T23:00')), pages }).page, 'air');
});
test('theme replacement updates inherited roles while component overrides remain independent', () => {
  const appearance = defaultAppearance(), widget = makeWidget('metric', 'color-metric');
  widget.appearance = { value: '#ffcc88', opacity: 63 };
  for (const theme of Object.keys(THEMES)) {
    appearance.theme = theme;
    assert.equal(validateAppearance(appearance), true);
    const styles = widgetAppearanceStyle(widget, appearance);
    assert.ok(styles.includes(`--widget-title:${THEMES[theme].colors.title}`));
    assert.ok(styles.includes('--widget-value:#ffcc88'));
    assert.equal(surfaceOpacity(widget, appearance), 63);
  }
  appearance.colors.title = '#123456';
  assert.equal(resolveTheme(appearance).title, '#123456');
  assert.notEqual(THEMES.paper.colors.title, '#123456');
  delete widget.appearance;
  assert.ok(widgetAppearanceStyle(widget, appearance).includes(`--widget-value:${THEMES.paper.colors.value}`));
});
test('wallpaper transparency applies to component backgrounds and preserves explicit opacity including zero', () => {
  const appearance = defaultAppearance(), widget = makeWidget('clock', 'glass-clock');
  assert.equal(surfaceOpacity(widget, appearance), 0);
  appearance.wallpaper = 'mountains'; appearance.cardOpacity = 74;
  assert.equal(surfaceOpacity(widget, appearance), 74);
  widget.appearance = { opacity: 0 };
  assert.equal(surfaceOpacity(widget, appearance), 0);
  widget.appearance = { background: '#112233' }; appearance.wallpaper = 'none';
  assert.equal(surfaceOpacity(widget, appearance), 100);
});
test('appearance and per-widget colors persist without requiring a migration of existing layouts', () => {
  const memory = new Map(), storage = { getItem: key => memory.get(key) ?? null, setItem: (key, value) => memory.set(key, value) };
  const appearance = defaultAppearance(), pages = defaultLayouts();
  assert.equal(validateLayouts(pages), true);
  appearance.theme = 'ocean'; appearance.colors.unit = '#ccbbaa';
  appearance.customWallpaper = { name: '壁纸.png', src: 'data:image/webp;base64,YWJj' }; appearance.wallpaper = 'custom';
  pages[0].widgets[1].appearance = { background: '#223344', title: '#efefef', value: '#ffccaa', unit: '#aabbcc', opacity: 55 };
  assert.equal(saveAppearance(storage, appearance), null); assert.equal(saveLayouts(storage, pages), null);
  assert.deepEqual(loadAppearance(storage).appearance, appearance); assert.deepEqual(loadLayouts(storage).pages, pages);
  for (const invalid of [{ value: 'red;display:none' }, { opacity: -1 }, { opacity: 101 }, { unknown: '#ffffff' }]) {
    pages[0].widgets[1].appearance = invalid;
    assert.equal(validateLayouts(pages), false);
  }
});
test('invalid or unavailable appearance storage recovers safely and rejects external wallpaper sources', () => {
  const memory = new Map(), storage = { getItem: key => memory.get(key) ?? null, setItem: (key, value) => memory.set(key, value) };
  for (const mutate of [
    a => { a.theme = '__proto__'; }, a => { a.colors.title = 'url(https://example.com)'; },
    a => { a.wallpaper = 'custom'; }, a => { a.cardOpacity = 150; },
    a => { a.customWallpaper = { name: 'remote', src: 'https://example.com/photo.png' }; },
  ]) {
    const appearance = defaultAppearance(); mutate(appearance);
    assert.match(saveAppearance(storage, appearance), /无效/);
    memory.set(APPEARANCE_KEY, JSON.stringify({ version: 1, appearance }));
    assert.equal(loadAppearance(storage).recovered, true);
  }
  memory.set(APPEARANCE_KEY, '{bad json'); assert.equal(loadAppearance(storage).recovered, true);
  const denied = { getItem() { throw new Error('denied'); }, setItem() { throw new Error('quota'); } };
  assert.equal(loadAppearance(denied).recovered, true);
  assert.match(saveAppearance(denied, defaultAppearance()), /空间不足/);
});
