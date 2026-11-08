import {
  DAY_LABELS, MINUTE, defaultPolicy, wallTime, timeInput, timeLabel,
  loadPolicy, savePolicy, validatePolicy, activePlan, evaluateDisplay,
  CHART_DESCRIPTIONS,
} from './model.js';
import { escapeHTML as esc } from './charts.js';
import { WIDGET_DEFS, BINDINGS, makeWidget, defaultLayouts, minimumHeight, fits, findSpace, loadLayouts, saveLayouts } from './layout.js';
import { widgetMarkup } from './widgets.js';
import { COLUMN_PITCH, ROW_PITCH, geometryStyle } from './geometry.js';
import {
  COLOR_ROLES, THEMES, WALLPAPERS, WIDGET_COLOR_ROLES, defaultAppearance, resolveTheme,
  surfaceOpacity, widgetAppearanceStyle, wallpaperImage, loadAppearance, saveAppearance,
} from './appearance.js';

const $ = selector => document.querySelector(selector);
for (const [name, value] of Object.entries(geometryStyle)) $('#device-screen').style.setProperty(name, value);
const storage = { getItem: key => window.localStorage.getItem(key), setItem: (key, value) => window.localStorage.setItem(key, value) };
const loaded = loadPolicy(storage);
let policy = loaded.policy;
const loadedLayouts = loadLayouts(storage);
let pages = loadedLayouts.pages;
const loadedAppearance = loadAppearance(storage);
let appearance = loadedAppearance.appearance;
let wallpaperRequest = 0;
const layoutHistory = [];
const initialTime = wallTime('2026-11-06T18:42');
const state = {
  page: pages[0].id, editing: false, selectedWidget: null, now: initialTime, lastActivity: initialTime, supply: 'external',
  wakeOccurrence: null, dataState: 'normal', chartType: 'line', chartRange: '24',
  externalType: 'value', simulateBrightness: true, size: 'large', calibration: 1,
};
let clickTimer, dirty = false, lastDisplay;
const svg = (body, extra = '', viewBox = '0 0 48 48') => `<svg viewBox="${viewBox}" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" ${extra}>${body}</svg>`;
const icons = {
  wifi: svg('<path d="M1.86 12.86a20 20 0 0 1 28.28 0M6.1 17.1a14 14 0 0 1 19.8 0M10.34 21.34a8 8 0 0 1 11.32 0"/><circle cx="16" cy="27" r="1.5" fill="currentColor" stroke="none"/>', 'class="network"', '0 0 32 32'),
  offline: svg('<path d="M1.86 12.86a20 20 0 0 1 28.28 0M6.1 17.1a14 14 0 0 1 19.8 0M10.34 21.34a8 8 0 0 1 11.32 0"/><circle cx="16" cy="27" r="1.5" fill="currentColor" stroke="none"/><path d="m3 4 26 25" stroke-width="2"/>', 'class="network offline"', '0 0 32 32'),
  battery: svg('<rect y="2" width="72" height="27" rx="5" fill="var(--screen-line)"/><rect x="3" y="5" width="51" height="21" rx="2" fill="var(--chart-a)"/><rect x="73" y="9" width="5" height="13" rx="1" fill="var(--screen-line)"/>', 'class="battery"', '0 0 78 32'),
  charging: svg('<rect y="2" width="72" height="27" rx="5" fill="var(--screen-line)"/><rect x="3" y="5" width="51" height="21" rx="2" fill="var(--chart-a)"/><rect x="73" y="9" width="5" height="13" rx="1" fill="var(--screen-line)"/><rect x="27" y="5" width="18" height="21" rx="2" fill="var(--screen-line)"/><path d="m38 7-6 9 7-2-6 9" fill="none" stroke="var(--screen-fg)" stroke-width="3"/>', 'class="battery" data-charging="true"', '0 0 78 32'),
  sun: svg('<circle cx="24" cy="24" r="9"/><path d="M24 3v5m0 32v5M3 24h5m32 0h5M9 9l4 4m22 22 4 4M9 39l4-4M35 13l4-4"/>'),
  cloud: svg('<path d="M13 36a9 9 0 1 1 1-18 12 12 0 0 1 23-1 10 10 0 0 1-1 19Z"/>'),
  partly: svg('<circle cx="17" cy="16" r="8"/><path d="M17 2v3M3 16h3M6 5l3 3m17-3-3 3M15 38a7 7 0 0 1-1-14 10 10 0 0 1 19-2 8 8 0 0 1 2 16Z"/>'),
  moon: svg('<path d="M38 29A17 17 0 0 1 19 7a17 17 0 1 0 19 22Z"/>'),
  flash: svg('<path d="m27 3-17 24h13l-2 18 17-26H25Z"/>'),
};

