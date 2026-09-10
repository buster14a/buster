"use strict";
const fs = require("fs");
const assert = require("assert").strict;
if (process.argv.length !== 3) throw new Error("usage: node wasm_local_aggregate_execution.js module.wasm");
const bytes = fs.readFileSync(process.argv[2]);
assert(WebAssembly.validate(bytes), "independent module validation");
const instance = new WebAssembly.Instance(new WebAssembly.Module(bytes));
const values = [0n, 1n, 127n, 128n, -1n, -9223372036854775808n, 9223372036854775807n];
let checks = 0;
for (const x of values) for (const y of values) {
    const expected = [x + y, x + 2n * y + 1n, x + y + 12n, x + y + 16n, x];
    for (const [index, name] of ["sum", "mutation", "packed", "nested", "union"].entries()) {
        assert.equal(instance.exports["aggregate_" + name](x, y), BigInt.asIntN(64, expected[index]), name);
        checks++;
    }
}
console.log(checks + " independent Wasm local aggregate executions passed");
