# LZ4 compatibility harness

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

[Compatibility harness index](../compatibility.md). Read only the harness you are working on; pins, measurements, and past failure counts describe the revision recorded below and must be rechecked before reuse.

The opt-in LZ4 compatibility harness takes an external, pristine LZ4 v1.10.0
checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_lz4 --config Release /path/to/lz4-v1.10.0
```

The checkout must be tag `v1.10.0` at commit
`ebb370ca83af193212df4dcbadcc5d87bc0de2f0` with no tracked or untracked
changes. The harness owns an explicit 24-unit source manifest and does not
drive upstream's make or CMake files — upstream build-system detection is a
later driver milestone — so it compiles in named stages and fails at the
first one that breaks: the portable block library (`lz4.c`, `lz4hc.c`,
`xxhash.c`), then the frame library (`lz4frame.c`, `lz4file.c`), then the CLI
(`programs/*.c`), which is linked through `ide cc` so the driver itself is
covered across a multi-file library plus executable. Everything is built with
one flag set (`-I lib -I programs -DXXH_NAMESPACE=LZ4_ -DNDEBUG -O2`) because
upstream's three makefiles agree on it and objects that disagree would not
link. The CLI is the upstream `lz4-nomt` shape, without `-DLZ4IO_MULTITHREAD`,
which keeps the compressed bytes reproducible. `tests/freestanding.c` is the
one upstream unit left out of the manifest: it declares named-register
variables for a raw syscall, which the C frontend does not support, and it is
excluded rather than patched.

It then compiles and runs the upstream unit, round-trip, corruption, and
interoperability programs — `fuzzer`, `frametest`, `fullbench`,
`roundTripTest`, `decompress-partial`, `decompress-partial-usingDict`,
`checkFrame`, `abiTest`, `checkTag`, plus `datagen` as the corpus generator
they are written against — in the bounded `-i` forms upstream's makefile uses,
so a run is minutes rather than hours. `abiTest` links against the harness's
own static archive rather than an installed shared `liblz4`, since the harness
must not depend on a system-installed lz4; it therefore checks the
same-version API/ABI surface rather than cross-version stability.

The harness raises its own `RLIMIT_STACK` soft limit to 128 MB before it spawns
anything, and the reason is Buster's frame layout rather than an upstream
quirk: Buster gives every sibling block in a function its own frame slot where
Clang overlaps the ones whose live ranges cannot intersect, so the fuzzer's
`FUZ_unitTests` — a long chain of sibling scopes each holding a multi-megabyte
buffer — lands on a 10,0 MB frame under FAST, MIR_STACK and QUALITY and a
24,7 MB one under NONE, against the Clang build's 271 KB. Under the 8 MB a
login shell hands out, the Buster-built fuzzer dies with SIGSEGV before it
executes a single test; the NONE build still dies at 32 MB and passes at 40 MB,
so 128 MB is about three times the worst measured requirement. The program is
correct — every test passes once it has room — so this is a code-quality gap,
not a miscompile, and shrinking the frame is optimization work that is not to
be added unasked. The limit is raised rather than the manifest trimmed or the
iteration count lowered, because `FUZ_unitTests` is the part of the fuzzer
worth running. A soft limit is inherited at spawn, which is why it is set once
up front; it is a finite value rather than `RLIM_INFINITY`, which would move
the loader's mmap layout, and it is POSIX-only — a Windows thread's stack size
lives in the PE header of the image being run, so a parent cannot grant a child
more of it, and there the harness reports the limit unchanged.

Cross-checking runs both directions over a deterministic corpus: each frame
shape is compressed by the Buster CLI and by the Clang CLI, the compressed
bytes must be identical — LZ4 output is deterministic for a given level, so a
round-trip alone would accept two compressors that merely agree on what they
can each undo — and each side's output is then decompressed and integrity-
tested by the other. The rows cover `-1`, `-9` with linked blocks, block
checksums and content size, `-12`, an incompressible corpus, legacy frames,
and 4 MB blocks over an 8 MiB input; `tests/goldenSamples/skip.bin` covers
skippable frames alone and embedded in a concatenated stream.
`tests/basic_lz4_roundtrip.c` is the deterministic probe both compilers build
and whose output must match byte-for-byte: it drives the block, HC, and frame
APIs over unaligned little-endian records, explicit endian assertions, and a
4 MiB input, and it is cross-linked in both directions (Buster probe over the
Buster archive, Clang probe over the Buster archive, Buster probe over the
Clang archive) so a codegen difference stays separable from a linkage one.

Every workload runs under FAST, NONE, MIR_STACK, and QUALITY. The CPU axis is
ordered so it cannot mask a portable failure: the `portable` configuration
runs first and carries the upstream test suite, and only then do the explicit
`-march=baseline` and `-march=native` configurations repeat the three compile
stages, the probe, the large-input workload, and the full cross-check for all
four allocators. Compiler cost and generated-code quality are reported
separately and must not be conflated: `LZ4_METRIC` lines carry per-unit
compiler wall time with the `-fsource-metrics=` source metrics, while
`LZ4_THROUGHPUT` reports compress and decompress MB/s for the compression
workload and `LZ4_CODEGEN` reports instructions retired and instructions per
byte for the same runs. Both quote the uncompressed byte count, which is the
conventional denominator for a compression rate in either direction. The
throughput is end-to-end for the whole CLI process, including its startup and
its reads and writes, so it is indicative rather than a benchmark and drifts
with whatever else the machine is doing. The instruction counts do not: they
come from the same Linux hardware counter as `STEP_INSTRUCTIONS`, read either
side of a child that runs alone, and the counter follows this process tree
only, so they are contention-immune and are the number to trend. Where no
counter is available the `LZ4_CODEGEN` line is simply absent, which is never an
error. For scale, a full matrix takes about seven minutes and one recorded run
put the probe's cost at 13,819 instructions/byte for the Clang reference
against 94,005 for QUALITY, 97,552 for FAST, 181,586 for MIR_STACK and 313,577
for NONE over the same 53,7 MB workload — the allocator ordering the names
promise, with NONE 3,3x QUALITY. Two properties of that run are worth keeping
as expectations. Each allocator's instructions/byte was identical to three
decimals across all three CPU configurations, the raw counts differing by about
200 in 5,2 billion, which is what a working counter over a deterministic
workload should look like; and that identity means `-march=baseline` and
`-march=native` currently generate the same code as the default for LZ4, so a
future run where they diverge is signal rather than noise. Generated objects,
metrics, archives, corpora, and logs remain under
`build/lz4-v1.10.0-<pid>/`, which is about 1,4 GB for a full matrix — twelve
copies of every object plus the corpora — and is not cleaned up on the way out,
so delete the directories of runs you are done with.
