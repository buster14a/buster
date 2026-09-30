"use strict";
const fs = require("fs");
// The frozen integer oracle is loaded only after this process proves startup.
fs.writeSync(process.stdout.fd, `WASM_NODE_READY startup_ms=${Date.now()}\n`);
require("../tests/wasm_integer_execution.js");
