// Execute every scalar integer mapping against a BigInt reference, independently
// of Buster's IR, instruction table, and encoder. The C source uses parameters
// so constant folding cannot remove the operation under test.
'use strict';
const fs = require('node:fs');
const assert = require('node:assert/strict');
const bytes = fs.readFileSync(process.argv[2]);
assert(WebAssembly.validate(bytes), 'invalid scalar integer Wasm64 module');
const functions = new WebAssembly.Instance(new WebAssembly.Module(bytes)).exports;
let checks = 0;
for (const width of [32, 64]) {
    const sign = 1n << BigInt(width - 1);
    const mask = (1n << BigInt(width)) - 1n;
    const values = [0n, 1n, 3n, 7n, sign - 1n, sign, sign + 1n, mask - 1n, mask];
    for (const unsigned of [false, true]) {
        const prefix = `${unsigned ? 'u' : 'i'}${width}`;
        const typed = x => unsigned ? BigInt.asUintN(width, x) : BigInt.asIntN(width, x);
        const argument = x => width === 32 ? Number(BigInt.asIntN(width, x)) : BigInt.asIntN(width, x);
        const result = x => width === 32 ? Number(BigInt.asIntN(width, x)) : BigInt.asIntN(width, x);
        const check = (name, args, expected) => {
            assert.equal(functions[`${prefix}_${name}`](...args.map(argument)), expected, `${prefix}_${name}(${args.join(',')})`);
            checks += 1;
        };
        for (const aBits of values) {
            const a = typed(aBits);
            check('not', [a], result(~a));
            for (const bBits of values) {
                const b = typed(bBits);
                check('and', [a, b], result(a & b));
                check('or', [a, b], result(a | b));
                check('xor', [a, b], result(a ^ b));
                check('eq', [a, b], Number(a === b));
                check('ne', [a, b], Number(a !== b));
                check('lt', [a, b], Number(a < b));
                check('gt', [a, b], Number(a > b));
                check('le', [a, b], Number(a <= b));
                check('ge', [a, b], Number(a >= b));
            }
            for (const shift of [0n, 1n, 2n, BigInt(width - 2), BigInt(width - 1)]) {
                // Do not turn C undefined signed-left-shift cases into tests.
                if (unsigned || (a >= 0n && (a << shift) < sign)) {
                    check('shl', [a, shift], result(a << shift));
                }
                check('shr', [a, shift], result(a >> shift));
            }
        }
    }
}
console.log(`${checks}/${checks} scalar integer Wasm64 engine checks passed`);
