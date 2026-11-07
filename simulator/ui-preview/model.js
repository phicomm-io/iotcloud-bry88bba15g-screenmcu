/* Pure preview rules. Times are device wall time encoded as UTC, never host time. */
export const PAGE_DEFS = [
  { id: 'clock', title: '时钟' }, { id: 'air', title: '空气' },
  { id: 'trend', title: '趋势' }, { id: 'weather', title: '天气' },
  { id: 'reminders', title: '提醒' }, { id: 'external', title: '外部数据' },
];
export const STORAGE_KEY = 'bry88bba15g.ui-preview.v1';
export const DAY_LABELS = ['日', '一', '二', '三', '四', '五', '六'];
export const MINUTE = 60000;
const DAY = 24 * 60 * MINUTE;
const validPage = value => value === 'current' || PAGE_DEFS.some(page => page.id === value) || (typeof value === 'string' && /^custom-[a-zA-Z0-9_-]{1,56}$/.test(value));
export function defaultPolicy() {
  return {
    version: 1, awakeBrightness: 85,
    idle: { externalMinutes: 5, batteryMinutes: 1, action: 'dim', brightness: 12, page: 'clock' },
    plans: [
      { id: 'night', name: '夜间', enabled: true, days: [1, 2, 3, 4, 5, 6, 0], start: '23:00', end: '07:00', action: 'dim', brightness: 8, page: 'clock' },
      { id: 'afternoon', name: '午休', enabled: false, days: [1, 2, 3, 4, 5], start: '12:30', end: '13:30', action: 'dim', brightness: 15, page: 'weather' },
    ],
  };
}
export function minuteOfDay(time) {
  if (typeof time !== 'string' || !/^([01]\d|2[0-3]):[0-5]\d$/.test(time)) return null;
  const [h, m] = time.split(':').map(Number);
  return h * 60 + m;
}
export function wallTime(value) {
  if (typeof value !== 'string' || !/^\d{4}-\d\d-\d\dT\d\d:\d\d$/.test(value)) return NaN;
  const epoch = Date.parse(`${value}:00Z`);
  return Number.isFinite(epoch) && new Date(epoch).toISOString().slice(0, 16) === value ? epoch : NaN;
}
export function timeInput(epoch) { return new Date(epoch).toISOString().slice(0, 16); }
export function timeLabel(epoch) { return timeInput(epoch).slice(11); }
export function validatePolicy(policy) {
  if (!policy || policy.version !== 1 || !Number.isInteger(policy.awakeBrightness) || policy.awakeBrightness < 1 || policy.awakeBrightness > 100) return '正常亮度须为 1–100%。';
  const idle = policy.idle;
  if (!idle || ![idle.externalMinutes, idle.batteryMinutes].every(n => Number.isInteger(n) && n >= 0 && n <= 1440)) return '等待时间须为 0–1440 分钟；0 表示永不。';
  if (!Array.isArray(policy.plans) || policy.plans.length > 32) return '最多支持 32 条计划。';
  const ids = new Set();
  for (const entry of [idle, ...policy.plans]) {
    if (!['dim', 'off'].includes(entry.action) || !Number.isInteger(entry.brightness) || entry.brightness < 1 || entry.brightness > 100 || !validPage(entry.page)) return '请填写有效的休眠亮度与页面。';
  }
  for (const plan of policy.plans) {
    if (typeof plan.id !== 'string' || !/^[a-zA-Z0-9_-]{1,64}$/.test(plan.id) || ids.has(plan.id)) return '计划编号无效或重复。';
    ids.add(plan.id);
    if (typeof plan.name !== 'string' || !plan.name.trim() || plan.name.length > 24 || typeof plan.enabled !== 'boolean') return '计划名称须为 1–24 个字符。';
    if (!Array.isArray(plan.days) || plan.days.some(day => !Number.isInteger(day) || day < 0 || day > 6) || (plan.enabled && !plan.days.length)) return '启用的计划至少选择一个星期。';
    const start = minuteOfDay(plan.start), end = minuteOfDay(plan.end);
    if (start === null || end === null || start === end) return '计划起止时间须有效且不能相同。';
  }
  return null;
}
export function loadPolicy(storage) {
  try {
    const raw = storage.getItem(STORAGE_KEY);
    if (!raw) return { policy: defaultPolicy(), recovered: false };
    const policy = JSON.parse(raw);
    if (validatePolicy(policy)) throw new Error('Invalid preview policy');
    return { policy, recovered: false };
  } catch { return { policy: defaultPolicy(), recovered: true }; }
}
export function savePolicy(storage, policy) {
  const error = validatePolicy(policy);
  if (error) return error;
  try { storage.setItem(STORAGE_KEY, JSON.stringify(policy)); return null; }
  catch { return '浏览器未允许本地存储；当前预览仍可使用。'; }
}
/** First matching plan wins. An overnight interval belongs to its start day. */
export function activePlan(plans, now) {
  if (!Number.isFinite(now)) return null;
  const date = new Date(now), minute = date.getUTCHours() * 60 + date.getUTCMinutes();
  for (const plan of plans) {
    const start = minuteOfDay(plan.start), end = minuteOfDay(plan.end);
    if (!plan.enabled || start === null || end === null || start === end) continue;
    let startDay = date.getUTCDay(), startDate = Math.floor(now / DAY) * DAY;
    if (start < end) {
      if (minute < start || minute >= end) continue;
    } else {
      if (minute >= start) { /* before midnight */ }
      else if (minute < end) { startDay = (startDay + 6) % 7; startDate -= DAY; }
      else continue;
    }
    if (!plan.days.includes(startDay)) continue;
    const occurrence = `${plan.id}:${new Date(startDate).toISOString().slice(0, 10)}`;
    return { plan, occurrence };
  }
  return null;
}
export function idleMinutes(policy, supply) {
  return supply === 'battery' ? policy.idle.batteryMinutes : policy.idle.externalMinutes;
}
export function evaluateDisplay({ policy, now, lastActivity, supply, page, wakeOccurrence = null, pages = PAGE_DEFS }) {
  const active = activePlan(policy.plans, now);
  const timeout = idleMinutes(policy, supply);
  const idleFor = Math.max(0, (now - lastActivity) / MINUTE);
  const temporaryWake = Boolean(active && active.occurrence === wakeOccurrence && idleFor < (timeout || 1));
  const idleExpired = timeout > 0 && idleFor >= timeout;
  const sleepEntry = active && !temporaryWake ? active.plan : !active && idleExpired ? policy.idle : null;
  return {
    sleeping: Boolean(sleepEntry), temporaryWake,
    occurrence: active?.occurrence || null,
    planName: active?.plan.name || null,
    reason: sleepEntry ? active ? 'plan' : 'idle' : 'awake',
    page: sleepEntry && sleepEntry.page !== 'current' && pages.some(p => p.id === sleepEntry.page) ? sleepEntry.page : page,
    brightness: sleepEntry ? sleepEntry.action === 'off' ? 0 : sleepEntry.brightness : policy.awakeBrightness,
    idleFor,
  };
}
export const CHART_DESCRIPTIONS = {
  line: 'CO₂ 分钟趋势，缺失时段保留断点。',
  multi: '室内与室外温度，共用 °C 刻度。',
  dual: '温度与湿度分别对应左、右刻度，不相加。',
  bar: '外部用电示例，单位 W。',
  'stacked-bar': '照明、空调、电器按 W 累加，柱高为总量。',
  'stacked-area': '同单位分项随时间累加，顶边为总量。',
  donut: '同单位外部数据的组成比例。',
};
/** Fixed demo fixtures, never sensor samples. Range changes sampling and labels. */
export function chartFixture(kind, range, state) {
  const hours = Number(range), n = hours === 1 ? 13 : hours === 6 ? 19 : 25;
  const phase = i => i / (n - 1);
  const missing = i => (i > n * .35 && i < n * .52);
  const values = (formula, index) => Array.from({ length: n }, (_, i) => state === 'missing' || (missing(i) && (kind === 'line' || kind === 'multi' || kind === 'dual')) ? null : Math.round(formula(phase(i), index) * 10) / 10);
  const multiplier = state === 'extreme' ? 8 : 1;
  let title, unit, series;
  if (kind === 'line') {
    title = 'CO₂'; unit = 'ppm';
    series = [{ name: 'CO₂', unit, values: values(t => multiplier * (630 + 240 * Math.sin(t * 5) ** 2 + 100 * t)) }];
  } else if (kind === 'multi' || kind === 'dual') {
    title = kind === 'dual' ? '温湿度' : '温度对比'; unit = '°C';
    series = [
      { name: kind === 'dual' ? '温度' : '室内', unit: '°C', values: values(t => 23.6 + 1.5 * Math.sin(t * 5)) },
      kind === 'dual'
        ? { name: '湿度', unit: '%', axis: 'right', values: values(t => 48 + 8 * Math.cos(t * 4)) }
        : { name: '室外', unit: '°C', values: values(t => 20.1 + 5 * Math.sin(t * 3)) },
    ];
  } else {
    title = kind === 'donut' ? '用电分布' : '用电功率'; unit = 'W';
    series = [
      { name: '照明', unit, values: values(t => multiplier * (80 + 35 * Math.cos(t * 4))) },
      { name: '空调', unit, values: values(t => multiplier * (260 + 90 * Math.sin(t * 6) ** 2)) },
      { name: '电器', unit, values: values(t => multiplier * (120 + 50 * Math.sin(t * 3) ** 2)) },
    ];
    if (kind === 'bar') series = [{ name: '总功率', unit, values: series[0].values.map((_, i) => series.every(s => s.values[i] !== null) ? Math.round(series.reduce((sum, s) => sum + s.values[i], 0)) : null) }];
  }
  return { kind, hours, n, title, unit, series, stale: state === 'stale' };
}
