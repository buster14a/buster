/* Original sampled-profile views, sharing one exact time selection.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {element, svgElement, palette} from './charts.mjs';
import {coordinate, rational, exactText} from './types.mjs';

const text = value => exactText(value);
const appendTitle = (node, value) => { node.append(svgElement('title', {}, value)); return node; };
const position = (time, start, end) => 120 + 800 * coordinate(rational(time), rational(start), rational(end));
function section(name, description)
{
    const node = element('section', {class: 'profile-section'});
    node.append(element('h3', {}, name), element('p', {class: 'note'}, description));
    return node;
}
function rangeSelection(svg, start, end, onRange)
{
    let anchor = null;
    const timeAt = event =>
    {
        const bounds = svg.getBoundingClientRect();
        const x = (event.clientX - bounds.left) / bounds.width * 960;
        const scaled = Math.round(Math.max(0, Math.min(1, (x - 120) / 800)) * 1000000);
        return start + (end - start) * BigInt(scaled) / 1000000n;
    };
    svg.addEventListener('pointerdown', event =>
    {
        if (event.button !== 0) return;
        anchor = timeAt(event); svg.setPointerCapture(event.pointerId);
    });
    svg.addEventListener('pointerup', event =>
    {
        if (anchor === null) return;
        const tail = timeAt(event), head = anchor; anchor = null;
        if (tail !== head) onRange(head < tail ? head : tail, head < tail ? tail : head);
    });
    svg.addEventListener('pointercancel', () => { anchor = null; });
}
function sampleDetails(parent, sample)
{
    parent.replaceChildren(element('h3', {}, `Sample at ${sample.time} ns`), element('p', {}, `PID ${sample.pid} · TID ${sample.tid} · CPU ${sample.cpu ?? 'unknown'} · ${sample.source} mode ${sample.mode}`), element('p', {class: 'note'}, sample.weight !== null ? `${sample.weight} source counter units. ${sample.weightDescriptor.description}` : 'No per-sample source weight is available; this observation counts as one sample.'));
    if (sample.unwind) parent.append(element('p', {class: 'notice'}, sample.unwind));
    const list = element('ol');
    for (const frame of sample.stack)
    {
        const item = element('li');
        item.append(element('strong', {}, frame.name), element('code', {}, ` ${frame.pc === null ? 'unknown PC' : '0x' + frame.pc.toString(16)} · ${frame.mapping || 'unverified mapping'} · build ${frame.build ?? 'unknown'}`));
        if (frame.source) item.append(element('p', {}, `Source reported by trace: ${frame.source}:${frame.line ?? '?'} (matching source content is not available for opening)`));
        list.append(item);
    }
    parent.append(list, element('p', {class: 'note'}, 'Symbol names require the matching binary in the capture adapter. This view does not open arbitrary source paths or claim assembly/source content from an address alone.'));
}
export function renderProfile(parent, metadata, query, onRange)
{
    parent.replaceChildren();
    const banner = element('div', {class: 'stats'});
    banner.append(element('div', {}, `${query.sampleCount.toLocaleString()} / ${query.totalSamples.toLocaleString()} samples selected`), element('div', {}, `${metadata.lost} reported lost · ${metadata.skipped} skipped`), element('div', {}, `[${query.start}, ${query.end}) ns`));
    parent.append(banner);
    const details = element('details', {class: 'quality'}), summary = element('summary', {}, 'Source capabilities and quality');
    details.append(summary, element('pre', {}, JSON.stringify(metadata.capabilities, null, 2)), element('p', {class: 'note'}, 'Counts and source period weights are statistical evidence. Instrumented intervals are separate measurements. Missing samples do not establish idle time. Initial/final scheduling coverage, task reuse and source content can remain unknown.'));
    for (const warning of metadata.warnings) details.append(element('p', {class: 'notice'}, warning));
    if (query.unwindDiagnostics) details.append(element('p', {class: 'notice'}, `${query.unwindDiagnostics} selected samples have an explicit unwind diagnostic. Absence of a diagnostic does not prove complete unwinding.`));
    for (const weight of query.weights) details.append(element('p', {}, `${weight.name}: ${weight.value} in native unit ${weight.unit}; ${weight.samples} samples. ${weight.description}`));
    if (query.unknownWeights) details.append(element('p', {}, `${query.unknownWeights} selected samples have no supported per-sample weight.`));
    parent.append(details);
    const flame = section('Flame graph & call tree', 'Width is selected sample count. Each row follows root → leaf; counts are not invocation counts or instrumented function durations.');
    const nodes = [], pending = [{node: query.tree, x: 0, depth: 0, width: 1}];
    while (pending.length && nodes.length < 8000)
    {
        const current = pending.pop();
        nodes.push(current);
        let offset = current.x;
        for (const child of current.node.children.values())
        {
            const width = query.sampleCount ? child.count / query.sampleCount : 0;
            pending.push({node: child, x: offset, width, depth: current.depth + 1});
            offset += width;
        }
    }
    const depth = nodes.reduce((a, b) => Math.max(a, b.depth), 0);
    const svg = svgElement('svg', {viewBox: `0 0 960 ${Math.max(40, (depth + 1) * 24)}`, role: 'img', 'aria-label': 'Sample-count flame graph', class: 'flame'});
    for (const {node, x, width, depth} of nodes)
    {
        if (!node.count || width < 0.0003) continue;
        const rect = appendTitle(svgElement('rect', {x: x * 960, y: depth * 24, width: Math.max(0.2, width * 960 - 1), height: 22, fill: palette[depth % palette.length], tabindex: 0}), `${node.name}\nInclusive samples: ${node.count}\nLeaf samples: ${node.self}`);
        svg.append(rect);
        if (width > 0.04) svg.append(svgElement('text', {x: x * 960 + 5, y: depth * 24 + 15, fill: '#fff', 'font-size': 11, 'pointer-events': 'none'}, `${node.name} (${node.count})`.slice(0, Math.floor(width * 130))));
    }
    flame.append(svg);
    if (pending.length) flame.append(element('p', {class: 'notice'}, 'Flame drawing limited to 8,000 nodes; exact function totals and native download remain available.'));
    const tree = element('details'), treeSummary = element('summary', {}, 'Call tree paths and inclusive / leaf sample counts');
    tree.append(treeSummary);
    const treeTable = element('table'), treeHead = element('tr');
    for (const name of ['Call path', 'Inclusive samples', 'Leaf samples']) treeHead.append(element('th', {}, name));
    treeTable.append(treeHead);
    for (const entry of nodes.slice(1, 2000))
    {
        const row = element('tr');
        row.append(element('td', {style: `padding-left:${Math.min(entry.depth, 12) * 12}px`}, entry.node.name), element('td', {}, entry.node.count), element('td', {}, entry.node.self)); treeTable.append(row);
    }
    tree.append(treeTable); flame.append(tree); parent.append(flame);
    const functions = section('Self & inclusive attribution', 'Inclusive means a sample contains the function; recursive occurrences count once per sample. Self means it is the leaf of the retained stack.');
    const functionTable = element('table'), functionHead = element('tr');
    for (const name of ['Function', 'Self samples', 'Inclusive samples', 'Mapping / build']) functionHead.append(element('th', {}, name));
    functionTable.append(functionHead);
    for (const item of query.functions.slice(0, 1000))
    {
        const row = element('tr');
        row.append(element('td', {}, item.name), element('td', {}, item.self), element('td', {}, item.count), element('td', {class: 'mono'}, `${item.mapping || 'unverified'} · ${item.build ?? 'unknown'}`)); functionTable.append(row);
    }
    functions.append(element('div', {class: 'scroll'})); functions.lastChild.append(functionTable); parent.append(functions);
    const chronology = section('Chronological sampled stacks', 'Each vertical mark is one timestamped stack snapshot; its width carries no duration. Drag horizontally to select a range shared by every profile view. Raw nanosecond inputs remain exact.');
    const lanes = [...new Set(query.samples.map(sample => `TID ${sample.tid} / CPU ${sample.cpu ?? '?'}`))];
    const laneHeight = 90;
    const timeline = svgElement('svg', {viewBox: `0 0 960 ${Math.max(130, lanes.length * laneHeight + 35)}`, role: 'img', 'aria-label': 'Chronological sampled stacks; drag to select time', class: 'timeline'});
    const samplePanel = element('div', {class: 'sample-detail'});
    let marks = 0;
    for (const [index, lane] of lanes.entries()) timeline.append(svgElement('text', {x: 5, y: index * laneHeight + 22, class: 'axis'}, lane));
    for (const sample of query.samples)
    {
        const lane = lanes.indexOf(`TID ${sample.tid} / CPU ${sample.cpu ?? '?'}`);
        const levels = Math.min(sample.stack.length, 16);
        for (let level = 0; level < levels && marks++ < 50000; ++level)
        {
            const frame = sample.stack[level];
            const mark = appendTitle(svgElement('rect', {x: position(sample.time, query.start, query.end), y: lane * laneHeight + 28 + level * 3, width: 2, height: 3, fill: palette[level % palette.length], tabindex: 0}), `${sample.time} ns\n${frame.name}\nTID ${sample.tid} CPU ${sample.cpu}`);
            mark.addEventListener('click', () => { sampleDetails(samplePanel, sample); samplePanel.scrollIntoView({block: 'nearest'}); });
            timeline.append(mark);
        }
    }
    timeline.append(svgElement('text', {x: 120, y: Math.max(120, lanes.length * laneHeight + 24), class: 'axis'}, `${query.start} ns`), svgElement('text', {x: 920, y: Math.max(120, lanes.length * laneHeight + 24), 'text-anchor': 'end', class: 'axis'}, `${query.end} ns`));
    rangeSelection(timeline, query.start, query.end, onRange);
    chronology.append(timeline, samplePanel);
    if (query.timelineTruncated || marks > 50000) chronology.append(element('p', {class: 'notice'}, 'Chronological drawing uses at most 20,000 samples / 50,000 frame marks. Query totals cover the complete selected population.'));
    parent.append(chronology);
    const spans = section('Scheduling & instrumented intervals', 'Only retained source intervals are shown. Sample gaps have no implied thread state. Paired perf switch intervals retain their source label; unknown initial/final edges are omitted.');
    const intervals = [...query.intervals.map(item => ({start: item.time, end: item.end, name: item.name, lane: metadata.tracks.get(item.track)?.name ?? item.track, depth: item.depth})), ...query.running.map(item => ({...item, name: `Observed running: TID ${item.tid} on CPU ${item.cpu}`, lane: `CPU ${item.cpu}`, depth: 0})), ...query.offCpu.map(item => ({...item, name: `${item.runnable ? 'Runnable after switch-out' : 'Off CPU; wakeup boundary/reason unknown'}: TID ${item.tid}; source post-state ${item.postState}; endpoint CPUs ${item.fromCpu} → ${item.toCpu}`, lane: `TID ${item.tid} off CPU`, depth: 1}))];
    if (intervals.length)
    {
        const names = [...new Set(intervals.map(item => item.lane))];
        const picture = svgElement('svg', {viewBox: `0 0 960 ${names.length * 54 + 25}`, role: 'img', 'aria-label': 'Source scheduling and instrumentation intervals', class: 'timeline'});
        for (const [index, name] of names.entries()) picture.append(svgElement('text', {x: 4, y: index * 54 + 17, class: 'axis'}, name.slice(0, 25)));
        for (const item of intervals.slice(0, 10000))
        {
            const start = item.start < query.start ? query.start : item.start;
            const end = item.end > query.end ? query.end : item.end;
            const x = position(start, query.start, query.end), width = Math.max(2, position(end, query.start, query.end) - x);
            picture.append(appendTitle(svgElement('rect', {x, y: names.indexOf(item.lane) * 54 + 22 + Math.min(item.depth, 4) * 4, width, height: 8, fill: palette[Math.min(item.depth, 7)]}), `${item.name}\n[${item.start},${item.end}) ns\nSource duration ${item.end - item.start} ns\nSelected overlap ${end - start} ns`));
        }
        for (const flow of query.flows)
        {
            const from = metadata.tracks.get(flow.from.track)?.name, to = metadata.tracks.get(flow.to.track)?.name;
            const a = names.indexOf(from), b = names.indexOf(to);
            if (a >= 0 && b >= 0) picture.append(appendTitle(svgElement('path', {d: `M${position(flow.from.time, query.start, query.end)},${a * 54 + 26}L${position(flow.to.time, query.start, query.end)},${b * 54 + 26}`, stroke: '#b4552d', fill: 'none', 'stroke-width': 2}), `Explicit source flow ${flow.id}; ${flow.from.time} → ${flow.to.time} ns. Correlation is not a dependency.`));
        }
        rangeSelection(picture, query.start, query.end, onRange); spans.append(picture);
    }
    else spans.append(element('p', {class: 'notice'}, 'No scheduling/instrumentation interval is available in this source and selection.'));
    for (const [name, duration] of query.observedRunning) spans.append(element('p', {}, `${name}: ${duration} ns of verified running overlap.`));
    for (const item of query.migrations) spans.append(element('p', {}, `Observed CPU transition for TID ${item.tid}: CPU ${item.fromCpu} at switch-out ${item.start} ns → CPU ${item.toCpu} at switch-in ${item.end} ns. The exact migration instant and task-reuse history remain unknown.`));
    spans.append(element('p', {class: 'note'}, `${query.flows.length} explicit source flows overlap this range. Off-CPU blocking reasons, general PID/TID lifetime reconstruction and assembly are unavailable.`));
    parent.append(spans);
    const counters = section('Correlated counters', 'Each track retains its own unit. Points are source readings; a line between readings is a visual guide, not an observed continuous signal.');
    const counterTracks = [...new Set(query.counters.map(item => item.track))];
    for (const id of counterTracks)
    {
        const values = query.counters.filter(item => item.track === id), track = metadata.tracks.get(id);
        const sorted = values.map(item => rational(item.value)).sort((a, b) => a.n * b.d < b.n * a.d ? -1 : a.n * b.d > b.n * a.d ? 1 : 0);
        const picture = svgElement('svg', {viewBox: '0 0 960 135', role: 'img', 'aria-label': `${track.name} counter in ${track.unit}`, class: 'timeline'});
        picture.append(svgElement('text', {x: 4, y: 20, class: 'axis'}, `${track.name} (${track.unit})`));
        let path = '';
        for (const item of values)
        {
            const x = position(item.time, query.start, query.end), y = 112 - 75 * coordinate(rational(item.value), sorted[0], sorted.at(-1));
            path += `${path ? 'L' : 'M'}${x},${y}`;
            picture.append(appendTitle(svgElement('circle', {cx: x, cy: y, r: 4, fill: palette[0]}), `${item.time} ns: ${text(item.value)} ${item.unit}`));
        }
        picture.prepend(svgElement('path', {d: path, stroke: palette[0], 'stroke-dasharray': '3 3', fill: 'none'}));
        rangeSelection(picture, query.start, query.end, onRange); counters.append(picture);
    }
    if (!counterTracks.length) counters.append(element('p', {class: 'notice'}, 'No counter observations are available in this source/range. Whole-execution peak RSS is not a time series.'));
    parent.append(counters);
}
