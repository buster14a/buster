/* Original browser-native sample/weight/range/quality regression probes.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception */
import {parseTrace, queryProfile, Wire} from '../perf/trace.mjs';
import {canDistribute, renderers, registerRenderer} from '../perf/charts.mjs';
import {sha256} from '../perf/types.mjs';
const results = [];
function check(value, name) { if (!value) throw new Error(name); results.push(name); }
function rejects(action, name) { let rejected = false; try { action(); } catch { rejected = true; } check(rejected, name); }
const variable = value => { const result = []; do { result.push(Number(value & 127n) | (value > 127n ? 128 : 0)); value >>= 7n; } while (value); return result; };
const number = (field, value) => [...variable(BigInt(field) << 3n), ...variable(BigInt(value))];
const message = (field, value) => [...variable((BigInt(field) << 3n) | 2n), ...variable(BigInt(value.length)), ...value];
const string = (field, value) => message(field, [...new TextEncoder().encode(value)]);
const packet = (time, fields, flags = 0) => message(1, [...number(8, time), ...number(10, 1), ...number(58, 6), ...number(13, flags), ...fields]);
function dictionary(name)
{
    return message(12, [...message(5, [...number(1, 1), ...string(2, name)]), ...message(6, [...number(1, 1), ...number(2, 1)]), ...message(7, [...number(1, 1), ...number(2, 1)])]);
}
const sample = (time, weight, flags = 0) => packet(time, message(135, [...message(1, [...number(2, 1), ...number(3, 2)]), ...message(3, number(2, 0)), ...number(6, 1), ...(weight === null ? [] : [...message(9, [...string(2, 'Exact test weight'), ...number(3, 1), ...number(5, 1)]), ...number(11, weight)])]), flags);
try
{
    const started = performance.now();
    const fixtureURL = new URLSearchParams(location.search).get('fixture') ?? '/evidence/profile-example/capture.pftrace';
    const fixture = new Uint8Array(await (await fetch(fixtureURL)).arrayBuffer());
    const trace = parseTrace(fixture), all = queryProfile(trace);
    check(all.sampleCount === 8, 'Native PerfSample fixture has eight source samples');
    check(all.functions.find(item => item.name === 'parse').self === 4, 'All leaf parse samples = four');
    check(all.functions.find(item => item.name === 'main').count === 6, 'Inclusive main samples = six');
    check(all.functions.find(item => item.name === 'worker').count === 2, 'Inclusive worker samples = two');
    check(queryProfile(trace, 1002000000n, 1007000000n).sampleCount === 5, 'Independent SQL reference half-open selection = five');
    check(queryProfile(trace, 1003000000n, 1007000000n).sampleCount === 4, 'Separate half-open selection excludes exact right boundary');
    check(queryProfile(trace, 1003000000n, 1007000000n, {tid: '101'}).sampleCount === 2, 'Native thread identity filters retained samples');
    check(queryProfile(trace, 1003000000n, 1007000000n, {cpu: '2'}).sampleCount === 2, 'Native CPU identity filters retained samples');
    check(all.weights.length === 0 && all.unknownWeights === 8, 'Cumulative PerfSample timebase counts are never summed as periods');
    check(trace.counters.length === 8 && trace.flows.length === 1, 'Eight native counter points and one explicit dispatch flow');
    check(all.observedRunning.get('CPU 2 TID 100') === 9000000n && all.observedRunning.get('CPU 3 TID 101') === 3000000n, 'Scheduling totals follow exact switch intervals');
    check(trace.intervals.find(item => item.name === 'parse').end - trace.intervals.find(item => item.name === 'parse').time === 2500000n, 'Instrumented parse duration is separate from sampling weight');
    check(trace.intervals.find(item => item.name === 'encode task').end - trace.intervals.find(item => item.name === 'encode task').time === 3000000n, 'Instrumented encode duration = three milliseconds');
    check(!trace.capabilities.assembly && !trace.capabilities.source_lines && !trace.capabilities.off_cpu_reasons, 'Unavailable source/assembly/blocking capabilities stay explicit');
    const exact = parseTrace(new Uint8Array([...packet(1n, dictionary('first'), 1), ...sample(2n, 9007199254740993n)]));
    check(queryProfile(exact).weights[0].value === 9007199254740993n, 'Native StackSample primary weight retains integers above binary64 precision');
    const epochs = parseTrace(new Uint8Array([...packet(1n, dictionary('first'), 1), ...sample(3n, null), ...packet(4n, dictionary('second'), 1), ...sample(5n, null)]));
    check(queryProfile(epochs).functions.some(item => item.name === 'first') && queryProfile(epochs).functions.some(item => item.name === 'second'), 'Same dictionary IID in cleared epochs has distinct identity');
    rejects(() => parseTrace(new Uint8Array([...packet(1n, dictionary('first'), 1), ...packet(2n, dictionary('conflict'))])), 'Conflicting native dictionary identity is refused');
    rejects(() => parseTrace(new Uint8Array(sample(1n, null))), 'Missing callstack dictionary is refused');
    rejects(() => parseTrace(new Uint8Array([...packet(1n, dictionary('first'), 1), ...sample(9223372036854775808n, null)])), 'Unsupported native timestamp range is refused exactly');
    rejects(() => [...new Wire(new Uint8Array([0])).fields()], 'Zero protobuf field number is refused');
    rejects(() => [...new Wire(new Uint8Array([10, 9, 1])).fields()], 'Truncated native length-delimited message is refused');
    rejects(() => [...new Wire(new Uint8Array([8, 128, 128, 128, 128, 128, 128, 128, 128, 128, 2])).fields()], 'Overflowing uint64 varint is refused');
    rejects(() => queryProfile(trace, 5n, 5n), 'Empty raw-nanosecond range is refused');
    check(!canDistribute({definition: {measurements: {x: {raw_retained: false, statistic: 'median', sample_level: 'process_summary'}}}}, 'x'), 'Summaries cannot invent a raw-observation distribution');
    check(canDistribute({definition: {measurements: {x: {raw_retained: true, statistic: 'observation', sample_level: 'process'}}}}, 'x'), 'Retained process observations can support a descriptive distribution');
    check(renderers.has('lines') && renderers.has('scatter') && renderers.has('heatmap') && renderers.has('ecdf') && renderers.has('box'), 'First-party renderer registry exposes generic views');
    rejects(() => registerRenderer('lines', {render() {}}), 'Renderer extensions cannot replace an existing identifier');
    const realURL = new URLSearchParams(location.search).get('real') ?? '/evidence/real-profile/capture.pftrace';
    const realBytes = new Uint8Array(await (await fetch(realURL)).arrayBuffer());
    const realStarted = performance.now(), real = parseTrace(realBytes), realQuery = queryProfile(real);
    const expected = Number(new URLSearchParams(location.search).get('samples') ?? '871');
    check(realQuery.sampleCount === expected, 'Real first-party 9700X native sample count matches source/independent oracle');
    check(realQuery.weights[0].value === BigInt(expected) * 1000000n && realQuery.unknownWeights === 0, 'Real source CPU-clock periods survive native import');
    check(realQuery.functions.some(item => item.name === 'run_c_lexer_benchmark' && item.count === expected), 'Real matching-binary symbol attribution is retained');
    check(real.lost === 0n && real.warnings.length === 0, 'Qualified real trace imports without native loss/clock warnings');
    const rangeStarted = performance.now();
    const range = queryProfile(real, 2652773000000000n, 2652773500000000n);
    const rangeMs = performance.now() - rangeStarted;
    const realMs = performance.now() - realStarted;
    const report = {passed: true, first_party: true, checks: results.length, synthetic_samples: all.sampleCount, real_samples: realQuery.sampleCount, real_bytes: realBytes.length, real_sha256: await sha256(realBytes), real_parse_and_query_ms: realMs, real_range_ms: rangeMs, real_selected_samples: range.sampleCount, elapsed_ms: performance.now() - started, userAgent: navigator.userAgent, heap: performance.memory ? {used_js_heap_bytes: performance.memory.usedJSHeapSize, total_js_heap_bytes: performance.memory.totalJSHeapSize, note: 'Chromium process-reported heap snapshot; not a peak-memory guarantee or whole-browser RSS'} : {unavailable: true}, results};
    document.querySelector('#result').textContent = JSON.stringify(report, null, 2);
}
catch (error) { document.querySelector('#result').textContent = JSON.stringify({passed: false, checks: results.length, error: error.stack}, null, 2); }
