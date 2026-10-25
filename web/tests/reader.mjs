// Real browser interoperability probes; independent native checks run in C.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
import {readArrow, readJSONL, equivalent, parseExactJSON, JsonNumber, Decimal, decimalFromString, coordinate, rational, exactText, comparePopulation} from '../perf/data.mjs';

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
try
{
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
    const parsed = parseExactJSON('{"n":18446744073709551615,"d":1.250000,"s":"literal 17"}');
    check(parsed.n instanceof JsonNumber && parsed.n.text === '18446744073709551615' && parsed.d.text === '1.250000' && parsed.s === 'literal 17', 'Lexical numbers never pass through Number ingestion');
    rejects(() => parseExactJSON('{"n":1e}'), 'Malformed numeric token rejected');
    rejects(() => decimalFromString('1.2500001', 6, 38), 'Decimal rounding rejected');
    rejects(() => readJSONL('{"time_ns":"0","items":"0","penalty":"1.25","status":"ok","reason":null,"digest":18446744073709551615}', example.fields), 'Numeric uint64 JSON token rejected');
    const large = rational(18446744073709551614n);
    check(coordinate(large, rational(18446744073709551613n), rational(18446744073709551615n)) === 0.5, 'Plot coordinates subtract large exact origins first');
    const fieldNames = ['延迟 ns', 'a field with spaces', '__proto__'];
    const original = Arrow.tableFromArrays({a: [1n, 2n], b: [1n, 2n], c: [1n, 2n]});
    const schema = new Arrow.Schema(original.schema.fields.map((field, i) => new Arrow.Field(fieldNames[i], field.type, field.nullable)));
    const table = new Arrow.Table(schema, [new Arrow.RecordBatch(schema, original.batches[0].data)]);
    const unusual = readArrow(Arrow.tableToIPC(table));
    check(unusual.fields.every((field, i) => field.name === fieldNames[i]) && unusual.rows[0]['__proto__'] === 1n, 'Unicode, spaced and prototype-like exact field names');
    check(exactText(example.rows[1].digest) === '18446744073709551615', 'Exact inspection/export text');
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
    window.__browserResult = {passed: true, checks: results.length, rows, typed_cells: cells, results, userAgent: navigator.userAgent};
    document.querySelector('#result').textContent = JSON.stringify(window.__browserResult, null, 2);
}
catch (error)
{
    window.__browserResult = {passed: false, checks: results.length, error: error.stack};
    document.querySelector('#result').textContent = JSON.stringify(window.__browserResult, null, 2);
}
