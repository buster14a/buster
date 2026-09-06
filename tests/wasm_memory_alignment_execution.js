// Execute a complete C-frontend-produced module in an independent engine.
"use strict";
const fs = require("fs");
const assert = require("assert").strict;
if (process.argv.length !== 3) throw new Error("usage: node [memory64 flags] wasm_memory_alignment_execution.js module.wasm");
const moduleBytes = fs.readFileSync(process.argv[2]);
const instance = new WebAssembly.Instance(new WebAssembly.Module(moduleBytes));
const e = instance.exports;
const cases = [
    ["i8", -7, -100, -100],
    ["i16", -1234, -23456, -23456],
    ["i32", 11, 0xf1234567, 0xf1234567 | 0],
    ["i64", 19n, 0x8123456789abcdefn, BigInt.asIntN(64, 0x8123456789abcdefn)],
    ["f32", 1.5, 3.25, 3.25],
    ["f64", 2.5, -7.125, -7.125],
    ["pointer", 0n, 0x12345678n, 0x12345678n],
];
for (const [name, initial, written, expected] of cases) {
    assert.equal(e["read_" + name](), initial, name + " initializer");
    e["write_" + name](written);
    assert.equal(e["read_" + name](), expected, name + " store/load round trip");
}
console.log("14/14 frontend-to-Wasm over-aligned scalar checks passed");
