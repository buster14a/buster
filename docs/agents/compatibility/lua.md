# Lua compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in Lua compatibility harness takes an external, pristine Lua v5.4.8
checkout; upstream sources are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_lua --config Release /path/to/lua-5.4.8/src /path/to/lua-v5.4.8
```

The first path is `src/` from the official `lua-5.4.8.tar.gz` release
(SHA-256 `4f18ddae154e793e46eeab727c59ef1c0c0c2b744e7b94219710d76f530629ae`);
the second is a pristine Git checkout at commit
`6e22fedb74cf0c9b6656e9fce8b7331db847c605` with no tracked or untracked
changes. The harness verifies all overlapping production files byte-for-byte
and authenticates the release-only `luac.c` (15,145 bytes, Buster hash
`6cdc1a7db5393273`). It owns an explicit 34-unit executable manifest (including
`luac.c`), compiles each unit with Buster while recording source metrics and
fallback diagnostics, compiles `ltests.c` and a distinct test-interpreter object
set with `-DLUA_USER_H=\"ltests.h\"`, links
both `lua` and `luac` through `ide cc`, builds Clang references, and compares a
deterministic workload covering parsing, bytecode generation, table operations,
closures, coroutines, varargs, floating point, and protected errors. Once the
manifest compiles, it exercises FAST, NONE, MIR_STACK, and QUALITY link gates,
keeps the ltests-enabled support and test-interpreter object sets as separate
compile/link gates, then runs the unmodified `testes/all.lua` suite in Lua's
`-e _U=true` user mode with the production FAST Buster and Clang interpreters.
It compares both exit statuses and normalized transcripts (entropy, clock,
memory, path, date, random-range, sorting, and short-circuit-statistic lines
are excluded), and records compile/runtime timings. Generated objects, metrics, workloads, and logs remain under
`build/lua-v5.4.8-<pid>/`; `test_lua` is opt-in because it consumes an external
checkout and reports the first unsupported frontend construct without altering
upstream sources.
