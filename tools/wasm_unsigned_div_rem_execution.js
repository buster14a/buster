// #1954: unsigned high-bit witnesses on the driver's freshly emitted artifact.
// Expectations and signed-opcode controls are independent of Buster's emitter.
// The frozen integer oracles and their existing completion policy stay intact.
'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
fs.writeSync(process.stdout.fd, `WASM_NODE_READY startup_ms=${Date.now()}\n`);
const uptime_us = () => Math.round(process.uptime() * 1e6);
process.on('exit', () => fs.writeSync(process.stdout.fd, `WASM_NODE_EXIT uptime_us=${uptime_us()}\n`));
if (process.argv.length !== 3) throw new Error('usage: node wasm_unsigned_div_rem_execution.js module.wasm');

const witnesses = [
    {name: 'u32_div', a: 4294967295, b: 2, expected: 2147483647, signed: 0},
    {name: 'u32_rem', a: 4294967295, b: 2, expected: 1, signed: -1},
    {name: 'u64_div', a: 18446744073709551615n, b: 2n, expected: 9223372036854775807n, signed: 0n},
    {name: 'u64_rem', a: 18446744073709551615n, b: 2n, expected: 1n, signed: -1n},
];
function check_unsigned_div_rem(exports) {
    let checks = 0;
    for (const witness of witnesses) {
        const label = `${witness.name}(UINT${typeof witness.a === 'bigint' ? 64 : 32}_MAX,2)`;
        assert.equal(exports[witness.name](witness.a, witness.b), witness.expected, label);
        checks += 1;
    }
    return checks;
}

// Four parameterized functions, without memory or imports, use published Wasm
// opcodes. Each mutant substitutes just div_u/rem_u with div_s/rem_s in one
// export. These controls exercise the checker, not production-lowering coverage.
function signedness_control(mutation) {
    const opcodes = [0x6e, 0x70, 0x80, 0x82];
    if (mutation >= 0) opcodes[mutation] -= 1;
    const exports = [4];
    const code = [4];
    for (let index = 0; index < witnesses.length; index += 1) {
        const name = Buffer.from(witnesses[index].name, 'ascii');
        exports.push(name.length, ...name, 0, index);
        code.push(7, 0, 0x20, 0, 0x20, 1, opcodes[index], 0x0b);
    }
    const bytes = Uint8Array.from([
        0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
        1, 13, 2, 0x60, 2, 0x7f, 0x7f, 1, 0x7f, 0x60, 2, 0x7e, 0x7e, 1, 0x7e,
        3, 5, 4, 0, 0, 1, 1,
        7, exports.length, ...exports,
        10, code.length, ...code,
    ]);
    assert(WebAssembly.validate(bytes), 'invalid unsigned div/rem control');
    return new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;
}

const bytes = fs.readFileSync(process.argv[2]);
assert(WebAssembly.validate(bytes), 'invalid unsigned div/rem Buster artifact');
console.log(`WASM_UNSIGNED_ARTIFACT sha256=${crypto.createHash('sha256').update(bytes).digest('hex')}`);
const functions = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;
const checks = check_unsigned_div_rem(functions);
for (const witness of witnesses) {
    console.log(`WASM_UNSIGNED_WITNESS export=${witness.name} dividend=${witness.a} divisor=${witness.b} expected=${witness.expected} status=pass`);
}
assert.equal(check_unsigned_div_rem(signedness_control(-1)), 4, 'unsigned control baseline');
console.log('WASM_UNSIGNED_CONTROL baseline=unsigned status=pass');
let rejected = 0;
for (let mutation = 0; mutation < witnesses.length; mutation += 1) {
    const control = signedness_control(mutation);
    const witness = witnesses[mutation];
    // A trap, missing export, invalid module or unrelated exception is not an
    // arithmetic rejection. Require this equality's exact wrong/right values.
    assert.throws(() => check_unsigned_div_rem(control), error =>
        error instanceof assert.AssertionError && error.operator === 'strictEqual' &&
        error.message.startsWith(`${witness.name}(`) &&
        error.actual === witness.signed && error.expected === witness.expected,
        `${witness.name}: signed opcode must fail its high-bit equality`);
    console.log(`WASM_UNSIGNED_CONTROL export=${witness.name} mutation=signed-opcode actual=${witness.signed} expected=${witness.expected} status=rejected`);
    rejected += 1;
}
console.log(`${checks}/4 unsigned high-bit div/rem checks and ${rejected}/4 signedness controls passed`);
process.stdout.write(`WASM_NODE_DONE uptime_us=${uptime_us()} resources=${process.getActiveResourcesInfo().join(',')}\n`);
