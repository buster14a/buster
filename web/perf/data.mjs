// Browser-side Arrow reading. Exact values survive ingestion and inspection;
// conversion to Number is confined to physical floating types and plot pixels.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
export class JsonNumber
{
    constructor(text) { this.text = text; }
    toString() { return this.text; }
}

export function parseExactJSON(text)
{
    const tokens = [];
    let output = '';
    let quoted = false;
    let escaped = false;
    for (let i = 0; i < text.length; ++i)
    {
        const c = text[i];
        if (quoted)
        {
            output += c;
            if (escaped) escaped = false;
            else if (c === '\\') escaped = true;
            else if (c === '"') quoted = false;
        }
        else if (c === '"')
        {
            quoted = true;
            output += c;
        }
        else if (c === '-' || (c >= '0' && c <= '9'))
        {
            const start = i;
            while (i + 1 < text.length && !/[\s,\]}:]/u.test(text[i + 1])) ++i;
            const number = text.slice(start, i + 1);
            if (!/^-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?$/u.test(number)) throw new Error('Invalid JSON number');
            output += JSON.stringify(`\u0000buster-number:${tokens.length}`);
            tokens.push(number);
        }
        else output += c;
    }
    return JSON.parse(output, (_key, value) =>
    {
        if (typeof value === 'string' && value.startsWith('\u0000buster-number:'))
        {
            const index = value.slice(15);
            if (!/^(0|[1-9][0-9]*)$/u.test(index) || tokens[index] === undefined) throw new Error('Invalid numeric marker');
            return new JsonNumber(tokens[index]);
        }
        if (typeof value === 'string' && value.includes('\u0000')) throw new Error('Embedded NUL is unsupported');
        return value;
    });
}

export class Decimal
{
    constructor(unscaled, scale)
    {
        this.unscaled = BigInt(unscaled);
        this.scale = scale;
    }
    toString()
    {
        const sign = this.unscaled < 0n ? '-' : '';
        let digits = (this.unscaled < 0n ? -this.unscaled : this.unscaled).toString();
        if (this.scale <= 0) return sign + digits + '0'.repeat(-this.scale);
        digits = digits.padStart(this.scale + 1, '0');
        return `${sign}${digits.slice(0, -this.scale)}.${digits.slice(-this.scale)}`;
    }
    toJSON() { return this.toString(); }
}

export function decimalFromString(text, scale, precision)
{
    if (typeof text !== 'string' || !/^-?(0|[1-9][0-9]*)(\.[0-9]+)?$/u.test(text)) throw new Error('Decimal must be an exact string');
    if (Math.abs(scale) > 76 || precision > 76) throw new Error('Decimal mapping exceeds supported scale/precision');
    const negative = text.startsWith('-');
    const [whole, fraction = ''] = (negative ? text.slice(1) : text).split('.');
    let digits = BigInt(whole + fraction);
    const shift = scale - fraction.length;
    if (shift >= 0) digits *= 10n ** BigInt(shift);
    else
    {
        const divisor = 10n ** BigInt(-shift);
        if (digits % divisor !== 0n) throw new Error('Decimal would round');
        digits /= divisor;
    }
    if (digits.toString().length > precision) throw new Error('Decimal precision overflow');
    return new Decimal(negative ? -digits : digits, scale);
}

function arrowValue(type, value)
{
    if (value === null) return null;
    switch (type.typeId)
    {
        case 2:
            if (type.bitWidth === 64 && typeof value !== 'bigint') throw new Error('Reader did not retain exact int64');
            return value;
        case 7:
        {
            let integer = 0n;
            for (let i = value.length - 1; i >= 0; --i) integer = (integer << 32n) | BigInt(value[i] >>> 0);
            if ((value[value.length - 1] & 0x80000000) !== 0) integer -= 1n << BigInt(value.length * 32);
            return new Decimal(integer, type.scale);
        }
        case 12:
        case 16: return Array.from(value, item => arrowValue(type.children[0].type, item));
        case 13: return Object.fromEntries(type.children.map(field => [field.name, arrowValue(field.type, value[field.name])]));
        default: return value;
    }
}

