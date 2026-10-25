// Original bounded Arrow IPC / FlatBuffers and typed JSONL readers.
// Supported storage is explicit; no generated or upstream reader is imported.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import {BYTE_LIMIT, ROW_LIMIT, CELL_LIMIT, DEPTH_LIMIT, JsonNumber, Decimal, decimalFromString, parseExactJSON} from './types.mjs';

const decoder = new TextDecoder('utf-8', {fatal: true});
const require = (condition, reason) => { if (!condition) throw new Error(reason); };

class Flat
{
    constructor(bytes)
    {
        this.bytes = bytes;
        this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    }
    range(position, size)
    {
        require(Number.isSafeInteger(position) && position >= 0 && Number.isSafeInteger(size) && size >= 0 && position <= this.bytes.length && size <= this.bytes.length - position, 'Truncated or oversized Arrow metadata/buffer');
    }
    u(position, width = 4)
    {
        this.range(position, width);
        return width === 1 ? this.view.getUint8(position) : width === 2 ? this.view.getUint16(position, true) : width === 4 ? this.view.getUint32(position, true) : this.view.getBigUint64(position, true);
    }
    n64(position)
    {
        const value = this.u(position, 8);
        require(value <= BigInt(Number.MAX_SAFE_INTEGER), 'Arrow length/offset exceeds the bounded reader');
        return Number(value);
    }
    target(position)
    {
        const distance = this.u(position);
        const target = position + distance;
        require(distance >= 4, 'Invalid FlatBuffers reference');
        this.range(target, 4);
        return target;
    }
    field(table, field, width = 4)
    {
        this.range(table, 4);
        const vtable = table - this.view.getInt32(table, true);
        const size = this.u(vtable, 2);
        const objectSize = this.u(vtable + 2, 2);
        require(size >= 4 && size % 2 === 0 && objectSize >= 4, 'Invalid FlatBuffers table');
        this.range(vtable, size);
        this.range(table, objectSize);
        const offset = field * 2 + 4 < size ? this.u(vtable + 4 + field * 2, 2) : 0;
        if (offset === 0) return 0;
        require(offset >= 4 && offset <= objectSize && width <= objectSize - offset, 'Invalid FlatBuffers field');
        return table + offset;
    }
    scalar(table, field, width, fallback = 0)
    {
        const position = this.field(table, field, width);
        return position ? this.u(position, width) : fallback;
    }
    object(table, field)
    {
        const position = this.field(table, field);
        return position ? this.target(position) : 0;
    }
    string(table, field, optional = true)
    {
        const position = this.object(table, field);
        if (!position) { require(optional, 'Missing Arrow field name'); return ''; }
        const size = this.u(position);
        this.range(position + 4, size + 1);
        require(this.u(position + 4 + size, 1) === 0, 'Invalid Arrow string terminator');
        return decoder.decode(this.bytes.subarray(position + 4, position + 4 + size));
    }
    vector(table, field, width)
    {
        const position = this.object(table, field);
        if (!position) return {start: 0, count: 0};
        const count = this.u(position);
        this.range(position + 4, count * width);
        return {start: position + 4, count};
    }
    metadata(table, field)
    {
        const vector = this.vector(table, field, 4);
        require(vector.count <= 4096, 'Arrow metadata exceeds the entry budget');
        const result = [];
        const keys = new Set();
        for (let i = 0; i < vector.count; ++i)
        {
            const entry = this.target(vector.start + 4 * i);
            const key = this.string(entry, 0);
            require(!keys.has(key), 'Duplicate Arrow metadata key');
            keys.add(key);
            result.push([key, this.string(entry, 1)]);
        }
        return result.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
    }
}

