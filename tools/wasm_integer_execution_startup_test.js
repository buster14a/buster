// #1793/#2066: actual frozen oracle, independent Wasm, and shutdown controls.
"use strict";
const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {spawnSync} = require("node:child_process");
const root = path.resolve(__dirname, "..");
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "buster-integer-shim-"));
const summary = "1504 frontend-to-Wasm integer checks passed";
function leb(value)
{
    const bytes = [];
    do
    {
        let byte = value & 127;
        value >>>= 7;
        if (value) byte |= 128;
        bytes.push(byte);
    } while (value);
    return bytes;
}
function section(id, bytes)
{
    return [id, ...leb(bytes.length), ...bytes];
}
// These core Wasm opcodes and types are independent of Buster's emitter.
// Unary NOT is XOR with -1; comparisons always return i32.
function module_bytes(mutate)
{
    const types = [];
    const functions = [];
    const exports = [];
    const bodies = [];
    let index = 0;
    for (const bits of [32, 64])
    {
        for (const signed of [false, true])
        {
            const type = bits === 32 ? 0x7f : 0x7e;
            const delta = bits === 32 ? 0 : 0x12;
            const comparison_delta = bits === 32 ? 0 : 0x0b;
            const ops = [
                ["add", 0x6a + delta], ["sub", 0x6b + delta], ["mul", 0x6c + delta],
                ["div", (signed ? 0x6d : 0x6e) + delta],
                ["rem", (signed ? 0x6f : 0x70) + delta],
                ["and", 0x71 + delta], ["or", 0x72 + delta], ["xor", 0x73 + delta],
                ["shl", 0x74 + delta], ["shr", (signed ? 0x75 : 0x76) + delta],
                ["eq", 0x46 + comparison_delta, true], ["ne", 0x47 + comparison_delta, true],
                ["lt", (signed ? 0x48 : 0x49) + comparison_delta, true],
                ["gt", (signed ? 0x4a : 0x4b) + comparison_delta, true],
                ["le", (signed ? 0x4c : 0x4d) + comparison_delta, true],
                ["ge", (signed ? 0x4e : 0x4f) + comparison_delta, true],
                ["not", 0x73 + delta, false, true],
            ];
            for (const [op, opcode, comparison, unary] of ops)
            {
                const name = Buffer.from((signed ? "s" : "u") + bits + "_" + op);
                types.push(0x60, unary ? 1 : 2, type, ...(unary ? [] : [type]), 1,
                           comparison ? 0x7f : type);
                functions.push(...leb(index));
                exports.push(...leb(name.length), ...name, 0, ...leb(index));
                const body = [0, 0x20, 0, ...(unary ? [bits === 32 ? 0x41 : 0x42, 0x7f] : [0x20, 1]),
                              mutate && index === 0 ? 0x6b : opcode, 0x0b];
                bodies.push(...leb(body.length), ...body);
                index++;
            }
        }
    }
    return Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0,
        ...section(1, [...leb(index), ...types]),
        ...section(3, [...leb(index), ...functions]),
        ...section(7, [...leb(index), ...exports]),
        ...section(10, [...leb(index), ...bodies])]);
}
function run(shim, module, preload, timeout = 30000)
{
    const args = preload ? ["--require", preload, shim, module] : [shim, module];
    return spawnSync(process.execPath, args, {encoding: "utf8", timeout, maxBuffer: 1024 * 1024});
}
function succeeded(result)
{
    assert.equal(result.error, undefined, result.error && result.error.message);
    assert.equal(result.status, 0, result.stderr);
    assert.equal(result.stderr, "");
    assert.match(result.stdout, /^WASM_NODE_READY startup_ms=\d+\r?\n/);
    assert.equal(result.stdout.split(summary).length, 2, result.stdout);
    assert.match(result.stdout, /WASM_NODE_DONE uptime_us=\d+ resources=[^\r\n]*\r?\nWASM_NODE_EXIT uptime_us=\d+\r?\n$/);
}
try
{
    fs.mkdirSync(path.join(temporary, "tools"));
    fs.mkdirSync(path.join(temporary, "tests"));
    const shim = path.join(temporary, "tools/shim.js");
    const oracle = path.join(temporary, "tests/wasm_integer_execution.js");
    fs.copyFileSync(path.join(root, "tools/wasm_integer_execution_startup.js"), shim);
    fs.copyFileSync(path.join(root, "tests/wasm_integer_execution.js"), oracle);
    const module = path.join(temporary, "control.wasm");
    const mutant = path.join(temporary, "mutant.wasm");
    const valid_bytes = module_bytes(false);
    assert(WebAssembly.validate(valid_bytes), "independent baseline must validate");
    fs.writeFileSync(module, valid_bytes);
    fs.writeFileSync(mutant, module_bytes(true));
    succeeded(run(shim, module));
    console.log("SHIM_CONTROL independent-1504-check-oracle=pass");

    const live = path.join(temporary, "live.js");
    fs.writeFileSync(live,
        'const log = console.log; process.on("exit", () => { if (console.log !== log) throw Error("console-restore-control"); }); setInterval(() => {}, 1000);\n');
    succeeded(run(shim, module, live));
    // The same completed oracle must remain live without explicit exit.
    // Allow normal Node startup time, then require the completed summary and
    // DONE as well as the timeout; a pre-startup timeout cannot satisfy this control.
    const implicit = path.join(temporary, "tools/implicit.js");
    const text = fs.readFileSync(shim, "utf8");
    assert.equal(text.split("process.exit(0);").length, 2);
    fs.writeFileSync(implicit, text.replace("process.exit(0);", ""));
    const stalled = run(implicit, module, live);
    assert.equal(stalled.error && stalled.error.code, "ETIMEDOUT");
    assert(stalled.stdout.includes(summary), stalled.stdout);
    assert(stalled.stdout.includes("WASM_NODE_DONE"), stalled.stdout);
    assert(!stalled.stdout.includes("WASM_NODE_EXIT"), stalled.stdout);
    console.log("SHIM_CONTROL live-resource=pass implicit-exit=timeout");

    const buffered = path.join(temporary, "buffered.js");
    fs.writeFileSync(buffered, "process.stdout.write = () => true; setInterval(() => {}, 1000);\n");
    succeeded(run(shim, module, buffered));
    console.log("SHIM_CONTROL asynchronous-stdout-disabled=pass");

    const bad = run(shim, mutant);
    assert.equal(bad.status, 1, bad.stderr);
    assert(bad.stderr.includes("u32_add("), bad.stderr);
    assert(!bad.stdout.includes(summary));
    assert(!bad.stdout.includes("WASM_NODE_DONE"));
    const missing = run(shim, path.join(temporary, "missing.wasm"));
    assert.equal(missing.status, 1, missing.stderr);
    assert(missing.stderr.includes("ENOENT"), missing.stderr);
    assert(!missing.stdout.includes(summary));
    assert(!missing.stdout.includes("WASM_NODE_DONE"));

    const write_failure = path.join(temporary, "write-failure.js");
    fs.writeFileSync(write_failure,
        'const fs = require("fs"); const write = fs.writeSync; fs.writeSync = (fd, text) => { if (String(text).includes("1504 frontend-to-Wasm")) throw Error("summary-write-control"); return write(fd, text); };\n');
    const failed_write = run(shim, module, write_failure);
    assert.equal(failed_write.status, 1, failed_write.stderr);
    assert(failed_write.stderr.includes("summary-write-control"));
    assert(!failed_write.stdout.includes(summary));
    assert(!failed_write.stdout.includes("WASM_NODE_DONE"));
    console.log("SHIM_CONTROL arithmetic-load-output-failures=rejected");

    const stderr = path.join(temporary, "stderr.js");
    fs.writeFileSync(stderr, 'require("fs").writeSync(process.stderr.fd, "stderr-control\\n");\n');
    const noisy = run(shim, module, stderr);
    assert.equal(noisy.status, 0);
    assert.equal(noisy.stderr, "stderr-control\n");
    assert(noisy.stdout.includes(summary));
    // The production harness retains its separate empty-stderr requirement.
    assert.notEqual(noisy.stderr, "");
    assert.equal(fs.readFileSync(oracle, "utf8"),
                 fs.readFileSync(path.join(root, "tests/wasm_integer_execution.js"), "utf8"));
    console.log("SHIM_CONTROL stderr-preserved=pass frozen-oracle-unchanged=pass");
}
finally
{
    fs.rmSync(temporary, {recursive: true, force: true});
}
