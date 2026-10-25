/* Original static history/compare/profile application. No package setup,
 * injected renderer code, query server or upstream viewer is required.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {loadDataset, verifiedFetch, parseExactJSON, exactText, jsonExact, rationalText, comparePopulation, sha256} from './data.mjs';
import {element, renderers, specializedViews} from './charts.mjs';
import {renderProfile} from './profile-view.mjs';

const $ = id => document.getElementById(id);
const state = {catalog: null, dataset: null, table: null, rows: [], filters: new Map(), page: 0, worker: null, profile: null, request: 0, pending: new Map(), load: 0, tableLoad: 0, rangeLoad: 0, tab: 'history'};
const initial = new URLSearchParams(location.hash.slice(1));
const measurements = () => Object.keys(state.table?.definition.measurements ?? {});
const selectedColumn = () => $('metric').value;
const say = message => { $('status').textContent = message; };
const unique = (rows, name) => [...new Set(rows.map(row => exactText(row[name])))];
function options(node, entries, selected)
{
    node.replaceChildren();
    for (const [value, label] of entries) node.append(element('option', {value}, label));
    if (entries.some(entry => entry[0] === selected)) node.value = selected;
}
async function guard(action)
{
    try { await action(); }
    catch (error) { say(error.message); console.error(error); }
}
function share()
{
    const hash = new URLSearchParams();
    if (state.dataset) hash.set('dataset', state.dataset.manifest.id);
    if (state.table) hash.set('table', state.table.definition.id);
    hash.set('tab', state.tab);
    for (const id of ['metric', 'view', 'x', 'group', 'facet', 'facet-value']) if ($(id).value) hash.set(id, $(id).value);
    if (state.filters.size) hash.set('filters', JSON.stringify([...state.filters]));
    if (state.profile)
    {
        hash.set('profile', state.profile.id); hash.set('trace', state.profile.identity);
        hash.set('start', $('range-start').value); hash.set('end', $('range-end').value);
        if ($('thread').value) hash.set('tid', $('thread').value);
        if ($('cpu').value) hash.set('cpu', $('cpu').value);
    }
    history.replaceState(null, '', '#' + hash.toString());
}
function tab(name)
{
    for (const item of ['history', 'compare', 'profile', 'evidence'])
    {
        $(item + '-panel').hidden = item !== name;
        $(item + '-tab').setAttribute('aria-selected', String(item === name));
    }
    state.tab = name; share();
}
function exportRows(rows = state.rows)
{
    const output = new Blob([rows.map(row => jsonExact(row)).join('\n') + '\n'], {type: 'application/x-ndjson;charset=utf-8'});
    const url = URL.createObjectURL(output), anchor = element('a', {href: url, download: `${state.dataset.manifest.id}-${state.table.definition.id}.jsonl`});
    anchor.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
}
function inspect(row, index)
{
    $('row-details').replaceChildren(element('h3', {}, `Exact row ${index + 1}`), element('pre', {}, jsonExact(row)));
    const binding = state.table.definition.bindings?.execution_key;
    const traces = state.dataset.manifest.traces ?? [];
    if (traces.length)
    {
        $('row-details').append(element('p', {class: 'note'}, binding ? `Execution key: ${exactText(row[binding])}. Open only a profile whose execution identity matches this row.` : 'This table does not bind a per-row execution. Available profile associations remain dataset-level and are labelled separately.'));
        const linked = binding ? traces.filter(profile => profile.execution_id === exactText(row[binding])) : traces;
        if (!linked.length) $('row-details').append(element('p', {class: 'notice'}, 'No native profile is retained for this exact execution identity.'));
        for (const profile of linked)
        {
            const button = element('button', {class: 'secondary'}, `Inspect ${profile.id} · ${profile.execution_id}`);
            button.addEventListener('click', () => guard(async () => { $('profile').value = profile.id; tab('profile'); await loadProfile(profile); }));
            $('row-details').append(button);
        }
    }
}
function exactTable()
{
    const fields = state.table.fields;
    const start = state.page * 100, rows = state.rows.slice(start, start + 100);
    const table = element('table'), head = element('thead'), header = element('tr'), body = element('tbody');
    for (const field of fields) header.append(element('th', {}, field.name));
    head.append(header); table.append(head, body);
    for (const [index, row] of rows.entries())
    {
        const tr = element('tr', {tabindex: 0});
        for (const field of fields) tr.append(element('td', {title: exactText(row[field.name])}, exactText(row[field.name])));
        tr.addEventListener('click', () => inspect(row, start + index));
        tr.addEventListener('keydown', event => { if (event.key === 'Enter') inspect(row, start + index); });
        body.append(tr);
    }
    $('table-view').replaceChildren(table);
    $('row-count').textContent = `${state.rows.length.toLocaleString()} selected / ${state.table.rows.length.toLocaleString()} retained rows · ∅ = null`;
    $('page-number').textContent = `Page ${state.page + 1} / ${Math.max(1, Math.ceil(state.rows.length / 100))}`;
    $('previous-page').disabled = state.page === 0;
    $('next-page').disabled = start + 100 >= state.rows.length;
}
function filteredRows()
{
    const names = new Set(state.table.fields.map(field => field.name));
    for (const name of state.filters.keys()) if (!names.has(name)) throw new Error('Shared filter refers to a missing column');
    const binding = profileTimeBinding();
    const range = state.profile?.lastQuery;
    return state.table.rows.filter(row =>
    {
        const raw = binding ? row[binding.column] : null;
        const time = typeof raw === 'bigint' || Number.isSafeInteger(raw) ? BigInt(raw) + binding.origin : null;
        return [...state.filters].every(([name, value]) => exactText(row[name]) === value) && (!$('facet').value || $('facet-value').value === '*' || exactText(row[$('facet').value]) === $('facet-value').value) && (!binding || !range || time !== null && time >= range.start && time < range.end);
    });
}
function profileTimeBinding()
{
    const binding = state.table?.definition.bindings?.time;
    const traces = state.dataset?.manifest.traces ?? [];
    const id = binding?.trace_id ?? (traces.length === 1 ? traces[0].id : null);
    if (!state.profile || id !== state.profile.id || binding?.unit !== 'ns' || !/^\d+$/u.test(binding.origin_in_trace_ns ?? '') || !state.table.fields.some(field => field.name === binding.column)) return null;
    return {column: binding.column, origin: BigInt(binding.origin_in_trace_ns)};
}
function draw()
{
    if (!state.table) return;
    state.rows = filteredRows(); state.page = 0;
    $('active-filters').replaceChildren(...[...state.filters].map(([name, value]) => element('span', {}, `${name}: ${value}`)));
    const column = selectedColumn(), annotation = state.table.definition.measurements?.[column], metric = state.dataset.identities.metrics.get(annotation?.metric_id);
    $('metric-info').replaceChildren();
    for (const value of [metric ? `${metric.name ?? metric.id} · ${metric.unit.symbol}` : 'Preserved source values · no metric annotation', annotation ? `${annotation.sample_level} · ${annotation.statistic}` : 'No performance meaning is inferred', metric ? `Quantity: ${metric.quantity} · aggregation: ${metric.aggregation}` : 'Generic table remains available']) $('metric-info').append(element('div', {}, value));
    $('chart').replaceChildren();
    if ($('view').value === 'table') $('chart').append(element('p', {class: 'note'}, 'The exact table below preserves values without assigning graph semantics.'));
    else
    {
        try
        {
            const renderer = renderers.get($('view').value);
            if (!renderer) throw new Error('Unsupported renderer. Select an installed first-party view.');
            const binding = profileTimeBinding();
            const onTimeRange = binding && binding.column === $('x').value ? (a, b) => guard(() => applyRange(a + binding.origin, b + binding.origin)) : null;
            $('chart').append(renderer.render(state.table, {x: $('x').value, y: column, group: $('group').value, facet: $('facet').value, onInspect: inspect, onTimeRange}, state.rows));
            if (binding) $('chart').append(element('p', {class: 'note'}, 'This table explicitly binds nanoseconds into the selected native trace. Its rows follow the shared half-open range. Drag the time axis to update every profile view.'));
        }
        catch (error) { $('chart').append(element('p', {class: 'notice'}, error.message)); }
    }
    exactTable(); compareOptions(); share();
}
function facetValues(selected)
{
    const name = $('facet').value;
    options($('facet-value'), [['*', 'All values'], ...(name ? unique(state.table.rows, name).slice(0, 5000).map(value => [value, value]) : [])], selected);
}
function compareOptions()
{
    const column = $('compare-column').value;
    if (!column) return;
    const values = unique(state.rows, column).map(value => [value, value]);
    const baseline = $('baseline').value, candidate = $('candidate').value;
    options($('baseline'), values, baseline); options($('candidate'), values, candidate || values[1]?.[0]);
}
function compare()
{
    if (!state.table) return;
    const column = $('compare-column').value;
    const left = state.rows.filter(row => exactText(row[column]) === $('baseline').value), right = state.rows.filter(row => exactText(row[column]) === $('candidate').value);
    const result = comparePopulation(state.dataset, state.table, selectedColumn(), left, right);
    const parent = $('comparison'); parent.replaceChildren(element('h3', {class: result.eligible ? 'success' : 'ineligible'}, result.classification ?? 'ineligible'));
    const format = value => value ? rationalText(value, 9) : 'unavailable';
    const entries = [['Baseline descriptive median', format(result.baseline)], ['Candidate descriptive median', format(result.candidate)], ['Effect', format(result.delta)], ['Independent baseline / candidate units', `${result.independentLeft ?? 0} / ${result.independentRight ?? 0}`], ['Excluded rows', result.excluded ?? left.length + right.length], ['Estimator', result.estimator ?? 'No eligible estimator'], ['Uncertainty', result.uncertainty ?? 'Not estimated']];
    if (result.interval) entries.push(['95% interval', `${format(result.interval[0])} to ${format(result.interval[1])}`]);
    entries.push(['Relative effect', result.relative ? `${format({n: result.relative.n * 100n, d: result.relative.d})}%` : 'Unavailable for zero/negative baseline or ineligible comparison']);
    const table = element('table');
    for (const [name, value] of entries) { const row = element('tr'); row.append(element('th', {}, name), element('td', {}, value)); table.append(row); }
    parent.append(table);
    for (const reason of result.reasons ?? []) parent.append(element('p', {class: 'notice'}, reason));
    parent.append(element('p', {class: 'note'}, 'Descriptive medians do not establish a matched experiment. Phase medians are not added, inner repetitions are not promoted to independent fresh processes, and unknown identities cannot acquire automatic regression labels.'));
    window.__lastComparison = result;
}
async function loadTable(id, restore = false)
{
    const generation = state.load, tableLoad = ++state.tableLoad, dataset = state.dataset;
    state.table = null; state.rows = [];
    for (const parent of ['chart', 'table-view', 'metric-info', 'row-details']) $(parent).replaceChildren();
    $('row-count').textContent = 'Selected table is not yet verified';
    say('Verifying table partitions…');
    let table;
    try { table = await dataset.table(id); }
    catch (error) { if (generation === state.load && tableLoad === state.tableLoad) $('dataset-state').textContent = 'Selected resources unverified'; throw error; }
    if (generation !== state.load || tableLoad !== state.tableLoad) return;
    state.table = table;
    $('dataset-state').textContent = `${dataset.manifest.kind} · ${dataset.manifest.status}`;
    const fields = table.fields.map(field => [field.name, field.name]);
    const metrics = measurements();
    if (!restore) $('view').value = metrics.length ? 'lines' : 'table';
    options($('metric'), (metrics.length ? metrics : fields.map(field => field[0])).map(name => [name, name]), restore ? initial.get('metric') : metrics[0]);
    options($('x'), fields, (restore ? initial.get('x') : null) ?? table.definition.dimensions?.find(name => name !== $('metric').value) ?? fields[0]?.[0]);
    options($('group'), [['', 'No additional group'], ...fields], (restore ? initial.get('group') : null) ?? table.definition.bindings?.build_context);
    options($('facet'), [['', 'No facet'], ...fields], restore ? initial.get('facet') : '');
    facetValues(restore ? initial.get('facet-value') : '*');
    options($('compare-column'), fields, table.definition.bindings?.build_context ?? fields[0]?.[0]);
    if (restore && initial.get('filters'))
    {
        const filters = JSON.parse(initial.get('filters'));
        if (!Array.isArray(filters) || filters.length > 64 || filters.some(pair => !Array.isArray(pair) || pair.length !== 2 || pair.some(value => typeof value !== 'string'))) throw new Error('Invalid shared filters');
        state.filters = new Map(filters);
    }
    else state.filters.clear();
    draw(); say(`Verified ${table.rows.length.toLocaleString()} rows. Exact source values remain available for inspection and export.`);
    window.__performanceApp = {state, draw, compare, loadProfile, applyRange};
}
function provenance()
{
    const dataset = state.dataset, parent = $('provenance');
    parent.replaceChildren(element('h3', {}, 'Evidence and provenance'), element('p', {}, `Dataset ${dataset.manifest.id} · ${dataset.manifest.kind} · ${dataset.manifest.status}`), element('p', {class: 'mono'}, `Manifest SHA-256 ${dataset.digest}`), element('p', {class: 'note'}, 'A complete manifest describes the declared generation. Its source families may be partial, planned or unsupported. A valid hash proves content identity; it does not establish performance acceptance.'));
    const link = element('a', {href: dataset.url.href, target: '_blank', rel: 'noopener noreferrer'}, 'Original immutable manifest ↗'); parent.append(link);
    for (const limitation of dataset.manifest.limitations ?? []) parent.append(element('p', {class: 'notice'}, limitation));
    for (const context of dataset.manifest.contexts ?? []) parent.append(element('h4', {}, `${context.id} · ${context.kind}`), element('pre', {}, jsonExact(context)));
    const resources = element('details'); resources.append(element('summary', {}, 'Native resources, hashes and sizes'));
    for (const resource of dataset.resources.values())
    {
        const p = element('p'); p.append(element('a', {href: dataset.resourceURL(resource.id), target: '_blank', rel: 'noopener noreferrer'}, resource.id), element('code', {}, ` · ${resource.bytes} bytes · ${resource.sha256}`)); resources.append(p);
    }
    parent.append(resources);
}
async function loadDatasetSelection(id, restore = false)
{
    const entry = state.catalog.datasets.find(item => item.id === id);
    if (!entry) throw new Error('The linked dataset is not in this sealed generation');
    ++state.load; const generation = state.load;
    state.worker?.terminate(); state.worker = null; state.profile = null; state.table = null; state.dataset = null;
    for (const promise of state.pending.values()) promise.reject(new Error('Dataset selection changed'));
    state.pending.clear(); $('profile-views').replaceChildren(); $('range-controls').hidden = true;
    for (const parent of ['chart', 'table-view', 'metric-info', 'row-details', 'comparison', 'provenance']) $(parent).replaceChildren();
    $('dataset-title').textContent = entry.name;
    $('dataset-state').textContent = 'Unverified'; $('row-count').textContent = '';
    say('Verifying immutable manifest…');
    const dataset = await loadDataset({...entry, manifest: new URL(entry.manifest, location.href).href});
    if (generation !== state.load) return;
    if (dataset.manifest.id !== entry.id) throw new Error('Catalog and manifest identities differ');
    state.dataset = dataset;
    $('dataset-title').textContent = entry.name;
    $('dataset-description').textContent = entry.description;
    $('dataset-state').textContent = `${dataset.manifest.kind} · ${dataset.manifest.status}`;
    options($('table'), [...dataset.identities.tables.values()].map(item => [item.id, item.id]), restore ? initial.get('table') : entry.table);
    options($('profile'), [...dataset.identities.traces.values()].map(item => [item.id, `${item.id} · ${item.execution_id}`]), restore ? initial.get('profile') : undefined);
    $('load-profile').disabled = dataset.identities.traces.size === 0;
    profileLink(); provenance();
    if ($('table').value) await loadTable($('table').value, restore);
    else { $('chart').replaceChildren(element('p', {}, 'This generation has no numeric table. Inspect its native profile or provenance.')); $('table-view').replaceChildren(); say('Manifest verified; native resources load on request.'); }
    if (restore && initial.get('profile') && initial.get('trace'))
    {
        const profile = dataset.identities.traces.get(initial.get('profile'));
        if (profile) await loadProfile(profile, true);
        else say('This profile link needs the original local file; it is not in the published dataset.');
    }
    tab(restore && ['history', 'compare', 'profile', 'evidence'].includes(initial.get('tab')) ? initial.get('tab') : 'history');
}
function profileLink()
{
    const profile = state.dataset?.identities.traces.get($('profile').value), anchor = $('download-profile');
    anchor.hidden = !profile;
    if (profile) anchor.href = state.dataset.resourceURL(profile.resource);
    $('profile-context').textContent = profile ? `${profile.execution_id} · ${profile.profiler?.name ?? 'native source'} · ${profile.profiler?.schema_version ?? 'version unknown'}. ${(profile.limitations ?? []).join(' ')}` : 'This dataset has no native trace. Local files remain unverified against its execution identities.';
}
function requestWorker(type, values)
{
    const id = ++state.request;
    return new Promise((resolve, reject) =>
    {
        state.pending.set(id, {resolve, reject});
        const message = {type, id, ...values};
        state.worker.postMessage(message, type === 'load' ? [values.bytes] : []);
    });
}
async function startProfile(profile, bytes, identity, restore)
{
    state.worker?.terminate();
    for (const promise of state.pending.values()) promise.reject(new Error('Profile selection changed'));
    state.pending.clear();
    state.worker = new Worker(new URL('./trace-worker.mjs', import.meta.url), {type: 'module'});
    state.worker.onmessage = event =>
    {
        const pending = state.pending.get(event.data.id);
        if (!pending) return;
        state.pending.delete(event.data.id);
        if (event.data.type === 'error') pending.reject(new Error(event.data.message)); else pending.resolve(event.data);
    };
    state.worker.onerror = event =>
    {
        for (const promise of state.pending.values()) promise.reject(new Error(event.message || 'Native worker failed'));
        state.pending.clear();
    };
    const metadata = await requestWorker('load', {bytes: bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength), sha256: identity});
    if (restore && initial.get('trace') !== identity) throw new Error('Shared trace identity differs; range was not applied');
    state.profile = {...metadata, id: profile.id, identity};
    $('range-controls').hidden = false;
    $('range-start').value = restore && /^\d+$/u.test(initial.get('start') ?? '') ? initial.get('start') : metadata.start.toString();
    $('range-end').value = restore && /^\d+$/u.test(initial.get('end') ?? '') ? initial.get('end') : metadata.end.toString();
    options($('thread'), [['', 'All threads'], ...[...metadata.threads].map(([id, thread]) => [id, `${id} · ${thread.name}`])], restore ? initial.get('tid') : '');
    // Sample task identities can exist without a ProcessTree/ThreadDescriptor.
    const full = await requestWorker('range', {identity, start: metadata.start.toString(), end: metadata.end.toString()});
    const tids = [...new Set(full.result.samples.map(sample => sample.tid.toString()))];
    const cpus = [...new Set(full.result.samples.filter(sample => sample.cpu !== null).map(sample => sample.cpu.toString()))];
    options($('thread'), [['', 'All threads'], ...tids.map(id => [id, `${id} · ${metadata.threads.get(id)?.name ?? 'name unknown'}`])], restore ? initial.get('tid') : '');
    options($('cpu'), [['', 'All CPUs'], ...cpus.map(id => [id, `CPU ${id}`])], restore ? initial.get('cpu') : '');
    await applyRange();
    say(`Verified native trace · ${metadata.sampleCount.toLocaleString()} samples · worker initialization ${metadata.elapsed_ms.toFixed(1)} ms. No source/assembly content is assumed.`);
}
async function loadProfile(profile, restore = false)
{
    const dataset = state.dataset, generation = state.load;
    const resource = dataset.resources.get(profile.resource);
    state.worker?.terminate(); state.worker = null; state.profile = null;
    for (const pending of state.pending.values()) pending.reject(new Error('Profile selection changed'));
    state.pending.clear(); $('profile-views').replaceChildren(); $('range-controls').hidden = true;
    say(`Loading native trace (${Number(resource.bytes).toLocaleString()} bytes), then verifying its hash…`);
    let bytes;
    try { bytes = await dataset.resource(profile.resource); }
    catch (error) { $('profile-views').append(element('p', {class: 'notice'}, 'Native resource verification failed. Download/local-open remains available; no profile totals are accepted.')); throw error; }
    if (dataset !== state.dataset || generation !== state.load) return;
    await startProfile(profile, bytes, resource.sha256, restore);
}
async function applyRange(start, end)
{
    if (!state.profile) throw new Error('Load a native trace first');
    if (typeof start === 'bigint') $('range-start').value = start.toString();
    if (typeof end === 'bigint') $('range-end').value = end.toString();
    if (!/^\d+$/u.test($('range-start').value) || !/^\d+$/u.test($('range-end').value)) throw new Error('Range bounds must be integer raw nanoseconds');
    const left = BigInt($('range-start').value), right = BigInt($('range-end').value);
    if (left < state.profile.start || right > state.profile.end || left >= right) throw new Error('Range must be nonempty and inside the retained trace bounds');
    const identity = state.profile.identity;
    const rangeLoad = ++state.rangeLoad;
    const filters = {};
    if ($('thread').value) filters.tid = $('thread').value;
    if ($('cpu').value) filters.cpu = $('cpu').value;
    const response = await requestWorker('range', {identity, start: left.toString(), end: right.toString(), filters});
    if (state.profile?.identity !== identity || rangeLoad !== state.rangeLoad || $('range-start').value !== left.toString() || $('range-end').value !== right.toString()) return;
    renderProfile($('profile-views'), state.profile, response.result, (a, b) => guard(() => applyRange(a, b)));
    state.profile.lastQuery = response.result; state.profile.range_ms = response.elapsed_ms;
    if (profileTimeBinding()) draw();
    share(); say(`Shared selection [${left}, ${right}) ns · ${response.result.sampleCount} samples · native query ${response.elapsed_ms.toFixed(1)} ms.`);
}
async function census()
{
    const entry = state.catalog.census;
    if (!entry) return;
    const bytes = await verifiedFetch(new URL(entry.url, location.href), entry.sha256, entry.bytes);
    const families = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(bytes));
    if (!Array.isArray(families) || families.length !== 19) throw new Error('Frozen family census differs');
    const container = $('family-census'); container.append(element('h3', {}, 'Frozen archive · 19 families'));
    for (const family of families)
    {
        const item = element('div', {class: 'family-item'});
        item.append(element('strong', {}, family.family), element('span', {}, `${family.disposition} · ${family.files} paths · ${family.source_rows} preserved rows`));
        item.title = `${family.scope}\n${family.reason}`;
        const dataset = state.catalog.datasets.find(candidate => candidate.family === family.family);
        if (dataset)
        {
            const button = element('button', {}, 'Open family evidence');
            button.addEventListener('click', () => guard(async () => { $('dataset').value = dataset.id; await loadDatasetSelection(dataset.id); }));
            item.append(button);
        }
        container.append(item);
    }
}
async function main()
{
    const response = await fetch(new URL('./catalog.json', import.meta.url), {credentials: 'omit'});
    if (!response.ok || Number(response.headers.get('Content-Length') ?? 0) > 262144) throw new Error('Sealed catalog unavailable or oversized');
    const content = new Uint8Array(await response.arrayBuffer());
    if (content.length > 262144) throw new Error('Catalog exceeds 256 KiB');
    const catalog = JSON.parse(new TextDecoder('utf-8', {fatal: true}).decode(content));
    if (catalog.version !== 1 || catalog.status !== 'complete' || !/^[0-9a-f]{40}$/u.test(catalog.evidence_commit) || !/^[0-9a-f]{40}$/u.test(catalog.source_commit) || !Array.isArray(catalog.datasets) || catalog.datasets.length > 128) throw new Error('Unsupported/incomplete catalog generation');
    state.catalog = catalog;
    $('generation').textContent = `Reviewed source ${catalog.source_commit.slice(0, 12)} · evidence ${catalog.evidence_commit.slice(0, 12)} · ${catalog.recorded_at}`;
    $('catalog-status').textContent = `${catalog.datasets.length} datasets · ${content.length.toLocaleString()} catalog bytes · frozen input, not a live completeness claim`;
    options($('dataset'), catalog.datasets.map(entry => [entry.id, entry.name]), initial.get('dataset') ?? catalog.datasets[0]?.id);
    options($('view'), [...[...renderers].map(([id, renderer]) => [id, renderer.name]), ['table', 'Exact table']], initial.get('view') ?? 'lines');
    for (const view of specializedViews)
    {
        const item = element('div'); item.append(element('strong', {}, view.label), element('p', {class: 'note'}, view.needs)); $('specialized-list').append(item);
    }
    for (const name of ['history', 'compare', 'profile', 'evidence']) $(name + '-tab').addEventListener('click', () => tab(name));
    $('dataset').addEventListener('change', () => guard(() => loadDatasetSelection($('dataset').value)));
    $('table').addEventListener('change', () => guard(() => loadTable($('table').value)));
    for (const name of ['metric', 'view', 'x', 'group', 'facet-value']) $(name).addEventListener('change', () => guard(draw));
    $('facet').addEventListener('change', () => guard(() => { facetValues('*'); draw(); }));
    $('add-filter').addEventListener('click', () => guard(() => { if ($('facet').value && $('facet-value').value !== '*') state.filters.set($('facet').value, $('facet-value').value); draw(); }));
    $('reset-filters').addEventListener('click', () => guard(() => { state.filters.clear(); $('facet').value = ''; facetValues('*'); draw(); }));
    $('previous-page').addEventListener('click', () => { --state.page; exactTable(); });
    $('next-page').addEventListener('click', () => { ++state.page; exactTable(); });
    $('compare-column').addEventListener('change', compareOptions);
    $('compare').addEventListener('click', () => guard(compare));
    $('export').addEventListener('click', () => exportRows());
    $('profile').addEventListener('change', profileLink);
    $('load-profile').addEventListener('click', () => guard(() => loadProfile(state.dataset.identities.traces.get($('profile').value))));
    $('apply-range').addEventListener('click', () => guard(() => applyRange()));
    $('full-range').addEventListener('click', () => guard(() => applyRange(state.profile.start, state.profile.end)));
    for (const name of ['thread', 'cpu']) $(name).addEventListener('change', () => guard(() => applyRange()));
    $('local-profile').addEventListener('change', () => guard(async () =>
    {
        const file = $('local-profile').files[0];
        if (!file || file.size > 64 * 1024 * 1024) throw new Error('Local native trace exceeds the 64 MiB reader budget');
        const bytes = new Uint8Array(await file.arrayBuffer()), identity = await sha256(bytes);
        $('profile-context').textContent = `${file.name} · local native input; source/execution identity is unknown. A shared link needs this exact file again.`;
        await startProfile({id: 'local'}, bytes, identity, false);
    }));
    await loadDatasetSelection($('dataset').value, true);
    await census();
    window.__performanceReady = {passed: true, catalogBytes: content.length, source: catalog.source_commit, evidence: catalog.evidence_commit};
}
guard(main);
