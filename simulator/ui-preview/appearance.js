export const APPEARANCE_KEY = 'bry88bba15g.ui-appearance.v1';
export const COLOR_ROLES = {
  canvas: '屏幕背景', background: '组件背景', title: '标题', value: '数值', unit: '单位与次要文字',
  body: '正文与图例', accent: '强调色', line: '分隔线', chartA: '图表一', chartB: '图表二', chartC: '图表三', danger: '异常提示',
};
const palette = (canvas, background, title, value, unit, body, accent, line, chartB, chartC, danger) =>
  ({ canvas, background, title, value, unit, body, accent, line, chartA: accent, chartB, chartC, danger });
export const THEMES = {
  forest: { name: '森绿', colors: palette('#101611', '#1b251d', '#d9e5d0', '#f0f3e8', '#b1bbab', '#e1e9db', '#b7dca5', '#354335', '#8ebfcd', '#dcb981', '#efab91') },
  ink: { name: '深墨', colors: palette('#101216', '#20242b', '#d8dce4', '#f4f5f7', '#a7afbd', '#e0e4ec', '#c5d3e8', '#3b424f', '#96c9c0', '#d8b79a', '#f1a6a6') },
  ocean: { name: '海蓝', colors: palette('#0c1721', '#162d3d', '#c5e1ed', '#edf8ff', '#a1bdcf', '#d5e8f2', '#8fd9ef', '#2e4e61', '#a8cf9d', '#ecc393', '#ffb4a0') },
  amber: { name: '暖杏', colors: palette('#211811', '#35271d', '#e9d4bd', '#fff2df', '#c5ae98', '#ecdfce', '#efc28c', '#554232', '#a6c4b2', '#b8b5df', '#f4a59c') },
  plum: { name: '紫雾', colors: palette('#1b1523', '#2d233a', '#e0d1ed', '#f8efff', '#b9a8c8', '#e8dcee', '#d5b5f0', '#4b3b5b', '#99cbd2', '#e4c297', '#f5a4b5') },
  paper: { name: '纸白', colors: palette('#eeeee6', '#fcfcf6', '#394536', '#1c2a1c', '#5c6857', '#34412f', '#426c3a', '#cbd1c1', '#346f86', '#9c682c', '#a13828') },
};
export const WALLPAPERS = {
  none: { name: '纯色', image: null },
  mountains: { name: '暮山', image: './wallpapers/mountains.svg' },
  aurora: { name: '极光', image: './wallpapers/aurora.svg' },
  dunes: { name: '沙丘', image: './wallpapers/dunes.svg' },
};
export const WIDGET_COLOR_ROLES = {
  clock: ['background', 'value', 'unit'], metric: ['background', 'title', 'value', 'unit'],
  text: ['background', 'title', 'body'], chart: ['background', 'title', 'value', 'unit', 'body'],
  weather: ['background', 'title', 'value', 'unit', 'body'], reminders: ['background', 'title', 'value', 'unit', 'body'],
};
const isObject = value => value !== null && typeof value === 'object' && !Array.isArray(value);
export const isColor = value => typeof value === 'string' && /^#[0-9a-f]{6}$/i.test(value);
const percentage = (value, max = 100) => Number.isInteger(value) && value >= 0 && value <= max;
export function defaultAppearance() {
  return { theme: 'forest', colors: {}, wallpaper: 'none', customWallpaper: null, cardOpacity: 80, wallpaperDim: 24, pageRailHeight: 12 };
}
export function validateWidgetAppearance(value) {
  if (value === undefined) return true; // Existing saved layouts inherit their theme.
  return isObject(value) && Object.entries(value).every(([key, color]) =>
    key === 'opacity' ? percentage(color) : ['background', 'title', 'value', 'unit', 'body'].includes(key) && isColor(color));
}
export function validateAppearance(value) {
  if (!isObject(value) || !Object.hasOwn(THEMES, value.theme) || !isObject(value.colors) ||
      !Object.entries(value.colors).every(([key, color]) => Object.hasOwn(COLOR_ROLES, key) && isColor(color)) ||
      !percentage(value.cardOpacity) || !percentage(value.wallpaperDim, 70) || ![6,12,20].includes(value.pageRailHeight??12)) return false;
  const image = value.customWallpaper;
  if (image !== null && (!isObject(image) || typeof image.name !== 'string' || image.name.length > 100 ||
      typeof image.src !== 'string' || image.src.length > 800000 || !/^data:image\/(?:png|jpeg|webp);base64,[a-zA-Z0-9+/]+={0,2}$/.test(image.src))) return false;
  return value.wallpaper === 'custom' ? image !== null : Object.hasOwn(WALLPAPERS, value.wallpaper);
}
export function resolveTheme(appearance) { return { ...THEMES[appearance.theme].colors, ...appearance.colors }; }
export function surfaceOpacity(widget, appearance) {
  const custom = widget.appearance || {};
  if (custom.opacity !== undefined) return custom.opacity;
  if (appearance.wallpaper !== 'none') return appearance.cardOpacity;
  return custom.background || (widget.type !== 'clock' && widget.h < 4) ? 100 : 0;
}
export function widgetAppearanceStyle(widget, appearance) {
  const custom = widget.appearance || {}, colors = resolveTheme(appearance);
  return ['background', 'title', 'value', 'unit', 'body'].map(role => `--widget-${role}:${custom[role] || colors[role]}`).join(';') +
    `;--widget-opacity:${surfaceOpacity(widget, appearance)}%`;
}
export function wallpaperImage(appearance) {
  return appearance.wallpaper === 'custom' ? appearance.customWallpaper?.src : WALLPAPERS[appearance.wallpaper]?.image;
}
export function loadAppearance(storage) {
  try {
    const raw = storage.getItem(APPEARANCE_KEY);
    if (!raw) return { appearance: defaultAppearance(), recovered: false };
    const data = JSON.parse(raw);
    if (data.version !== 1 || !validateAppearance(data.appearance)) throw new Error('Invalid appearance');
    return { appearance: data.appearance, recovered: false };
  } catch { return { appearance: defaultAppearance(), recovered: true }; }
}
export function saveAppearance(storage, appearance) {
  if (!validateAppearance(appearance)) return '配色或壁纸设置无效，未保存。';
  try { storage.setItem(APPEARANCE_KEY, JSON.stringify({ version: 1, appearance })); return null; }
  catch { return '浏览器未允许存储或空间不足；本次配色与壁纸仍可预览。'; }
}
