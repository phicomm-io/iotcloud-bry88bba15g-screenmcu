import { PAGE_DEFS } from './model.js';
import { validateWidgetAppearance } from './appearance.js';

export const LAYOUT_KEY = 'bry88bba15g.ui-layout.v1';
export const BINDINGS = {
  co2: { title: 'CO₂', unit: 'ppm' }, pm: { title: 'PM2.5', unit: 'µg/m³' },
  temp: { title: '温度', unit: '°C' }, hum: { title: '湿度', unit: '%' },
  tvoc: { title: 'TVOC', unit: '' }, hcho: { title: '甲醛', unit: '' },
  power: { title: '当前功率', unit: 'W' },
};
export const WIDGET_DEFS = {
  clock: { title: '时钟', symbol: '◷', w: 4, h: 2, minW: 2, minH: 2 },
  metric: { title: '数值', symbol: '↗', w: 2, h: 2, minW: 2, minH: 1 },
  text: { title: '文字', symbol: 'Aa', w: 2, h: 2, minW: 2, minH: 2 },
  chart: { title: '图表', symbol: '▥', w: 4, h: 2, minW: 4, minH: 2 },
  weather: { title: '天气', symbol: '☼', w: 2, h: 2, minW: 2, minH: 2 },
  reminders: { title: '提醒', symbol: '☷', w: 2, h: 2, minW: 2, minH: 2 },
};
const chartTypes = ['line', 'multi', 'dual', 'bar', 'stacked-bar', 'stacked-area', 'donut'];
export function makeWidget(type, id) {
  const def = WIDGET_DEFS[type];
  return { id, type, title: type === 'metric' ? BINDINGS.co2.title : def.title, x: 0, y: 0, w: def.w, h: def.h,
    binding: 'co2', text: '保持通风，\n让空气流动起来。', chartType: 'line', range: '24' };
}
export function defaultLayouts() {
  const item = (id, type, x, y, w, h, extra = {}) => ({ ...makeWidget(type, id), x, y, w, h, ...extra });
  return PAGE_DEFS.map(page => ({ ...page, widgets: page.id === 'clock' ? [
    item('clock-time', 'clock', 0, 0, 4, 2),
    item('clock-co2', 'metric', 0, 2, 2, 2, { binding: 'co2', title: 'CO₂' }),
    item('clock-pm', 'metric', 2, 2, 2, 2, { binding: 'pm', title: 'PM2.5' }),
    item('clock-temp', 'metric', 0, 4, 2, 2, { binding: 'temp', title: '温度' }),
    item('clock-hum', 'metric', 2, 4, 2, 2, { binding: 'hum', title: '湿度' }),
  ] : page.id === 'air' ? ['co2', 'pm', 'temp', 'hum', 'tvoc', 'hcho'].map((binding, i) => item(`air-${binding}`, 'metric', (i % 2) * 2, Math.floor(i / 2) * 2, 2, 2, { binding, title: BINDINGS[binding].title }))
    : page.id === 'trend' ? [item('trend-chart', 'chart', 0, 0, 4, 6)]
    : page.id === 'weather' ? [item('weather-main', 'weather', 0, 0, 4, 6)]
    : page.id === 'reminders' ? [item('reminders-main', 'reminders', 0, 0, 4, 6)]
    : [item('external-main', 'metric', 0, 0, 4, 6, { binding: 'power', title: '当前功率' })] }));
}
export function minimumHeight(widget) {
  return widget.type === 'metric' && widget.w < 4 ? 2 : WIDGET_DEFS[widget.type]?.minH;
}
export function fits(page, widget, ignoreId = widget.id) {
  const def = WIDGET_DEFS[widget.type];
  if (!def || ![widget.x, widget.y, widget.w, widget.h].every(Number.isInteger) || widget.x < 0 || widget.y < 0 || widget.w < def.minW || widget.h < minimumHeight(widget) || widget.x + widget.w > 4 || widget.y + widget.h > 6) return false;
  return !page.widgets.some(other => other.id !== ignoreId && widget.x < other.x + other.w && widget.x + widget.w > other.x && widget.y < other.y + other.h && widget.y + widget.h > other.y);
}
export function findSpace(page, widget) {
  for (let y = 0; y <= 6 - widget.h; y++) for (let x = 0; x <= 4 - widget.w; x++) {
    const candidate = { ...widget, x, y };
    if (fits(page, candidate)) return candidate;
  }
  return null;
}
export function validateLayouts(pages) {
  if (!Array.isArray(pages) || !pages.length || pages.length > 8) return false;
  if (pages.reduce((total, page) => total + (Array.isArray(page?.widgets) ? page.widgets.length : 0), 0) > 32) return false;
  const ids = new Set(), widgetIds = new Set();
  return pages.every(page => {
    if (!page || typeof page.id !== 'string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(page.id) || ids.has(page.id) || typeof page.title !== 'string' || !page.title.trim() || page.title.length > 16 || !Array.isArray(page.widgets) || page.widgets.length > 8) return false;
    ids.add(page.id);
    return page.widgets.every(widget => {
      if (!widget || typeof widget.id !== 'string' || !/^[a-zA-Z0-9_-]{1,80}$/.test(widget.id) || widgetIds.has(widget.id) || typeof widget.title !== 'string' || widget.title.length > 24 || typeof widget.text !== 'string' || widget.text.length > 300 || !BINDINGS[widget.binding] || !chartTypes.includes(widget.chartType) || !['1', '6', '24'].includes(widget.range) || !fits(page, widget)) return false;
      if (!validateWidgetAppearance(widget.appearance)) return false;
      widgetIds.add(widget.id); return true;
    });
  });
}
export function loadLayouts(storage) {
  try {
    const raw = storage.getItem(LAYOUT_KEY);
    if (!raw) return { pages: defaultLayouts(), recovered: false };
    const data = JSON.parse(raw);
    if (data.version !== 1 || !validateLayouts(data.pages)) throw new Error('Invalid layout');
    return { pages: data.pages, recovered: false };
  } catch { return { pages: defaultLayouts(), recovered: true }; }
}
export function saveLayouts(storage, pages) {
  if (!validateLayouts(pages)) return '页面布局无效，未保存。';
  try { storage.setItem(LAYOUT_KEY, JSON.stringify({ version: 1, pages })); return null; }
  catch { return '浏览器未允许存储；本次布局仍可预览。'; }
}
