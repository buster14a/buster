# zlib compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in zlib compatibility harness takes an external, pristine zlib v1.3.1
checkout; upstream sources are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_zlib --config Release /path/to/zlib-v1.3.1
```

The checkout must be commit
`51b7f2abdade71cd9bb0e7a373ef2610ec6f9daf` with no tracked or untracked
changes. The harness explicitly compiles all 15 zlib library translation units
with Clang and Buster and the example, minigzip, and infcover test units with
Buster, builds `libz.a`, exercises FAST, NONE, MIR_STACK and QUALITY, cross-links both
archive directions, compares a deterministic compression/decompression probe
and minigzip corpus hashes with Clang, and records source metrics plus actual
compression-workload throughput. After the explicit manifest passes, it
extracts a clean upstream archive and runs its unmodified configure script and
`libz.a` make target with the selected Buster driver. Generated objects, metrics,
archives, and logs remain under `build/zlib-v1.3.1-<pid>/`.
