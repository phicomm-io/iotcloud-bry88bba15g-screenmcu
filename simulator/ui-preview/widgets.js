import { BINDINGS } from './layout.js';
import { DAY_LABELS, timeLabel, chartFixture } from './model.js';
import { escapeHTML as esc, chartMarkup, chartReading } from './charts.js';
import { COLUMN_PITCH, STRIP_PADDING, widgetRect } from './geometry.js';

const baseMetric = widgetRect({ w: 2, h: 2 });

export function widgetMarkup(widget, context) {
  const { state, values: v, icons } = context;
  const wide = widget.w === 4, tall = widget.h >= 4, full = wide && widget.h === 6;
  const rect = widgetRect(widget);
  const title = text => `<div class="widget-label">${esc(text)}</div>`;
  if (widget.type === 'clock') {
    const date = new Date(state.now);
    return `<div class="widget-clock${widget.h === 1 ? ' compact-clock' : ''}"><div class="widget-time">${timeLabel(state.now)}</div><div class="widget-date">${date.getUTCMonth() + 1}月${date.getUTCDate()}日 · 周${DAY_LABELS[date.getUTCDay()]}</div></div>`;
  }
  if (widget.type === 'metric') {
    const binding = BINDINGS[widget.binding], value = v[widget.binding] || '--';
    const color = ['co2', 'pm', 'tvoc'].includes(widget.binding) ? state.dataState === 'extreme' ? 'danger' : 'accent' : '';
    const row = widget.w > widget.h;
    const scale = row ? 1 : Math.min(rect.width / baseMetric.width, rect.height / baseMetric.height);
    return `<div class="metric-content${row ? ' metric-row' : ''}${row && widget.h === 1 ? ' metric-slim' : ''}" style="--metric-scale:${scale.toFixed(4)}"><div class="metric-heading">${title(widget.title || binding.title)}${binding.unit ? `<span class="metric-unit">${binding.unit}</span>` : ''}</div><div class="metric-value ${color}" data-fit-readout>${esc(value)}</div></div>`;
  }
  if (widget.type === 'text') {
    const text = state.dataState === 'missing' ? '暂无内容' : state.dataState === 'extreme' ? `${widget.text}\n这是用于检查换行和边界的长文本。更多内容在网页查看，小屏保持清晰。` : widget.text;
    const lineHeight = wide && tall ? 66 : wide ? 60 : 48;
    const lines = Math.max(1, Math.floor((rect.height - 88) / lineHeight));
    return `${title(widget.title)}<div class="widget-text-body" style="-webkit-line-clamp:${lines}">${esc(text)}</div>`;
  }
  if (widget.type === 'chart') {
    const fixture = chartFixture(widget.chartType, widget.range, state.dataState);
    if (widget.h <= 3) {
      const reading = chartReading(fixture, true), height = rect.height - 64;
      const width = rect.width - COLUMN_PITCH - STRIP_PADDING;
      return `<div class="chart-strip"><div class="chart-strip-meta"><div class="chart-strip-title">${esc(widget.title === '图表' ? fixture.title : widget.title)}</div><div class="chart-strip-value" data-fit-readout>${esc(reading.value)}</div><div class="chart-strip-unit">${esc(reading.unit)}</div><div class="chart-strip-period">${fixture.hours}h</div></div><div class="widget-chart strip-plot" style="--strip-height:${height}px">${chartMarkup(fixture, state.now, { compact: true, strip: true, width, height })}</div></div>`;
    }
    return `${title(widget.title === '图表' ? fixture.title : widget.title)}<div class="widget-chart${full ? ' full-chart' : ''}">${chartMarkup(fixture, state.now, { compact: true, width: rect.width - 32, height: rect.height - (full ? 368 : 248), showValue: full })}</div>`;
  }
  if (widget.type === 'weather') {
    const missing = state.dataState === 'missing', temperature = missing ? '--' : state.dataState === 'extreme' ? '-12°' : '26°';
    if (wide && widget.h <= 2) {
      const w = rect.width - 24, h = rect.height - 24;
      const part = (cls, text, x, y, width, height, size, align = 'left') => `<span class="${cls}" style="left:${x}px;top:${y}px;width:${width}px;height:${height}px;font-size:${size}px;text-align:${align}">${text}</span>`;
      const location = esc(widget.title === '天气' ? '广州' : widget.title), condition = missing ? '--' : '晴间多云';
      if (widget.h === 1) return `<div class="compact-weather">${part('widget-label', location, 0, 4, 130, 36, 28)}${part('weather-condition', condition, 0, 56, 130, 36, 24)}${part('weather-icon', icons.partly, 142, (h - 64) / 2, 64, 64, 0)}${part('weather-temperature', temperature, 220, (h - 84) / 2, w - 220, 84, 72, 'right')}</div>`;
      const summary = ['27°', '62%', '2.4'].map((value, i) => part('metric-unit', ['体感', '湿度', '风速 m/s'][i], Math.floor(i * w / 3), 148, Math.floor(w / 3), 32, 24, 'center') + part('weather-temperature', missing ? '--' : value, Math.floor(i * w / 3), 180, Math.floor(w / 3), 40, 32, 'center')).join('');
      return `<div class="compact-weather">${part('widget-label', location, 0, 0, w, 40, 32)}${part('weather-icon', icons.partly, 0, 56, 88, 88, 0)}${part('weather-temperature', temperature, 108, 48, 150, 96, 80)}${part('weather-condition', condition, 270, 60, w - 270, 72, 28)}${summary}</div>`;
    }
    if (full) return `${title(widget.title === '天气' ? '广州' : widget.title)}<div class="widget-weather-full"><div class="weather-icon">${icons.partly}</div><div class="weather-temperature">${temperature}</div><p class="weather-condition">${missing ? '暂无天气' : '晴间多云'}</p></div><div class="weather-summary"><div><span>体感</span>${missing ? '--' : '27°'}</div><div><span>湿度</span>${missing ? '--' : '62%'}</div><div><span>风速 m/s</span>${missing ? '--' : '2.4'}</div></div><div class="weather-days"><div class="weather-day"><span>明天</span>${icons.sun}<span>${missing ? '--' : '23 / 28°'}</span></div><div class="weather-day"><span>周日</span>${icons.cloud}<span>${missing ? '--' : '21 / 25°'}</span></div></div>`;
    return `<div class="widget-weather"><div class="widget-weather-label">${title(widget.title === '天气' ? '广州' : widget.title)}${icons.partly}</div><div class="metric-value">${temperature}</div><div class="widget-condition">${missing ? '暂无天气' : '晴间多云'}</div>${tall ? '<div class="widget-weather-extra">明天<br>23 / 28°</div>' : ''}</div>`;
  }
  if (widget.type === 'reminders') {
    if (state.dataState === 'missing') return `${title(widget.title)}<div class="widget-empty">暂无提醒</div>`;
    const long = state.dataState === 'extreme';
    if (full) return `${title(widget.title)}<div class="reminder-list"><article class="reminder-card"><div class="reminder-time"><i></i>19:00</div><h3>${long ? '今晚的会议与需要提前准备的资料清单' : '开窗通风'}</h3><p>${long ? '这是较长的提醒正文，用来检查小屏文字换行与截断。正文只保留可舒适阅读的行数。' : '让新鲜空气进来。'}</p></article><article class="reminder-card"><div class="reminder-time"><i></i>21:30</div><h3>休息一下</h3><p>离开屏幕，准备休息。</p></article></div>`;
    return `${title(widget.title)}<div class="widget-reminder-time">19:00</div><div class="widget-reminder-title">${long ? '今晚会议准备资料' : '开窗通风'}</div>${tall ? '<div class="widget-reminder-body">让新鲜空气进来。</div>' : ''}`;
  }
  return '';
}