function schemaFrom(flat, table)
{
    require(flat.scalar(table, 0, 2) === 0, 'Big-endian Arrow streams are unsupported');
    require(flat.vector(table, 3, 8).count === 0, 'Arrow schema features require a newer reader');
    const root = flat.vector(table, 1, 4);
    const schema = {fields: [], all: [], metadata: flat.metadata(table, 2)};
    const frames = [{vector: root, cursor: 0, fields: schema.fields, names: new Set(), parent: null}];
    while (frames.length)
    {
        const frame = frames[frames.length - 1];
        if (frame.cursor === frame.vector.count)
        {
            if (frame.parent) frame.parent.next = schema.all.length;
            frames.pop();
            continue;
        }
        require(schema.all.length < 4096, 'Arrow field budget exceeded');
        const position = flat.target(frame.vector.start + 4 * frame.cursor++);
        const name = flat.string(position, 0, false);
        require(!frame.names.has(name), 'Repeated exact Arrow field name');
        frame.names.add(name);
        require(flat.object(position, 4) === 0, 'Dictionary-encoded Arrow is preserved for download but unsupported by this reader');
        const id = flat.scalar(position, 2, 1);
        const typePosition = flat.object(position, 3);
        require(typePosition !== 0, 'Missing Arrow type');
        const type = {typeId: id, bitWidth: 0, precision: 0, scale: 0, fixedSize: 0, isSigned: false, children: []};
        const field = {name, type, nullable: flat.scalar(position, 1, 1) !== 0, metadata: flat.metadata(position, 6), index: schema.all.length, next: schema.all.length + 1};
        const children = flat.vector(position, 5, 4);
        if ([1, 4, 5, 6, 19, 20].includes(id)) require(children.count === 0, 'Unexpected primitive Arrow children');
        else if (id === 2)
        {
            type.bitWidth = flat.scalar(typePosition, 0, 4);
            type.isSigned = flat.scalar(typePosition, 1, 1) !== 0;
            require(children.count === 0 && [8, 16, 32, 64].includes(type.bitWidth), 'Unsupported integer width');
        }
        else if (id === 3)
        {
            type.precision = flat.scalar(typePosition, 0, 2);
            type.bitWidth = [16, 32, 64][type.precision];
            require(children.count === 0 && type.bitWidth !== undefined, 'Unsupported floating-point precision');
        }
        else if (id === 7)
        {
            type.precision = flat.scalar(typePosition, 0, 4);
            type.scale = flat.scalar(typePosition, 1, 4) | 0;
            type.bitWidth = flat.scalar(typePosition, 2, 4, 128);
            const maximum = {32: 9, 64: 18, 128: 38, 256: 76}[type.bitWidth] ?? 0;
            require(children.count === 0 && type.precision > 0 && type.precision <= maximum && Math.abs(type.scale) <= 76, 'Unsupported decimal precision/scale');
        }
        else if (id === 12 || id === 21) require(children.count === 1, 'Arrow list must have one child');
        else if (id === 13) { /* Ordinary struct. */ }
        else if (id === 15 || id === 16)
        {
            type.fixedSize = flat.scalar(typePosition, 0, 4);
            require(type.fixedSize <= (id === 15 ? BYTE_LIMIT : ROW_LIMIT) && children.count === (id === 15 ? 0 : 1), 'Invalid fixed Arrow width');
        }
        else throw new Error(`Arrow type ${id} is preserved for download but unsupported by this reader`);
        frame.fields.push(field);
        schema.all.push(field);
        if (children.count)
        {
            require(frames.length < DEPTH_LIMIT, 'Arrow nesting exceeds the reader budget');
            frames.push({vector: children, cursor: 0, fields: type.children, names: new Set(), parent: field});
        }
    }
    return schema;
}

