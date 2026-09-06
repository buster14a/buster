#!/usr/bin/env python3
"""Focused Linux-host regression against the repository's real C implementation.

Requires a host C compiler. This does not replace test_all or Release self-host.
Set CC=gcc, AUDIT_OPT=-O0, or AUDIT_SANITIZE=1 to vary the host configuration.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def build(source: str, out: Path) -> None:
    compiler = shlex.split(os.environ.get("CC", "clang"))
    flags = [os.environ.get("AUDIT_OPT", "-O2"), "-g", "-fwrapv", "-funsigned-char",
             "-Wno-unused-function", "-ffunction-sections", "-fdata-sections", "-I", str(ROOT / "src")]
    if os.environ.get("AUDIT_SANITIZE") == "1":
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    subprocess.run(compiler + flags + [str(ROOT / "tests" / source), "-Wl,--gc-sections",
                                      "-pthread", "-lm", "-o", str(out)], check=True)


def leb(value: int) -> bytes:
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return bytes(out)


def module(store: bool, value_type: int, instruction: bytes) -> bytes:
    # memory64, one function; the memory access bytes come directly from wasm.c.
    params = bytes([0x7e, value_type]) if store else bytes([0x7e])
    results = b"" if store else bytes([value_type])
    function_type = bytes([1, 0x60, len(params)]) + params + bytes([len(results)]) + results
    body = bytes([0, 0x20, 0]) + (bytes([0x20, 1]) if store else b"") + instruction + bytes([0x0b])
    sections = [(1, function_type), (3, bytes([1, 0])), (5, bytes([1, 4, 1])),
                (10, bytes([1]) + leb(len(body)) + body)]
    return bytes([0, 97, 115, 109, 1, 0, 0, 0]) + b"".join(bytes([i]) + leb(len(p)) + p for i, p in sections)


def main() -> None:
    with tempfile.TemporaryDirectory(prefix="buster-wasm-alignment-") as directory:
        directory = Path(directory)
        binary = directory / "probe"
        build("wasm_memory_alignment_regression.c", binary)
        output = subprocess.check_output([str(binary)], text=True)
        names = []
        for line in output.splitlines():
            kind, alignment, store, value_type, encoded = line.split()
            name = directory / f"kind{kind}-align{alignment}-store{store}.wasm"
            name.write_bytes(module(bool(int(store)), int(value_type), bytes.fromhex(encoded)))
            names.append(str(name))
        if len(names) != 112:
            raise RuntimeError(f"Expected 112 emitted cases, got {len(names)}")
        js = directory / "validate.js"
        js.write_text("""const fs = require('fs');
let failed = 0;
for (const path of process.argv.slice(2)) {
    try { new WebAssembly.Module(fs.readFileSync(path)); }
    catch (e) { ++failed; if (failed <= 4) console.error(path + ': ' + e.message); }
}
console.log((process.argv.length - 2 - failed) + '/' + (process.argv.length - 2) + ' Wasm modules valid');
process.exitCode = failed ? 1 : 0;
""")
        node = shlex.split(os.environ.get("NODE", "node"))
        node_flags = shlex.split(os.environ.get("AUDIT_NODE_FLAGS", "--experimental-wasm-memory64"))
        subprocess.run(node + node_flags + [str(js)] + names, check=True)


if __name__ == "__main__":
    main()