export function readArrow(bytes)
{
    if (!globalThis.Arrow) throw new Error('The pinned Arrow reader is unavailable');
    const native = globalThis.Arrow.tableFromIPC(bytes);
    const fields = native.schema.fields;
    const names = new Set();
    for (const field of fields)
    {
        if (names.has(field.name)) throw new Error('Duplicate Arrow field');
        names.add(field.name);
    }
    const rows = [];
    for (let r = 0; r < native.numRows; ++r)
    {
        const row = Object.create(null);
        for (const field of fields) row[field.name] = arrowValue(field.type, native.getChild(field.name).get(r));
        rows.push(row);
    }
    return {fields, rows, native};
}

function jsonValue(field, value)
{
    const type = field.type;
    if (value === null)
    {
        if (!field.nullable) throw new Error(`Null in nonnullable ${field.name}`);
        return null;
    }
    switch (type.typeId)
    {
        case 2:
        {
            const wide = type.bitWidth === 64;
            const text = wide ? value : value instanceof JsonNumber ? value.text : '';
            if (typeof text !== 'string' || !/^-?(0|[1-9][0-9]*)$/u.test(text)) throw new Error(`Invalid integer mapping: ${field.name}`);
            const integer = BigInt(text);
            const limit = 1n << BigInt(type.bitWidth - (type.isSigned ? 1 : 0));
            const minimum = type.isSigned ? -limit : 0n;
            if (integer < minimum || integer >= limit) throw new Error('Integer overflow');
            return wide ? integer : Number(integer);
        }
        case 7: return decimalFromString(value, type.scale, type.precision);
        case 3:
        {
            if (!(value instanceof JsonNumber)) throw new Error('Float must use JSON number');
            const number = Number(value.text);
            if (!Number.isFinite(number)) throw new Error('Nonfinite JSON float');
            if (type.precision === 0) throw new Error('JSON float16 mapping is unavailable in this browser');
            return type.precision === 1 ? Math.fround(number) : number;
        }
        case 5:
        case 20:
            if (typeof value !== 'string') throw new Error('UTF-8 must use a JSON string');
            return value;
        case 6:
            if (typeof value !== 'boolean') throw new Error('Boolean mapping mismatch');
            return value;
        case 4:
        case 15:
        case 19:
            if (typeof value !== 'string') throw new Error('Binary must use base64');
            return Uint8Array.from(atob(value), c => c.charCodeAt(0));
        case 12:
        case 16:
            if (!Array.isArray(value)) throw new Error('List mapping mismatch');
            return value.map(item => jsonValue(type.children[0], item));
        case 13:
            if (!value || Array.isArray(value) || Object.keys(value).length !== type.children.length) throw new Error('Struct mapping mismatch');
            return Object.fromEntries(type.children.map(child => [child.name, jsonValue(child, value[child.name])]));
        default: throw new Error(`Native-only Arrow type ${String(type)}`);
    }
}

export function readJSONL(text, fields)
{
    const rows = [];
    for (const line of text.split('\n'))
    {
        if (!line.trim()) continue;
        const row = parseExactJSON(line);
        if (Array.isArray(row) || !row || Object.keys(row).length !== fields.length) throw new Error('JSONL field set differs');
        rows.push(Object.fromEntries(fields.map(field =>
        {
            if (!Object.hasOwn(row, field.name)) throw new Error(`Missing exact field ${field.name}`);
            return [field.name, jsonValue(field, row[field.name])];
        })));
    }
    return rows;
}

export function exactText(value)
{
    if (value === null || value === undefined) return '∅';
    if (value instanceof Decimal || value instanceof JsonNumber || typeof value === 'bigint') return value.toString();
    if (ArrayBuffer.isView(value)) return btoa(String.fromCharCode(...value));
    if (typeof value === 'object') return JSON.stringify(value, (_key, child) => typeof child === 'bigint' ? child.toString() : child);
    return String(value);
}

export function equivalent(left, right, fields)
{
    return left.length === right.length && left.every((row, r) => fields.every(field => exactText(row[field.name]) === exactText(right[r][field.name])));
}