function batchFrom(flat, table, body, schema)
{
    const rows = Number(flat.scalar(table, 0, 8, 0n));
    const nodes = flat.vector(table, 1, 16);
    const buffers = flat.vector(table, 2, 16);
    require(Number.isSafeInteger(rows) && rows <= ROW_LIMIT && nodes.count === schema.all.length, 'Arrow record batch exceeds the row/field budget');
    require(flat.object(table, 3) === 0 && flat.vector(table, 4, 8).count === 0, 'Compressed or variadic Arrow buffers are unsupported');
    let buffer = 0;
    const data = new Flat(body);
    const take = () =>
    {
        require(buffer < buffers.count, 'Missing Arrow data buffer');
        const position = buffers.start + buffer++ * 16;
        const offset = flat.n64(position);
        const size = flat.n64(position + 8);
        data.range(offset, size);
        return body.subarray(offset, offset + size);
    };
    const arrays = [];
    let slots = 0;
    for (const field of schema.all)
    {
        const type = field.type;
        const length = flat.n64(nodes.start + field.index * 16);
        const nulls = flat.n64(nodes.start + field.index * 16 + 8);
        slots += length;
        require(length <= ROW_LIMIT && slots <= CELL_LIMIT && nulls <= length && (field.nullable || nulls === 0), 'Arrow nullability or materialized-cell budget exceeded');
        const array = {length, nulls, validity: new Uint8Array(), offsets: null, data: new Uint8Array(), view: null};
        arrays.push(array);
        if (type.typeId === 1) { require(nulls === length, 'Invalid null Arrow array'); continue; }
        array.validity = take();
        require(array.validity.length === 0 ? nulls === 0 : array.validity.length >= Math.ceil(length / 8), 'Invalid Arrow validity bitmap');
        if (array.validity.length)
        {
            let actual = 0;
            for (let i = 0; i < length; ++i) actual += ((array.validity[i >> 3] >> (i & 7)) & 1) === 0;
            require(actual === nulls, 'Arrow null count differs from bitmap');
        }
        if (type.typeId === 13)
        {
            for (const child of type.children) require(flat.n64(nodes.start + child.index * 16) === length, 'Arrow struct child length differs');
        }
        else if (type.typeId === 16) require(flat.n64(nodes.start + type.children[0].index * 16) === length * type.fixedSize, 'Fixed list child length differs');
        else if ([4, 5, 12, 19, 20, 21].includes(type.typeId))
        {
            const width = type.typeId >= 19 ? 8 : 4;
            const offsets = take();
            array.offsets = new Flat(offsets);
            array.offsets.range(0, (length + 1) * width);
            const list = type.typeId === 12 || type.typeId === 21;
            if (!list) array.data = take();
            const maximum = list ? flat.n64(nodes.start + type.children[0].index * 16) : array.data.length;
            let previous = 0;
            for (let i = 0; i <= length; ++i)
            {
                const offset = width === 8 ? array.offsets.n64(i * width) : array.offsets.u(i * width);
                require(offset >= previous && offset <= maximum, 'Invalid or nonmonotonic Arrow offset');
                previous = offset;
            }
        }
        else
        {
            array.data = take();
            const size = type.typeId === 6 ? Math.ceil(length / 8) : length * (type.typeId === 15 ? type.fixedSize : type.bitWidth / 8);
            require(array.data.length >= size, 'Arrow primitive data buffer is too short');
        }
        array.view = new DataView(array.data.buffer, array.data.byteOffset, array.data.byteLength);
    }
    require(buffer === buffers.count, 'Unexpected trailing Arrow data buffers');
    for (const field of schema.fields) require(arrays[field.index].length === rows, 'Root Arrow length differs from record batch');
    return {rows, arrays, slots};
}

function arrowCell(field, array, row)
{
    const type = field.type;
    const id = type.typeId;
    if (id === 1 || array.validity.length && ((array.validity[row >> 3] >> (row & 7)) & 1) === 0) return null;
    const view = array.view;
    if (id === 2)
    {
        const bytes = type.bitWidth / 8;
        const position = row * bytes;
        return bytes === 8 ? type.isSigned ? view.getBigInt64(position, true) : view.getBigUint64(position, true) : bytes === 4 ? type.isSigned ? view.getInt32(position, true) : view.getUint32(position, true) : bytes === 2 ? type.isSigned ? view.getInt16(position, true) : view.getUint16(position, true) : type.isSigned ? view.getInt8(position) : view.getUint8(position);
    }
    if (id === 7)
    {
        const width = type.bitWidth / 8;
        let integer = 0n;
        for (let i = width - 1; i >= 0; --i) integer = integer << 8n | BigInt(array.data[row * width + i]);
        if ((array.data[(row + 1) * width - 1] & 128) !== 0) integer -= 1n << BigInt(type.bitWidth);
        require((integer < 0n ? -integer : integer).toString().length <= type.precision, 'Arrow decimal precision overflow');
        return new Decimal(integer, type.scale);
    }
    if (id === 3)
    {
        if (type.bitWidth === 64) return view.getFloat64(row * 8, true);
        if (type.bitWidth === 32) return view.getFloat32(row * 4, true);
        const raw = view.getUint16(row * 2, true);
        const exponent = raw >> 10 & 31;
        const fraction = raw & 1023;
        const magnitude = exponent === 31 ? fraction ? NaN : Infinity : exponent === 0 ? fraction * 2 ** -24 : (1024 + fraction) * 2 ** (exponent - 25);
        return raw & 32768 ? -magnitude : magnitude;
    }
    if (id === 6) return ((array.data[row >> 3] >> (row & 7)) & 1) !== 0;
    if ([4, 5, 15, 19, 20].includes(id))
    {
        const width = id >= 19 ? 8 : 4;
        const start = id === 15 ? row * type.fixedSize : width === 8 ? array.offsets.n64(row * width) : array.offsets.u(row * width);
        const end = id === 15 ? start + type.fixedSize : width === 8 ? array.offsets.n64((row + 1) * width) : array.offsets.u((row + 1) * width);
        const bytes = array.data.subarray(start, end);
        return id === 5 || id === 20 ? decoder.decode(bytes) : bytes;
    }
    return undefined;
}