function display() { return evaluateDisplay({ policy, pages, ...state }); }
function values() {
  if (state.dataState === 'missing') return { co2: '--', pm: '--', temp: '--', hum: '--', tvoc: '--', hcho: '--', power: '--' };
  if (state.dataState === 'extreme') return { co2: '9999', pm: '999', temp: '-10.5', hum: '100', tvoc: '差', hcho: '--', power: '12560' };
  return { co2: '639', pm: '12', temp: '23.6', hum: '48', tvoc: '优', hcho: '--', power: '468' };
}
function statusRow(page) {
  const title = page.widgets.some(widget => widget.type === 'clock' && widget.y === 0) ? '' : page.title;
  return `<div class="screen-top"><span class="screen-page-title">${esc(title)}</span>${state.dataState === 'stale' ? '<span class="data-note">已过期</span>' : ''}<span class="screen-icons">${lastDisplay?.sleeping ? icons.moon : ''}${state.dataState === 'missing' ? icons.offline : icons.wifi}${state.supply === 'external' ? icons.charging : icons.battery}</span></div>`;
}
function pageRail(page) { return `<div class="screen-page-rail" role="img" aria-label="第 ${pages.findIndex(p => p.id === page) + 1} 页，共 ${pages.length} 页">${pages.map(p => `<i data-page-segment="${p.id}"${p.id === page ? ' class="active"' : ''}></i>`).join('')}</div>`; }
function widgetContents(widget, index) {
  return `${widgetMarkup(widget, { state, values: values(), icons })}<span class="widget-edit-index" aria-hidden="true">${index + 1}</span><span class="resize-grip" data-resize aria-hidden="true"></span>`;
}
function fitReadouts(root = $('#screen-content')) {
  const scale = $('#device-screen').getBoundingClientRect().width / 480;
  for (const element of root.querySelectorAll('[data-fit-readout]')) {
    // Keep the largest type that fits the actual font, including signs and decimals.
    const range = document.createRange(); range.selectNodeContents(element);
    for (let attempt = 0; attempt < 2; attempt++) {
      const width = range.getBoundingClientRect().width / scale, available = element.clientWidth;
      if (!width || width <= available) break;
      const font = parseFloat(getComputedStyle(element).fontSize);
      element.style.fontSize = `${Math.floor(font * (available - 1) / width)}px`;
    }
  }
}
function renderScreen() {
  lastDisplay = display();
  const page = lastDisplay.page;
  const definition = pages.find(p => p.id === page);
  const image = wallpaperImage(appearance);
  $('#screen-content').style.backgroundImage = image ? `url("${image}")` : 'none';
  $('#screen-content').style.setProperty('--wallpaper-dim', String(appearance.wallpaperDim / 100));
  $('#screen-content').style.setProperty('--page-rail-height', `${appearance.pageRailHeight??12}px`);
  $('#screen-content').style.setProperty('--page-title-offset', `${Math.max(0,(appearance.pageRailHeight??12)+2-16)}px`);
  $('#screen-content').classList.toggle('wallpaper-active', Boolean(image));
  $('#screen-content').innerHTML = `<section class="screen-page ${state.dataState === 'stale' ? 'data-stale' : ''}" data-page="${page}">${pageRail(page)}${statusRow(definition)}<div class="screen-widget-grid">${definition.widgets.map((widget, index) => `<div class="ui-widget widget-${widget.type}${widget.w === 4 ? ' widget-wide' : ''}${widget.h >= 4 ? ' widget-tall' : ''}${state.selectedWidget === widget.id ? ' selected' : ''}" data-widget-id="${widget.id}" style="grid-column:${widget.x + 1} / span ${widget.w};grid-row:${widget.y + 1} / span ${widget.h};${widgetAppearanceStyle(widget, appearance)}">${widgetContents(widget, index)}</div>`).join('')}</div></section>`;
  fitReadouts();
  $('#device-screen').classList.toggle('editing', state.editing && !lastDisplay.sleeping);
  $('#edit-layout').setAttribute('aria-pressed', String(state.editing));
  $('#editing-mode').checked = state.editing;
  $('.interaction-hint').textContent = state.editing ? '点选组件 · 拖动排列 · 下方圆键模拟触摸' : '单击翻页 · 双击回首页 · 休眠时首触唤醒';
  $('#device-screen').style.filter = state.simulateBrightness ? `brightness(${lastDisplay.brightness / 100})` : 'none';
  $('#device-screen').style.backgroundColor = lastDisplay.brightness === 0 ? '#000' : 'var(--screen-bg)';
  // Zero is an actual dark display even with visual brightness simulation disabled.
  $('#screen-content').style.visibility = lastDisplay.brightness === 0 ? 'hidden' : 'visible';
  $('#device-screen').dataset.sleeping = String(lastDisplay.sleeping);
  $('#device-screen').dataset.page = page;
  $('#device-screen').dataset.brightness = String(lastDisplay.brightness);
  $('#preview-title').textContent = definition.title;
  const status = lastDisplay.sleeping ? lastDisplay.reason === 'plan' ? `计划休眠 · ${lastDisplay.planName}` : '无操作休眠' : lastDisplay.temporaryWake ? '临时唤醒' : '正常显示';
  $('#display-state').textContent = status;
  $('#display-brightness').textContent = lastDisplay.brightness === 0 ? '熄屏' : `${lastDisplay.brightness}%`;
  $('#state-dot').style.background = lastDisplay.sleeping ? '#a09570' : '#78945d';
  $('#page-counter').textContent = `${String(pages.findIndex(p => p.id === page) + 1).padStart(2, '0')} / ${String(pages.length).padStart(2, '0')}`;
  document.querySelectorAll('[data-page-button]').forEach(button => {
    const selected = button.dataset.pageButton === page;
    button.classList.toggle('selected', selected);
    button.setAttribute('aria-current', selected ? 'page' : 'false');
  });
  $('#simulated-time').value = timeInput(state.now);
  const pending = activePlan(policy.plans, state.now);
  $('#simulation-result').textContent = `${status} · ${definition.title}${lastDisplay.brightness ? ` · ${lastDisplay.brightness}%` : ' · 熄屏'}。${pending ? `当前命中「${pending.plan.name}」。` : '当前无定时计划。'}已闲置 ${Math.floor(lastDisplay.idleFor)} 分钟。`;
}
function wake() {
  state.lastActivity = state.now;
  state.wakeOccurrence = activePlan(policy.plans, state.now)?.occurrence || null;
}
function touch(direction = 1) {
  const wasSleeping = display().sleeping;
  wake();
  if (!wasSleeping) {
    const index = pages.findIndex(p => p.id === state.page);
    state.page = direction === 'home' ? pages[0].id : pages[(index + direction + pages.length) % pages.length].id;
  }
  state.selectedWidget = null;
  renderScreen(); renderComposer();
}
function selectPage(page) { wake(); state.page = page; state.selectedWidget = null; renderScreen(); renderComposer(); }
function queueTouch() { clearTimeout(clickTimer); clickTimer = setTimeout(() => touch(), 230); }
function doubleTouch() { clearTimeout(clickTimer); touch('home'); }
function makeNav() {
  $('#page-nav').innerHTML = pages.map((page, index) => `<button data-page-button="${page.id}"${index === 0 ? ' class="selected"' : ''}><span class="nav-number">${String(index + 1).padStart(2, '0')}</span>${esc(page.title)}<span class="nav-arrow" aria-hidden="true">↗</span></button>`).join('');
  $('#page-count').textContent = String(pages.length).padStart(2, '0');
  $('#add-page').disabled = pages.length >= 8;
}
function pageOptions(value) { return `<option value="current"${value === 'current' ? ' selected' : ''}>保持当前页面</option>${pages.map(page => `<option value="${page.id}"${value === page.id ? ' selected' : ''}>${esc(page.title)}</option>`).join('')}`; }
function displayFields(entry, prefix) {
  return `<div class="display-fields"><label class="field">休眠行为<select data-edit="action" id="${prefix}-action"><option value="dim"${entry.action === 'dim' ? ' selected' : ''}>指定亮度与页面</option><option value="off"${entry.action === 'off' ? ' selected' : ''}>完全熄屏</option></select></label><div data-dim-fields${entry.action === 'off' ? ' hidden' : ''}><label class="range-label" for="${prefix}-brightness"><span>休眠亮度</span><output>${entry.brightness}%</output></label><input type="range" min="1" max="100" value="${entry.brightness}" id="${prefix}-brightness" data-edit="brightness"><label class="field">休眠页面<select data-edit="page" id="${prefix}-page">${pageOptions(entry.page)}</select></label></div></div>`;
}
function renderPlanList() {
  $('#schedule-list').innerHTML = policy.plans.map((plan, index) => `<article class="schedule-card${plan.enabled ? '' : ' disabled'}" data-plan-id="${esc(plan.id)}"><div class="schedule-heading"><input type="checkbox" role="switch" data-edit="enabled" aria-label="启用${esc(plan.name)}"${plan.enabled ? ' checked' : ''}><input type="text" data-edit="name" maxlength="24" value="${esc(plan.name)}" aria-label="计划名称"><div class="schedule-actions"><button data-plan-action="up" aria-label="提高优先级"${index === 0 ? ' disabled' : ''}>↑</button><button data-plan-action="down" aria-label="降低优先级"${index === policy.plans.length - 1 ? ' disabled' : ''}>↓</button><button data-plan-action="remove" aria-label="删除计划">×</button></div></div><div class="plan-fields"><div class="days" aria-label="计划开始星期">${[1, 2, 3, 4, 5, 6, 0].map(day => `<button class="day-button" data-day="${day}" aria-pressed="${plan.days.includes(day)}" aria-label="星期${DAY_LABELS[day]}">${DAY_LABELS[day]}</button>`).join('')}</div><div class="two-fields"><label class="field">开始<input type="time" data-edit="start" value="${plan.start}" required aria-label="计划开始时间"></label><label class="field">结束<input type="time" data-edit="end" value="${plan.end}" required aria-label="计划结束时间"></label></div>${plan.start > plan.end ? '<p class="field-note">次日结束</p>' : ''}${displayFields(plan, `plan-${plan.id}`)}</div></article>`).join('');
  $('#add-plan').disabled = policy.plans.length >= 32;
}
function readTimeout(name, value) {
  const known = ['0', '1', '2', '5', '10', '30'];
  $(`#idle-${name}`).value = known.includes(String(value)) ? String(value) : 'custom';
  $(`#custom-${name}-field`).hidden = known.includes(String(value));
  if (!known.includes(String(value))) $(`#custom-${name}`).value = value;
}
function renderPolicy() {
  $('#awake-brightness').value = policy.awakeBrightness;
  $('#awake-brightness-value').textContent = `${policy.awakeBrightness}%`;
  readTimeout('external', policy.idle.externalMinutes);
  readTimeout('battery', policy.idle.batteryMinutes);
  $('#idle-display-fields').innerHTML = displayFields(policy.idle, 'idle');
  renderPlanList();
}
function policyChanged() {
  dirty = true;
  const error = validatePolicy(policy);
  $('#policy-message').textContent = error || '有未保存的调整 · 画面已预览';
  $('#policy-message').classList.toggle('error', Boolean(error));
  $('#save-policy').disabled = Boolean(error);
  // Invalid drafts remain editable but do not enter the rendering rules.
  if (!error) renderScreen();
}
function updateDisplayFields(target, entry, root) {
  const key = target.dataset.edit;
  if (!key || !['action', 'brightness', 'page'].includes(key)) return false;
  entry[key] = key === 'brightness' ? Number(target.value) : target.value;
  if (key === 'brightness') target.previousElementSibling.querySelector('output').textContent = `${entry.brightness}%`;
  if (key === 'action') root.querySelector('[data-dim-fields]').hidden = entry.action === 'off';
  return true;
}
function changePanel(name) {
  for (const panel of ['widgets', 'display', 'sleep']) {
    const selected = panel === name;
    $(`#tab-${panel}`).setAttribute('aria-selected', String(selected));
    $(`#tab-${panel}`).tabIndex = selected ? 0 : -1;
    $(`#panel-${panel}`).hidden = !selected;
  }
}
function appearanceMessage(message, error = false) {
  $('#appearance-message').textContent = message;
  $('#appearance-message').classList.toggle('error', error);
}
function applyTheme() {
  const colors = resolveTheme(appearance);
  const variables = { canvas: 'screen-bg', background: 'screen-panel', body: 'screen-fg', unit: 'screen-muted', accent: 'screen-accent', line: 'screen-line', chartA: 'chart-a', chartB: 'chart-b', chartC: 'chart-c', danger: 'screen-danger' };
  for (const [role, variable] of Object.entries(variables)) document.documentElement.style.setProperty(`--${variable}`, colors[role]);
}
function colorControl(role, color, custom, scope) {
  const id = `${scope}-color-${role}`;
  return `<div class="color-control" data-color-row="${role}"><label for="${id}">${COLOR_ROLES[role]}<code>${color.toUpperCase()}</code></label><input type="color" id="${id}" data-${scope}-color="${role}" value="${color}" aria-label="${scope === 'widget' ? '组件' : '主题'}${COLOR_ROLES[role]}颜色"><button data-${scope}-color-reset="${role}" aria-label="${COLOR_ROLES[role]}${scope === 'widget' ? '恢复跟随主题' : '恢复预设'}"${custom ? '' : ' disabled'}>${scope === 'widget' ? custom ? '跟随主题' : '使用主题' : custom ? '还原' : '预设'}</button></div>`;
}
function renderAppearance() {
  $('#page-rail-height').value=appearance.pageRailHeight??12;
  const colors = resolveTheme(appearance);
  $('#theme-name').textContent = `${THEMES[appearance.theme].name}${Object.keys(appearance.colors).length ? ' · 自定义' : ''}`;
  $('#theme-presets').innerHTML = Object.entries(THEMES).map(([id, theme]) => `<button data-theme="${id}" aria-pressed="${appearance.theme === id}"><span class="theme-swatches" aria-hidden="true">${['canvas', 'accent', 'chartB', 'chartC'].map(role => `<i style="--swatch:${theme.colors[role]}"></i>`).join('')}</span>${theme.name}</button>`).join('');
  $('#theme-colors').innerHTML = Object.keys(COLOR_ROLES).map(role => colorControl(role, colors[role], Object.hasOwn(appearance.colors, role), 'theme')).join('');
  $('#reset-theme-colors').disabled = !Object.keys(appearance.colors).length;
  $('#wallpaper-presets').innerHTML = Object.entries(WALLPAPERS).map(([id, wallpaper]) => `<button data-wallpaper="${id}" aria-pressed="${appearance.wallpaper === id}"><span class="wallpaper-thumb"${wallpaper.image ? ` style="background-image:url('${wallpaper.image}')"` : ''} aria-hidden="true"></span>${wallpaper.name}</button>`).join('') + (appearance.customWallpaper ? `<button data-wallpaper="custom" aria-pressed="${appearance.wallpaper === 'custom'}"><span class="wallpaper-thumb" style="background-image:url('${appearance.customWallpaper.src}')" aria-hidden="true"></span>自选</button>` : '');
  $('#wallpaper-options').hidden = appearance.wallpaper === 'none';
  $('#remove-wallpaper').hidden = !appearance.customWallpaper;
  $('#wallpaper-filename').hidden = !appearance.customWallpaper;
  $('#wallpaper-filename').textContent = appearance.customWallpaper?.name || '';
  for (const [id, key] of [['card-opacity', 'cardOpacity'], ['wallpaper-dim', 'wallpaperDim']]) {
    $(`#${id}`).value = appearance[key]; $(`#${id}-value`).textContent = `${appearance[key]}%`;
  }
}
function appearanceChanged(refreshFields = true) {
  const error = saveAppearance(storage, appearance);
  applyTheme(); renderScreen();
  if (refreshFields) { renderAppearance(); renderComposer(); }
  appearanceMessage(error || '配色与壁纸已保存在此浏览器', Boolean(error));
}
function widgetAppearanceFields(widget) {
  const custom = widget.appearance || {}, colors = resolveTheme(appearance), inherited = custom.opacity === undefined;
  return `<div class="widget-appearance"><div class="section-title"><h3>组件配色</h3><button id="reset-widget-colors" class="text-button"${Object.keys(custom).length ? '' : ' disabled'}>全部跟随主题</button></div><div class="color-controls">${WIDGET_COLOR_ROLES[widget.type].map(role => colorControl(role, custom[role] || colors[role], Object.hasOwn(custom, role), 'widget')).join('')}</div><label class="range-label" for="widget-opacity"><span>背景不透明度</span><output id="widget-opacity-value">${surfaceOpacity(widget, appearance)}%</output></label><input id="widget-opacity" type="range" min="0" max="100" value="${surfaceOpacity(widget, appearance)}"><div class="opacity-inheritance"><label><input type="checkbox" id="widget-opacity-inherit"${inherited ? ' checked' : ''}>跟随页面</label><span>文字保持不透明</span></div></div>`;
}
function updateWidgetAppearance(key, value, refreshFields = true) {
  const widget = selectedWidget(); if (!widget) return;
  rememberLayout();
  const custom = { ...widget.appearance };
  if (value === undefined) delete custom[key]; else custom[key] = value;
  if (Object.keys(custom).length) widget.appearance = custom; else delete widget.appearance;
  finishLayout('组件配色已保存，未自定义的颜色跟随主题', refreshFields);
}
function resizeScreen() {
  if (state.size === 'physical') {
    // 35 mm width at CSS reference density; height follows 480:854 (about 62.3 mm).
    // The user-supplied 35 × 65 mm is approximate; never stretch the pixel canvas.
    const scale = 35 * 96 / 25.4 / 480 * state.calibration;
    $('#device-area').style.setProperty('--screen-scale', String(scale));
  } else $('#device-area').style.removeProperty('--screen-scale');
  document.documentElement.style.setProperty('--calibration', String(state.calibration));
  $('#calibration').hidden = state.size !== 'physical';
  document.querySelectorAll('[data-size]').forEach(button => {
    const selected = button.dataset.size === state.size;
    button.classList.toggle('selected', selected);
    button.setAttribute('aria-pressed', String(selected));
  });
}