export function rational(value)
{
    if (value instanceof Decimal)
    {
        return value.scale >= 0 ? {n: value.unscaled, d: 10n ** BigInt(value.scale)} : {n: value.unscaled * 10n ** BigInt(-value.scale), d: 1n};
    }
    if (typeof value === 'bigint') return {n: value, d: 1n};
    if (typeof value === 'number' && Number.isFinite(value))
    {
        const [mantissa, exponent = '0'] = value.toString().split('e');
        const fraction = mantissa.split('.')[1]?.length ?? 0;
        const shift = Number(exponent) - fraction;
        const integer = BigInt(mantissa.replace('.', ''));
        return shift >= 0 ? {n: integer * 10n ** BigInt(shift), d: 1n} : {n: integer, d: 10n ** BigInt(-shift)};
    }
    return null;
}

export const compareRational = (a, b) => a.n * b.d < b.n * a.d ? -1 : a.n * b.d > b.n * a.d ? 1 : 0;
export const subtract = (a, b) => ({n: a.n * b.d - b.n * a.d, d: a.d * b.d});
export const divide = (a, b) => ({n: a.n * b.d * (b.n < 0n ? -1n : 1n), d: a.d * (b.n < 0n ? -b.n : b.n)});

export function coordinate(value, minimum, maximum)
{
    // Approximate only the exact difference/extent at the rendering boundary.
    const extent = subtract(maximum, minimum);
    if (extent.n === 0n) return 0.5;
    const position = divide(subtract(value, minimum), extent);
    return Number(position.n) / Number(position.d);
}

export function median(values)
{
    const ordered = [...values].sort(compareRational);
    if (!ordered.length) return null;
    const middle = Math.floor(ordered.length / 2);
    if (ordered.length % 2) return ordered[middle];
    const a = ordered[middle - 1];
    const b = ordered[middle];
    return {n: a.n * b.d + b.n * a.d, d: 2n * a.d * b.d};
}

export function rationalText(value, places = 6)
{
    if (!value) return 'unavailable';
    const negative = value.n < 0n;
    const numerator = negative ? -value.n : value.n;
    const whole = numerator / value.d;
    const fraction = ((numerator % value.d) * 10n ** BigInt(places) / value.d).toString().padStart(places, '0').replace(/0+$/u, '');
    return `${negative ? '-' : ''}${whole}${fraction ? '.' + fraction : ''}`;
}

export async function sha256(bytes)
{
    const digest = await crypto.subtle.digest('SHA-256', bytes);
    return Array.from(new Uint8Array(digest), byte => byte.toString(16).padStart(2, '0')).join('');
}

