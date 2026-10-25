/* Original bounded native Perfetto reader and sampled-profile queries.
 * Wire contracts: published Perfetto v58.2 protos. No generated code/runtime.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {BYTE_LIMIT, DEPTH_LIMIT} from './types.mjs';

const demand = (ok, message) => { if (!ok) throw new Error(message); };
const textDecoder = new TextDecoder('utf-8', {fatal: true});
const LIMIT = 1000000;
const signed = value => BigInt.asIntN(64, value);
const order = (a, b) => a < b ? -1 : a > b ? 1 : 0;

export class Wire
{
    constructor(bytes, budget = {fields: 0})
    {
        demand(bytes instanceof Uint8Array && bytes.length <= BYTE_LIMIT, 'Native trace exceeds byte budget');
        this.bytes = bytes;
        this.at = 0;
        this.budget = budget;
    }
    varint()
    {
        let value = 0n;
        let ended = false;
        for (let i = 0; i < 10 && !ended; ++i)
        {
            demand(this.at < this.bytes.length, 'Truncated protobuf integer');
            const byte = this.bytes[this.at++];
            demand(i < 9 || byte <= 1, 'Protobuf uint64 overflow');
            value |= BigInt(byte & 127) << BigInt(i * 7);
            ended = byte < 128;
        }
        demand(ended, 'Unterminated protobuf integer');
        return value;
    }
    *fields()
    {
        while (this.at < this.bytes.length)
        {
            demand(++this.budget.fields <= 32000000, 'Native trace field/work budget exceeded');
            const tag = this.varint();
            const field = Number(tag >> 3n);
            const wire = Number(tag & 7n);
            demand(field > 0 && field <= 0x1fffffff, 'Invalid protobuf field');
            let value;
            if (wire === 0) value = this.varint();
            else
            {
                demand(wire === 1 || wire === 2 || wire === 5, 'Unsupported protobuf wire type');
                const size = wire === 2 ? this.varint() : BigInt(wire === 1 ? 8 : 4);
                demand(size <= BigInt(this.bytes.length - this.at), 'Truncated protobuf field');
                value = this.bytes.subarray(this.at, this.at + Number(size));
                this.at += Number(size);
                if (wire !== 2)
                {
                    let integer = 0n;
                    for (let i = 0; i < value.length; ++i) integer |= BigInt(value[i]) << BigInt(i * 8);
                    value = integer;
                }
            }
            yield {field, wire, value};
        }
    }
}

function message(bytes, budget)
{
    const result = new Map();
    for (const item of new Wire(bytes, budget).fields())
    {
        const previous = result.get(item.field) ?? [];
        previous.push(item);
        result.set(item.field, previous);
    }
    return result;
}
const items = (fields, field) => fields.get(field) ?? [];
const last = (fields, field) => fields.get(field)?.at(-1);
function integer(fields, field, fallback = 0n)
{
    const item = last(fields, field);
    demand(!item || item.wire === 0, `Unsupported native integer encoding at field ${field}`);
    return item?.value ?? fallback;
}
function bytes(fields, field)
{
    const item = last(fields, field);
    demand(!item || item.wire === 2, `Unsupported native message encoding at field ${field}`);
    return item?.value;
}
const string = (fields, field, fallback = '') => bytes(fields, field) === undefined ? fallback : textDecoder.decode(bytes(fields, field));
const nested = (fields, field, budget) => message(bytes(fields, field) ?? new Uint8Array(), budget);
function repeated(fields, field, budget)
{
    const result = [];
    for (const item of items(fields, field))
    {
        if (item.wire === 0 || item.wire === 1) result.push(item.value);
        else
        {
            demand(item.wire === 2, 'Unsupported repeated native integer');
            const input = new Wire(item.value, budget);
            while (input.at < input.bytes.length)
            {
                demand(++budget.fields <= 32000000, 'Native repeated-field budget exceeded');
                result.push(input.varint());
                demand(result.length <= LIMIT, 'Native repeated-field capacity exceeded');
            }
        }
    }
    return result;
}

export function parseTrace(input)
{
    const budget = {fields: 0};
    const sequences = new Map();
    const dictionaries = new Map();
    const frameCache = new Map();
    const samples = [];
    const tracks = new Map();
    const events = [];
    const switches = [];
    const threads = new Map();
    const warnings = new Set();
    const clocks = new Map();
    const result = {samples, tracks, intervals: [], counters: [], flows: [], running: [], offCpu: [], migrations: [], threads, warnings: [], lost: 0n, skipped: 0n, packets: 0, fields: 0, primaryClock: 6n, dictionaries};
    let retained = 0;
    let stackFrames = 0;
    let eventOrder = 0;
    const dictionary = (sequence, kind, iid, optional = false) =>
    {
        const found = dictionaries.get(`${sequence.key}:${kind}:${iid}`);
        demand(optional || found, `Missing native dictionary ${kind}/${iid}; incremental-state coverage is incomplete`);
        return found;
    };
    const clockTime = (timestamp, clock) =>
    {
        demand(typeof timestamp === 'bigint' && timestamp >= 0n && timestamp <= 9223372036854775807n, 'Native timestamp outside supported signed nanoseconds');
        if (clock === result.primaryClock) return timestamp;
        const source = clocks.get(String(clock));
        const target = clocks.get(String(result.primaryClock));
        demand(source !== undefined && target !== undefined, `Unaligned clock ${clock}; no shared clock snapshot`);
        return timestamp + target - source;
    };
    const resolveStack = (sequence, iid, inline) =>
    {
        const stack = inline ?? dictionary(sequence, 7, iid);
        const frameIDs = repeated(stack, 2, budget);
        demand(frameIDs.length > 0 && frameIDs.length <= DEPTH_LIMIT, 'Missing or oversized sampled stack');
        const frames = [];
        for (const id of frameIDs)
        {
            const key = `${sequence.key}:6:${id}`;
            const cached = frameCache.get(key);
            if (cached) { frames.push(cached); continue; }
            const frame = dictionary(sequence, 6, id);
            const name = dictionary(sequence, 5, integer(frame, 2), true);
            const mapping = dictionary(sequence, 19, integer(frame, 3), true);
            const paths = mapping ? repeated(mapping, 7, budget).map(path => string(dictionary(sequence, 17, path), 2)) : [];
            const build = mapping ? dictionary(sequence, 16, integer(mapping, 2), true) : null;
            const buildBytes = build ? bytes(build, 2) : null;
            const source = dictionary(sequence, 18, integer(frame, 5), true);
            const label = name ? string(name, 2) : `[unresolved frame ${id}]`;
            const resolved = {id: key, name: label, mapping: paths.join('/'), build: buildBytes ? Array.from(buildBytes, x => x.toString(16).padStart(2, '0')).join('') : null, pc: last(frame, 4) ? integer(frame, 4) : null, source: source ? string(source, 2) : null, line: last(frame, 6) ? integer(frame, 6) : null, kind: integer(frame, 7), resolved: Boolean(name) && !label.startsWith('[unresolved ')};
            frameCache.set(key, resolved);
            frames.push(resolved);
        }
        stackFrames += frames.length;
        demand(stackFrames <= 2000000, 'Native sampled-frame materialization budget exceeded');
        return frames;
    };
    for (const outer of new Wire(input instanceof Uint8Array ? input : new Uint8Array(input), budget).fields())
    {
        if (outer.field !== 1) { warnings.add(`Unknown top-level field ${outer.field} retained only in original download`); continue; }
        demand(outer.wire === 2 && ++result.packets <= LIMIT, 'Native packet budget exceeded');
        const packet = message(outer.value, budget);
        demand([1, 2, 6, 11, 60, 66, 135].filter(field => last(packet, field)).length <= 1, 'Conflicting supported native packet payloads');
        demand(!last(packet, 50) && !last(packet, 97), 'Compressed native packets need an unsupported reader capability');
        const sequenceID = integer(packet, 10);
        let sequence = sequences.get(String(sequenceID));
        if (!sequence)
        {
            demand(sequences.size < 256, 'Native sequence budget exceeded');
            sequence = {epoch: 0, key: `${sequenceID}:0`, valid: true, clock: 6n};
            sequences.set(String(sequenceID), sequence);
        }
        if (integer(packet, 42) !== 0n) { sequence.valid = false; warnings.add('A packet reported previous data dropped; dependent incremental state is unavailable until cleared'); }
        const flags = integer(packet, 13);
        if ((flags & 1n) !== 0n)
        {
            sequence.key = `${sequenceID}:${++sequence.epoch}`;
            sequence.valid = true;
            sequence.clock = 6n;
        }
        if ((flags & 2n) !== 0n && !sequence.valid) { ++result.skipped; continue; }
        const defaults = bytes(packet, 59);
        if (defaults) sequence.clock = integer(message(defaults, budget), 58, sequence.clock);
        const snapshot = bytes(packet, 6);
        if (snapshot)
        {
            const data = message(snapshot, budget);
            if (last(data, 2))
            {
                const clock = integer(data, 2);
                demand(samples.length + events.length + switches.length === 0 || clock === result.primaryClock, 'Primary trace clock changed after timed observations');
                result.primaryClock = clock;
            }
            for (const item of items(data, 1))
            {
                demand(item.wire === 2, 'Invalid native clock snapshot');
                const value = message(item.value, budget);
                demand(integer(value, 3) === 0n && integer(value, 4, 1n) === 1n, 'Incremental/scaled clocks are unsupported');
                clocks.set(String(integer(value, 1)), integer(value, 2));
            }
        }
        const interned = bytes(packet, 12);
        if (interned)
        {
            for (const item of new Wire(interned, budget).fields())
            {
                if (![1, 2, 5, 6, 7, 16, 17, 18, 19, 48, 49, 50].includes(item.field)) { warnings.add(`Unsupported interned dictionary ${item.field}`); continue; }
                demand(item.wire === 2, 'Invalid native dictionary message');
                const entry = message(item.value, budget);
                const iid = integer(entry, 1);
                demand(iid !== 0n, 'Native dictionary has no identity');
                const key = `${sequence.key}:${item.field}:${iid}`;
                demand(!dictionaries.has(key), 'Repeated native dictionary identity within one incremental epoch');
                demand(++retained <= LIMIT, 'Native dictionary budget exceeded');
                dictionaries.set(key, entry);
            }
        }
        const descriptor = bytes(packet, 60);
        if (descriptor)
        {
            const data = message(descriptor, budget);
            const uuid = integer(data, 1);
            const thread = nested(data, 4, budget);
            const counter = nested(data, 8, budget);
            demand(integer(counter, 5) === 0n, 'Incremental track counters need an unsupported reader capability');
            const value = {id: String(uuid), name: string(data, 2, `Track ${uuid}`), pid: last(data, 4) ? integer(thread, 1) : null, tid: last(data, 4) ? integer(thread, 2) : null, unit: string(counter, 6, ['', 'ns', 'count', 'B'][Number(integer(counter, 3))] ?? 'unknown'), multiplier: signed(integer(counter, 4, 1n)), counter: last(data, 8) !== undefined};
            const old = tracks.get(value.id);
            demand(!old || old.name === value.name, 'Conflicting native track identity');
            tracks.set(value.id, value);
            demand(tracks.size <= 4096, 'Native track budget exceeded');
            if (value.tid !== null) threads.set(String(value.tid), {pid: value.pid, name: string(thread, 5, value.name)});
        }
        const tree = bytes(packet, 2);
        if (tree)
        {
            for (const item of items(message(tree, budget), 2))
            {
                demand(item.wire === 2, 'Invalid process-tree thread');
                const value = message(item.value, budget);
                threads.set(String(integer(value, 1)), {pid: integer(value, 3), name: string(value, 2)});
            }
            demand(threads.size <= 4096, 'Native thread budget exceeded');
        }
        const timestamp = last(packet, 8) ? clockTime(integer(packet, 8), integer(packet, 58, sequence.clock)) : null;
        for (const kind of [66, 135])
        {
            const payload = bytes(packet, kind);
            if (!payload) continue;
            const data = message(payload, budget);
            if (kind === 66)
            {
                result.lost += integer(data, 17);
                result.skipped += integer(data, 18);
                if (!last(data, 4)) continue;
            }
            demand(sequence.valid, 'Sample references unavailable incremental state');
            demand(timestamp !== null && samples.length < LIMIT, 'Missing sample timestamp or sample budget exceeded');
            let pid, tid, cpu, mode, stack, weight = null, weightDescriptor = null;
            let unwind = '';
            if (kind === 66)
            {
                pid = integer(data, 2); tid = integer(data, 3); cpu = integer(data, 1); mode = integer(data, 5);
                stack = resolveStack(sequence, integer(data, 4));
                if (last(data, 16)) unwind = `Native unwind error enum ${integer(data, 16)}`;
                if (last(data, 6)) warnings.add('PerfSample.timebase_count is cumulative source state; it is not used as a per-sample weight');
            }
            else
            {
                const task = bytes(data, 1) ? nested(data, 1, budget) : dictionary(sequence, 48, integer(data, 2));
                const execution = bytes(data, 3) ? nested(data, 3, budget) : dictionary(sequence, 49, integer(data, 4));
                pid = integer(task, 2); tid = integer(task, 3); cpu = last(execution, 2) ? integer(execution, 2) : null; mode = integer(execution, 3);
                stack = resolveStack(sequence, integer(data, 6), bytes(data, 5) ? nested(data, 5, budget) : null);
                if (last(data, 11))
                {
                    weight = signed(integer(data, 11));
                    demand(weight >= 0n, 'Negative sampled counter weight is unsupported');
                    const descriptor = bytes(data, 9) ? nested(data, 9, budget) : dictionary(sequence, 50, integer(data, 10));
                    weightDescriptor = {name: string(descriptor, 2), unit: integer(descriptor, 3), multiplier: signed(integer(descriptor, 5, 1n)), description: string(descriptor, 6), scope: integer(descriptor, 7)};
                    demand(weightDescriptor.multiplier > 0n, 'Invalid sampled counter multiplier');
                    weight *= weightDescriptor.multiplier;
                }
                unwind = string(data, 7);
                if (last(data, 8)) unwind = 'An interned unwind diagnostic is present; its text dictionary is unsupported';
            }
            samples.push({time: timestamp, pid, tid, cpu, mode, source: kind === 66 ? 'PerfSample' : 'StackSample', stack, weight, weightDescriptor, unwind});
        }
        const event = bytes(packet, 11);
        if (event)
        {
            demand(timestamp !== null && events.length < LIMIT, 'Missing event timestamp or event budget exceeded');
            const data = message(event, budget);
            demand(!last(data, 1) && !last(data, 16) && !last(data, 17), 'Legacy/delta TrackEvent encoding is unsupported');
            const iid = integer(data, 10);
            const name = last(data, 23) ? string(data, 23) : iid ? string(dictionary(sequence, 2, iid), 2) : '';
            const type = integer(data, 9);
            let value = null;
            if (last(data, 30)) value = signed(integer(data, 30));
            if (last(data, 44))
            {
                const raw = last(data, 44);
                demand(raw.wire === 1, 'Invalid native double counter');
                const buffer = new ArrayBuffer(8);
                new DataView(buffer).setBigUint64(0, raw.value, true);
                value = new DataView(buffer).getFloat64(0, true);
                demand(Number.isFinite(value), 'Nonfinite native counter');
            }
            events.push({time: timestamp, track: String(integer(data, 11)), type, name, value, flows: repeated(data, 47, budget), ends: repeated(data, 48, budget), order: eventOrder++});
        }
        const ftrace = bytes(packet, 1);
        if (ftrace)
        {
            const bundle = message(ftrace, budget);
            const cpu = integer(bundle, 1);
            if (integer(bundle, 3) !== 0n) warnings.add('Ftrace source reports lost events; scheduling coverage is partial');
            demand(!last(bundle, 4), 'Compact ftrace scheduling requires an unsupported reader capability');
            for (const item of items(bundle, 2))
            {
                demand(item.wire === 2 && switches.length < LIMIT, 'Invalid ftrace event or scheduling budget exceeded');
                const value = message(item.value, budget);
                if (!last(value, 4)) continue;
                const data = nested(value, 4, budget);
                switches.push({time: clockTime(integer(value, 1), 6n), cpu, previous: integer(data, 2), next: integer(data, 6), state: integer(data, 4), previousName: string(data, 1), nextName: string(data, 5)});
            }
        }
        if (last(packet, 33)) warnings.add('Trace statistics are retained in the original resource; collector-specific coverage needs its quality receipt');
    }
    samples.sort((a, b) => order(a.time, b.time));
    events.sort((a, b) => order(a.time, b.time) || a.order - b.order);
    switches.sort((a, b) => order(a.time, b.time));
    const pending = new Map();
    const flows = new Map();
    for (const event of events)
    {
        const track = tracks.get(event.track);
        demand(track, `Missing native track ${event.track}`);
        const stack = pending.get(event.track) ?? [];
        if (event.type === 1n)
        {
            demand(stack.length < DEPTH_LIMIT, 'Native instrumented nesting budget exceeded');
            stack.push({...event, depth: stack.length});
            pending.set(event.track, stack);
        }
        else if (event.type === 2n)
        {
            const begin = stack.pop();
            if (begin) result.intervals.push({...begin, end: event.time, complete: true});
            else warnings.add('An instrumented slice ends without a retained begin');
        }
        else if (event.type === 4n)
        {
            demand(track.counter && event.value !== null, 'Native counter has no descriptor/value');
            result.counters.push({...event, value: typeof event.value === 'bigint' ? event.value * track.multiplier : event.value * Number(track.multiplier), unit: track.unit});
        }
        else if (event.type === 3n) result.intervals.push({...event, end: event.time, complete: true, depth: stack.length});
        else warnings.add(`Unsupported native track event type ${event.type}`);
        for (const id of [...event.flows, ...event.ends])
        {
            const old = flows.get(String(id));
            if (old) result.flows.push({id: String(id), from: old, to: event});
            flows.set(String(id), event);
        }
        for (const id of event.ends) flows.delete(String(id));
    }
    for (const stack of pending.values()) if (stack.length) warnings.add('Instrumented slices remain open at the capture boundary');
    const running = new Map();
    for (const event of switches)
    {
        const old = running.get(String(event.cpu));
        if (old && old.next === event.previous && event.time >= old.time)
        {
            result.running.push({start: old.time, end: event.time, cpu: event.cpu, tid: event.previous, pid: threads.get(String(event.previous))?.pid ?? null, stateAfter: event.state});
        }
        else if (old) warnings.add('Discontinuous scheduling records; unpaired interval is unavailable');
        running.set(String(event.cpu), event);
    }
    const transitions = [];
    for (const event of switches)
    {
        if (event.previous !== 0n) transitions.push({...event, tid: event.previous, out: true});
        if (event.next !== 0n) transitions.push({...event, tid: event.next, out: false});
    }
    transitions.sort((a, b) => order(a.time, b.time) || (a.out === b.out ? 0 : a.out ? -1 : 1));
    const switchedOut = new Map();
    for (const event of transitions)
    {
        const id = String(event.tid);
        if (event.out)
        {
            if (switchedOut.has(id)) warnings.add('Discontinuous off-CPU transition; an unmatched interval remains unknown');
            switchedOut.set(id, event);
        }
        else
        {
            const before = switchedOut.get(id);
            if (before && before.time <= event.time)
            {
                const interval = {start: before.time, end: event.time, tid: event.tid, fromCpu: before.cpu, toCpu: event.cpu, postState: before.state, runnable: before.state === 0n};
                result.offCpu.push(interval);
                if (before.cpu !== event.cpu) result.migrations.push(interval);
            }
            switchedOut.delete(id);
        }
    }
    const times = [...samples.map(item => item.time), ...events.map(item => item.time), ...switches.map(item => item.time)];
    demand(times.length > 0, 'Native trace has no supported timed observations');
    result.start = times.reduce((a, b) => a < b ? a : b);
    result.end = times.reduce((a, b) => a > b ? a : b) + 1n;
    result.fields = budget.fields;
    result.warnings = [...warnings];
    result.capabilities = {sampled_stacks: samples.length > 0, period_weights: samples.some(sample => sample.weight !== null), instrumented_intervals: result.intervals.some(item => item.end > item.time), scheduling: result.running.length > 0, paired_off_cpu: result.offCpu.length > 0, observed_cpu_transitions: result.migrations.length > 0, counters: result.counters.length > 0, explicit_flows: result.flows.length > 0, source_lines: samples.some(sample => sample.stack.some(frame => frame.source !== null)), assembly: false, off_cpu_reasons: false, pid_tid_lifetime_reconstruction: false};
    return result;
}

export function queryProfile(trace, start = trace.start, end = trace.end, filters = {})
{
    demand(typeof start === 'bigint' && typeof end === 'bigint' && start < end, 'Profile range must be nonempty raw nanoseconds [start,end)');
    const selected = trace.samples.filter(sample => sample.time >= start && sample.time < end && (filters.tid === undefined || sample.tid === BigInt(filters.tid)) && (filters.cpu === undefined || sample.cpu === BigInt(filters.cpu)));
    const functions = new Map();
    const root = {name: '(all selected samples)', count: 0, weight: 0n, self: 0, children: new Map()};
    const weights = new Map();
    let unknownWeights = 0;
    let unwindDiagnostics = 0;
    for (const sample of selected)
    {
        let node = root;
        ++root.count;
        root.weight += sample.weight ?? 0n;
        const seen = new Set();
        for (let i = 0; i < sample.stack.length; ++i)
        {
            const frame = sample.stack[i];
            const key = JSON.stringify([frame.build, frame.mapping, frame.name]);
            const item = functions.get(key) ?? {...frame, count: 0, self: 0, weight: 0n, selfWeight: 0n};
            if (!seen.has(key)) { ++item.count; item.weight += sample.weight ?? 0n; seen.add(key); }
            if (i === sample.stack.length - 1) { ++item.self; item.selfWeight += sample.weight ?? 0n; }
            functions.set(key, item);
            const child = node.children.get(key) ?? {name: frame.name, frame, count: 0, weight: 0n, self: 0, children: new Map()};
            ++child.count;
            child.weight += sample.weight ?? 0n;
            if (i === sample.stack.length - 1) ++child.self;
            node.children.set(key, child);
            node = child;
        }
        if (sample.weightDescriptor)
        {
            const key = `${sample.weightDescriptor.name}:${sample.weightDescriptor.unit}:${sample.weightDescriptor.scope}`;
            const group = weights.get(key) ?? {...sample.weightDescriptor, value: 0n, samples: 0};
            group.value += sample.weight; ++group.samples; weights.set(key, group);
        }
        else ++unknownWeights;
        unwindDiagnostics += Boolean(sample.unwind);
    }
    const counters = trace.counters.filter(item => item.time >= start && item.time < end);
    const intervals = trace.intervals.filter(item => item.time < end && (item.end > start || item.end === item.time && item.time >= start));
    const running = trace.running.filter(item => item.start < end && item.end > start && (filters.tid === undefined || item.tid === BigInt(filters.tid)) && (filters.cpu === undefined || item.cpu === BigInt(filters.cpu)));
    const offCpu = (trace.offCpu ?? []).filter(item => item.start < end && item.end > start && (filters.tid === undefined || item.tid === BigInt(filters.tid)));
    const migrations = (trace.migrations ?? []).filter(item => item.start < end && item.end >= start && (filters.tid === undefined || item.tid === BigInt(filters.tid)) && (filters.cpu === undefined || item.fromCpu === BigInt(filters.cpu) || item.toCpu === BigInt(filters.cpu)));
    const observedRunning = new Map();
    for (const item of running)
    {
        const key = `CPU ${item.cpu} TID ${item.tid}`;
        const duration = (item.end < end ? item.end : end) - (item.start > start ? item.start : start);
        observedRunning.set(key, (observedRunning.get(key) ?? 0n) + duration);
    }
    return {start, end, sampleCount: selected.length, totalSamples: trace.samples.length, weights: [...weights.values()], unknownWeights, unwindDiagnostics, functions: [...functions.values()].sort((a, b) => b.count - a.count || b.self - a.self), tree: root, samples: selected.slice(0, 20000), timelineTruncated: selected.length > 20000, counters, intervals, flows: trace.flows.filter(flow => flow.from.time < end && flow.to.time >= start), running, offCpu, migrations, observedRunning};
}
