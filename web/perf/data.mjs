// First-party dataset validation, immutable resource loading and comparison.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import {BYTE_LIMIT, CELL_LIMIT, ROW_LIMIT, JsonNumber, parseExactJSON, rational, compareRational, exactText, median, subtract, divide, sha256} from './types.mjs';
import {readArrow, readJSONL, signature} from './arrow.mjs';
export * from './types.mjs';
export {readArrow, readJSONL, signature} from './arrow.mjs';

const demand = (condition, reason) => { if (!condition) throw new Error(reason); };
const object = value => value !== null && typeof value === 'object' && !Array.isArray(value) && !(value instanceof JsonNumber);
const scalarEqual = (left, right) =>
{
    const a = rational(left);
    const b = rational(right);
    return a && b ? compareRational(a, b) === 0 : left === right;
};

function validTimestamp(text)
{
    const parts = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?(?:Z|([+-])(\d{2}):(\d{2}))$/u.exec(text);
    if (!parts) return false;
    const [year, month, day, hour, minute, second] = parts.slice(1, 7).map(Number);
    const leap = year % 4 === 0 && (year % 100 !== 0 || year % 400 === 0);
    return month >= 1 && month <= 12 && day >= 1 && day <= [31, leap ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][month - 1] && hour <= 23 && minute <= 59 && second <= 59 && (!parts[7] || Number(parts[8]) <= 23 && Number(parts[9]) <= 59);
}

export function validateManifest(manifest, canonical)
{
    const tasks = [{schema: canonical, value: manifest, parent: -1, kind: 'all', valid: true, depth: 0, oneOf: null, matches: 0}];
    const push = (schema, value, parent, kind = 'all') =>
    {
        demand(tasks.length < 262144 && tasks[parent].depth < 128, 'Manifest schema work budget exceeded');
        tasks.push({schema, value, parent, kind, valid: true, depth: tasks[parent].depth + 1, oneOf: null, matches: 0});
    };
    for (let cursor = 0; cursor < tasks.length; ++cursor)
    {
        const task = tasks[cursor];
        const s = task.schema;
        const v = task.value;
        if (s === false) { task.valid = false; continue; }
        if (s === true) continue;
        if (s.$ref)
        {
            demand(s.$ref.startsWith('#/$defs/') && canonical.$defs[s.$ref.slice(8)], 'Unsupported canonical schema reference');
            push(canonical.$defs[s.$ref.slice(8)], v, cursor);
        }
        if (s.type)
        {
            const number = rational(v);
            task.valid &&= s.type === 'object' ? object(v) : s.type === 'array' ? Array.isArray(v) : s.type === 'string' ? typeof v === 'string' : s.type === 'boolean' ? typeof v === 'boolean' : s.type === 'null' ? v === null : s.type === 'number' ? v instanceof JsonNumber && number !== null : s.type === 'integer' ? v instanceof JsonNumber && number !== null && number.n % number.d === 0n : false;
        }
        if (Object.hasOwn(s, 'const')) task.valid &&= scalarEqual(v, s.const);
        if (s.enum) task.valid &&= s.enum.some(option => scalarEqual(v, option));
        if (v instanceof JsonNumber)
        {
            const value = rational(v);
            for (const [name, sign, exclusive] of [['minimum', -1, false], ['maximum', 1, false], ['exclusiveMinimum', -1, true], ['exclusiveMaximum', 1, true]])
            {
                if (Object.hasOwn(s, name))
                {
                    const bound = rational(s[name]);
                    const comparison = value && bound ? compareRational(value, bound) : null;
                    task.valid &&= comparison !== null && (sign < 0 ? exclusive ? comparison > 0 : comparison >= 0 : exclusive ? comparison < 0 : comparison <= 0);
                }
            }
        }
        if (typeof v === 'string')
        {
            let length = 0;
            for (const _point of v) ++length;
            task.valid &&= (s.minLength === undefined || length >= s.minLength) && (s.maxLength === undefined || length <= s.maxLength);
            if (s.pattern)
            {
                const match = new RegExp(s.pattern, 'u').exec(v);
                task.valid &&= match !== null && match.index === 0 && match[0].length === v.length;
            }
            if (s.format === 'date-time') task.valid &&= validTimestamp(v);
            if (s.format === 'uri')
            {
                try
                {
                    const url = new URL(v);
                    task.valid &&= url.protocol === 'https:' && !!url.hostname && !url.username && !url.password && !url.hash && !/[\s\\\u0000-\u001f]/u.test(v);
                }
                catch { task.valid = false; }
            }
        }
        if (Array.isArray(v))
        {
            task.valid &&= (s.minItems === undefined || v.length >= s.minItems) && (s.maxItems === undefined || v.length <= s.maxItems);
            if (s.items !== undefined) for (const item of v) push(s.items, item, cursor);
        }
        if (object(v))
        {
            for (const name of s.required ?? []) task.valid &&= Object.hasOwn(v, name);
            for (const name of Object.keys(v))
            {
                if (Object.hasOwn(s.properties ?? {}, name)) push(s.properties[name], v[name], cursor);
                else if (s.additionalProperties === false) task.valid = false;
                else if (object(s.additionalProperties)) push(s.additionalProperties, v[name], cursor);
            }
        }
        if (s.oneOf)
        {
            task.oneOf = s.oneOf.length;
            for (const alternative of s.oneOf) push(alternative, v, cursor, 'one');
        }
        if (s.not) push(s.not, v, cursor, 'not');
    }
    for (let i = tasks.length - 1; i >= 0; --i)
    {
        const task = tasks[i];
        task.valid &&= task.oneOf === null || task.matches === 1;
        if (task.parent >= 0)
        {
            const parent = tasks[task.parent];
            if (task.kind === 'one') parent.matches += task.valid;
            else parent.valid &&= task.kind === 'not' ? !task.valid : task.valid;
        }
    }
    demand(tasks[0].valid, 'Manifest violates the canonical v1 schema');
    return true;
}