export async function verifiedFetch(url, digest, size, signal)
{
    const response = await fetch(url, {signal, credentials: 'omit'});
    if (!response.ok) throw new Error(`Resource unavailable (${response.status})`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (size !== undefined && BigInt(bytes.byteLength) !== BigInt(size)) throw new Error('Resource size mismatch');
    if (await sha256(bytes) !== digest) throw new Error('Resource checksum mismatch');
    return bytes;
}

export class Dataset
{
    constructor(manifest, url, digest)
    {
        if (manifest.format !== 'buster-benchmark' || !(manifest.version instanceof JsonNumber) || manifest.version.text !== '1') throw new Error('Unsupported benchmark version');
        const supported = new Set(['arrow-ipc', 'jsonl.arrow-mapping.v1', 'perfetto.protobuf']);
        for (const capability of manifest.requires ?? []) if (!supported.has(capability)) throw new Error(`Unsupported required capability: ${capability}`);
        this.manifest = manifest;
        this.url = new URL(url, location.href);
        this.digest = digest;
        this.resources = new Map();
        for (const resource of manifest.resources)
        {
            if (this.resources.has(resource.id)) throw new Error('Duplicate resource identity');
            this.resources.set(resource.id, resource);
        }
        this.loaded = new Map();
    }
    resourceURL(id)
    {
        const resource = this.resources.get(id);
        if (!resource) throw new Error(`Missing resource ${id}`);
        if (resource.url)
        {
            const url = new URL(resource.url);
            if (url.protocol !== 'https:' || url.username || url.password || url.hash) throw new Error('Unsafe remote resource URL');
            return url;
        }
        if (!/^[A-Za-z0-9][A-Za-z0-9._/-]*$/u.test(resource.path) || resource.path.split('/').some(part => !part || part === '.' || part === '..')) throw new Error('Unsafe local resource path');
        const url = new URL(resource.path, this.url);
        if (!url.href.startsWith(new URL('.', this.url).href)) throw new Error('Resource escapes dataset');
        return url;
    }
    async resource(id, signal)
    {
        const resource = this.resources.get(id);
        return verifiedFetch(this.resourceURL(id), resource.sha256, resource.bytes, signal);
    }
    async table(id, signal)
    {
        if (this.loaded.has(id)) return this.loaded.get(id);
        const definition = (this.manifest.tables ?? []).find(table => table.id === id);
        if (!definition) throw new Error('Unknown table');
        let fields;
        if (definition.schema)
        {
            const schema = readArrow(await this.resource(definition.schema, signal));
            if (schema.rows.length) throw new Error('Schema sidecar has observations');
            fields = schema.fields;
        }
        const rows = [];
        const seen = new Set();
        for (const part of definition.data)
        {
            if (seen.has(part)) throw new Error('Repeated partition');
            seen.add(part);
            const resource = this.resources.get(part);
            const bytes = await this.resource(part, signal);
            let partition;
            if (resource.format === 'arrow-ipc-stream')
            {
                const arrow = readArrow(bytes);
                if (fields && signature(fields) !== signature(arrow.fields)) throw new Error('Partition schema differs');
                fields ??= arrow.fields;
                partition = arrow.rows;
            }
            else if (resource.format === 'jsonl' && definition.schema && fields) partition = readJSONL(new TextDecoder('utf-8', {fatal: true}).decode(bytes), fields);
            else throw new Error(`Unavailable table reader: ${resource.format}`);
            if (resource.rows !== undefined && BigInt(partition.length) !== BigInt(resource.rows)) throw new Error('Partition row count differs');
            rows.push(...partition);
        }
        const result = {definition, fields, rows};
        this.loaded.set(id, result);
        return result;
    }
}

export function signature(fields)
{
    return JSON.stringify(fields.map(field => [field.name, String(field.type), field.nullable, field.type.children ? signature(field.type.children) : null]));
}

export async function loadDataset(entry, signal)
{
    const url = new URL(entry.manifest, location.href);
    const bytes = await verifiedFetch(url, entry.sha256, entry.bytes, signal);
    const manifest = parseExactJSON(new TextDecoder('utf-8', {fatal: true}).decode(bytes));
    return new Dataset(manifest, url.href, entry.sha256);
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
    const annotation = table.definition.measurements[column];
    const metric = dataset.manifest.metrics.find(item => item.id === annotation.metric_id);
    const eligibility = comparisonEligibility(dataset, table, annotation, left, right);
    const valid = row => row[column] !== null && (!annotation.status_column || row[annotation.status_column] === 'ok') && rational(row[column]) !== null;
    const a = left.filter(valid);
    const b = right.filter(valid);
    const baseline = median(a.map(row => rational(row[column])));
    const candidate = median(b.map(row => rational(row[column])));
    const result = {baseline, candidate, delta: baseline && candidate ? subtract(candidate, baseline) : null, independentLeft: a.length, independentRight: b.length, excluded: left.length + right.length - a.length - b.length, ...eligibility};
    if (!eligibility.eligible || !baseline || !candidate) return {...result, classification: 'ineligible', uncertainty: 'not estimated'};
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
    if (b.some(row => !matchSet.has(key(row, matchColumns))) || a.some(row => !b.some(other => key(other, matchColumns) === key(row, matchColumns)))) return {...result, eligible: false, classification: 'ineligible', reasons: ['Unmatched populations'], uncertainty: 'not estimated'};
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
    }
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
    const relative = baseline.n > 0n ? divide(subtract(candidate, baseline), baseline) : null;
    return {...result, interval, relative, classification: regression ? 'regression' : improvement ? 'improvement' : interval ? 'uncertain' : 'insufficient independent units', uncertainty: interval ? '95% percentile bootstrap, 1,000 resamples of independent ' + (effects ? 'pairs' : 'units') : 'not estimated (fewer than two independent units)'};
}
