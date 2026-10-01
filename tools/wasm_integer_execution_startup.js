"use strict";
const fs = require("fs");
// The frozen integer oracle is loaded only after this process proves startup.
fs.writeSync(process.stdout.fd, `WASM_NODE_READY startup_ms=${Date.now()}\n`);
// Separate slow execution from a slow exit (#2066). DONE is queued on the
// same stream after the oracle's summary once it returns; EXIT is written
// synchronously once the event loop has drained. Both are process uptimes.
const uptime_us = () => Math.round(process.uptime() * 1e6);
process.on("exit", () => fs.writeSync(process.stdout.fd, `WASM_NODE_EXIT uptime_us=${uptime_us()}\n`));
require("../tests/wasm_integer_execution.js");
process.stdout.write(`WASM_NODE_DONE uptime_us=${uptime_us()} resources=${process.getActiveResourcesInfo().join(",")}\n`);