const currentPage = () => pages.find(page => page.id === state.page);
const selectedWidget = () => currentPage()?.widgets.find(widget => widget.id === state.selectedWidget);
const newId = prefix => `${prefix}-${globalThis.crypto?.randomUUID?.() || `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`}`;
function layoutMessage(message, error = false) { $('#layout-message').textContent = message; $('#layout-message').classList.toggle('error', error); }
function rememberLayout() { layoutHistory.push(JSON.stringify(pages)); if (layoutHistory.length > 20) layoutHistory.shift(); }
function reconcileSleepPages() {
  let changed = false;
  for (const entry of [policy.idle, ...policy.plans]) if (entry.page !== 'current' && !pages.some(page => page.id === entry.page)) { entry.page = 'current'; changed = true; }
  return changed;
}
function finishLayout(message = '布局已保存在此浏览器', updateFields = true) {
  const error = saveLayouts(storage, pages);
  if (reconcileSleepPages()) { dirty = true; $('#policy-message').textContent = '引用已删除页面的休眠设置已改为保持当前页，请保存。'; }
  makeNav(); renderScreen(); renderPolicy();
  if (updateFields) renderComposer();
  $('#undo-layout').disabled = !layoutHistory.length;
  layoutMessage(error || message, Boolean(error));
}
function editing(enabled = true) {
  if (enabled) { const visible = display().page; wake(); state.page = visible; }
  state.editing = enabled;
  if (enabled && !selectedWidget()) state.selectedWidget = currentPage().widgets[0]?.id || null;
  renderScreen(); renderComposer();
  if (enabled) changePanel('widgets');
}
function renderComposer() {
  const page = currentPage(); if (!page) return;
  if (document.activeElement !== $('#page-name')) $('#page-name').value = page.title;
  $('#remove-page').disabled = pages.length <= 1;
  $('#undo-layout').disabled = !layoutHistory.length;
  const widget = selectedWidget();
  $('#remove-widget').disabled = !widget;
  if (!widget) { $('#widget-properties').innerHTML = '<p class="field-note">进入编辑模式，点选屏幕上的小组件；或从上方添加一个。</p>'; return; }
  const def = WIDGET_DEFS[widget.type];
  const minH = minimumHeight(widget);
  const select = (key, label, values) => `<label class="field">${label}<select data-widget-edit="${key}" id="widget-${key}">${values.map(([value, text]) => `<option value="${value}"${String(widget[key]) === String(value) ? ' selected' : ''}>${esc(text)}</option>`).join('')}</select></label>`;
  let specific = widget.type === 'metric' ? select('binding', '数据', Object.entries(BINDINGS).map(([id, binding]) => [id, binding.title]))
    : widget.type === 'text' ? `<label class="field">文字内容<textarea data-widget-edit="text" id="widget-text" maxlength="300">${esc(widget.text)}</textarea></label>`
    : widget.type === 'chart' ? select('chartType', '图表类型', [['line', '折线'], ['multi', '多曲线叠放'], ['dual', '多曲线 · 双单位'], ['bar', '柱状'], ['stacked-bar', '堆叠柱状'], ['stacked-area', '堆叠面积'], ['donut', '环形']]) + select('range', '时间范围', [['1', '1 小时'], ['6', '6 小时'], ['24', '24 小时']]) : '';
  const presets = widget.type === 'metric' ? [[2, 1], [2, 2], [3, 3], [4, 4], [4, 1], [4, 2]] : widget.type === 'chart' ? [[4, 2], [4, 4], [4, 6]] : widget.type === 'clock' ? [[4, 1], [4, 2], [2, 2]] : widget.type === 'weather' ? [[4, 1], [4, 2], [2, 2], [4, 6]] : [];
  const sizeNote = widget.type === 'metric' ? '方形等比放大；2 列或 4 列可缩至 1 行，左侧标题与单位、右侧读数。' : widget.type === 'chart' ? '2 行紧凑布局：左 1 列显示标题和读数，右 3 列显示图表。' : `最小 ${def.minW} 列 × ${minH} 行。`;
  $('#widget-properties').innerHTML = `<label class="field">${def.title} · 标题<input data-widget-edit="title" id="widget-title" type="text" maxlength="24" value="${esc(widget.title)}"></label>${specific}${presets.length ? `<div class="widget-presets" aria-label="常用尺寸">${presets.map(([w, h]) => `<button data-widget-size="${w},${h}" aria-pressed="${widget.w === w && widget.h === h}">${w} 列 × ${h} 行</button>`).join('')}</div>` : ''}<div class="two-fields">${select('w', '宽度', Array.from({ length: 5 - def.minW }, (_, i) => [i + def.minW, `${i + def.minW} 格`]))}${select('h', '高度', Array.from({ length: 7 - minH }, (_, i) => [i + minH, `${i + minH} 格`]))}</div><div class="two-fields">${select('x', '起始列', Array.from({ length: 5 - widget.w }, (_, i) => [i, `第 ${i + 1} 列`]))}${select('y', '起始行', Array.from({ length: 7 - widget.h }, (_, i) => [i, `第 ${i + 1} 行`]))}</div><p class="widget-size-note">${sizeNote} 不允许重叠或超出页面。</p>`;
  $('#widget-properties').insertAdjacentHTML('beforeend', widgetAppearanceFields(widget));
}
function addWidget(type) {
  editing(true);
  const page = currentPage();
  if (page.widgets.length >= 8) { layoutMessage('每页最多 8 个小组件。', true); return; }
  if (pages.reduce((total, page) => total + page.widgets.length, 0) >= 32) { layoutMessage('最多 32 个小组件，请先移除不需要的组件。', true); return; }
  const widget = findSpace(page, makeWidget(type, newId('widget')));
  if (!widget) { layoutMessage('空间不足。请先移除或缩小组件，也可以新建组合页面。', true); return; }
  rememberLayout(); page.widgets.push(widget); state.selectedWidget = widget.id;
  finishLayout('已添加，布局自动保存在此浏览器');
}
function updateWidget(key, value, updateFields = true) {
  const widget = selectedWidget(); if (!widget) return;
  const candidate = { ...widget, [key]: ['x', 'y', 'w', 'h'].includes(key) ? Number(value) : value };
  if (key === 'binding') candidate.title = BINDINGS[value].title;
  if (!fits(currentPage(), candidate)) { layoutMessage('该位置或尺寸会重叠或超出边界，请先腾出空间。', true); renderComposer(); return; }
  rememberLayout(); Object.assign(widget, candidate); finishLayout(undefined, updateFields);
}
function moveSelected(dx, dy) {
  const widget = selectedWidget(); if (!widget) return;
  const candidate = { ...widget, x: widget.x + dx, y: widget.y + dy };
  if (!fits(currentPage(), candidate)) { layoutMessage('不能移到组件或页面边界之外。', true); return; }
  rememberLayout(); Object.assign(widget, candidate); finishLayout();
}
function updateDemoChart() {
  let page = currentPage(), widget = selectedWidget();
  if (widget?.type !== 'chart') widget = page.widgets.find(item => item.type === 'chart');
  if (!widget) { page = pages.find(item => item.widgets.some(w => w.type === 'chart')); widget = page?.widgets.find(w => w.type === 'chart'); }
  if (!widget) { layoutMessage('请先添加一个图表小组件。', true); changePanel('widgets'); return; }
  rememberLayout(); widget.chartType = state.chartType; widget.range = state.chartRange; wake(); state.page = page.id;
  finishLayout();
}
function updateExternalDemo() {
  const page = pages.find(item => item.id === 'external');
  if (!page || page.widgets.length !== 1) { changePanel('widgets'); layoutMessage('使用数值或文字小组件组合外部内容。'); return; }
  const widget = page.widgets[0], type = state.externalType === 'chart' ? 'chart' : state.externalType === 'text' ? 'text' : 'metric';
  const candidate = { ...widget, type, binding: 'power', title: type === 'metric' ? '当前功率' : type === 'chart' ? '图表' : '留言', chartType: state.chartType, range: state.chartRange };
  if (!fits(page, candidate)) { changePanel('widgets'); layoutMessage('组件空间不足，请在编辑模式中调整。', true); return; }
  rememberLayout(); Object.assign(widget, candidate); wake(); state.page = page.id; finishLayout();
}

