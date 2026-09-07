# yyjson compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in yyjson compatibility harness takes an external, pristine yyjson
0.12.0 checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_yyjson --config Release /path/to/yyjson-v0.12.0
```

The checkout must be tag `0.12.0` at commit
`8b4a38dc994a110abaec8a400615567bd996105f` with no tracked or untracked
changes. The harness compiles the unmodified yyjson amalgamation and its
upstream utility/test units with Buster, links and runs all 12 upstream test
executables, and compares a deterministic parse/serialize corpus with Clang.
It exercises FAST, NONE, MIR_STACK, and QUALITY for the amalgamation and
corpus; optional SIMD is explicitly disabled. Source metrics, compiler timing,
allocator diagnostics, and generated objects/logs remain under
`build/yyjson-v0.12.0-<pid>/`.