export async function verifiedFetch(url, digest, size, signal)
{
    demand(typeof digest === 'string' && /^[0-9a-f]{64}$/u.test(digest), 'Invalid resource hash');
    const expected = size === undefined ? null : BigInt(size);
    demand(expected === null || expected >= 0n && expected <= BigInt(BYTE_LIMIT), 'Resource exceeds the reader byte budget');
    const response = await fetch(url, {signal, credentials: 'omit'});
    demand(response.ok && response.body, `Resource unavailable (${response.status})`);
    const chunks = [];
    let length = 0;
    const reader = response.body.getReader();
    try
    {
        while (true)
        {
            const {done, value} = await reader.read();
            if (done) break;
            length += value.byteLength;
            demand(length <= BYTE_LIMIT && (expected === null || BigInt(length) <= expected), 'Response exceeds its declared byte budget');
            chunks.push(value);
        }
        demand(expected === null || BigInt(length) === expected, 'Resource size mismatch');
    }
    catch (error) { await reader.cancel().catch(() => {}); throw error; }
    finally { reader.releaseLock(); }
    const bytes = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) { bytes.set(chunk, offset); offset += chunk.length; }
    demand(await sha256(bytes) === digest, 'Resource checksum mismatch');
    return bytes;
}

export class Dataset
{
    constructor(manifest, url, digest, canonical)
    {
        validateManifest(manifest, canonical);
        const supported = new Set(['arrow-ipc', 'jsonl.arrow-mapping.v1', 'perfetto.protobuf']);
        for (const capability of manifest.requires ?? []) demand(supported.has(capability), `Unsupported required capability: ${capability}`);
        this.manifest = manifest;
        this.url = new URL(url, globalThis.location?.href ?? 'http://localhost/');
        this.digest = digest;
        this.identities = Object.create(null);
        for (const name of ['resources', 'tables', 'metrics', 'scopes', 'instruments', 'contexts', 'traces'])
        {
            const entries = manifest[name] ?? [];
            demand(entries.length <= 4096, 'Manifest identity budget exceeded');
            this.identities[name] = new Map();
            for (const entry of entries)
            {
                demand(!this.identities[name].has(entry.id), `Duplicate ${name} identity`);
                this.identities[name].set(entry.id, entry);
            }
        }
        this.resources = this.identities.resources;
        let resourceBytes = 0n;
        for (const resource of this.resources.values())
        {
            const size = BigInt(resource.bytes);
            demand(size <= BigInt(BYTE_LIMIT) && size <= 18446744073709551615n, 'Resource byte count exceeds the reader budget');
            resourceBytes += size;
            if (resource.rows !== undefined) demand(BigInt(resource.rows) <= 18446744073709551615n, 'Resource row count overflows uint64');
            this.resourceURL(resource.id);
        }
        demand(resourceBytes <= 256n * 1024n * 1024n, 'Dataset resource budget exceeded');
        for (const context of manifest.contexts ?? []) for (const id of context.evidence ?? []) demand(this.resources.has(id), 'Unresolved context evidence');
        for (const trace of manifest.traces ?? [])
        {
            demand(this.resources.get(trace.resource)?.format === 'perfetto-protobuf' && this.identities.contexts.get(trace.execution_id)?.kind === 'execution', 'Unresolved native trace/execution');
            for (const id of trace.source_artifacts ?? []) demand(this.resources.has(id), 'Unresolved source artifact');
        }
        this.loaded = new Map();
        this.materializedCells = 0;
    }
    resourceURL(id)
    {
        const resource = this.resources.get(id);
        demand(resource, `Missing resource ${String(id)}`);
        if (resource.url)
        {
            const url = new URL(resource.url);
            demand(url.protocol === 'https:' && !url.username && !url.password && !url.hash, 'Unsafe remote resource URL');
            return url;
        }
        demand(/^[A-Za-z0-9][A-Za-z0-9._/-]*$/u.test(resource.path) && resource.path.split('/').every(part => part && part !== '.' && part !== '..'), 'Unsafe local resource path');
        const url = new URL(resource.path, this.url);
        demand(url.href.startsWith(new URL('.', this.url).href), 'Resource escapes dataset');
        return url;
    }
    async resource(id, signal)
    {
        const resource = this.resources.get(id);
        demand(resource, `Missing resource ${String(id)}`);
        return verifiedFetch(this.resourceURL(id), resource.sha256, resource.bytes, signal);
    }
    async table(id, signal)
    {
        if (this.loaded.has(id)) return this.loaded.get(id);
        const definition = this.identities.tables.get(id);
        demand(definition, `Unknown table ${String(id)}`);
        let schema;
        if (definition.schema)
        {
            demand(this.resources.get(definition.schema)?.format === 'arrow-ipc-stream', 'Invalid schema sidecar format');
            schema = readArrow(await this.resource(definition.schema, signal));
            demand(schema.rows.length === 0, 'Schema sidecar contains observations');
        }
        const rows = [];
        const seen = new Set();
        let slots = 0;
        for (const part of definition.data)
        {
            demand(!seen.has(part), 'Repeated ordered partition');
            seen.add(part);
            const resource = this.resources.get(part);
            const bytes = await this.resource(part, signal);
            let partition;
            if (resource.format === 'arrow-ipc-stream')
            {
                const arrow = readArrow(bytes);
                demand(!schema || signature(schema.fields, schema.schema.metadata) === signature(arrow.fields, arrow.schema.metadata), 'Partition Arrow schema differs');
                schema ??= arrow;
                partition = arrow.rows;
                slots += arrow.slots;
            }
            else
            {
                demand(resource.format === 'jsonl' && schema, 'JSONL requires an ordinary Arrow schema sidecar');
                const budget = {cells: this.materializedCells + slots};
                partition = readJSONL(new TextDecoder('utf-8', {fatal: true}).decode(bytes), schema.fields, budget);
                slots = budget.cells - this.materializedCells;
            }
            demand(resource.rows === undefined || BigInt(partition.length) === BigInt(resource.rows), 'Partition row count differs');
            demand(rows.length + partition.length <= ROW_LIMIT && slots + this.materializedCells <= CELL_LIMIT, 'Dataset materialized-row/cell budget exceeded');
            for (const row of partition) rows.push(row);
        }
        const fields = schema.fields;
        const names = new Set(fields.map(field => field.name));
        const column = name => demand(typeof name === 'string' && names.has(name), `Missing exact Arrow column: ${String(name)}`);
        for (const name of ['dimensions', 'primary_key', 'order_by']) for (const field of definition[name] ?? []) column(field);
        for (const [name, annotation] of Object.entries(definition.measurements ?? {}))
        {
            column(name);
            for (const [collection, key] of [['metrics', 'metric_id'], ['scopes', 'scope_id'], ['instruments', 'instrument_id']]) demand(this.identities[collection].has(annotation[key]), 'Unresolved measurement identity');
            for (const key of ['status_column', 'reason_column']) if (annotation[key]) column(annotation[key]);
            if (annotation.sample_count?.column) column(annotation.sample_count.column);
            for (const interval of annotation.intervals ?? []) { column(interval.low_column); column(interval.high_column); }
            for (const name of Object.values(annotation.counter_quality_columns ?? {})) column(name);
            for (const row of rows)
            {
                const status = annotation.status_column ? row[annotation.status_column] : 'ok';
                if (row[name] === null || status !== 'ok') demand(status !== 'ok' && typeof row[annotation.reason_column] === 'string' && row[annotation.reason_column].length, 'Missing/invalid measurement has no status and reason');
            }
        }
        const result = {definition, fields, rows, slots};
        this.materializedCells += slots;
        this.loaded.set(id, result);
        return result;
    }
}

