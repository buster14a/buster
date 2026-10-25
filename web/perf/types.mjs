// Original exact JSON and numeric primitives. No runtime/package dependencies.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
export const BYTE_LIMIT = 64 * 1024 * 1024;
export const ROW_LIMIT = 1000000;
export const CELL_LIMIT = 2000000;
export const DEPTH_LIMIT = 128;

export class JsonNumber
{
    constructor(text) { this.text = text; }
    toString() { return this.text; }
    toJSON() { return this.text; }
}

export function parseExactJSON(text)
{
    if (typeof text !== 'string' || text.length > BYTE_LIMIT) throw new Error('JSON exceeds the reader byte budget');
    let cursor = 0;
    let nodes = 0;
    const frames = [];
    const whitespace = () => { while (cursor < text.length && /[ \t\r\n]/u.test(text[cursor])) ++cursor; };
    const string = () =>
    {
        if (text[cursor] !== '"') throw new Error('Expected JSON string');
        const start = cursor++;
        let escaped = false;
        let ended = false;
        while (cursor < text.length && !ended)
        {
            const c = text[cursor++];
            if (escaped) escaped = false;
            else if (c === '\\') escaped = true;
            else if (c === '"') ended = true;
        }
        if (!ended) throw new Error('Unterminated JSON string');
        const value = JSON.parse(text.slice(start, cursor));
        if (/[\ud800-\udfff]/u.test(value)) throw new Error('Unpaired Unicode surrogate');
        return value;
    };
    const value = () =>
    {
        whitespace();
        if (++nodes > 262144) throw new Error('JSON exceeds the node budget');
        const c = text[cursor];
        if (c === '"') return string();
        if (c === '{' || c === '[')
        {
            if (frames.length >= DEPTH_LIMIT) throw new Error('JSON nesting exceeds the reader budget');
            ++cursor;
            const object = c === '{';
            const output = object ? Object.create(null) : [];
            frames.push({output, object, state: 0, key: null, keys: new Set()});
            return output;
        }
        for (const [literal, output] of [['null', null], ['true', true], ['false', false]])
        {
            if (text.startsWith(literal, cursor)) { cursor += literal.length; return output; }
        }
        const number = /-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?/y;
        number.lastIndex = cursor;
        const match = number.exec(text);
        if (!match) throw new Error(`Invalid JSON value at ${cursor}`);
        cursor = number.lastIndex;
        return new JsonNumber(match[0]);
    };
    const root = value();
    while (frames.length)
    {
        whitespace();
        const frame = frames[frames.length - 1];
        const c = text[cursor];
        if (frame.object)
        {
            if (frame.state === 0 || frame.state === 4)
            {
                if (c === '}' && frame.state === 0) { ++cursor; frames.pop(); continue; }
                frame.key = string();
                if (frame.keys.has(frame.key)) throw new Error('Duplicate exact JSON key');
                frame.keys.add(frame.key);
                frame.state = 1;
            }
            else if (frame.state === 1)
            {
                if (c !== ':') throw new Error('Expected JSON colon');
                ++cursor;
                frame.state = 2;
            }
            else if (frame.state === 2)
            {
                frame.state = 3;
                frame.output[frame.key] = value();
            }
            else if (c === ',') { ++cursor; frame.state = 4; }
            else if (c === '}') { ++cursor; frames.pop(); }
            else throw new Error('Expected JSON object separator');
        }
        else if (frame.state === 0 || frame.state === 2)
        {
            if (c === ']' && frame.state === 0) { ++cursor; frames.pop(); continue; }
            frame.state = 1;
            frame.output.push(value());
        }
        else if (c === ',') { ++cursor; frame.state = 2; }
        else if (c === ']') { ++cursor; frames.pop(); }
        else throw new Error('Expected JSON array separator');
    }
    whitespace();
    if (cursor !== text.length) throw new Error('Trailing JSON input');
    return root;
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
    if (typeof text !== 'string' || text.length > 160 || !/^-?(0|[1-9][0-9]*)(\.[0-9]+)?$/u.test(text)) throw new Error('Decimal must be an exact decimal string');
    if (Math.abs(scale) > 76 || precision < 1 || precision > 76) throw new Error('Unsupported decimal scale/precision');
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

export function exactText(value)
{
    if (value === null || value === undefined) return '∅';
    if (value instanceof Decimal || value instanceof JsonNumber || typeof value === 'bigint') return value.toString();
    if (ArrayBuffer.isView(value))
    {
        let bytes = '';
        for (const byte of value) bytes += String.fromCharCode(byte);
        return btoa(bytes);
    }
    if (typeof value === 'number' && Object.is(value, -0)) return '-0';
    if (typeof value === 'object') return jsonExact(value);
    return String(value);
}

export function equivalent(left, right, fields)
{
    return left.length === right.length && left.every((row, r) => fields.every(field => jsonExact(row[field.name]) === jsonExact(right[r][field.name])));
}

export function jsonExact(value)
{
    const output = [];
    let length = 0;
    const append = text =>
    {
        length += text.length;
        if (length > BYTE_LIMIT) throw new Error('Exact JSON export exceeds the byte budget');
        output.push(text);
    };
    const frames = [{value, started: false, cursor: 0}];
    let cells = 0;
    while (frames.length)
    {
        const frame = frames[frames.length - 1];
        if (!frame.started)
        {
            if (++cells > CELL_LIMIT) throw new Error('Exact JSON export exceeds the cell budget');
            const item = frame.value;
            let primitive;
            if (item === null) primitive = 'null';
            else if (item instanceof JsonNumber) primitive = item.text;
            else if (typeof item === 'bigint' || item instanceof Decimal) primitive = JSON.stringify(item.toString());
            else if (typeof item === 'number') primitive = Object.is(item, -0) ? '-0' : Number.isFinite(item) ? item.toString() : undefined;
            else if (typeof item === 'string' || typeof item === 'boolean') primitive = JSON.stringify(item);
            else if (ArrayBuffer.isView(item)) primitive = JSON.stringify(exactText(item));
            if (primitive !== undefined) { append(primitive); frames.pop(); continue; }
            if (!item || typeof item !== 'object') throw new Error('Value has no exact JSON mapping');
            frame.started = true;
            frame.array = Array.isArray(item);
            frame.keys = frame.array ? null : Object.keys(item);
            frame.end = frame.array ? item.length : frame.keys.length;
            append(frame.array ? '[' : '{');
        }
        if (frame.cursor === frame.end) { append(frame.array ? ']' : '}'); frames.pop(); continue; }
        if (frame.cursor) append(',');
        const key = frame.array ? frame.cursor : frame.keys[frame.cursor];
        ++frame.cursor;
        if (!frame.array) { append(JSON.stringify(key)); append(':'); }
        if (frames.length >= DEPTH_LIMIT) throw new Error('Exact JSON export nesting exceeds the reader budget');
        frames.push({value: frame.value[key], started: false, cursor: 0});
    }
    return output.join('');
}

export function rational(value)
{
    if (value instanceof Decimal) return value.scale >= 0 ? {n: value.unscaled, d: 10n ** BigInt(value.scale)} : {n: value.unscaled * 10n ** BigInt(-value.scale), d: 1n};
    if (typeof value === 'bigint') return {n: value, d: 1n};
    if (value instanceof JsonNumber || typeof value === 'number' && Number.isFinite(value))
    {
        const text = value instanceof JsonNumber ? value.text : value.toString();
        if (text.length > 256) return null;
        const [mantissa, exponent = '0'] = text.toLowerCase().split('e');
        if (Math.abs(Number(exponent)) > 1024) return null;
        const fraction = mantissa.split('.')[1]?.length ?? 0;
        const shift = Number(exponent) - fraction;
        const integer = BigInt(mantissa.replace('.', ''));
        return shift >= 0 ? {n: integer * 10n ** BigInt(shift), d: 1n} : {n: integer, d: 10n ** BigInt(-shift)};
    }
    return null;
}

export const compareRational = (a, b) => a.n * b.d < b.n * a.d ? -1 : a.n * b.d > b.n * a.d ? 1 : 0;
export const subtract = (a, b) => ({n: a.n * b.d - b.n * a.d, d: a.d * b.d});
export const divide = (a, b) =>
{
    if (b.n === 0n) throw new Error('Zero denominator');
    return {n: a.n * b.d * (b.n < 0n ? -1n : 1n), d: a.d * (b.n < 0n ? -b.n : b.n)};
};

export function coordinate(value, minimum, maximum)
{
    const extent = subtract(maximum, minimum);
    if (extent.n === 0n) return 0.5;
    const position = divide(subtract(value, minimum), extent);
    // Bound numerator/denominator before Number conversion. Even enormous
    // exact origins cannot overflow plotting into Infinity/Infinity.
    return Number(position.n * 1000000000000n / position.d) / 1000000000000;
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
