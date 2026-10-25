// Real browser interoperability probes; independent native checks run in C.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import {readArrow, readJSONL, equivalent, parseExactJSON, JsonNumber, Decimal, decimalFromString, coordinate, rational, exactText, jsonExact, comparePopulation, Dataset, validateManifest, verifiedFetch, sha256} from '../perf/data.mjs';

const results = [];
function check(result, name)
{
    if (!result) throw new Error(name);
    results.push(name);
}
function rejects(action, name)
{
    let rejected = false;
    try { action(); } catch { rejected = true; }
    check(rejected, name);
}
async function rejectsAsync(action, name)
{
    let rejected = false;
    try { await action(); } catch { rejected = true; }
    check(rejected, name);
}
function rename(bytes, from, to)
{
    const a = new TextEncoder().encode(from);
    const b = new TextEncoder().encode(to);
    check(a.length === b.length, 'Wire name mutation retains metadata extent');
    const output = bytes.slice();
    let count = 0;
    for (let i = 0; i + a.length <= output.length; ++i)
    {
        if (a.every((byte, j) => output[i + j] === byte)) { output.set(b, i); ++count; }
    }
    check(count === 1, 'Wire name mutation changes one exact schema field');
    return output;
}
try
{
    const started = performance.now();
    let rows = 0;
    let cells = 0;
    for (const [directory, name] of [['foreign-strings-example', 'link-results'], ['foreign-strings-example', 'executions'], ['profile-example', 'arbitrary-custom']])
    {
        const base = `/evidence/${directory}/${name}`;
        const arrow = readArrow(new Uint8Array(await (await fetch(base + '.arrow')).arrayBuffer()));
        const sidecar = readArrow(new Uint8Array(await (await fetch(base + '.schema.arrow')).arrayBuffer()));
        const jsonl = readJSONL(await (await fetch(base + '.jsonl')).text(), sidecar.fields);
        check(sidecar.rows.length === 0, `${name}: zero-row sidecar`);
        check(equivalent(arrow.rows, jsonl, arrow.fields), `${name}: exact ordered typed equivalence`);
        rows += arrow.rows.length;
        cells += arrow.rows.length * arrow.fields.length;
    }
    check(rows === 302 && cells === 7320, 'All 302 rows and 7,320 cells');
    const example = readArrow(new Uint8Array(await (await fetch('/evidence/profile-example/arbitrary-custom.arrow')).arrayBuffer()));
    check(example.rows[1].digest === 18446744073709551615n, 'uint64 maximum remains BigInt');
    check(example.rows[0].penalty instanceof Decimal && example.rows[0].penalty.toString() === '1.250000', 'Decimal scale retained');
    check(example.rows[1].items === 0n && example.rows[1].penalty === null && example.rows[1].reason === 'zero denominator', 'Legitimate zero and null/reason remain distinct');
    const parsed = parseExactJSON('{"n":18446744073709551615,"d":1.250000,"s":"literal 17","marker":"\\u0000buster-number:0"}');
    check(parsed.n instanceof JsonNumber && parsed.n.text === '18446744073709551615' && parsed.d.text === '1.250000' && parsed.s === 'literal 17' && parsed.marker === '\0buster-number:0', 'Lexical numbers and marker-like strings retain distinct identities');
    rejects(() => parseExactJSON('{"n":1e}'), 'Malformed numeric token rejected');
    for (const bad of ['{"x":1,"x":2}', '{"x":1,"\\u0078":2}', '[1,]', '{"a":1,}', '"\\uD800"', '01']) rejects(() => parseExactJSON(bad), 'Malformed or ambiguous JSON rejected');
    rejects(() => decimalFromString('1.2500001', 6, 38), 'Decimal rounding rejected');
    rejects(() => readJSONL('{"time_ns":"0","items":"0","penalty":"1.25","status":"ok","reason":null,"digest":18446744073709551615}', example.fields), 'Numeric uint64 JSON token rejected');
    const large = rational(18446744073709551614n);
    check(coordinate(large, rational(18446744073709551613n), rational(18446744073709551615n)) === 0.5, 'Plot coordinates subtract large exact origins first');
    check(coordinate(rational(10n ** 1000n + 1n), rational(10n ** 1000n), rational(10n ** 1000n + 2n)) === 0.5, 'Enormous exact origins cannot overflow plotting into Infinity');
    const raw = new Uint8Array(await (await fetch('/evidence/profile-example/arbitrary-custom.arrow')).arrayBuffer());
    check(readArrow(rename(raw, 'digest', '延迟')).rows[1]['延迟'] === 18446744073709551615n, 'Exact Unicode Arrow field binding');
    check(readArrow(rename(raw, 'digest', 'a b c ')).rows[1]['a b c '] === 18446744073709551615n, 'Exact spaced Arrow field binding');
    const linkRaw = new Uint8Array(await (await fetch('/evidence/foreign-strings-example/link-results.arrow')).arrayBuffer());
    const unusual = readArrow(rename(linkRaw, 'condition', '__proto__'));
    check(Object.hasOwn(unusual.rows[0], '__proto__') && typeof unusual.rows[0]['__proto__'] === 'number', 'Prototype-like Arrow field remains ordinary data');
    rejects(() => readArrow(raw.subarray(0, 17)), 'Truncated Arrow framing rejected');
    check(exactText(example.rows[1].digest) === '18446744073709551615', 'Exact inspection/export text');
    check(jsonExact({value: [-0, 18446744073709551615n]}) === '{"value":[-0,"18446744073709551615"]}', 'Nested exact export retains signed zero and wide integers');
    check(!equivalent([{value: [-0]}], [{value: [0]}], [{name: 'value'}]), 'Nested signed zeros cannot pass exact equivalence');
    const canonical = await (await fetch('../perf/buster-manifest-v1.schema.json')).json();
    const manifestBytes = new Uint8Array(await (await fetch('/evidence/profile-example/manifest.bmf.json')).arrayBuffer());
    const manifest = parseExactJSON(new TextDecoder('utf-8', {fatal: true}).decode(manifestBytes));
    const originalDataset = new Dataset(manifest, new URL('/evidence/profile-example/manifest.bmf.json', location.href), await sha256(manifestBytes), canonical);
    const loaded = await originalDataset.table('arbitrary-custom');
    check(loaded.rows.length === 2 && loaded.rows[1].digest === 18446744073709551615n, 'Hash-verified lazy dataset loading');
    check(originalDataset.resourceURL('capture').pathname.endsWith('/capture.pftrace'), 'Native profile links to exact execution');
    rejects(() => new Dataset({...manifest, version: new JsonNumber('2')}, originalDataset.url, 'test', canonical), 'Unknown manifest version rejected');
    rejects(() => new Dataset({...manifest, requires: ['unknown.codec']}, originalDataset.url, 'test', canonical), 'Unknown required capability rejected');
    rejects(() => validateManifest({...manifest, recorded_at: '2026-02-29T12:00:00Z'}, canonical), 'Impossible calendar timestamp rejected');
    rejects(() => new Dataset({...manifest, resources: [...manifest.resources, manifest.resources[0]]}, originalDataset.url, 'test', canonical), 'Duplicate resource identity rejected');
    await rejectsAsync(() => verifiedFetch(originalDataset.resourceURL('arbitrary-custom-arrow'), '0'.repeat(64), '1104'), 'Corrupt resource hash rejected');
    await rejectsAsync(() => verifiedFetch(originalDataset.resourceURL('arbitrary-custom-arrow'), manifest.resources[0].sha256, '1103'), 'Response cannot exceed declared size');
    const dataset = {manifest: {status: 'complete', contexts: [{id: 'host', facts: {}, unknowns: {}}, {id: 'protocol', facts: {}, unknowns: {}}], metrics: [{id: 'custom', direction: 'lower'}]}};
    const annotation = {metric_id: 'custom', sample_level: 'process', status_column: 'status'};
    const contract = {estimator: 'paired', independent_unit: 'execution', independent_unit_columns: ['execution'], match_columns: ['case'], pair_columns: ['pair'], correctness_column: 'correct', coverage_column: 'coverage', identity_columns: {machine: 'machine', corpus: 'corpus', harness: 'harness', scope: 'scope', configuration: 'configuration', profile_state: 'profiled'}};
    const compared = {fields: [], definition: {bindings: {environment_context: 'host', protocol_context: 'protocol'}, measurements: {value: annotation}, metadata: {comparison: contract}}};
    const population = (value, prefix) => [1, 2, 3].map(pair => ({value: BigInt(value), execution: prefix + pair, pair, case: 'one', correct: true, coverage: 'complete', status: 'ok', machine: 'fixed', corpus: 'fixed', harness: 'fixed', scope: 'whole', configuration: 'fixed', profiled: false}));
    const a = population(10, 'a');
    const b = population(12, 'b');
    check(comparePopulation(dataset, compared, 'value', a, b).classification === 'regression', 'Matched independent pairs expose regression and uncertainty');
    check(comparePopulation(dataset, compared, 'value', a, population(8, 'b')).classification === 'improvement', 'Direction-aware improvement');
    check(comparePopulation(dataset, compared, 'value', population(0, 'a'), b).relative === null, 'Zero baseline has no relative percentage');
    check(comparePopulation(dataset, compared, 'value', population(-1, 'a'), b).relative === null, 'Negative baseline has no relative percentage');
    const mismatch = b.map(row => ({...row, machine: 'different'}));
    check(!comparePopulation(dataset, compared, 'value', a, mismatch).eligible, 'Incompatible machine cannot acquire a regression label');
    check(!comparePopulation(dataset, compared, 'value', [...a, a[0]], b).eligible, 'Repeated execution is not another independent unit');
    check(!comparePopulation(dataset, compared, 'value', a, b.slice(1)).eligible, 'Incomplete pairs cannot acquire a regression label');
    check(!comparePopulation(dataset, compared, 'value', a, b.map(row => ({...row, coverage: 'partial'}))).eligible, 'Incomplete correctness/coverage is ineligible');
    check(!comparePopulation(dataset, compared, 'value', a, b.map((row, i) => i === 0 ? {...row, value: null, status: 'censored'} : row)).eligible, 'Censored measurements cannot silently become a complete-case regression');
    const pairedA = [0n, 100n, 101n].map((value, i) => ({...a[i], value}));
    const pairedB = [100n, 101n, 102n].map((value, i) => ({...b[i], value}));
    check(comparePopulation(dataset, compared, 'value', pairedA, pairedB).delta.n === 1n, 'Paired effect uses median pair differences');
    window.__browserResult = {passed: true, first_party: true, checks: results.length, rows, typed_cells: cells, elapsed_ms: performance.now() - started, results, userAgent: navigator.userAgent};
    document.querySelector('#result').textContent = JSON.stringify(window.__browserResult, null, 2);
}
catch (error)
{
    window.__browserResult = {passed: false, checks: results.length, error: error.stack};
    document.querySelector('#result').textContent = JSON.stringify(window.__browserResult, null, 2);
}