let schemaPromise;
export async function loadDataset(entry, signal)
{
    schemaPromise ??= fetch(new URL('./buster-manifest-v1.schema.json', import.meta.url), {credentials: 'omit'}).then(response => { demand(response.ok, 'Canonical schema unavailable'); return response.json(); });
    const canonical = await schemaPromise;
    const url = new URL(entry.manifest, globalThis.location?.href ?? import.meta.url);
    const bytes = await verifiedFetch(url, entry.sha256, entry.bytes, signal);
    const manifest = parseExactJSON(new TextDecoder('utf-8', {fatal: true}).decode(bytes));
    return new Dataset(manifest, url.href, entry.sha256, canonical);
}
export function comparisonEligibility(dataset, table, annotation, left, right)
{
    const reasons = [];
    const contract = table.definition.metadata?.comparison;
    if (!contract || !['paired', 'independent'].includes(contract.estimator)) reasons.push('No declared population-matching protocol');
    if (!contract?.independent_unit) reasons.push('Independent experimental unit is unknown');
    if (!contract?.independent_unit_columns?.length) reasons.push('Independent unit identities are unknown');
    if (!contract?.match_columns?.length) reasons.push('Matched conditions are unknown');
    const identities = ['machine', 'corpus', 'harness', 'scope', 'configuration', 'profile_state'];
    if (identities.some(name => typeof contract?.identity_columns?.[name] !== 'string')) reasons.push('Machine/corpus/harness/scope/configuration/profile identities are incomplete');
    if (!contract?.correctness_column || !contract?.coverage_column) reasons.push('Correctness or coverage is unknown');
    for (const role of ['environment_context', 'protocol_context'])
    {
        const id = table.definition.bindings?.[role];
        const context = (dataset.manifest.contexts ?? []).find(item => item.id === id);
        if (!context || Object.keys(context.unknowns ?? {}).length) reasons.push(`${role.replace('_context', '')} is incomplete`);
    }
    if (dataset.manifest.status !== 'complete') reasons.push(`Dataset is ${dataset.manifest.status}`);
    if (!annotation.sample_level || annotation.sample_level === 'unknown') reasons.push('Sample level is unknown');
    if (contract)
    {
        for (const row of [...left, ...right])
        {
            if (row[contract.correctness_column] !== true || row[contract.coverage_column] !== 'complete') reasons.push('Population has failed or incomplete correctness/coverage');
        }
        if (contract.estimator === 'paired' && !contract.pair_columns?.length) reasons.push('Pair identities are unavailable');
    }
    return {eligible: reasons.length === 0, reasons: [...new Set(reasons)], contract};
}