$('#widget-palette').innerHTML = Object.entries(WIDGET_DEFS).map(([id, def]) => `<button data-add-widget="${id}"><span aria-hidden="true">${def.symbol}</span>${def.title}</button>`).join('');
$('#widget-palette').addEventListener('click', event => { const button = event.target.closest('[data-add-widget]'); if (button) addWidget(button.dataset.addWidget); });
$('#edit-layout').addEventListener('click', () => editing(!state.editing));
$('#editing-mode').addEventListener('change', event => editing(event.target.checked));
$('#page-name').addEventListener('input', event => {
  const name = event.target.value.trim();
  if (!name) { layoutMessage('请输入页面名称。', true); return; }
  rememberLayout(); currentPage().title = name; finishLayout(undefined, false);
});
$('#add-page').addEventListener('click', () => {
  if (pages.length >= 8) return;
  rememberLayout();
  const page = { id: newId('custom'), title: `组合 ${pages.filter(p => p.id.startsWith('custom-')).length + 1}`, widgets: [] };
  pages.push(page); wake(); state.page = page.id; state.selectedWidget = null; state.editing = true;
  finishLayout('空白页面已创建，选择小组件开始组合'); changePanel('widgets');
});
$('#remove-widget').addEventListener('click', () => {
  if (!selectedWidget()) return;
  rememberLayout(); currentPage().widgets = currentPage().widgets.filter(widget => widget.id !== state.selectedWidget); state.selectedWidget = null;
  finishLayout('已移除，可撤销');
});
$('#remove-page').addEventListener('click', () => {
  if (pages.length <= 1) return;
  rememberLayout(); pages = pages.filter(page => page.id !== state.page); state.page = pages[0].id; state.selectedWidget = null;
  finishLayout('已删除页面，可撤销；引用此页的休眠设置改为保持当前页');
});
$('#undo-layout').addEventListener('click', () => {
  const previous = layoutHistory.pop(); if (!previous) return;
  pages = JSON.parse(previous);
  if (!pages.some(page => page.id === state.page)) state.page = pages[0].id;
  state.selectedWidget = null; finishLayout('已撤销上一次布局调整');
});
$('#widget-properties').addEventListener('input', event => {
  const key = event.target.dataset.widgetEdit;
  if (['title', 'text'].includes(key)) updateWidget(key, event.target.value, false);
  const role = event.target.dataset.widgetColor;
  if (role) {
    updateWidgetAppearance(role, event.target.value, false);
    const row = event.target.closest('.color-control'); row.querySelector('code').textContent = event.target.value.toUpperCase();
    row.querySelector('button').disabled = false; row.querySelector('button').textContent = '跟随主题';
    $('#reset-widget-colors').disabled = false;
  }
  if (event.target.id === 'widget-opacity') {
    updateWidgetAppearance('opacity', Number(event.target.value), false);
    $('#widget-opacity-value').textContent = `${event.target.value}%`; $('#widget-opacity-inherit').checked = false;
    $('#reset-widget-colors').disabled = false;
  }
});
$('#widget-properties').addEventListener('change', event => {
  const key = event.target.dataset.widgetEdit;
  if (key && !['title', 'text'].includes(key)) updateWidget(key, event.target.value);
  const role = event.target.dataset.widgetColor;
  if (role) {
    if (selectedWidget()?.appearance?.[role] !== event.target.value) updateWidgetAppearance(role, event.target.value);
    else renderComposer();
  }
  if (event.target.id === 'widget-opacity-inherit') updateWidgetAppearance('opacity', event.target.checked ? undefined : Number($('#widget-opacity').value));
  if (event.target.id === 'widget-opacity') {
    if (selectedWidget()?.appearance?.opacity !== Number(event.target.value)) updateWidgetAppearance('opacity', Number(event.target.value));
    else renderComposer();
  }
});
$('#widget-properties').addEventListener('click', event => {
  const reset = event.target.closest('[data-widget-color-reset]');
  if (reset) { updateWidgetAppearance(reset.dataset.widgetColorReset, undefined); return; }
  if (event.target.closest('#reset-widget-colors')) {
    const widget = selectedWidget(); if (!widget) return;
    rememberLayout(); delete widget.appearance; finishLayout('组件已恢复跟随主题'); return;
  }
  const button = event.target.closest('[data-widget-size]'), widget = selectedWidget();
  if (!button || !widget) return;
  const [w, h] = button.dataset.widgetSize.split(',').map(Number), candidate = { ...widget, w, h };
  if (!fits(currentPage(), candidate)) { layoutMessage('此尺寸会重叠或超出边界，请先腾出空间。', true); return; }
  rememberLayout(); Object.assign(widget, candidate); finishLayout();
});
let drag = null, ignoreDragClick = false;
$('#device-screen').addEventListener('pointerdown', event => {
  if (!state.editing || display().sleeping || event.button !== 0) return;
  const target = event.target.closest('[data-widget-id]'); if (!target) return;
  state.selectedWidget = target.dataset.widgetId; renderComposer();
  document.querySelectorAll('.ui-widget').forEach(item => item.classList.toggle('selected', item.dataset.widgetId === state.selectedWidget));
  drag = { id: state.selectedWidget, startX: event.clientX, startY: event.clientY, original: { ...selectedWidget() }, resize: Boolean(event.target.closest('[data-resize]')), moved: false, candidate: null };
  $('#device-screen').setPointerCapture(event.pointerId);
});
$('#device-screen').addEventListener('pointermove', event => {
  if (!drag) return;
  const scale = $('#device-screen').getBoundingClientRect().width / 480;
  const dx = Math.round((event.clientX - drag.startX) / (COLUMN_PITCH * scale)), dy = Math.round((event.clientY - drag.startY) / (ROW_PITCH * scale));
  if (!dx && !dy && !drag.moved) return;
  drag.moved = true;
  const def = WIDGET_DEFS[drag.original.type], original = drag.original;
  const w = Math.max(def.minW, Math.min(4 - original.x, original.w + dx));
  drag.candidate = { ...original, ...(drag.resize ? { w, h: Math.max(minimumHeight({ ...original, w }), Math.min(6 - original.y, original.h + dy)) } : { x: Math.max(0, Math.min(4 - original.w, original.x + dx)), y: Math.max(0, Math.min(6 - original.h, original.y + dy)) }) };
  const element = document.querySelector(`[data-widget-id="${drag.id}"]`); element.classList.add('dragging');
  if (drag.resize) {
    element.style.gridColumn = `${original.x + 1} / span ${drag.candidate.w}`; element.style.gridRow = `${original.y + 1} / span ${drag.candidate.h}`;
    element.classList.toggle('widget-wide', drag.candidate.w === 4); element.classList.toggle('widget-tall', drag.candidate.h >= 4);
    element.style.setProperty('--widget-opacity', `${surfaceOpacity(drag.candidate, appearance)}%`);
    element.innerHTML = widgetContents(drag.candidate, currentPage().widgets.findIndex(widget => widget.id === drag.id)); fitReadouts(element);
  }
  else element.style.transform = `translate(${(drag.candidate.x - original.x) * COLUMN_PITCH}px, ${(drag.candidate.y - original.y) * ROW_PITCH}px)`;
});
function endDrag(cancelled = false) {
  if (!drag) return;
  if (drag.moved) {
    ignoreDragClick = true; setTimeout(() => { ignoreDragClick = false; }, 300);
    if (!cancelled && drag.candidate && fits(currentPage(), drag.candidate)) { rememberLayout(); Object.assign(selectedWidget(), drag.candidate); finishLayout(); }
    else { renderScreen(); layoutMessage('位置冲突，已恢复原布局。', true); }
  }
  drag = null;
}
$('#device-screen').addEventListener('pointerup', () => endDrag());
$('#device-screen').addEventListener('pointercancel', () => endDrag(true));

