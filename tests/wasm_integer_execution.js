// Check the frontend and binary emitter together in an independent engine.
"use strict";
const fs = require("fs");
const assert = require("assert").strict;
if (process.argv.length !== 3) throw new Error("usage: node wasm_integer_execution.js module.wasm");
const e = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(process.argv[2]))).exports;
let checks = 0;
for (const bits of [32, 64]) {
    for (const signed of [false, true]) {
        const name = (signed ? "s" : "u") + bits;
        const normalize = x => signed ? BigInt.asIntN(bits, x) : BigInt.asUintN(bits, x);
        const argument = x => bits === 64 ? x : Number(x);
        const check = (op, a, b, expected, comparison = false) => {
            const actual = e[name + "_" + op](argument(a), argument(b));
            const result = comparison ? Number(expected) : argument(BigInt.asIntN(bits, expected));
            assert.equal(actual, result, `${name}_${op}(${a}, ${b})`);
            checks++;
        };
        const small = signed ? [-37n, 5n] : [37n, 5n];
        for (const [a, b] of [small, [0n, 1n], [1n, 1n]]) {
            check("add", a, b, a + b);
            check("sub", a, b, a - b);
            check("mul", a, b, a * b);
            check("div", a, b, a / b);
            check("rem", a, b, a % b);
        }
        const high = 1n << BigInt(bits - 1);
        const values = [0n, 1n, 5n, high - 1n, high, (1n << BigInt(bits)) - 1n].map(normalize);
        for (const a of values) {
            check("not", a, 0n, ~a);
            for (const b of values) {
                check("and", a, b, a & b);
                check("or", a, b, a | b);
                check("xor", a, b, a ^ b);
                check("eq", a, b, a === b, true);
                check("ne", a, b, a !== b, true);
                check("lt", a, b, a < b, true);
                check("le", a, b, a <= b, true);
                check("gt", a, b, a > b, true);
                check("ge", a, b, a >= b, true);
            }
            for (const shift of [0n, 1n, BigInt(bits - 1)]) {
                check("shr", a, shift, a >> shift);
                // Signed left shifts must stay representable and nonnegative.
                if (!signed || (a >= 0n && (a << shift) < high)) check("shl", a, shift, a << shift);
            }
        }
    }
}
console.log(`${checks} frontend-to-Wasm integer checks passed`);