export function comparePopulation(dataset, table, column, left, right)
{
    const annotation = table.definition.measurements?.[column];
    const metric = dataset.manifest.metrics?.find(item => item.id === annotation?.metric_id);
    if (!annotation || !metric) return {eligible: false, classification: 'ineligible', reasons: ['No declared metric semantics'], uncertainty: 'not estimated'};
    const eligibility = comparisonEligibility(dataset, table, annotation, left, right);
    const valid = row => row[column] !== null && (!annotation.status_column || row[annotation.status_column] === 'ok') && rational(row[column]) !== null;
    const a = left.filter(valid);
    const b = right.filter(valid);
    const baseline = median(a.map(row => rational(row[column])));
    const candidate = median(b.map(row => rational(row[column])));
    const result = {baseline, candidate, delta: baseline && candidate ? subtract(candidate, baseline) : null, observationsLeft: a.length, observationsRight: b.length, independentLeft: null, independentRight: null, excluded: left.length + right.length - a.length - b.length, ...eligibility};
    if (!eligibility.eligible || !baseline || !candidate) return {...result, classification: 'ineligible', uncertainty: 'not estimated'};
    if (result.excluded) return {...result, eligible: false, classification: 'ineligible', reasons: ['Missing, invalid or censored observations; no complete-population inference'], uncertainty: 'not estimated'};
    const contract = eligibility.contract;
    const key = (row, columns) => JSON.stringify(columns.map(name => exactText(row[name])));
    const matchColumns = [...new Set([...contract.match_columns, ...Object.values(contract.identity_columns), ...(annotation.derivation?.denominator && table.fields.some(field => field.name === annotation.derivation.denominator) ? [annotation.derivation.denominator] : [])])];
    const independentA = new Set(a.map(row => key(row, contract.independent_unit_columns)));
    const independentB = new Set(b.map(row => key(row, contract.independent_unit_columns)));
    result.independentLeft = independentA.size;
    result.independentRight = independentB.size;
    if (independentA.size !== a.length || independentB.size !== b.length) return {...result, eligible: false, classification: 'ineligible', reasons: ['Repeated independent unit; select one observation per unit'], uncertainty: 'not estimated'};
    if ([...a, ...b].some(row => [...matchColumns, ...contract.independent_unit_columns, ...(contract.pair_columns ?? [])].some(name => row[name] === null || row[name] === undefined || row[name] === 'unknown'))) return {...result, eligible: false, classification: 'ineligible', reasons: ['Unknown matching or unit identity'], uncertainty: 'not estimated'};
    const matchSet = new Set(a.map(row => key(row, matchColumns)));
    const candidateMatches = new Set(b.map(row => key(row, matchColumns)));
    if (matchSet.size !== candidateMatches.size || [...candidateMatches].some(value => !matchSet.has(value))) return {...result, eligible: false, classification: 'ineligible', reasons: ['Unmatched populations'], uncertainty: 'not estimated'};
    if (matchSet.size !== 1) return {...result, eligible: false, classification: 'ineligible', reasons: ['Select one matched condition or facet before estimating a population effect'], uncertainty: 'not estimated'};
    if (contract.estimator === 'independent' && [...independentA].some(id => independentB.has(id))) return {...result, eligible: false, classification: 'ineligible', reasons: ['Overlapping experimental units cannot use an independent estimator'], uncertainty: 'not estimated'};
    let effects;
    if (contract.estimator === 'paired')
    {
        const pairs = new Map();
        for (const row of a)
        {
            const id = key(row, [...matchColumns, ...contract.pair_columns]);
            if (pairs.has(id)) return {...result, eligible: false, classification: 'ineligible', reasons: ['Duplicate pair identity']};
            pairs.set(id, rational(row[column]));
        }
        const seen = new Set();
        effects = [];
        for (const row of b)
        {
            const id = key(row, [...matchColumns, ...contract.pair_columns]);
            if (!pairs.has(id) || seen.has(id)) return {...result, eligible: false, classification: 'ineligible', reasons: ['Incomplete or duplicate pairs']};
            seen.add(id);
            effects.push(subtract(rational(row[column]), pairs.get(id)));
        }
        if (seen.size !== pairs.size) return {...result, eligible: false, classification: 'ineligible', reasons: ['Incomplete pairs']};
        result.independentPairs = effects.length;
        result.delta = median(effects);
    }
    result.estimator = effects ? 'median of matched pair differences' : 'difference of independent population medians';
    if (a.length + b.length > 512) return {...result, classification: 'inference budget exceeded', uncertainty: 'not estimated (more than 512 selected independent observations); refine the population', relative: null};
    let seed = 384;
    const random = size => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed % size; };
    const estimates = [];
    if (a.length > 1 && b.length > 1)
    {
        for (let repetition = 0; repetition < 1000; ++repetition)
        {
            if (effects) estimates.push(median(effects.map(() => effects[random(effects.length)])));
            else estimates.push(subtract(median(b.map(() => rational(b[random(b.length)][column]))), median(a.map(() => rational(a[random(a.length)][column])))));
        }
    }
    estimates.sort(compareRational);
    const interval = estimates.length ? [estimates[25], estimates[974]] : null;
    const regression = interval && (metric.direction === 'lower' ? interval[0].n > 0n : metric.direction === 'higher' ? interval[1].n < 0n : false);
    const improvement = interval && (metric.direction === 'lower' ? interval[1].n < 0n : metric.direction === 'higher' ? interval[0].n > 0n : false);
    const relative = baseline.n > 0n ? divide(result.delta, baseline) : null;
    return {...result, interval, relative, classification: regression ? 'regression' : improvement ? 'improvement' : interval ? 'uncertain' : 'insufficient independent units', uncertainty: interval ? '95% percentile bootstrap, 1,000 resamples of independent ' + (effects ? 'pairs' : 'units') : 'not estimated (fewer than two independent units)'};
}