function arrowRow(schema, batch, row)
{
    const result = Object.create(null);
    const frames = [{fields: schema.fields, cursor: 0, end: schema.fields.length, row, output: result, structure: true}];
    while (frames.length)
    {
        const frame = frames[frames.length - 1];
        if (frame.cursor === frame.end) { frames.pop(); continue; }
        const index = frame.cursor++;
        const field = frame.structure ? frame.fields[index] : frame.field;
        const at = frame.structure ? frame.row : index;
        const array = batch.arrays[field.index];
        const primitive = arrowCell(field, array, at);
        const key = frame.structure ? field.name : index - frame.begin;
        if (primitive !== undefined) { frame.output[key] = primitive; continue; }
        require(frames.length < DEPTH_LIMIT, 'Arrow value nesting exceeds the reader budget');
        if (field.type.typeId === 13)
        {
            const child = Object.create(null);
            frame.output[key] = child;
            frames.push({fields: field.type.children, cursor: 0, end: field.type.children.length, row: at, output: child, structure: true});
        }
        else
        {
            const type = field.type;
            const width = type.typeId === 21 ? 8 : 4;
            const begin = type.typeId === 16 ? at * type.fixedSize : width === 8 ? array.offsets.n64(at * width) : array.offsets.u(at * width);
            const end = type.typeId === 16 ? begin + type.fixedSize : width === 8 ? array.offsets.n64((at + 1) * width) : array.offsets.u((at + 1) * width);
            const child = [];
            frame.output[key] = child;
            frames.push({field: type.children[0], cursor: begin, begin, end, output: child, structure: false});
        }
    }
    return result;
}

export function readArrow(bytes)
{
    require(bytes instanceof Uint8Array && bytes.length <= BYTE_LIMIT, 'Arrow input exceeds the byte budget');
    const stream = new Flat(bytes);
    let cursor = 0;
    let schema = null;
    const rows = [];
    let batches = 0;
    let slots = 0;
    while (cursor < bytes.length)
    {
        let size = stream.u(cursor);
        cursor += 4;
        if (size === 0xffffffff) { size = stream.u(cursor); cursor += 4; }
        if (size === 0) { require(cursor === bytes.length, 'Trailing bytes after Arrow EOS'); break; }
        stream.range(cursor, size);
        const metadata = new Flat(bytes.subarray(cursor, cursor + size));
        cursor += size;
        const message = metadata.target(0);
        const version = metadata.scalar(message, 0, 2);
        const type = metadata.scalar(message, 1, 1);
        const header = metadata.object(message, 2);
        const bodySizeRaw = metadata.scalar(message, 3, 8, 0n);
        require([3, 4].includes(version) && header !== 0 && bodySizeRaw <= BigInt(BYTE_LIMIT), 'Unsupported Arrow IPC message version/body');
        const bodySize = Number(bodySizeRaw);
        stream.range(cursor, bodySize);
        const body = bytes.subarray(cursor, cursor + bodySize);
        cursor += bodySize;
        if (type === 1)
        {
            require(schema === null && bodySize === 0, 'Duplicate or invalid Arrow schema');
            schema = schemaFrom(metadata, header);
        }
        else if (type === 3 && schema)
        {
            const batch = batchFrom(metadata, header, body, schema);
            slots += batch.slots;
            require(slots <= CELL_LIMIT && rows.length + batch.rows <= ROW_LIMIT, 'Arrow materialization exceeds the row/cell budget');
            for (let r = 0; r < batch.rows; ++r) rows.push(arrowRow(schema, batch, r));
            ++batches;
        }
        else throw new Error(`Arrow message ${type} is unsupported`);
    }
    require(schema !== null && cursor === bytes.length, 'Missing or truncated Arrow schema');
    return {fields: schema.fields, schema, rows, batches, slots};
}

