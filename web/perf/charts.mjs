/* Original renderer registry. Evidence selects columns, never executable code.
 * Exact values reach rendering; only normalized plot coordinates approximate.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {rational, compareRational, coordinate, exactText, median, rationalText} from './types.mjs';

export function element(tag, attributes = {}, text)
{
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attributes))
    {
        if (key === 'class') node.className = value;
        else node.setAttribute(key, String(value));
    }
    if (text !== undefined) node.textContent = String(text);
    return node;
}
export function svgElement(tag, attributes = {}, text)
{
    const node = document.createElementNS('http://www.w3.org/2000/svg', tag);
    for (const [key, value] of Object.entries(attributes)) node.setAttribute(key, String(value));
    if (text !== undefined) node.textContent = String(text);
    return node;
}
export const palette = ['#17605b', '#b4552d', '#514589', '#9c4163', '#537927', '#356a9c', '#946a18', '#566168'];
export const renderers = new Map();
export function registerRenderer(id, definition)
{
    if (renderers.has(id) || !/^[a-z][a-z-]*$/u.test(id) || typeof definition.render !== 'function') throw new Error('Invalid/duplicate first-party renderer');
    renderers.set(id, Object.freeze(definition));
}
const key = value => exactText(value);
const extent = values =>
{
    const sorted = values.filter(value => value !== null).sort(compareRational);
    return sorted.length ? [sorted[0], sorted.at(-1)] : null;
};
export function metricRows(table, column, rows)
{
    const annotation = table.definition.measurements?.[column];
    return rows.map((row, index) => ({row, index, value: row[column] !== null && (!annotation?.status_column || row[annotation.status_column] === 'ok') ? rational(row[column]) : null}));
}
export function canDistribute(table, column)
{
    const annotation = table.definition.measurements?.[column];
    return Boolean(annotation?.raw_retained) && ['observation', 'process', 'execution', 'independent_unit', 'raw'].includes(annotation.sample_level) && !/median|mean|summary/iu.test(annotation.statistic ?? '');
}

function chartFrame(title, xLabel, yLabel)
{
    const svg = svgElement('svg', {viewBox: '0 0 960 390', role: 'img', 'aria-label': title, class: 'chart'});
    svg.append(svgElement('title', {}, title), svgElement('path', {d: 'M88 26V330H930', stroke: '#a7b4b0', fill: 'none'}));
    for (let i = 1; i <= 4; ++i) svg.append(svgElement('path', {d: `M88 ${330 - i * 72}H930`, stroke: '#e6ebe8', fill: 'none'}));
    svg.append(svgElement('text', {x: 510, y: 381, 'text-anchor': 'middle', class: 'axis'}, xLabel), svgElement('text', {x: 18, y: 24, class: 'axis'}, yLabel));
    return svg;
}
function title(node, text) { node.append(svgElement('title', {}, text)); return node; }
function series(table, options, rows)
{
    const contract = table.definition.metadata?.comparison;
    const identities = Object.values(contract?.identity_columns ?? {});
    const fields = new Set(table.fields.map(field => field.name));
    const bindings = Object.entries(table.definition.bindings ?? {}).filter(([role, value]) => role.endsWith('_context') && typeof value === 'string' && fields.has(value)).map(([, value]) => value);
    const dimensions = [...new Set([...(table.definition.dimensions ?? []), ...identities, ...bindings, ...(options.group ? [options.group] : [])])].filter(name => name !== options.x && name !== options.y && name !== options.facet);
    const groups = new Map();
    for (const point of metricRows(table, options.y, rows))
    {
        const identity = dimensions.map(name => `${name}=${key(point.row[name])}`).join(' · ') || 'selected population';
        const values = groups.get(identity) ?? [];
        values.push(point);
        groups.set(identity, values);
    }
    if (groups.size > 96) throw new Error('More than 96 distinct populations. Filter a condition/facet before plotting.');
    return groups;
}
function xy(table, options, rows, line)
{
    if (rows.length > 12000) throw new Error('Plot budget is 12,000 rows; select a smaller drilldown. Exact table export remains available.');
    const points = metricRows(table, options.y, rows);
    const ys = extent(points.map(point => point.value));
    if (!ys) throw new Error('No measured numeric values in this selection. Null/censored values remain gaps.');
    const numericX = rows.every(row => row[options.x] === null || rational(row[options.x]) !== null);
    const xs = numericX ? extent(rows.map(row => rational(row[options.x]))) : null;
    const categories = [...new Set(rows.map(row => key(row[options.x])))];
    const x = row => 88 + 842 * (xs ? coordinate(rational(row[options.x]), ...xs) : categories.length < 2 ? 0.5 : categories.indexOf(key(row[options.x])) / (categories.length - 1));
    const y = value => 330 - 288 * coordinate(value, ...ys);
    const svg = chartFrame(`${line ? 'History/parameter lines' : 'Scatter'}: ${options.y}`, options.x, options.y);
    if (options.onTimeRange && xs && rows.every(row => row[options.x] === null || typeof row[options.x] === 'bigint' || Number.isSafeInteger(row[options.x])))
    {
        let anchor = null;
        const at = event =>
        {
            const bounds = svg.getBoundingClientRect();
            const fraction = BigInt(Math.round(Math.max(0, Math.min(1, ((event.clientX - bounds.left) / bounds.width * 960 - 88) / 842)) * 1000000));
            const minimum = xs[0].n / xs[0].d, maximum = xs[1].n / xs[1].d;
            return minimum + (maximum - minimum) * fraction / 1000000n;
        };
        svg.addEventListener('pointerdown', event => { if (event.button === 0) { anchor = at(event); svg.setPointerCapture(event.pointerId); } });
        svg.addEventListener('pointerup', event => { const tail = at(event), head = anchor; anchor = null; if (head !== null && head !== tail) options.onTimeRange(head < tail ? head : tail, head < tail ? tail : head); });
        svg.addEventListener('pointercancel', () => { anchor = null; });
    }
    for (const [position, value] of [[330, ys[0]], [43, ys[1]]]) svg.append(svgElement('text', {x: 78, y: position, 'text-anchor': 'end', class: 'axis'}, rationalText(value)));
    if (categories.length) for (const index of [...new Set([0, Math.floor((categories.length - 1) / 2), categories.length - 1])]) svg.append(svgElement('text', {x: numericX && xs ? x(rows.find(row => key(row[options.x]) === categories[index])) : 88 + 842 * (categories.length < 2 ? 0.5 : index / (categories.length - 1)), y: 350, 'text-anchor': 'middle', class: 'axis'}, categories[index].slice(0, 28)));
    const legend = element('div', {class: 'legend'});
    let color = 0;
    for (const [identity, values] of series(table, options, rows))
    {
        const fill = palette[color++ % palette.length];
        let path = '';
        const sorted = numericX ? [...values].sort((a, b) => a.row[options.x] === null ? 1 : b.row[options.x] === null ? -1 : compareRational(rational(a.row[options.x]), rational(b.row[options.x]))) : values;
        let previous = false;
        for (const point of sorted)
        {
            if (point.value === null || point.row[options.x] === null) { previous = false; continue; }
            const px = x(point.row), py = y(point.value);
            path += `${previous ? 'L' : 'M'}${px},${py}`;
            previous = true;
            const dot = title(svgElement('circle', {cx: px, cy: py, r: 3.5, fill, tabindex: 0}), `${identity}\n${options.x}: ${key(point.row[options.x])}\n${options.y}: ${key(point.row[options.y])}\nExact source row ${point.index + 1}`);
            dot.addEventListener('click', () => options.onInspect?.(point.row, point.index));
            svg.append(dot);
        }
        if (line) svg.prepend(svgElement('path', {d: path, stroke: fill, 'stroke-width': 1.5, fill: 'none'}));
        const label = element('span', {style: `--series:${fill}`}, identity);
        legend.append(label);
    }
    const container = element('div');
    container.append(svg, legend, element('p', {class: 'note'}, 'Series stay separate across declared dimensions and comparison identities. Missing/censored points break lines. Coordinates approximate after exact origin subtraction; inspect/export keeps source precision. Archive recorded_at is ingestion time, not commit time.'));
    return container;
}
registerRenderer('lines', {name: 'History / parameter lines', render: (table, options, rows) => xy(table, options, rows, true)});
registerRenderer('scatter', {name: 'Scatter / parameter sweep', render: (table, options, rows) => xy(table, options, rows, false)});
registerRenderer('small-multiples', {name: 'Small-multiple lines', render: (table, options, rows) =>
{
    if (!options.facet) throw new Error('Choose a facet column and All values for small multiples.');
    const values = [...new Set(rows.map(row => key(row[options.facet])))];
    if (values.length > 24) throw new Error('Small-multiple budget is 24 facets; refine filters.');
    const result = element('div');
    for (const value of values)
    {
        result.append(element('h4', {}, `${options.facet}: ${value}`), xy(table, options, rows.filter(row => key(row[options.facet]) === value), true));
    }
    return result;
}});
registerRenderer('heatmap', {name: 'Heatmap', render: (table, options, rows) =>
{
    if (!options.group) throw new Error('Choose a group column for the second heatmap dimension.');
    const columns = [...new Set(rows.map(row => key(row[options.x])))];
    const groups = [...new Set(rows.map(row => key(row[options.group])))];
    if (columns.length * groups.length > 4096) throw new Error('Heatmap budget is 4,096 cells. Filter the population.');
    const values = metricRows(table, options.y, rows);
    const bounds = extent(values.map(item => item.value));
    if (!bounds) throw new Error('No numeric heatmap values.');
    const tableNode = element('table', {class: 'heatmap'}), head = element('tr');
    head.append(element('th', {}, `${options.group} / ${options.x}`));
    for (const column of columns) head.append(element('th', {}, column));
    tableNode.append(head);
    for (const group of groups)
    {
        const row = element('tr'); row.append(element('th', {}, group));
        for (const column of columns)
        {
            const cellValues = values.filter(item => key(item.row[options.x]) === column && key(item.row[options.group]) === group && item.value !== null);
            const cell = element('td');
            if (cellValues.length === 1)
            {
                cell.textContent = key(cellValues[0].row[options.y]);
                cell.style.background = `hsl(166 28% ${94 - coordinate(cellValues[0].value, ...bounds) * 52}%)`;
                cell.addEventListener('click', () => options.onInspect?.(cellValues[0].row, cellValues[0].index));
            }
            else cell.textContent = cellValues.length ? `${cellValues.length} observations — refine filters` : '∅';
            row.append(cell);
        }
        tableNode.append(row);
    }
    return tableNode;
}});
for (const mode of ['ecdf', 'box']) registerRenderer(mode, {name: mode === 'ecdf' ? 'Observation ECDF' : 'Observation box plot', render: (table, options, rows) =>
{
    if (!canDistribute(table, options.y)) throw new Error('This column contains summaries or lacks retained raw observations. An observation distribution cannot be reconstructed. Use lines, scatter or the exact table.');
    if (series(table, options, rows).size !== 1) throw new Error('Filter one compatible population before drawing an observation distribution. Distinct dimensions/build/profile conditions are not pooled.');
    const values = metricRows(table, options.y, rows).filter(item => item.value !== null).map(item => item.value).sort(compareRational);
    const bounds = extent(values);
    if (!bounds) throw new Error('No retained numeric observations.');
    const svg = chartFrame(mode === 'ecdf' ? 'Empirical distribution of retained observations' : 'Descriptive observation box plot', options.y, mode === 'ecdf' ? 'Cumulative fraction' : 'Source observations');
    const x = value => 88 + 842 * coordinate(value, ...bounds);
    if (mode === 'ecdf')
    {
        let path = 'M88,330';
        for (let i = 0; i < values.length; ++i) path += `H${x(values[i])}V${330 - 288 * (i + 1) / values.length}`;
        svg.append(svgElement('path', {d: path, stroke: palette[0], fill: 'none', 'stroke-width': 2}));
    }
    else
    {
        const quantile = part => values[Math.floor((values.length - 1) * part)];
        const q1 = quantile(0.25), q3 = quantile(0.75), middle = median(values);
        svg.append(svgElement('path', {d: `M${x(values[0])},180H${x(values.at(-1))}`, stroke: palette[0]}), svgElement('rect', {x: x(q1), y: 130, width: Math.max(1, x(q3) - x(q1)), height: 100, fill: '#b4d9cd', stroke: palette[0]}), title(svgElement('path', {d: `M${x(middle)},130V230`, stroke: palette[0], 'stroke-width': 3}), `median ${rationalText(middle)}`));
    }
    return svg;
}});

export const specializedViews = Object.freeze([
    {id: 'scaling', label: 'Scaling / efficiency', needs: 'A declared work denominator, fixed-work or scaled-work protocol and compatible resource counts. Scatter supports the measured sweep; efficiency is unavailable until that model is declared.'},
    {id: 'pareto', label: 'Pareto frontier', needs: 'Two compatible quantities from the same execution, directions, scope and configuration. A visual tradeoff alone establishes no optimality.'},
    {id: 'roofline', label: 'Roofline / model', needs: 'Measured work, traffic at a stated memory level, and matching machine ceilings. Missing source facts cannot be inferred from throughput.'},
    {id: 'energy', label: 'Energy / quality', needs: 'Aligned energy and quality observations with units, bounds and baseline/idle policy.'},
    {id: 'allocation-io', label: 'Allocation / I/O', needs: 'Retained observations/events with lifetime or byte semantics. Capacity, allocated bytes and RSS remain separate.'}
]);
