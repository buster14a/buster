# cJSON compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in cJSON compatibility harness takes an external, pristine cJSON
1.7.19 checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_cjson --config Release /path/to/cjson-v1.7.19
```

The checkout must be commit
`c859b25da02955fef659d658b8f324b5cde87be3` with no tracked or untracked
changes. The harness compiles `cJSON.c`, `cJSON_Utils.c`, Unity and every
upstream test with Buster, links/runs the 18 core and 3 utility tests through
the system linker, compares a deterministic parse/print round trip with
Clang, and exercises FAST, NONE, MIR_STACK and QUALITY. Generated objects,
metrics and logs remain under `build/cjson-v1.7.19-<pid>/`.