function jsonPrimitive(field, value)
{
    const type = field.type;
    const id = type.typeId;
    if (value === null) { require(field.nullable, `Null in nonnullable ${field.name}`); return null; }
    require(id !== 1, 'Non-null value in Arrow Null field');
    if (id === 2)
    {
        const wide = type.bitWidth === 64;
        const text = wide ? value : value instanceof JsonNumber ? value.text : '';
        require(typeof text === 'string' && /^-?(0|[1-9][0-9]*)$/u.test(text) && text.length <= 21, 'Invalid exact integer mapping');
        const integer = BigInt(text);
        const limit = 1n << BigInt(type.bitWidth - (type.isSigned ? 1 : 0));
        require(integer >= (type.isSigned ? -limit : 0n) && integer < limit, 'Arrow integer overflow');
        return wide ? integer : Number(integer);
    }
    if (id === 7) return decimalFromString(value, type.scale, type.precision);
    if (id === 3)
    {
        require(value instanceof JsonNumber && type.precision !== 0, 'Unsupported JSON floating-point mapping');
        const number = type.precision === 1 ? Math.fround(Number(value.text)) : Number(value.text);
        require(Number.isFinite(number), 'Nonfinite JSON float');
        return number;
    }
    if (id === 5 || id === 20) { require(typeof value === 'string', 'UTF-8 JSON mapping mismatch'); return value; }
    if (id === 6) { require(typeof value === 'boolean', 'Boolean JSON mapping mismatch'); return value; }
    if (id === 4 || id === 15 || id === 19)
    {
        require(typeof value === 'string' && /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/u.test(value), 'Binary must use canonical base64');
        const raw = atob(value);
        require(btoa(raw) === value && (id !== 15 || raw.length === type.fixedSize), 'Noncanonical or wrong-width binary');
        return Uint8Array.from(raw, c => c.charCodeAt(0));
    }
    return undefined;
}

export function readJSONL(text, fields, budget = {cells: 0})
{
    require(text.length <= BYTE_LIMIT, 'JSONL byte budget exceeded');
    require(Number.isSafeInteger(budget.cells) && budget.cells >= 0 && budget.cells <= CELL_LIMIT, 'Invalid aggregate JSONL cell budget');
    const rows = [];
    for (const line of text.split('\n'))
    {
        if (!line.trim()) continue;
        require(rows.length < ROW_LIMIT, 'JSONL row budget exceeded');
        const input = parseExactJSON(line);
        require(input && !Array.isArray(input) && typeof input === 'object' && !(input instanceof JsonNumber) && Object.keys(input).length === fields.length, 'JSONL root field set differs');
        const output = Object.create(null);
        const frames = [{fields, input, output, cursor: 0, end: fields.length, structure: true}];
        while (frames.length)
        {
            const frame = frames[frames.length - 1];
            if (frame.cursor === frame.end) { frames.pop(); continue; }
            require(++budget.cells <= CELL_LIMIT, 'JSONL materialized-cell budget exceeded');
            const index = frame.cursor++;
            const field = frame.structure ? frame.fields[index] : frame.field;
            const key = frame.structure ? field.name : index;
            require(Object.hasOwn(frame.input, key), `Missing exact Arrow field ${String(key)}`);
            const value = frame.input[key];
            const primitive = jsonPrimitive(field, value);
            if (primitive !== undefined) { frame.output[key] = primitive; continue; }
            require(frames.length < DEPTH_LIMIT, 'JSONL value nesting exceeds the reader budget');
            const structure = field.type.typeId === 13;
            const child = structure ? Object.create(null) : [];
            frame.output[key] = child;
            if (structure)
            {
                require(value && typeof value === 'object' && !Array.isArray(value) && !(value instanceof JsonNumber) && Object.keys(value).length === field.type.children.length, 'Struct JSON mapping mismatch');
                frames.push({fields: field.type.children, input: value, output: child, cursor: 0, end: field.type.children.length, structure: true});
            }
            else
            {
                require([12, 16, 21].includes(field.type.typeId) && Array.isArray(value) && (field.type.typeId !== 16 || value.length === field.type.fixedSize), 'List JSON mapping mismatch');
                frames.push({field: field.type.children[0], input: value, output: child, cursor: 0, end: value.length, structure: false});
            }
        }
        rows.push(output);
    }
    return rows;
}

export function signature(fields, metadata = [])
{
    const result = [metadata];
    const frames = [{fields, cursor: 0, depth: 0}];
    while (frames.length)
    {
        const frame = frames[frames.length - 1];
        if (frame.cursor === frame.fields.length) { frames.pop(); continue; }
        const field = frame.fields[frame.cursor++];
        const type = field.type;
        result.push([frame.depth, field.name, field.nullable, field.metadata, type.typeId, type.bitWidth, type.precision, type.scale, type.isSigned, type.fixedSize, type.children.length]);
        if (type.children.length) frames.push({fields: type.children, cursor: 0, depth: frame.depth + 1});
    }
    return JSON.stringify(result);
}
