const PALETTE = ['var(--chart-a)', 'var(--chart-b)', 'var(--chart-c)'];
export const escapeHTML = value => String(value).replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[char]);
export function formatValue(value, digits = 0) {
  if (value === null || !Number.isFinite(value)) return '--';
  return Number(value.toFixed(digits)).toLocaleString('en-US', { maximumFractionDigits: digits });
}
const num = n => Number(n.toFixed(2));
const current = series => series.values.at(-1);
function domain(values, zero = false) {
  const finite = values.filter(Number.isFinite);
  if (!finite.length) return [0, 1];
  let min = zero ? 0 : Math.min(...finite), max = Math.max(...finite);
  if (min === max) { max += Math.max(1, Math.abs(max) * .1); if (!zero) min -= 1; }
  const step = 10 ** Math.floor(Math.log10((max - min) / 3));
  return [zero ? 0 : Math.floor(min / step) * step, Math.ceil((max + (max - min) * .07) / step) * step];
}
/** One polyline per contiguous valid run: absent samples are never bridged. */
export function lineSegments(values, x, y) {
  const segments = []; let currentSegment = [];
  values.forEach((value, i) => {
    if (value === null || !Number.isFinite(value)) { if (currentSegment.length) segments.push(currentSegment); currentSegment = []; }
    else currentSegment.push([x(i), y(value)]);
  });
  if (currentSegment.length) segments.push(currentSegment);
  return segments;
}
function linePath(points) { return points.map(([x, y], i) => `${i ? 'L' : 'M'}${num(x)},${num(y)}`).join(' '); }
export function chartReading(fixture, abbreviated = false) {
  const additive = ['stacked-area', 'stacked-bar', 'donut'].includes(fixture.kind);
  const readings = fixture.series.map(current);
  const value = additive ? readings.some(Number.isFinite) ? readings.reduce((sum, n) => sum + (n || 0), 0) : null : readings[0];
  return { value: abbreviated && Math.abs(value) >= 1000 ? `${formatValue(value / 1000, 1)}k` : formatValue(value, fixture.unit === '°C' ? 1 : 0), unit: fixture.unit };
}
function legend(fixture, donut = false, strip = false) {
  const sum = fixture.series.reduce((total, s) => total + (current(s) || 0), 0);
  if (strip) return `<div class="chart-legend mini-legend">${fixture.series.map((s, i) => `<div class="legend-row"><span class="legend-name"><i class="legend-swatch" style="--series-color:${PALETTE[i]}"></i>${escapeHTML(s.name)}${fixture.kind === 'dual' ? ` ${escapeHTML(s.unit)}` : ''}</span></div>`).join('')}</div>`;
  return `<div class="chart-legend">${fixture.series.map((s, i) => `<div class="legend-row"><span class="legend-name"><i class="legend-swatch" style="--series-color:${PALETTE[i]}"></i>${escapeHTML(s.name)}</span><span class="legend-value">${current(s) === null ? '--' : donut ? `${formatValue(current(s) / (sum || 1) * 100)}%` : `${formatValue(current(s), s.unit === '°C' ? 1 : 0)} ${escapeHTML(s.unit)}`}</span></div>`).join('')}</div>`;
}
function donutChart(fixture, compact = false, strip = false) {
  const sum = fixture.series.reduce((total, s) => total + (current(s) || 0), 0), radius = 125, circumference = 2 * Math.PI * radius;
  let offset = 0;
  const slices = fixture.series.map((s, i) => {
    const length = (current(s) || 0) / (sum || 1) * circumference;
    const circle = `<circle cx="165" cy="165" r="${radius}" fill="none" stroke="${PALETTE[i]}" stroke-width="42" stroke-dasharray="${num(length)} ${num(circumference - length)}" stroke-dashoffset="${num(-offset)}" transform="rotate(-90 165 165)"/>`;
    offset += length; return circle;
  }).join('');
  return `<div class="donut-wrap${compact ? ' compact-donut' : ''}"><svg class="chart-svg" viewBox="0 0 330 330" role="img" aria-label="用电组成环形图">${slices}</svg>${strip ? '' : `<div class="donut-center"><strong>${formatValue(sum)}</strong><small>W</small></div>`}</div>${legend(fixture, true, strip)}`;
}
export function chartMarkup(fixture, now, options = {}) {
  const { kind, series, n, hours } = fixture;
  const strip = Boolean(options.strip);
  const isStack = kind === 'stacked-area' || kind === 'stacked-bar';
  const isBar = kind === 'bar' || kind === 'stacked-bar';
  const haveData = series.some(s => s.values.some(Number.isFinite));
  const last = isStack || kind === 'donut' ? series.reduce((sum, s) => sum + (current(s) || 0), 0) : current(series[0]);
  const primary = options.compact && !options.showValue ? '' : `<div class="chart-meta"><div class="chart-number">${haveData ? formatValue(last, fixture.unit === '°C' ? 1 : 0) : '--'}<small>${escapeHTML(fixture.unit)}</small></div><span class="chart-period">${hours}h</span></div>`;
  if (!haveData) return `${primary}<div class="chart-empty"${strip ? ` style="height:${options.height}px"` : ''}>暂无数据</div>${legend(fixture, false, strip)}`;
  if (kind === 'donut') return donutChart(fixture, options.compact, strip);
  const rightSeries = series.filter(s => s.axis === 'right'), dual = rightSeries.length > 0;
  const totals = Array.from({ length: n }, (_, i) => series.reduce((sum, s) => sum + (s.values[i] || 0), 0));
  const leftDomain = domain(isStack ? totals : series.filter(s => s.axis !== 'right').flatMap(s => s.values), isBar || isStack);
  const rightDomain = dual ? domain(rightSeries.flatMap(s => s.values)) : leftDomain;
  const canvasWidth = options.width || 416, canvasHeight = options.height || 315;
  const plot = { left: strip ? 72 : 84, right: canvasWidth - (dual ? strip ? 58 : 74 : 10), top: strip ? 16 : 24, bottom: canvasHeight - (strip ? 40 : 58) };
  const width = plot.right - plot.left, height = plot.bottom - plot.top;
  const x = i => plot.left + (isBar ? (i + .5) / n : i / (n - 1)) * width;
  const y = (value, axis = 'left') => { const [min, max] = axis === 'right' ? rightDomain : leftDomain; return plot.bottom - (value - min) / (max - min) * height; };
  let axes = '';
  for (const fraction of strip ? [0, 1] : [0, .5, 1]) {
    const py = plot.bottom - fraction * height;
    const left = leftDomain[0] + fraction * (leftDomain[1] - leftDomain[0]);
    axes += `<line class="gridline" x1="${plot.left}" x2="${plot.right}" y1="${py}" y2="${py}"/><text x="${plot.left - 12}" y="${py + 9}" text-anchor="end">${left >= 1000 ? `${formatValue(left / 1000, 1)}k` : formatValue(left)}</text>`;
    if (dual) axes += `<text x="${plot.right + 12}" y="${py + 9}" text-anchor="start">${formatValue(rightDomain[0] + fraction * (rightDomain[1] - rightDomain[0]))}</text>`;
  }
  for (const fraction of strip ? [0, 1] : [0, .5, 1]) {
    const stamp = new Date(now - (1 - fraction) * hours * 3600000);
    const label = `${String(stamp.getUTCHours()).padStart(2, '0')}:${String(stamp.getUTCMinutes()).padStart(2, '0')}`;
    axes += `<text x="${plot.left + width * fraction}" y="${canvasHeight - 11}" text-anchor="${fraction === 0 ? 'start' : fraction === 1 ? 'end' : 'middle'}">${label}</text>`;
  }
  let shapes = '';
  if (isBar) {
    const barWidth = width / n * .62;
    for (let i = 0; i < n; i++) {
      let baseline = 0;
      series.forEach((s, j) => {
        if (s.values[i] === null) return;
        const value = s.values[i];
        shapes += `<rect data-series="${j}" x="${num(x(i) - barWidth / 2)}" y="${num(y(baseline + value))}" width="${num(barWidth)}" height="${num(y(baseline) - y(baseline + value))}" fill="${PALETTE[j]}"/>`;
        baseline += value;
      });
    }
  } else if (isStack) {
    let baseline = Array(n).fill(0);
    series.forEach((s, j) => {
      const top = s.values.map((value, i) => baseline[i] + (value || 0));
      const points = top.map((v, i) => [x(i), y(v)]).concat(baseline.map((v, i) => [x(i), y(v)]).reverse());
      shapes += `<path data-series="${j}" d="${linePath(points)} Z" fill="${PALETTE[j]}" fill-opacity=".86"/>`;
      baseline = top;
    });
  } else {
    series.forEach((s, j) => {
      const segments = lineSegments(s.values, x, v => y(v, s.axis));
      for (const points of segments) shapes += `<path data-series="${j}" d="${linePath(points)}" fill="none" stroke="${PALETTE[j]}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"${s.axis === 'right' ? ' stroke-dasharray="8 8"' : ''}/>`;
      const value = current(s);
      if (value !== null) shapes += `<circle cx="${num(x(n - 1))}" cy="${num(y(value, s.axis))}" r="5" fill="${PALETTE[j]}"/>`;
    });
  }
  return `${primary}${strip ? '' : `<div class="chart-unit-label"><span>${escapeHTML(fixture.unit)}${options.compact && !options.showValue ? ` · ${hours}h` : ''}</span>${dual ? `<span>${escapeHTML(rightSeries[0].unit)}</span>` : ''}</div>`}<svg class="chart-svg" style="height:${canvasHeight}px" viewBox="0 0 ${canvasWidth} ${canvasHeight}" role="img" aria-label="${escapeHTML(fixture.title)}，${hours} 小时">${axes}${shapes}</svg>${legend(fixture, false, strip)}`;
}