applyTheme(); renderAppearance(); reconcileSleepPages(); makeNav(); renderPolicy(); renderScreen(); renderComposer();
$('#page-nav').addEventListener('click', event => { const button = event.target.closest('[data-page-button]'); if (button) selectPage(button.dataset.pageButton); });
if (loaded.recovered) $('#policy-message').textContent = '本地设置不可用，已使用默认演示。';
if (loadedLayouts.recovered) layoutMessage('本地布局不可用，已恢复默认页面。');
if (loadedAppearance.recovered) appearanceMessage('本地配色不可用，已恢复默认主题。', true);
$('#theme-presets').addEventListener('click', event => {
  const button = event.target.closest('[data-theme]'); if (!button) return;
  appearance.theme = button.dataset.theme; appearance.colors = {}; appearanceChanged();
});
$('#theme-colors').addEventListener('input', event => {
  const role = event.target.dataset.themeColor; if (!role) return;
  appearance.colors[role] = event.target.value; appearanceChanged(false);
  const row = event.target.closest('.color-control'); row.querySelector('code').textContent = event.target.value.toUpperCase();
  row.querySelector('button').disabled = false; row.querySelector('button').textContent = '还原';
  $('#reset-theme-colors').disabled = false;
});
$('#theme-colors').addEventListener('change', event => {
  const role = event.target.dataset.themeColor; if (!role) return;
  appearance.colors[role] = event.target.value; appearanceChanged();
});
$('#theme-colors').addEventListener('click', event => {
  const button = event.target.closest('[data-theme-color-reset]'); if (!button) return;
  delete appearance.colors[button.dataset.themeColorReset]; appearanceChanged();
});
$('#reset-theme-colors').addEventListener('click', () => { appearance.colors = {}; appearanceChanged(); });
$('#page-rail-height').addEventListener('change',event=>{appearance.pageRailHeight=Number(event.target.value);appearanceChanged();});
$('#wallpaper-presets').addEventListener('click', event => {
  const button = event.target.closest('[data-wallpaper]'); if (!button) return;
  wallpaperRequest++; appearance.wallpaper = button.dataset.wallpaper; appearanceChanged();
});
$('#remove-wallpaper').addEventListener('click', () => {
  wallpaperRequest++; appearance.customWallpaper = null;
  if (appearance.wallpaper === 'custom') appearance.wallpaper = 'none';
  appearanceChanged();
});
for (const [id, key] of [['card-opacity', 'cardOpacity'], ['wallpaper-dim', 'wallpaperDim']]) {
  $(`#${id}`).addEventListener('input', event => {
    appearance[key] = Number(event.target.value); $(`#${id}-value`).textContent = `${appearance[key]}%`; appearanceChanged(false);
  });
  $(`#${id}`).addEventListener('change', () => renderComposer());
}
$('#wallpaper-file').addEventListener('change', async event => {
  const file = event.target.files[0]; event.target.value = ''; if (!file) return;
  const request = ++wallpaperRequest;
  if (!['image/png', 'image/jpeg', 'image/webp'].includes(file.type) || file.size > 5 * 1024 * 1024) {
    appearanceMessage('请选择 5 MB 以内的 PNG、JPEG 或 WebP 图片。', true); return;
  }
  appearanceMessage('正在调整壁纸尺寸…');
  let bitmap;
  try {
    bitmap = await createImageBitmap(file);
    if (request !== wallpaperRequest) return;
    const canvas = document.createElement('canvas'); canvas.width = 480; canvas.height = 854;
    const scale = Math.max(canvas.width / bitmap.width, canvas.height / bitmap.height);
    canvas.getContext('2d').drawImage(bitmap, (480 - bitmap.width * scale) / 2, (854 - bitmap.height * scale) / 2, bitmap.width * scale, bitmap.height * scale);
    const src = canvas.toDataURL('image/webp', .85);
    if (src.length > 800000) throw new Error('Image too large');
    appearance.customWallpaper = { name: file.name.slice(0, 100), src }; appearance.wallpaper = 'custom'; appearanceChanged();
  } catch {
    if (request === wallpaperRequest) appearanceMessage('图片无法读取，请换一张 PNG、JPEG 或 WebP 图片。', true);
  } finally { bitmap?.close(); }
});
document.querySelectorAll('[data-panel]').forEach(button => button.addEventListener('click', () => changePanel(button.dataset.panel)));
$('.inspector-tabs').addEventListener('keydown', event => {
  if (!['ArrowLeft', 'ArrowRight'].includes(event.key)) return;
  event.preventDefault(); const names = ['widgets', 'display', 'sleep']; const current = names.findIndex(name => !$(`#panel-${name}`).hidden); const panel = names[(current + (event.key === 'ArrowRight' ? 1 : 2)) % 3]; changePanel(panel); $(`#tab-${panel}`).focus();
});
document.querySelectorAll('[data-size]').forEach(button => button.addEventListener('click', () => { state.size = button.dataset.size; resizeScreen(); }));
$('#calibration-scale').addEventListener('input', event => { state.calibration = Number(event.target.value) / 100; $('#calibration-value').textContent = `${event.target.value}%`; resizeScreen(); });
$('#device-screen').addEventListener('click', event => { if (ignoreDragClick) return; if (state.editing && !display().sleeping) { const widget = event.target.closest('[data-widget-id]'); if (widget) { state.selectedWidget = widget.dataset.widgetId; renderScreen(); renderComposer(); } return; } queueTouch(); });
$('#touch-key').addEventListener('click', queueTouch);
$('#device-screen').addEventListener('dblclick', () => { if (!state.editing) doubleTouch(); });
$('#touch-key').addEventListener('dblclick', doubleTouch);
$('#previous-page').addEventListener('click', () => touch(-1));
$('#next-page').addEventListener('click', () => touch(1));
$('#simulate-touch').addEventListener('click', () => touch());
document.addEventListener('keydown', event => {
  const inField = event.target.closest('input, select, textarea, dialog, .inspector-tabs');
  if (inField) return;
  if (state.editing && state.selectedWidget && ['ArrowRight', 'ArrowLeft', 'ArrowUp', 'ArrowDown'].includes(event.key)) {
    event.preventDefault(); moveSelected(event.key === 'ArrowLeft' ? -1 : event.key === 'ArrowRight' ? 1 : 0, event.key === 'ArrowUp' ? -1 : event.key === 'ArrowDown' ? 1 : 0); return;
  }
  if (['ArrowRight', 'ArrowLeft', 'Home'].includes(event.key)) {
    event.preventDefault(); touch(event.key === 'Home' ? 'home' : event.key === 'ArrowLeft' ? -1 : 1);
  } else if ((event.key === 'Enter' || event.key === ' ') && event.target.id === 'device-screen') { event.preventDefault(); touch(); }
});
$('#data-state').addEventListener('change', event => { state.dataState = event.target.value; renderScreen(); });
$('#awake-brightness').addEventListener('input', event => { policy.awakeBrightness = Number(event.target.value); $('#awake-brightness-value').textContent = `${event.target.value}%`; policyChanged(); });
$('#simulate-brightness').addEventListener('change', event => { state.simulateBrightness = event.target.checked; renderScreen(); });
$('#chart-type').addEventListener('change', event => { state.chartType = event.target.value; $('#chart-description').textContent = CHART_DESCRIPTIONS[state.chartType]; updateDemoChart(); });
$('#chart-range').addEventListener('change', event => { state.chartRange = event.target.value; updateDemoChart(); });
$('#external-type').addEventListener('change', event => { state.externalType = event.target.value; updateExternalDemo(); });
for (const [name, key] of [['external', 'externalMinutes'], ['battery', 'batteryMinutes']]) {
  $(`#idle-${name}`).addEventListener('change', event => {
    $(`#custom-${name}-field`).hidden = event.target.value !== 'custom';
    policy.idle[key] = Number(event.target.value === 'custom' ? $(`#custom-${name}`).value : event.target.value); policyChanged();
  });
  $(`#custom-${name}`).addEventListener('input', event => { policy.idle[key] = event.target.value ? Number(event.target.value) : NaN; policyChanged(); });
}
$('#idle-display-fields').addEventListener('input', event => { if (updateDisplayFields(event.target, policy.idle, $('#idle-display-fields'))) policyChanged(); });
$('#schedule-list').addEventListener('input', event => {
  const card = event.target.closest('[data-plan-id]'); if (!card) return;
  const plan = policy.plans.find(p => p.id === card.dataset.planId), target = event.target;
  const key = target.dataset.edit; if (!key) return;
  if (!updateDisplayFields(target, plan, card)) plan[key] = key === 'enabled' ? target.checked : target.value;
  if (key === 'enabled') card.classList.toggle('disabled', !plan.enabled);
  policyChanged();
});
$('#schedule-list').addEventListener('change', event => {
  if (['start', 'end'].includes(event.target.dataset.edit)) renderPlanList();
});
$('#schedule-list').addEventListener('click', event => {
  const button = event.target.closest('button'), card = button?.closest('[data-plan-id]'); if (!card) return;
  const index = policy.plans.findIndex(p => p.id === card.dataset.planId), plan = policy.plans[index];
  if (button.dataset.day !== undefined) {
    const day = Number(button.dataset.day);
    plan.days = plan.days.includes(day) ? plan.days.filter(d => d !== day) : [...plan.days, day];
    button.setAttribute('aria-pressed', String(plan.days.includes(day)));
  } else if (button.dataset.planAction) {
    if (button.dataset.planAction === 'remove') policy.plans.splice(index, 1);
    else {
      const next = index + (button.dataset.planAction === 'up' ? -1 : 1);
      if (next < 0 || next >= policy.plans.length) return;
      [policy.plans[index], policy.plans[next]] = [policy.plans[next], policy.plans[index]];
    }
    renderPlanList();
  }
  policyChanged();
});
$('#add-plan').addEventListener('click', () => {
  if (policy.plans.length >= 32) return;
  const suffix = globalThis.crypto?.randomUUID?.() || `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
  policy.plans.push({ id: `p-${suffix}`, name: `计划 ${policy.plans.length + 1}`, enabled: true, days: [1, 2, 3, 4, 5, 6, 0], start: '22:00', end: '06:00', action: 'dim', brightness: 10, page: pages[0].id });
  renderPlanList(); policyChanged();
});
$('#save-policy').addEventListener('click', () => {
  const error = savePolicy(storage, policy);
  $('#policy-message').textContent = error || '已保存到此浏览器';
  $('#policy-message').classList.toggle('error', Boolean(error));
  if (!error) dirty = false;
});
$('#simulated-time').addEventListener('change', event => {
  const now = wallTime(event.target.value);
  if (!Number.isFinite(now)) { event.target.setCustomValidity('请输入有效的设备日期与时间。'); event.target.reportValidity(); return; }
  event.target.setCustomValidity('');
  if (now < state.now) { state.lastActivity = now; state.wakeOccurrence = null; }
  state.now = now; renderScreen();
});
$('#advance-minute').addEventListener('click', () => { state.now += MINUTE; renderScreen(); });
$('#advance-half-hour').addEventListener('click', () => { state.now += 30 * MINUTE; renderScreen(); });
$('#jump-night').addEventListener('click', () => {
  const plan = policy.plans.find(p => p.enabled && p.days.length);
  if (!plan || validatePolicy(policy)) { $('#simulation-result').textContent = '先启用一条有效计划。'; return; }
  const base = new Date(state.now); base.setUTCHours(0, 0, 0, 0);
  for (let day = 0; day < 8; day++) {
    const candidate = base.getTime() + day * 86400000;
    if (!plan.days.includes(new Date(candidate).getUTCDay())) continue;
    const next = wallTime(`${timeInput(candidate).slice(0, 10)}T${plan.start}`);
    if (next < state.now) continue;
    state.now = next; state.wakeOccurrence = null; renderScreen(); break;
  }
});
$('#power-source').addEventListener('change', event => { state.supply = event.target.value; renderScreen(); });
$('#reset-preview').addEventListener('click', () => {
  policy = defaultPolicy(); pages = defaultLayouts(); layoutHistory.length = 0; saveLayouts(storage, pages);
  wallpaperRequest++; appearance = defaultAppearance();
  Object.assign(state, { page: 'clock', editing: false, selectedWidget: null, now: initialTime, lastActivity: initialTime, supply: 'external', wakeOccurrence: null, dataState: 'normal', chartType: 'line', chartRange: '24', externalType: 'value', simulateBrightness: true, size: 'large', calibration: 1 });
  $('#data-state').value = 'normal'; $('#chart-type').value = 'line'; $('#chart-range').value = '24'; $('#external-type').value = 'value'; $('#power-source').value = 'external'; $('#simulate-brightness').checked = true; $('#calibration-scale').value = '100'; $('#calibration-value').textContent = '100%'; $('#chart-description').textContent = CHART_DESCRIPTIONS.line;
  appearanceChanged(); makeNav(); renderPolicy(); renderComposer(); resizeScreen(); policyChanged();
});
$('#open-info').addEventListener('click', () => $('#info-dialog').showModal());
$('#close-info').addEventListener('click', () => $('#info-dialog').close());
$('#info-dialog').addEventListener('click', event => { if (event.target === $('#info-dialog')) { const r = event.target.getBoundingClientRect(); if (event.clientX < r.left || event.clientX > r.right || event.clientY < r.top || event.clientY > r.bottom) event.target.close(); } });

// Public read-only snapshot supports repeatable browser acceptance checks.
Object.defineProperty(window, 'previewSnapshot', { get: () => JSON.parse(JSON.stringify({ ...state, display: lastDisplay, policy, pages, appearance, dirty })) });
