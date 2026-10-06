"use strict";
const fs = require("fs");
const util = require("util");
// The frozen integer oracle is loaded only after this process proves startup.
fs.writeSync(process.stdout.fd, `WASM_NODE_READY startup_ms=${Date.now()}\n`);
// Preserve DONE/EXIT evidence while ending the observed PipeWrap event-loop
// stall (#1793/#2066). Flush the frozen oracle's console output synchronously
// before explicit exit; asynchronous writes could otherwise lose its summary.
const uptime_us = () => Math.round(process.uptime() * 1e6);
process.on("exit", () => fs.writeSync(process.stdout.fd, `WASM_NODE_EXIT uptime_us=${uptime_us()}\n`));
const original_log = console.log;
console.log = (...args) => fs.writeSync(process.stdout.fd, util.format(...args) + "\n");
try
{
    require("../tests/wasm_integer_execution.js");
}
finally
{
    console.log = original_log;
}
fs.writeSync(process.stdout.fd, `WASM_NODE_DONE uptime_us=${uptime_us()} resources=${process.getActiveResourcesInfo().join(",")}\n`);
process.exit(0);
