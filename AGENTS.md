# AGENTS.md

Instructions for coding agents working in this repository. When this document
and the code disagree, the code wins — update this file in the same change.

## Project

**buster** is a from-scratch C compiler and toolchain written in C. The C
frontend is the sole source-language frontend. The `ide` executable is a
headless compiler, test runner, benchmark driver, fuzz entrypoint, and metadata
tool; the target name is retained for build-script compatibility. The `ide cc`
driver also orchestrates optional external shader toolchains for SPIR-V,
NVPTX/PTX, AMDGCN/HSA code objects, Metal AIR/metallib, and DXIL; these
pipelines add no linked or vendored dependency to the executable.
Native outputs include freestanding PE32+ UEFI applications for x86-64 and
AArch64 alongside the hosted desktop and mobile targets.

The compiler pipeline is:

```text
C source -> preprocessing/parsing/semantic analysis -> canonical IR
         -> native machine IR/code generation -> object writer -> linker
         -> direct core Wasm64 emission for wasm64 targets
```

The experimental custom-language frontend, its semantic model, its
source-level interpreter/JIT entrypoints, the editor model built around it,
and their C implementation and test files are intentionally removed. The
`.bbb` fixture corpus and detailed former guidance remain as preservation
material for a future redesign, but are absent from the generated build graph,
unity translation unit, test registration, benchmarks, packaging, and CI.
Shared active facilities remain: the canonical IR, optimizers, machine
selection and scheduling, register allocators, x86-64 and AArch64 encoders,
debug emitters, object formats, linker, generic object JIT, assembler, and
Wasm64 backend. See `DORMANT_CUSTOM_COMPILER.md` for the historical reference.

## Current compiler priority

Prioritize compiler throughput and producing an artifact as soon as possible.
Do not add optimization passes or spend compile time improving generated code
unless that work is explicitly requested. Prefer direct, predictable lowering
and code generation with minimal analysis overhead.

The long-term implementation direction is to make the compiler broadly
SIMD-friendly: prefer compact contiguous data, batchable lane work, masks for
tails and irregular active sets, branch-regular loops, and layouts that can
evolve from scalar to vector processing. Reducing branch and cache misses is
therefore an architectural goal as well as a present-day performance lever.
Keep the measurement discipline explicit: record SIMD-readiness and miss-rate
improvements, but do not describe a scalar throughput regression as a current
speedup merely because a proxy improved.

### Data-oriented compiler design

- Optimize the real workload, especially its common cases, rather than every
  theoretically possible case. Organize and measure the data before optimizing
  the code that transforms it.
- Where there is one, look for many — including along the time axis. Before
  designing a scalar one-off path, look for the population across instructions,
  values, blocks, functions, modules, repeated queries, or successive compiler
  phases, and preserve enough context to process that population together.
- Put frequently processed values together and separate different states and
  cold metadata. Maximize useful information per cache line; avoid large hot
  objects, padding, scattered flags, and pointer graphs that spend bandwidth
  carrying data the current operation does not use.
- Replace per-object, last-minute decisions with explicit classification
  followed by homogeneous batch processing. Prefer masks, compact indices,
  active counts, command buffers, state-specific arrays, and other forms that
  can evolve into SIMD lanes over repeatedly testing independent booleans.
- Do not reread or recompute facts already available. Hoist invariant reads,
  calls, and branches even when an optimizing compiler might discover the same
  transformation. Make constraints explicit so hot interfaces retain the
  context needed to avoid generic work.
- Precompute offline or during serial prewarm whenever practical. The cheapest
  runtime operation is one that no longer exists; keep cold malformed-input and
  fallback handling outside regular hot loops where the public contract allows
  it.
- Estimate before implementing: organize the cases, rank them by
  `probability * count`, calculate approximate memory and compute cost, then
  measure. Track useful information per cache line and passes per item, not
  merely time attributed to a function.
- Apply the process recursively. After improving one hot data transformation,
  inspect the next largest remaining source of waste. Preserve maintainability,
  debuggability, determinism, concurrency, and portability while doing so;
  good data organization should improve all of them.
- For buster, favor SoA or compact projections for hot compiler facts, explicit
  active-lane masks and counts instead of fixed-maximum scans, grouped machine
  instruction states where ordering permits, and prewarmed compact plans rather
  than repeated metadata interpretation. Treat AVX-512 as a consequence of a
  sound data layout, not the starting point: a compiler can optimize the final
  instruction sequence, but it cannot repair wasteful data movement or context
  that the program discarded.

### SIMD transformation rules

- Choose the data layout before choosing SIMD instructions. Prefer SoA for
  homogeneous transforms, use hybrid/tiled layouts only when measurements show
  a scalar/SIMD tradeoff, and locally project fixed external AoS input into the
  compact form a hot transform needs. Public interfaces do not have to mirror
  internal storage.
- For small divergent operations, compute candidate results and select with a
  mask. For larger or more expensive divergence, first partition or compact
  stable indices, then run a dedicated homogeneous kernel over each set. A hot
  data-dependent branch needs strong measured predictability; a predictable
  loop branch is not a problem merely because it is a branch.
- Treat compare masks as useful data. Prefer compare -> mask -> compact/select
  pipelines, advance compact outputs by mask population count, and use
  precomputed permutation controls when a direct compress instruction is not
  available. Preserve stable order whenever output order is observable.
- Do not add software prefetch to linear streams the hardware already handles.
  Consider it only for measured, sufficiently distant irregular pointer/index
  accesses, and validate every supported microarchitecture. Likewise, avoid
  habitual unrolling beyond the natural SIMD width: inspect generated code,
  register pressure, and spills first.
- Use non-temporal loads or stores only to prevent demonstrated cache pollution,
  after the ordinary kernel is correct. Their ordering and visibility require
  an explicit fence and ownership proof; they are never a cosmetic replacement
  for normal memory operations.
- Inspect the generated instructions for every explicit SIMD kernel. Count
  useful lanes, loads/stores, shuffles, dependencies, spills, and work per
  iteration. Wider code is a win only when the measured bottleneck is compute
  rather than memory bandwidth and the target hardware executes that width
  economically.

## Self-hosting — reproduce first

All contributors—humans and coding agents—should reproduce the current
self-hosting fixed point from the repository root before changing the compiler:

```sh
./build.sh test_self_host --config Release
```

`test_self_host` builds the trusted bootstrap compiler, compiles the complete
unity-build compiler executable twice with its own C compiler, requires both
generations to be byte-identical, and runs the stage-2 benchmark. The same build.c-owned workflow
is also exposed as the `test_self_host` Ninja target. On Linux x86-64, its
expanded equivalent is:

```sh
./build.sh build --config Release -t ide
build/Release/ide cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self.metrics src/buster/apps/ide/ide.c -o build/ide-self
build/ide-self cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v -fsource-metrics=build/ide-self-stage2.metrics src/buster/apps/ide/ide.c -o build/ide-self-stage2
cmp build/ide-self build/ide-self-stage2
build/ide-self-stage2 bench
```

On macOS, both `ide cc` invocations additionally receive the SDK returned by
`xcrun --sdk macosx --show-sdk-path` through `-isysroot`, plus `-framework`
arguments for AppKit, Metal, QuartzCore, and Foundation.

The target and fixed point are currently available on Linux and Windows x86-64,
and on macOS. Preserve them when changing preprocessing, C semantics, IR, code
generation, object writing, or linking, and report both self-hosting failures and benchmark
regressions.

## Build

Three layers: `./build.sh` bootstraps `build/build` from `build.c` using
**tcc**, which then drives CMake + ninja (multi-config, outputs in
`build/<Config>/`).

Keep build orchestration and policy in `build.c`, with the least practical
process-launch and scripting overhead. Shell and PowerShell scripts exist only
to bootstrap `build/build`; do not grow them into build systems. Use CMake only
to generate/cache the platform build graph and let Ninja execute that graph;
do not implement workflows, iteration, comparison, parsing, timing, or other
general scripting in the CMake language when `build.c` can do the work
directly. Prefer one persistent native build-driver process over chains of
shell, CMake, and utility subprocesses.

```sh
./build.sh                          # configure + build (Debug, clang)
./build.sh build -t test_all        # build and run the full test suite
./build.sh build --config Release -t test_all
./build.sh generate --sanitize && ./build.sh build -t test_all   # sanitized run
./build.sh test_all_combinations    # the full local matrix CI runs
```

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

The opt-in stb compatibility harness takes an external, pristine nothings/stb
checkout; upstream headers are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_stb --config Release /path/to/stb
```

The checkout must be commit
`2c980bb59875b0d32144a71867fbdebb2f77cd20` with no tracked or untracked
changes. The harness compiles `stb_image.h`, `stb_image_write.h`, `stb_ds.h`,
and `stb_truetype.h` implementations separately and in one combined stress
translation unit, then links a deterministic probe. It compares decoded PNG
pixels, PNG/BMP/TGA encoded bytes and hashes, synthetic-font glyph bitmaps, and
`arrput`/`hmput` data-structure results against Clang. The applicable upstream
image-write test and the `test_ds.c` churn workload run from pristine
per-compiler output directories; the `STBDS_UNIT_TESTS` block (including its
array-key `STBDS_ADDRESSOF` assertion) is rejected by Clang at this pinned
revision, so it is intentionally excluded without modifying upstream sources.
The upstream truetype demo is also skipped because it hard-codes host font
paths, while the probe builds a project-owned sfnt fixture in memory.
FAST and NONE run first, followed by MIR_STACK and QUALITY. Every Buster unit
records unique/lexed file and byte counts (include amplification) plus
preprocessed token and spelling-byte totals. Generated wrappers, objects,
metrics, synthetic fonts, and logs remain under `build/stb-<hash>-<pid>/`.

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

The opt-in SQLite compatibility harness takes the official SQLite 3.53.4
downloads -- the amalgamation and the source distribution -- and neither is
copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_sqlite --config Release /path/to/sqlite-amalgamation-3530400 /path/to/sqlite-src-3530400
```

The first path is the extracted `sqlite-amalgamation-3530400.zip`, the second
the extracted `sqlite-src-3530400.zip`; both are pinned by content, the
amalgamation with a per-file byte count and Buster hash and the source
distribution through its `VERSION` and its Fossil `manifest.uuid`
(`bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc`) plus the
per-file pins of the nine upstream files it reaches into. The harness does not
drive upstream's configure script or its makefiles -- upstream build-system
detection is a later driver milestone -- and it does not build `testfixture`:
the TCL test suite needs the source-generation toolchain (tclsh, lemon,
`mkkeywordhash`) that the milestone explicitly defers, so the upstream tests it
runs are the ones that build against the amalgamation.

It compiles in named stages and fails at the first one that breaks: `library`
(`sqlite3.c`, one 9,5 MB translation unit of 295.692 lines), `shell`
(`shell.c`), `cli-link` (the `sqlite3` executable, linked through `ide cc`),
and `upstream-programs` (`test/speedtest1.c`, `test/wordcount.c`,
`test/kvtest.c` and `mptest/mptest.c`, each linked against the same library
object). Everything is built with one flag set --
`-DSQLITE_ENABLE_MATH_FUNCTIONS -DSQLITE_ENABLE_COLUMN_METADATA` and the
configuration's `-DSQLITE_THREADSAFE` -- because objects that disagree would
not link.

The cross-check is what the milestone rests on. Both compilers build the same
CLI, run the same deterministic SQL script, and must produce identical output
*and* byte-identical database files: SQLite's file format is fully specified,
so `main.db`, the `.backup` of it and the `.restore` of that backup are three
independent byte-for-byte comparisons per configuration and allocator, and
`speedtest1.db` and `wordcount.db` are two more over far larger schemas. The
script covers what the milestone asks for -- schema changes, transactions,
savepoints and rollback, indexes, joins, triggers, the eponymous virtual
tables the configuration has (`generate_series`, `pragma_table_info`), ANALYZE,
VACUUM, backup and restore, and `PRAGMA integrity_check`, `quick_check` and
`foreign_key_check` -- and it avoids every source of nondeterminism, so
`randomblob()` and `datetime()` are deliberately absent. `kvtest` fills its
blobs from the platform's random source, so it is run but not compared.

The upstream test suite runs in the staged subsets the milestone asks for:
`mptest` executes its own `multiwrite01.test`, `config01.test`, `config02.test`
and `crash01.test` scripts, which fork five client processes against one
database with different journal modes and memory-mapped I/O settings. That is
the part of the suite that reaches POSIX advisory locking, WAL-less recovery
and `mremap`, and it is where the harness found the loads that the address of
an indexed pointer must not perform.

Both configurations of the threading axis run: `threadsafe` first, which is
upstream's default build and the one that carries the upstream programs, then
`single-thread`. Every configuration runs under FAST, NONE, MIR_STACK and
QUALITY. `SQLITE_METRIC` lines carry per-unit compiler wall time with the
`-fsource-metrics=` source metrics, `SQLITE_LINK` the link time and image size,
`SQLITE_WORKLOAD` and `SQLITE_CODEGEN` the workload's wall time and
instructions retired, and `SQLITE_DATABASE` each database's size, hash and
whether it matched Clang. For scale, a full matrix takes about
four minutes, and one recorded run compiled the 295.692-line `sqlite3.c` in
351 ms under FAST and 368 ms under QUALITY, linked the CLI in 46 ms, and put
the SQL workload at 615,6 M instructions under FAST, 615,8 M under QUALITY,
1.088,9 M under MIR_STACK and 1.148,5 M under NONE -- the allocator ordering
the names promise. Generated objects, metrics, databases and logs remain under
`build/sqlite-3.53.4-<pid>/`, which is about 260 MB for a full matrix and is
not cleaned up on the way out, so delete the directories of runs you are done
with.

Two upstream constructs stay outside the harness for reasons worth keeping.
A `SQLITE_DEBUG` build is not part of the matrix: `assert()` expands to a GNU
statement expression, and `if( p->apCsr ) for(i=0; i<p->nCursor; i++) assert(
p->apCsr[i]==0 );` was measured as ending at the assert's closing brace rather
than at its semicolon until `c_ir_control_statement_ends_with_body` learned to
find the body past the header. That shape now compiles, but the debug build
still fails an internal assertion of its own, so it is a documented gap rather
than a passing configuration. And the harness never builds `testfixture`, as
above.

The opt-in sbase compatibility harness takes an external, pristine sbase
checkout; upstream sources are never copied into or patched in this
repository:

```sh
./build.sh build --config Release -t ide
./build/build test_sbase --config Release /path/to/sbase
```

The checkout must be commit
`c546c3a5724c81cee9a11d816a38ccdf17472129` with no tracked or untracked
changes. sbase is the breadth target of the compatibility set rather than a
depth one: about a hundred small POSIX programs over one shared static
library, so its failures are declaration and system-header failures spread
thin across many translation units instead of one deep code path. The harness
therefore reports a status for every utility — `SBASE_UTILITY` names its
compile, link and runtime result separately — instead of one aggregate
verdict.

It owns an explicit manifest of upstream's own Makefile lists: 19 libutf
units, 38 libutil units, the 5 units of `make`, and one translation unit per
utility, checked for existence before anything is built so a source upstream
renames is a manifest error rather than a compile error. Two sources upstream
generates during its build are produced once and shared by both compilers:
`getconf.h`, from `scripts/getconf.sh`, and `bc.c`, from `bc.y` through
`yacc`. Everything is compiled with upstream's own CPPFLAGS plus `-O2`, each
utility is linked through its own compiler driver — `ide cc` for Buster,
Clang for the reference — and the two archives are built with `ar`.

Correctness is decided by comparison with a Clang build of the same manifest,
never by absolute output. Three layers run against it: a probe per utility,
100 of them, each a deterministic shell command run from that build's own
directory over its own copy of the corpus; 24 cross-cutting cases covering
pipelines, redirected standard input, binary data with every byte value
including NUL, error exits, 5.000-argument command lines and locale-pinned
sorting; and the unmodified upstream test suite, all 54 scripts, copied
beside the binaries so they find `../echo` and `../bc.library` where they
expect them. Test verdicts are compared with the Clang build's rather than
required to be passes: `0051-grep.sh` depends on the host locale, and a
harness that demanded a pass there would be reporting the host rather than
the compiler. Every child runs under a deadline, because a miscompile does
not always crash — the first symptom of the `dc` defect this harness found
was an upstream test that never finished.

FAST and NONE run the complete set; MIR_STACK and QUALITY are sampled over 13
of the larger utilities once the complete rows pass, and their run directories
borrow the reference build's other programs so the utility under test is the
only Buster program in its own probe. After every allocator row passes, the
driver gate runs upstream's own makefile with `CC` set to `ide cc` against a
clean `git archive` export of the pinned commit, which covers make's implicit
rules, its generated header, its yacc source and its recursive sub-make.

All 100 utilities match the Clang build. `env` and `find` were the last two
that did not, and both failed for one linker reason rather than a codegen one:
they read `extern char **environ`, which reached the non-PIE executable through
a copy relocation whose slot the loader filled before glibc's startup code
stored the environment pointer. That is fixed by the alias sets described under
the driver below, and the two are ordinary passing rows now. Generated objects,
archives, metrics, corpora, the per-allocator run directories and the makefile
export remain under `build/sbase-<pid>/`, which is about 37 MB and is not
cleaned up on the way out.

The opt-in DoomGeneric compatibility harness takes an external, pristine
DoomGeneric checkout and a WAD the caller supplies separately; upstream sources
are never copied into or patched in this repository, and no Doom data is stored
in it at all:

```sh
./build.sh build --config Release -t ide
./build/build test_doom --config Release /path/to/doomgeneric /path/to/DOOM1.WAD
```

The checkout must be commit
`dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284` with no tracked or untracked
changes. The WAD is game data: the harness only checks that the file is one,
records its size and hash so a transcript can be traced back to it, and hands
the same file to both compilers. The shareware `DOOM1.WAD` (4.196.020 bytes) is
what the fixture's input script was written against, and the run starts with
`-warp 1 1 -skill 3`, which is the Doom I spelling of that argument. Another
IWAD runs, but its maps differ, so its transcript is its own — and a Doom II
IWAD would need the other `-warp` form.

The harness owns an explicit 80-unit manifest — upstream's makefile is a link
line for one window-system backend, and driving it is a later driver milestone
— and compiles in named stages, failing at the first one that breaks: the game
simulation and renderer (`engine`, 63 units), then the file, timing, sound and
video layer beneath the platform (`platform-io`, 17 units), then the platform
itself (`headless-platform`). `doomgeneric_xlib.c` is the one upstream unit
left out, because `tests/basic_doom_headless.c` replaces it. The 81 objects are
linked into an executable by `ide cc`, so the driver is covered across a
program of that size rather than only the compiler.

`tests/basic_doom_headless.c` is this repository's headless platform layer, and
it is what makes a whole game a compiler test. DoomGeneric leaves six functions
to the platform, and every upstream backend fills them with a window system and
a wall clock — the two things a deterministic test cannot use. This one
replaces both with counters: drawing a frame advances Doom's clock to the start
of the next game tic, a sleep advances it by the milliseconds asked for (Doom's
inner loops wait for the clock to move and would otherwise spin), input is a
table of scripted key events rather than a device, and the frame buffer is
cleared before Doom writes into it, because upstream mallocs it and only writes
the letterboxed region. The run prints one `DOOM_TICK` line per frame carrying
a hash of the frame buffer and a hash of the simulation state — player, level
counters and every thinker's position, angle, momentum, state index and health
— so a mismatch names the first divergent tic and says whether the pixels
moved, the simulation moved, or both.

The scripted run covers what the acceptance criteria ask for in one pass over
480 frames: WAD file I/O at startup, the renderer and the play simulation with
monsters awake, scripted movement, turning, firing and the use key, the menu,
the automap, a save and a load, the intermission, and the load of the next
level. The save/load leg is exact rather than indicative. `G_SaveGame` does not
save — it raises `sendsave`, which travels through the next ticcmd and only
then becomes the gameaction that the following tic carries out — so the world
that reaches the file is hashed while that gameaction is still pending. The
load is then observed with the simulation frozen (`P_Ticker` returns early
while the menu is up, which is what the platform raises around it), because the
tic that carries out a load also runs the world forward and would otherwise
hide what `p_saveg.c` restored behind one tic of simulation. The two hashes
must be equal, and the `DOOM_SAVELOAD` line reports them either way. The
savegame's *bytes* are deliberately not compared: a vanilla savegame stores
mobj pointers as the addresses they had, which differ between two runs of one
binary, so only its size is checked against the reference.

Every stage runs under FAST, NONE, MIR_STACK and QUALITY, and each build's
transcript must equal the Clang reference's exactly. Compiler cost and
generated-code quality are reported separately and must not be conflated:
`DOOM_METRIC` carries per-unit compiler wall time with the `-fsource-metrics=`
source metrics, `DOOM_FALLBACK` carries the number of functions that fell back
to the canonical emitter (a fallback is correct code, so the number is a
quality signal, not a failure), and `DOOM_RUN` carries the run's wall time and
instructions retired. The instruction counts come from the same Linux hardware
counter as `STEP_INSTRUCTIONS`, read either side of a child that runs alone,
so they follow this process tree only and are the number to trend; where no
counter is available the field is simply zero, which is never an error. One
recorded run put the Clang reference at 4,43 G instructions for the 480-frame
workload against 24,9 G for QUALITY, 28,3 G for FAST, 48,6 G for MIR_STACK and
54,7 G for NONE — the allocator ordering the names promise, with NONE 2,2x
QUALITY. The whole matrix takes about half a minute and leaves about 19 MB
under `build/doomgeneric-<pid>/`, which is not cleaned up on the way out.

A graphical backend is explicitly not part of this gate. The bugs this harness
found are reduced to `tests/basic_c_doom_shapes.c`, which the ordinary test
suite runs; the harness itself stays opt-in because it consumes an external
checkout and data that cannot be committed here.

The opt-in QuickJS compatibility harness takes an external, pristine QuickJS
2026-06-04 checkout and, optionally, a Test262 checkout; upstream sources are
never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_quickjs --config Release /path/to/quickjs /path/to/test262
```

QuickJS publishes releases as dated tarballs rather than tags, so the pin is
the commit whose tree is the release —
`3d5e064e9dd67c70f7962836505a7fa067bf0a4e`, the one carrying `VERSION`
`2026-06-04` — with no tracked or untracked changes. The harness checks the
commit, the working tree and the `VERSION` file before it compiles anything.
The Test262 path is optional: without it the conformance stage reports itself
skipped, since it is a second external dependency an order of magnitude larger
than the engine. Its pin is upstream's own `TEST262_COMMIT`,
`5c8206929d81b2d3d727ca6aac56c18358c8d790`.

The harness owns an explicit 9-unit manifest and compiles it in named stages,
failing at the first one that breaks: the library (`quickjs.c`, `dtoa.c`,
`libregexp.c`, `libunicode.c`, `cutils.c`, `quickjs-libc.c`), then the bytecode
compiler `qjsc.c`, then the one generated input the build needs, then the CLI
`qjs.c`, then the conformance runner `run-test262.c`. Everything is built with
one flag set (`-D_GNU_SOURCE -DCONFIG_VERSION -DCONFIG_CC -DCONFIG_PREFIX -O2
-funsigned-char -fwrapv`) because upstream's Makefile does, and objects that
disagreed about `CONFIG_VERSION` or `_GNU_SOURCE` would not link. The
documented minimal feature configuration is upstream's own `make qjs` default:
no LTO, no sanitizers, no shared libraries, no cosmopolitan build and no
`CONFIG_CHECK_JSVALUE`, which changes `JSValue`'s representation. Four units
stay outside the manifest and are excluded rather than patched:
`unicode_gen.c` regenerates `libunicode-table.h` from a Unicode data drop the
checkout does not carry, the `fuzz/` entry points need a fuzzing engine, and
`tests/bjson.c` and `examples/*.c` build shared libraries loaded at run time,
which is a later driver milestone.

The generated input is `repl.c`: `qjsc` compiles `repl.js` to the bytecode
array `qjs.c` links against. Both engines produce it, and the bytes must be
identical — a compiled-in JavaScript program is a serialized result, and the
strongest equality this harness can ask for. The same check runs over the
project's own workload. Two allocators cannot make it: `qjsc` is the one
QuickJS executable with no `--stack-size` switch, so it parses inside the
engine's fixed 1 MB default, and the NONE and MIR_STACK builds exhaust that
while parsing `repl.js`. Those two continue on the reference bytecode and the
log says so (`bytecode=reference-engine-stack-exhausted`); FAST and QUALITY
produce byte-identical bytecode.

Then the harness runs the nine upstream test scripts `make test` runs, in
upstream's order and with its flags, and compares exit status, standard output
and standard error against the Clang build run for run. It also runs a
project-owned deterministic workload, `tests/basic_quickjs_workload.js`, whose
transcript must match byte for byte: it covers floating-point edge cases,
64-bit and bigint arithmetic, tagged values and property shapes, closures,
generators and exceptions, garbage collection, regular expressions and unicode
tables, JSON, typed arrays and `DataView`, collections, sorting, `eval` and
`Function`, and fixed-epoch dates. Nothing in it reads the clock, the
environment or an address. The engine's own memory report (`qjs -d`) is
compared too: every line of it is a struct size or an allocation count, so an
identical report is a statement about the layouts the two compilers chose as
much as about the run.

The engine stack is the harness's one environmental concession, and the reason
is Buster's frame layout rather than anything upstream. Buster gives every
sibling block in a function its own frame slot where Clang overlaps the ones
whose live ranges cannot intersect, and `JS_CallInternal` — the whole
interpreter, one function, one block per opcode — is the worst case in the
corpus: its prologue probes 41 pages plus 928 bytes, about 165 KB, against the
Clang build's 856. Every nested JavaScript call costs that much C stack, so
QuickJS's own 1 MB default admits five of them where the Clang build manages
1.073 — the two numbers `qjs` reports when a script recurses until the engine
stops it. The harness therefore raises `RLIMIT_STACK` to 512 MB once, up
front, where a soft limit is inherited at spawn, and passes `--stack-size 64M`
to both engines so the comparison stays symmetric. The programs are correct —
every test passes once the engine has room — so this is a code-quality gap and
not a miscompile, and shrinking the frame is optimization work that is not to
be added unasked.

The conformance stage runs a bounded, deterministic Test262 subset: thirteen
directories covering arithmetic, `switch`, tagged templates, `Number`, `Math`,
`BigInt`, sorting, `Set`, `Reflect`, `RegExp.prototype.exec`,
`DataView.prototype`, `WeakRef` and `FinalizationRegistry` — 2.156 tests, run
single-threaded through upstream's own runner with upstream's own feature
policy and known-error list. The harness writes its own copy of
`test262.conf` with three paths bound and two settings dropped (`testdir=`,
which would enumerate the whole tree, and `reportfile=`); upstream's file is
read, never written. The Buster runner's summary line must equal the Clang
runner's exactly. Only FAST and QUALITY run it: `run-test262` fixes the engine
stack at the 1 MB default with no switch, and the NONE and MIR_STACK engines do
not fit their parser inside it, which surfaces as a "SyntaxError: stack
overflow" while a test is being compiled. Those two skip the stage and the log
says which directories they did not run and why.

Compiler cost and generated-code quality are reported separately and must not
be conflated. `QUICKJS_METRIC` carries per-unit compiler wall time beside
Clang's for the same unit, the `-fsource-metrics=` source metrics, and the
count of functions the machine backend handed to the canonical emitter;
`QUICKJS_STAGE` sums them per stage; `QUICKJS_WORKLOAD` and `QUICKJS_CODEGEN`
report the workload's wall time and instructions retired; `QUICKJS_MEMORY`
reports the engine's own allocation report. One recorded run compiled the
6-unit library in 0,90 s under FAST against Clang's 10,1 s for the same
sources — `quickjs.c` alone is 2,9 MB of source, 86.791 lines, 469.236 tokens,
and Buster compiles it in 0,60 s where Clang takes 8,6 — while the workload ran
573,8 M instructions under FAST and 990,1 M under NONE against Clang's 69,9 M.
Both directions are worth trending: the compile-time column is where Buster is
ahead, the instruction column is the generated-code gap. A full matrix takes
about two minutes plus the Clang reference build. Generated objects, metrics,
bytecode and logs remain under `build/quickjs-2026-06-04-<pid>/`, which is not
cleaned up on the way out.

The GNU and POSIX surface QuickJS needs, inventoried before any of it was
implemented: `__attribute__` (both spellings, including the aggregate form
between the keyword and the tag), `_Atomic` as a type specifier and the
`__c11_atomic_*` builtins, `__builtin_frame_address`, `__builtin_alloca`,
`__builtin_signbit`, the constant math builtins hidden behind `<math.h>`'s
`NAN` and `INFINITY`, computed goto, packed structs, bit-fields of enumerated
type, `_GNU_SOURCE` libc and POSIX interfaces from `<dlfcn.h>` to
`<sys/wait.h>`, and pthreads for its worker threads. `__attribute__((packed))`
and `__attribute__((aligned(N)))` now reach both layout engines; QuickJS's
packed structs each hold a single scalar, where the packed and natural layouts
agree, so the harness never depended on it.

The opt-in musl compatibility harness takes an external, pristine musl v1.2.6
checkout; upstream sources are never copied into or patched in this repository:

```sh
./build.sh build --config Release -t ide
./build/build test_musl --config Release /path/to/musl-v1.2.6
./build/build test_musl --config Release /path/to/musl-v1.2.6 /path/to/libc-test
```

The checkout must be tag `v1.2.6` at commit
`9fa28ece75d8a2191de7c5bb53bed224c5947417` with no tracked or untracked
changes. The optional second path is a pristine checkout of libc-test, musl's
own test suite, at commit `68edb8bd73dab8147ee54c8bec638f4d2b3cff37`; without
it the run stops after the shared musl and its dynamic probe. This is the
stretch compatibility target: musl is a whole libc, so the harness is written
to make partial coverage legible and gated rather than to claim a pass it has
not earned.

The harness first runs musl's own three header recipes with `sed`
(`bits/alltypes.h` through `tools/mkalltypes.sed`, `bits/syscall.h`, and
`src/internal/version.h`), then enumerates the manifest from the checkout the
way musl's makefile globs it — musl's `BASE_SRCS`, every `.c` under each
immediate subdirectory of `src/`, plus `src/malloc/mallocng`, `crt/` and
`ldso/`, and musl's `ARCH_SRCS`, every `.c`, `.s` and `.S` under those
directories' `x86_64` subdirectory — rather than carrying a written-down source
list, so a release that adds or removes a translation unit shows up as a
manifest change instead of a silent omission. On x86-64 that is 1356 portable
`.c` files and 50 architecture files, and because every architecture file
replaces a portable one of the same name the manifest is 1356 units: 1306
portable C, 18 architecture C and 32 assembly. The replacement rule and what
Buster does with it are described with the archive below.

One flag set drives both compilers, and it is musl's own `CFLAGS_ALL` minus the
flags musl's `configure` only offers a compiler that accepts them: `-std=c99
-nostdinc -fno-builtin -fno-strict-aliasing -fno-stack-protector
-D_XOPEN_SOURCE=700 -O2 -g0` plus musl's seven include paths.
`-ffreestanding` is absent because musl's configure falls back to
`-fno-builtin`, which is in the set; `-fexcess-precision=standard`,
`-frounding-math`, `-ffunction-sections`, `-fdata-sections`,
`-fomit-frame-pointer`, `-fno-unwind-tables`,
`-fno-asynchronous-unwind-tables` and `-Wa,--noexecstack` are `tryflag`-only,
so a compiler that rejects them gets a build without them, which is the shape
here. `-fno-stack-protector` is on both sides because Buster emits no canary
and the reference build must not either: its canary load reads the thread
pointer, which a program with no thread-local storage does not have.

The inline-assembly vocabulary the harness needs is the one a libc's atomics
and thread pointer are written in, and it is bounded by two rules rather than
by a list of accepted templates. A memory constraint (`m`, `=m`) carries the
operand's storage rather than its value: the register the emitter assigns
holds the address, the template reference expands to a memory reference
through it, and no value is loaded or stored around the assembly. A literal
register in a template is still refused, because one the emitter can also hand
to an operand could be overwritten under the template's feet; the two
exceptions are registers it can never hand out, each allowed only where it
cannot alias anything -- the stack pointer as a memory base, which is the
`lock orl $0,(%rsp)` fence idiom, and `%fs`/`%gs` before a colon, which is how
a thread-pointer read is spelled. GNU's semicolon separates statements while
this assembler reads one as a comment, so the inline-assembly path rewrites
the separators, folding `lock ; insn` into the single statement the assembler
wants and splitting the rest onto their own lines. The operand classes are the
fixed general registers, `r`, `m`, `x`, `t` and `u`, and `X` for an operand a
template does not care about the placement of, which is how musl's `remquol`
keeps the addresses of its arguments from being discarded. Every unit in the
manifest below compiles.

A literal register a template names has a third exception beside those two, and
it is the one that makes the reason above stop applying: a register the asm has
*already* committed to -- pinned by a fixed-class operand, or in its clobber
list -- is one the emitter cannot hand to anything else, so a template naming it
cannot overwrite anything. musl's `fmodl` is the shape: `fnstsw %%ax` beside an
`"=a"` output, where the literal register is that operand's own.

`x` is GNU's SSE register class, and it is a second register file rather than
another name for a general register. An operand in it is allocated a vector
register out of its own pool -- so a general and a vector operand in one
template can never collide -- carried in and out of its frame slot by a scalar
`MOVSS`/`MOVSD`, and spelled in the template by the register name alone,
because the SSE file has one name per register rather than a name per access
width and the instruction the template wrote is what says how much of it is
read. It carries a `float` or a `double` and nothing else: an x87 `long double`
is the other file, and an integer would need a move between files that no
operand here performs; both halves -- the value's type and the target having
the file at all -- are refused by the frontend with a source diagnostic rather
than left to the emitter. The vector register names joined the general ones in
the literal-register refusal for the same reason the general ones are there:
an operand is allocated one, and a template that also wrote one by hand could
overwrite it. `tests/basic_c_asm_sse_output.c` and
`tests/basic_c_asm_sse_input.c` are musl's own `sqrt`, `fabs` and `lrint`
reduced to their operands and run under every allocator, with their answers
checked -- an operand carried into the wrong register still assembles and still
hands back a number.

`t` and `u` are the top of the x87 register stack and the one below it, and
`st` is the clobber a template that pops declares. That file is where an
x86-64 `long double` already lives, so these operands need no conversion around
them at all -- and it is a stack rather than a set of registers, which is the
whole of the emitter's model: the operands are pushed deepest first, so `u`
lands in ST(1) and `t` on top; the template runs; an output in ST(0) is stored
and popped; and whatever is still standing is discarded. An `st` clobber says
the template popped what it was handed, so the depth is one shallower than the
pushes left it and nothing is read back -- musl's `llrintl` is `fistpll`, which
pops. The unwind runs *after* the general-file stores rather than straight
after the template, because the six padding bytes a stored eighty-bit value is
followed by are zeroed through RAX and `fmodl` reads its status word back out
of AX. What the frontend refuses is everything that model does not hold for: a
position named twice, a `u` without a `t`, an `st` clobber that is not beside
exactly one `t` input, an operand that is not the 80-bit spelling, an operand
reached through a pointer rather than sitting in a frame slot, and the class on
a target with no x87 file. A literal `%st` in a template stays refused whatever
the operands are, because a template that moved the stack itself would leave
that accounting wrong. `tests/basic_c_asm_x87_output.c` and
`tests/basic_c_asm_x87_clobber.c` are the fixtures, and `remquol` is the reason
their answers are checked rather than assembled: its quotient bits are decoded
out of the x87 status word, so a `fprem1` against the wrong stack position
returns a plausible remainder beside a wrong quotient.

Two more things a template may do, and they are what a libc's startup and its
dynamic loader are written in rather than its atomics. It may **name a
symbol**: a line beginning with a dot goes through the same directive table
module-level assembly uses, and every other line reaches `assembly_encode`
with the relocations it reports recorded against the module, so musl's
`GETFUNCSYM` -- `.hidden sym` followed by `lea sym(%rip),%0` -- reaches the
object as a PC-relative relocation against a hidden symbol the block may be
the only thing that names. The name a new symbol record keeps is the one in
the instruction's IR literal rather than the one in the substituted copy: the
retry that grows the code buffer rewinds the attempt arena, and a record that
outlives the attempt cannot point into it (`codegen_assembly_durable_name`).
And it may **write the stack pointer**, on the one condition that its last
statement is an unconditional `jmp`, which is musl's
`CRTJMP` -- `mov %1,%%rsp ; jmp *%0`. The rule the exception hangs on is not
where the name appears but that nothing the emitter puts after the block is
reached: RSP is never handed to an operand, so writing it cannot land under
one, and the frame it does move is never read again. The AT&T dereference star
comes with it and only directly in front of an operand reference, so `*` can
only ever dereference a value the C side computed.
`tests/basic_c_inline_asm_symbol.c` carries all of it, run under every
allocator: the three PC-relative references are checked against the addresses
they should have produced, and the hand-off ends the process itself because
nothing after it runs.

One flag the set does not carry is a dialect, and it used to make the two
compilers read different source: `ide cc` predefined `__GNUC__` only in a GNU
dialect while Clang predefines it in every one, so under `-std=c99` musl's
`<stddef.h>` gave Clang `__builtin_offsetof` and gave Buster the portable
`((size_t)((char *)&(((type *)0)->member) - (char *)0))`, and every `offsetof`
in the archive was a different expression on the two sides. That is fixed —
the macro describes which extensions the compiler implements, not which ones
the dialect permits, and both reference compilers report it beside
`__STRICT_ANSI__`; `__STRICT_ANSI__` is the switch that still flips. Both
sides now take the same branch of every one of these headers. The habit the
divergence taught is still worth keeping: a class that looks like a Buster gap
can be a branch the reference never took, so check what the unit actually
preprocessed to (`ide cc -E` with the same flags) before naming a construct.
`tests/basic_c_null_pointer_offsetof.c` still pins that the two `offsetof`
spellings agree for the single-member designator it covers; they did not agree
for a nested one, which is what `pthread_exit` walking the robust list stopped
reaching when the predefine changed and is fixed one commit later, and
`tests/basic_c_type_generic_math.c` pins the predefines themselves.

`<math.h>` splits on the same macro, and there the two branches are not
interchangeable, which is why the fold below outlived the divergence. `NAN` is
`__builtin_nanf("")` for a compiler that advertises the builtins and
`(0.0f/0.0f)` for one that does not; both sides take the first branch now, but
the second is still what any freestanding or older header hands the frontend,
and it is an operation that creates a NaN rather than a constant that already
is one — and IEEE-754 leaves the sign of an invalid operation's NaN
unspecified, which the two available answers use:
x86 hardware produces the negative default NaN, Clang folds to the positive
quiet one. The frontend therefore folds the four operations that create a NaN
out of operands that are not NaN — `0/0` and `inf/inf`, `0*inf`, `inf-inf`
and `inf+(-inf)` — to the positive quiet NaN at every width, in the
in-function path and in the x87 static-initializer folder alike, and reads
constant operands through a negate and through a conversion between float
widths so a source-level `-0.0f` and a widened literal fold the same way.
Ordinary constant arithmetic is still left to the backend: only the invalid
operations fold, because only their answer is a compatibility choice rather
than a value. `tests/basic_c_created_nan_sign.c` carries the created and the
propagated signs, and the created NaN's bytes are in
`tests/basic_c_long_double_static_initializer.c` beside the rest of the x87
initializer bytes Clang writes. `INFINITY` splits the same way, and its
non-GNU spelling is the overflowing literal `1e5000f`, which the x87 folder
refused until it learned that an overflow is a value — the infinity C names —
rather than a failure. Twenty-one `src/math` units and `functional/strtold`
stopped on that literal, and `fpclassify`'s `1/0.0` on the division by zero
beside it, which creates an infinity rather than a NaN. The same 21 units and
the same `strtold` stopped a second time on the *other* spelling when
`__GNUC__` started being predefined everywhere and the header began choosing
`__builtin_inff()` and `__builtin_nanf("")`: the folder is a recursive descent
over arithmetic and knew no calls at all, so the two constant-valued
intrinsics fold there now the way they already folded in
`c_ir_constant_evaluate`, with only the empty NaN payload accepted.
`tests/basic_c_long_double_static_special.c` carries both spellings against one
set of expected bytes, and the `long double` bullet under "Platform and backend
boundaries" is where the folder's whole boundary is written down.

One flag the reference carries and Buster does not is
`-fcomplex-arithmetic=improved`. Clang's default for C lowers a complex
multiply or divide to the compiler-runtime helpers -- `__mulxc3`, `__divxc3`
and their siblings -- which live in libgcc or compiler-rt, and this harness
links neither, so a reference object that calls one cannot be linked at all;
musl's `cpowl` is such an object. `improved` is the inline Smith form the
Buster frontend emits, bit-identical to it over the operand matrix described
under `_Complex` below, so the flag is the reference-side counterpart of the
probe's `-mstackrealign`: it makes the comparison possible rather than
changing what is compared. It is the one option in the set that dates the
reference compiler -- Clang grew `-fcomplex-arithmetic=` in 20 -- and an
older one rejects it as an unknown option rather than miscompiling, which the
reference build reports before any Buster invocation runs.

The Clang reference build runs first and every unit must compile — a musl unit
Clang cannot build under this flag set is a broken workspace, not a Buster
defect, and finding that out before a thousand Buster invocations keeps the two
apart. Buster then compiles the same manifest under FAST, and every unit that
fails is printed as a `MUSL_UNSUPPORTED` line carrying the first line of its
diagnostic. That list is the inventory: it names every component the archive
below is missing and why.

The gate is the compiled-unit count together with a hash of the newline-joined
sorted failing paths. The count alone would accept a change that fixed one unit
and broke another, so both are pinned as `MUSL_EXPECTED_COMPILED_UNITS` and
`MUSL_EXPECTED_FAILURE_HASH` in `build.c` and both are printed on the
`MUSL_INVENTORY` line, which is what a deliberate rebaseline needs. A fix and a
regression therefore both move a number that has to be updated on purpose.

Both object sets are archived with `ar` through a response file — a musl
archive is a thousand members, which is past what a Windows command line takes
— under musl's own `AOBJS` rule, which puts the `src/` units in `libc.a` and
nothing else. The `crt/` units are excluded for the reason in the
startup-object note below, and `ldso/` because a static libc that carries the
dynamic loader hands the linker a `dlopen` that wants `setjmp`, which is
architecture assembly this build does not have; both are still built, and
`MUSL_ARCHIVE` counts them as `startup_emitted`/`startup_absent` and
`loader_excluded`. That is what makes the archive linkable by an ordinary
program rather than only by a program that defines its own entry.
`MUSL_ARCHIVE` also carries the wall time both archives took. The freestanding
probe is then linked against each, and every link in the harness reports its
own `link_us`. The probe,
`tests/basic_musl_freestanding.c`, is a project-owned program with no include
of any kind: it is compiled `-nostdinc` against musl's own headers, entered at
`_start`, and linked with `ld -static` and nothing else, so no compiler driver,
no startup file and no host libc are on the link line and everything it
resolves comes out of the musl archive. It exercises the string, memory,
search and character routines, the seventeen x87 `long double` units named
below -- sixteen of them called directly and `__rem_pio2l` through `sinl` --
and the twenty-two under `src/complex` that take or return a
`long double _Complex`, each wide result recorded as the sign, exponent and
significand fields of musl's own `union ldshape` so that a result one ulp off
cannot pass, and a complex one recorded as both of its halves that way. It
writes a transcript through raw `write` and `exit` system calls; the Clang-built and Buster-built transcripts must be
identical byte for byte, so a routine that computes a different answer fails
the run where a link-and-exit check would not. The probe runs under FAST, NONE,
MIR_STACK and QUALITY against the one Buster-built archive, because the four
allocators have to produce the same answers rather than each produce some
answer. The reference is compiled with `-mstackrealign`: at process entry the
stack pointer carries the alignment the kernel leaves rather than the one a
`call` leaves, and Clang's aligned vector spills need the realignment while
Buster's emitters, which spill through plain moves, do not.

Two more links follow, and they are the ABI report. Two separately compiled
object sets calling each other across musl's own declarations is a direct test
of the calling convention, the struct layouts and the return shapes the two
compilers agree on, and a disagreement surfaces as a wrong answer rather than
as a link error. The first link is the Clang-compiled probe against the
Buster-built archive: a Clang caller into Buster-built musl. The second is that
same probe against a mixed archive, built by taking Buster's object for every
second unit and Clang's for the rest, so musl's own internal calls — `strstr`
into `memchr`, the character tables, the search routines — cross the boundary
in both directions inside one program. Both must reproduce the reference
transcript, and each prints a `MUSL_ABI` line.

The Buster-compiled probe is deliberately not linked against the Clang
archive. That pair does crash, and the reason is not an ABI disagreement: the
probe is entered at `_start` with the alignment the kernel leaves rather than
the one a `call` leaves, which is why the reference is compiled with
`-mstackrealign`, and the Clang probe built without that flag crashes against
Clang's own archive in exactly the same way. The Buster driver has no such
flag, so that direction would measure the probe's entry rather than the two
compilers, and the mixed archive covers what it was meant to cover.

Everything above is linked `-static`, and a shared object is the other half of
what a libc is. `libc.so` is built from the same object set on both sides, with
the three differences musl's own `libc.so` recipe states. `ldso/dlstart` and
`ldso/dynlink` join it, because musl's `LDSO_OBJS` rule puts the loader in the
shared library and its `AOBJS` rule keeps it out of the archive.
Nothing is substituted into it: `src/thread/__set_thread_area` used to leave
it for a project-owned object, and six more names used to need one, and both
sets are musl's own assembly now.

The link is `ld -shared -Bsymbolic --no-undefined -e _dlstart`, and the first
three of those are musl's own.

- `-e _dlstart`, because musl's `libc.so` *is* its dynamic loader and that is
  the entry point the kernel jumps to when the file is a program's `PT_INTERP`.
  It is what makes a shared musl testable here at all: nothing else on the
  machine can load one.
- `--no-undefined`, because a shared object may carry unresolved names and
  musl's loader will not. It reports each one it cannot relocate and leaves at
  exit 127, before the program's first instruction; upstream's `configure` asks
  for the flag for exactly that reason, and here it turns a name the object set
  is missing from a silent runtime death into a link error naming the symbol.
- `-Bsymbolic`, because this library is linked from the one object set the
  rest of the harness already measured and that set is compiled without
  `-fPIC`. The flag is a code model now rather than an accepted no-op --
  described under machine selection below -- so what forces `-Bsymbolic` here
  is the single object set and not a missing flag: a second,
  position-independent set would retire it, at the cost of another two
  thousand compiles. What the non-PIC set does emit is PC-relative, so the only
  references `ld` refuses to place in a shared object are the ones to symbols
  another object could interpose. Binding those at link time is what musl's own
  build does for everything except its public data, through `--dynamic-list`;
  taking the whole set costs the copy relocations that list exists to preserve
  and buys a shared musl out of the object set that is already built. The probe
  below references no libc data object, so nothing it measures turns on the
  difference.

One object set rather than two is the other departure. musl compiles a second,
`-fPIC` copy of every unit for `libc.so`; this harness links the one set both
compilers already produced, which keeps the shared stage at two links instead
of another two thousand compiles, and it is what makes the shared link a
statement about the code generation the rest of the harness already measured.

`--no-undefined` is what used to need a project-owned file beside the object
set. A static link pulls only the archive members a program reaches, so the
seven assembly-only units cost nothing until something calls one; every object
handed to `ld -shared` is in the result and every relocation in it is resolved
when the library loads, and six names were left over -- `__syscall_cp_asm` with
the three labels `__cp_begin`, `__cp_end` and `__cp_cancel` that bound its
cancellation window, and `setjmp`/`longjmp`. Every one of them is musl's own
x86-64 assembly now, so the shared object set is the compiled one and nothing
else. `setjmp` and `longjmp` in particular used to trap -- neither is
expressible in C -- and musl's loader saves a jump buffer around `dlopen`, so
that substitution is what held every libc-test unit that opens a library out of
the comparison.

The dynamic probe is `tests/basic_musl_freestanding.c` again, resolved against
the shared musl instead of the archive and started by the loader inside it. The
interpreter is named by absolute path because there is no installed musl to
point at: upstream installs `libc.so` as `/lib/ld-musl-x86_64.so.1` and links
every program against that name. Its gate is the static probe's, and
deliberately so: producing a shared object that links says nothing, and the
transcript has to be the reference's byte for byte under all four allocators,
so a routine that computes a different answer once it has been relocated rather
than linked fails here. A `MUSL_SHARED` line reports each side's library and a
`MUSL_DYNAMIC` line each program. The Buster-built library is about three times
the size of the reference's, which is the archive's ratio and the same emitter
spilling through the frame rather than through registers.

One compiler change came out of this and it is the only one that did: a
read-only object that carries a relocation is laid out with the writable data
rather than in the read-only section, described with the rest of the C rules
below. Twenty-one musl units hold one -- `__ctype_b_loc`'s `ptable`, the
`FILE *const stdout` trio, the locale tables -- and each of them put a
write-when-relocated word on a page the loader maps read-only, which is a
`DT_TEXTREL` musl's loader does not undo for the file it was itself started
from. `-fPIC` does not retire this one: a relocation into a read-only object
still has to be applied when the image loads whatever the code model is, and
what would retire it is a relocated-read-only section of its own, which this
object writer does not have.

The manifest is musl's own x86-64 configuration, replacement rule included and
whole. Every `.c` in a source directory is collected, and so is every `.c`,
`.s` and `.S` in its architecture subdirectory: musl's `ARCH_SRCS` is
`src/*/$(ARCH)/*.[csS]`, not its assembly half. Where a portable file and an
architecture file name the same unit the architecture file wins and the
portable one is not built -- musl's `REPLACED_OBJS`. That is 1356 units, 32 of
them assembly and 18 of them architecture C. Each replacement is named, on a
`MUSL_ASSEMBLY` or a `MUSL_ARCHITECTURE` line, because which units the archive
holds musl's own x86-64 implementation for is inventory worth having rather
than an exclusion. One unit's `.c` is empty with no architecture file to
replace it -- `src/thread/tls`, which x86-64 does not need -- and it is the
empty translation unit musl itself compiles rather than a reported gap.

All 1356 build, so the failing set is empty and its hash is the hash of
nothing. The last class to go was the inline-assembly operand classes musl's
own math is written in, and it went in two halves: the SSE register class as an
output (`"=x"`, `"+x"`: `sqrt`, `sqrtf`, `fabs`, `fabsf`) and as an input
(`"x"`: `llrint`, `llrintf`, `lrint`, `lrintf`), which was issue 765; and the
x87 stack as an operand (`"+t"`, `"u"`: `fabsl`, `fmodl`, `remainderl`,
`remquol`, `rintl`, `sqrtl`) with `st` as a clobber (`llrintl`, `lrintl`),
which was issue 766. Those were two register files rather than four gaps, and
both are described with the vocabulary above. The other two architecture C
units, `fma` and `fmaf`, build for a different reason: neither `__FMA__` nor
`__FMA4__` is defined under this flag set, so both fall through to
`#include "../fma.c"` and are the portable code reached by an architecture
path. Clang compiles all eighteen, and so does Buster now, so the two archives
hold musl's own implementations of the same units. The four classes are pinned
as fixtures -- `tests/basic_c_asm_sse_output.c`,
`tests/basic_c_asm_sse_input.c`, `tests/basic_c_asm_x87_output.c` and
`tests/basic_c_asm_x87_clobber.c` -- each musl's own templates reduced to their
operands and run under every allocator with their answers checked, so a class
that regresses moves the musl count and one fixture together.

A refused architecture unit does not leave a hole in the archive, and the
fallback that makes that true is still here even though nothing takes it now.
`hypot` calls `sqrt`, so a `libc.a` without one is a `libc.a` nothing links
against, and every stage after the archive -- the freestanding probe, both ABI
links, the shared object, the whole libc-test suite -- would stop measuring
anything at all. The archive therefore falls back to the portable file the
architecture source displaced, and says so: each fallback is a
`MUSL_SUBSTITUTED` line naming the file refused and the file taken, beside the
`MUSL_UNSUPPORTED` line carrying the diagnostic. The unit still does not count
as compiled, so the substitution is a reported gap rather than a repaired one.
There are none today -- `substituted=0` on the `MUSL_SUMMARY` line -- which is
what makes this archive musl's own rather than a portable stand-in for it.
Both archives are 1349 members.

Before the driver took assembly input this was three groups of reported
exclusions: seven `assembly-only` units whose `.c` is empty because the
architecture supplies the implementation in assembly, thirty-two
`architecture-assembly` files the harness replaced with the portable C, and
whatever else failed. The last of the third group to go was
`vfprintf`/`vfwprintf`, both on wide floating-point `va_arg`, described with
the type below.

Static initializers are no longer a class, and neither are the three
singletons that stood beside them: 1344 to 1347. `src/misc/ioctl`'s
`compat_map` sizes its entries with `sizeof(struct { ... })`, and the parser
registered an aggregate an expression defines only inside a function body, so
the same definition in a file-scope initializer had no type for the sizeof
fold to resolve and the whole initializer was refused. `res_msend` writes
`int qpos[nqueries], apos[nqueries];`, a variable-length array in a
comma-separated declarator list, which the list path could not lower --
its diagnostic named an alignment, which is what an unresolved layout leaves
behind, and not the over-aligned automatic that reads like. `lsearch` and
`lfind` walk their tables through `char (*p)[width]`, a pointer to a variably
modified array, which is the type `char p[][width]` adjusts to and now becomes
the same local an array parameter does. Fixing the third also fixed a
wrong-code defect the compile inventory could not see: indexing either shape
with fewer subscripts than it has dimensions leaves an array, and the decay
that turns one into the address of its first element keys on the IR array
type a variably modified array does not have, so `p[i]` loaded one element
where the row's address was meant. Inline assembly is no
longer a class: `crt/rcrt1`, `ldso/dlstart`, `ldso/dynlink` and
`src/thread/__unmapself` were the four units that reached code generation and
stopped on a template, and they compile now that the inline path relocates a
symbol reference and takes musl's two hand-off shapes. Neither is an
inline-assembly *operand* class: the sixteen architecture C units under
`src/math/x86_64` stopped on the SSE register class and on the x87 stack, and
both are implemented, which is what took the count to the whole manifest.

`src/complex` is no longer a class either. Eighteen units returned a
`long double _Complex` and were refused at the signature boundary until the
System V COMPLEX_X87 result class reached the canonical emitter, described
with the type below: 1326 to 1344, and `functional/tgmath` in libc-test moved
from `blocked-compile` to the `vfprintf` link blocker with it. Neither is
wide floating-point `va_arg` a class any more, which is what `vfprintf` and
`vfwprintf` stopped on until the read described with the type below was
implemented: 1344 to 1346. Conflicting
declarations are no longer a class: C11 6.2.7p3 makes an unprototyped
`long f();` compatible with a non-variadic prototype for the same function,
which is what musl's `pthread_cancel.c` and `__libc_start_main.c` write.
Neither is wide floating-point `va_arg`, which is what `vfprintf` and
`vfwprintf` stopped on until the read described with the type below was
implemented (measured 2026-08-29).

`_Complex` is a two-field aggregate of its real type, real part first. That is
the layout every psABI specifies and, with one exception, also the argument
and result shape each of them classifies a complex value into, so the existing
struct rules produce the right registers with nothing added to the backends --
cross-probed against Clang for System V x86-64, Win64, AAPCS64 and Darwin
AArch64, and differenced against a Clang build across the boundary on the
first three. The exception is System V x86-64's `long double _Complex`
*result*, which the psABI returns in ST(0)/ST(1) under its COMPLEX_X87 class
where the equivalent `struct { long double a, b; }` is returned in memory
through a hidden pointer. It is the one place the aggregate model is not the
ABI, and it is keyed on the type being complex rather than on its shape,
because the two spellings have identical layouts and different conventions --
which is what Clang compiles, and what `ir_classify_abi_value` now keys the
class on. The classifier gives that result four eightbyte parts, one
X87/X87_UP pair per half in layout order; `ir_abi_value_is_complex_x87_result`
is the predicate over that shape and the only thing the backends ask. The
canonical x86-64 emitter pushes the imaginary half and then the real one
before its `RET`, so the real half is on top, and pops them in that order into
the result slot after a call. Every other position is the two-field aggregate
unchanged: the argument is a thirty-two-byte memory slot under the same psABI,
which is why `cabsl`, `cargl`, `creall` and `cimagl` compiled before any of
this, and the loads, stores and copies move bytes.
`tests/basic_c_complex_x87_caller.c` and its callee are the pair compiled by
one compiler and linked against the other in both directions, which is the
only thing that pins the register order to the platform's; the single
translation unit in `tests/basic_c_complex_arithmetic.c` cannot see a
disagreement about it.

Multiplication and division are lowered inline -- the naive product and
Smith's algorithm, which is what Clang emits for
`-fcomplex-arithmetic=improved` -- rather than as the `__muldc3`/`__divdc3`
calls Clang emits by default, because this toolchain neither ships nor links a
compiler runtime to resolve them. Against `improved` the two agree bit for bit
over an operand matrix covering the signed zeroes, the subnormal and overflow
edges and the infinities, `long double` included;
`c_ir_emit_complex_divide` in `c_gen.c` records where the default's library
helpers differ. Smith's algorithm compares the magnitudes of the two
denominator halves, and there is no absolute-value opcode, so
`c_ir_emit_float_magnitude` clears the sign bit through a stack slot. The
80-bit x87 spelling has no integer of its own width to pun through, so it is
punned one halfword at a time -- bit 15 of the sixteen-bit sign/exponent field
at byte eight, the `se` member of musl's own `union ldshape` -- rather than
whole, which is what lets `cpowl` compile with the other seventeen. GNU's imaginary literal suffix
comes with the type, in both orders and both letters, because it is the only
way a `<complex.h>` can define `I`: musl spells `_Complex_I` as
`(0.0f+1.0fi)` and glibc as `(1.0iF)`. Two shapes stay unsupported and
diagnosed: a complex global initializer, and a complex operand inside an
integer constant expression, which the parser's folder reduces to the
operand's real part rather than refusing -- C does not admit one there either
way.

`long double` is no longer among them. Seventeen units were static
initializers until the folder learned the two shapes musl writes -- `floorl`,
`ceill`, `roundl`, `truncl`, `rintl`, `modfl` and `__rem_pio2l` open with
`static const long double toint = 1/LDBL_EPSILON;`, and `atanl`, `expl`,
`logl`, `log2l`, `log10l`, `log1pl`, `powl`, `tgammal`, `erfl` and `exp10l`
carry `long double` coefficient tables -- and then stopped in the canonical
emitter instead, which rejected a whole function when one of its values had a
type that *contained* an x87 `long double` without *being* one, so an array of
them was refused whether or not it carried an initializer. Classifying those
aggregates by the ABI rather than by that shape test released them along with
every unit reaching a value through musl's `union ldshape`: 54 units, 1192 to
1246, and the whole x87 code-generation class with them. The four left in
that class were inline assembly -- `crt/rcrt1` and `ldso/dlstart` on
`GETFUNCSYM`, `src/thread/__unmapself` and `ldso/dynlink` on `CRTJMP`, the
last of them having joined once the portable `offsetof` in its `MIN_TLS_ALIGN`
folded and it stopped failing earlier -- and they closed together, 1322 to
1326, when the inline-assembly path learned to relocate a symbol reference and
to take a stack hand-off. Nothing in the inventory stops on an assembly
template now.

musl's startup objects are their own report now that module-level assembly
goes through the real assembler. `crt/crt1.c` and `crt/Scrt1.c` compile, and
the harness writes `crt1.o` and `Scrt1.o` beside the archive rather than into
it, the way musl's own build keeps startup objects out of `libc.a`; a
`MUSL_STARTUP` line names each object it produced, and each one it did not
with the reason. The produced `crt1.o` is a complete startup object:
`_start`'s bytes are Clang's, `_DYNAMIC` is weak and hidden, `_init` and
`_fini` are weak undefined, and `ld -static` links it into a program the
kernel enters and that exits cleanly.

`rcrt1.o` joined them once the inline-assembly arm learned the same
relocation plumbing: `crt/rcrt1.c` and `ldso/dlstart.c` stop on x86-64's
`GETFUNCSYM` in `arch/x86_64/reloc.h`, which is *inline* assembly carrying a
`.hidden` directive and a RIP-relative `lea` against a symbol with an output
operand, and that shape now reaches the object as a relocation like any other.
`crti.o` and `crtn.o` are there too, and they are the whole reason a section
keeps its own name through the assembler: each contributes one and two bytes
to `.init` and `.fini`, which the system linker concatenates in order.
`startup_absent` is zero.

`ldso/dlstart.c` and `ldso/dynlink.c` compile now too, and stay out of
`libc.a` under musl's own `AOBJS` rule rather than because of anything Buster
cannot build, for the reason the archive note above gives.

The archive is linkable, which it was not until `__attribute__((weak))` and
`__attribute__((alias))` reached the object writer. musl publishes `malloc`,
`free`, `errno` and most of its pthread surface as weak aliases of internal
names -- `weak_alias(old, new)` is
`extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))`, which
also needs the `__typeof` spelling -- and while the attribute went
unimplemented every one of those names was absent from the objects even
though every unit holding them compiled. 250 weak symbols now come out of
`libc-buster.a`, `malloc` among them. The compiled-unit inventory did not
move by a single unit when they appeared, which is exactly why the probe
exists: the gap was invisible from the compile side and showed up only when
something linked. The probe therefore calls `stpcpy`, `stpncpy`, `strchrnul`
and `memrchr`, four names musl publishes *only* through `weak_alias`, so the
link that used to fail is now part of the gate. It does not call `malloc`:
musl's allocator takes a lock through the thread pointer, which a program
entered at `_start` with no startup object never established, and the
Clang-built archive faults in the same place.

### libc-test

Given a second path, the harness then runs libc-test, musl's own test suite.
It carries no tags and no version file, so the pin is a bare commit on
upstream's master, `68edb8bd73dab8147ee54c8bec638f4d2b3cff37`, verified
pristine the same way musl's checkout is. Upstream's canonical remote is
`https://git.musl-libc.org/cgit/libc-test`; `https://repo.or.cz/libc-test.git`
is the same history and is what a clone usually resolves to. Upstream's own
makefile builds in-tree, which is exactly what the pin exists to prevent, so
the harness drives the sources from where they sit and writes every artefact
into `libc-test/` under the run directory.

One generated header stands between the suite and musl: `src/common/options.h`,
which upstream derives by preprocessing `options.h.in` against the libc under
test and turning what survives into defines. The harness runs that recipe --
the four `sed` expressions written at the top of `options.h.in` itself, which
upstream's makefile spells again in awk -- and both sides compile against the
one copy. The reference preprocessor generates it, and that is not a statement
about which compiler is trusted: `ide cc -E` emits a token stream with the line
structure removed, the whole file coming back as one line of space-separated
tokens, and this recipe like musl's own three is line-oriented. The header
describes the musl under test rather than either compiler, so one copy is also
the right shape for a comparison.

One flag set again drives both compilers, and it is upstream's own
`config.mak.def` `CFLAGS` minus what only one of them takes, plus the
`-nostdinc` and musl include set that makes a test see the musl under test
instead of the host libc: `-std=c99 -nostdinc -fno-builtin
-fno-strict-aliasing -fno-stack-protector -D_POSIX_C_SOURCE=200809L -O2 -g0`,
with `-D_XOPEN_SOURCE=700` added for `src/api` because that is what upstream's
own api rule adds. `-pedantic-errors` and `-frounding-math` are dropped
because Buster rejects both, which is the same `tryflag` reasoning musl's flag
set is built on; the warning flags and `-g` change nothing about what is
compiled; `-D_FILE_OFFSET_BITS=64` is marked glibc-specific by upstream and
musl has one `off_t`; and the `-lpthread -lm -lrt` link libraries have no
meaning where there is one archive and no compiler driver. Clang alone gets
`-Werror=implicit-function-declaration`, which is upstream's and is not a
divergence: Buster rejects an undeclared callee outright, so the flag is what
makes the two compilers agree about a missing declaration -- which is most of
what the api subset is testing.

Two objects sit on every static link line that neither archive supplies, and
each side uses its own. `crt1.o` is musl's startup object, which is not an archive
member on either side — a program links it explicitly and `libc.a` separately —
so a libc-test program is one compiler's code from `_start` down. If Buster
ever stops producing it the reference's copy stands in on both sides, and the
`LIBCTEST_MANIFEST` line says which of the two arrangements is in force rather
than leaving it to be inferred. That is the only such object now. A
project-owned `__set_thread_area` used to sit beside it, because the harness
substituted the portable C sibling for every architecture-assembly unit and
that sibling is written for architectures with a `SYS_set_thread_area` system
call -- x86-64 has none, so it returned `-ENOSYS`, `__init_tp` failed, and
`__init_tls` crashed the process before `main` in *both* archives. Both
archives now hold musl's own `arch_prctl(ARCH_SET_FS)`, and the replacement
is gone.

Nine units are built differently, and upstream's sibling `.mk` is what says so.
It is a make fragment of one to four lines, and the harness reads it for the
three facts it states rather than interpreting it: `$(N).LIBS:=$(B)/$(N).so`
means the unit is a shared object instead of a program, `-rdynamic` means its
program has to export its own symbols because the library it opens resolves
against them, and any other `name.so` in the file is a sibling its program
needs beside it. Each of the four shapes upstream writes states its fact in a
token, so the file is scanned for tokens; anything a later release adds that
this does not understand shows up as a unit that fails to build rather than as
one quietly held out, which is the trade this stage wants.

What comes out of that is three link shapes rather than one. A shared-object
unit is compiled `-fPIC -DSHARED`, which is upstream's own `.lo` rule, and
linked `-shared` against the shared musl; upstream never runs one, so both
sides building it is the pass. That flag is the same code model on both sides
now, and this stage is where it is measured against a real linker: what a
shared object demands of the code generator is a thread-local model the loader
can place and a GOT-indirect reference for every other symbol another object
could interpose, and `ld` says so by name for each one that is missing. A
program with a `.mk` is linked against the shared
musl rather than the archive, with that musl as its interpreter and its own
directory as its run path, which is upstream's `-rpath='$ORIGIN'` spelled as a
directory this harness knows. Every other program is linked `-static` exactly
as before. The thread-pointer replacement is on the static link lines only: the
shared musl already carries it, and a second definition would be a duplicate
symbol rather than a substitution. Each side's dynamic programs are laid out
under `src/<subset>/<stem>` in that side's own tree and run from its root,
because three of the nine find the library they open by a path rather than by a
name -- two spell it from the working directory and one derives it from
`argv[0]` -- and that layout is upstream's own.

Each unit is classified rather than merely passed or failed, because most of
the suite cannot be reached yet and a single total would hide which wall it is
behind. The classification is derived from the checkout and from what the two
sides do, not written down:

- `excluded-reference` — the Clang-built musl of this same configuration
  cannot compile, link or run it green. It says nothing about Buster, so it is
  held out of the comparison. This used to be most of the suite -- musl's
  x86-64 assembly was in neither archive nor either shared object, so `fenv`
  was a stub, `clone` was absent and the thread tests hung out their
  ten-second deadline, and `setjmp` was a trap so nothing that called `dlopen`
  could run -- and building the assembly is what released it: 144 of the 199
  `src/math` units and 34 of the 69 `regression` ones left this state, 31 and
  1 remain, and the suite's run time fell from 53,6 seconds to 3,9.
- `blocked-compile` — Buster cannot compile the test itself.
- `blocked-link` — Buster compiled it and the link against the Buster-built
  libc could not be made. Every undefined symbol is recorded, not just the
  first: the link stops at the startup object every time, so a ranking built
  from first symbols would name one symbol and hide the rest. A link that
  failed for another reason — a relocation a shared object cannot carry, or a
  sibling shared object this side did not build — carries the linker's own
  words instead of claiming zero unresolved symbols, and `ld` reports its
  warnings before the error that stopped it, so the line kept is the first one
  that is not a warning.
- `fail` — it ran, and its transcript or exit status differs from the Clang
  reference. This is the only state that is a defect in generated code.
- `pass` — it ran and matched; in `src/api` it compiled; in a shared-object
  unit both sides built the library.

Five units were what building the assembly newly put in front of Buster, and
each was filed rather than absorbed. Three are closed: `functional/setjmp`
compiles once an aggregate can be assigned across a `volatile` qualifier
(issue 735), and `functional/pthread_robust` and
`regression/pthread-robust-detach` pass once a walked-through aggregate member
is not loaded (issue 737). The other two, `functional/tls_align_dlopen` and
`functional/tls_init_dlopen`, joined the shared-object group that was already
there rather than being anything new; `tls_init_dlopen` is closed with the
rest of that group and `tls_align_dlopen` is one of the two the dropped
constructor still holds. 243 to 386, and nothing that passed before stopped
passing.

Applying `ARCH_SRCS` whole moved the reference and not the suite. The Clang
archive holds musl's own x86-64 `sqrt`, `fabs`, the `lrint` family and the x87
`long double` remainders now instead of the portable files, and Buster's holds
the portable files for the sixteen it cannot compile. `src/math` is the subset
that would show it -- these are its implementations -- and all 168 of its
passing units answer the same as before, which is what a correctly rounded
portable implementation standing in for a hardware one should do.

Buster compiles every unit first and unconditionally, before the reference
decides reach, because its diagnostic is inventory in its own right, and
`buster_compile_failed` on each `LIBCTEST_SUBSET` line reports that count
independently of the state. The `long double` test tables under `src/math`
are what that separation was for: 63 of the 199 units failed to compile while
only 21 of them were classified `blocked-compile`, the rest being
`excluded-reference` already, so the state counts alone would have hidden
two thirds of one compile gap.

The four subsets are reported separately rather than as one total.
`src/api` is compile-only by upstream's own design: its units are declaration
conformance checks with no runtime, and upstream links a single `main.exe`
from all of them, so there a successful compile is the pass. `src/functional`,
`src/math` and `src/regression` are compiled, linked and run, and a run is
green when the program exits zero and prints nothing, which is the protocol
upstream writes down in its README. Upstream's `src/common` becomes a support
archive on each side, minus `runtest.c`, which is the process supervisor the
harness performs itself; upstream gives each test five seconds there, and the
harness gives it ten so a loaded runner cannot turn a slow test into a
classified failure. `src/musl` is not a subset: its one unit tests musl
internals through a header this build does not publish.

The gate is the passing count together with a hash of the newline-joined
`state subset/unit` lines of everything that did not pass, taken in manifest
order — the four subsets in the order above, each sorted by unit name —
pinned as `LIBC_TEST_EXPECTED_PASSING` and `LIBC_TEST_EXPECTED_STATE_HASH`
in `build.c` and printed on the `LIBCTEST_INVENTORY` line, which is what a
deliberate rebaseline needs. The hash covers the excluded states as well as
the blocked ones, so a change in the reference's reach is a deliberate edit
too.

Every unit that is not passing prints a `LIBCTEST_UNIT` line carrying its
state and the reason — a compiler diagnostic, the count and the first of the
unresolved symbols, or the reference's own reason for being out of reach — and
each subset then prints one `LIBCTEST_SUBSET` line with its counts and its
compile, link and run time. `LIBCTEST_SUPPORT` reports upstream's support
library per side, including its archive time, and any support unit that did
not compile is named on a `LIBCTEST_SUPPORT_UNSUPPORTED` line. All nine
compile on both sides today, so the line is absent.

`LIBCTEST_BLOCKER` is the stage's most useful output while most of the suite
is out of reach: the symbols the Buster-built archive could not supply, ranked
by how many tests wanted each. It is the work list in the order that unblocks
the most tests, and it is why this stage grows by itself as the compiler
improves rather than needing to be extended by hand.

The suite is built twice, and the second time is the only coverage the NONE
register allocator has against a whole libc. Both compile inventories — musl's
1356 units and libc-test's 424 — are compiled under FAST, and building either
of them four times over would produce four object sets and one answer. All
four allocators do run on the freestanding probe, each required to reproduce
the reference transcript byte for byte, but that is one program of about two
kilobytes against FAST's 424. So one subset is built and run a second time
under `-fregister-allocator=none`: `src/functional`, 77 units of ordinary C
whose programs run in a couple of seconds. Compiling the musl manifest a
second time under NONE and gating the unit count would have been cheaper and
could only ever have caught a refusal — the half of the compiler the register
allocator is not in — where this compiles, links, runs and compares generated
code.

Only the test's own object is rebuilt. It links against the same Buster-built
libc, startup object and support archive as the pass above, so a unit that
answers differently here answers differently because of the allocator and not
because of anything underneath it. Each unit is classified from scratch,
against the same reference transcript the first pass recorded, rather than by
comparing the two Buster passes: a unit FAST cannot compile says nothing about
NONE, and the reference's reach is the one thing the two passes do share. The
gate is its own count and its own hash — `LIBC_TEST_ALLOCATOR_EXPECTED_PASSING`
and `LIBC_TEST_ALLOCATOR_EXPECTED_STATE_HASH`, printed on
`LIBCTEST_ALLOCATOR_INVENTORY` beside a `LIBCTEST_ALLOCATOR` line of counts and
cost, with one `LIBCTEST_ALLOCATOR_UNIT` line for each unit that is not
passing — because folding it into `LIBC_TEST_EXPECTED_PASSING` would make a
difference between two allocators and a regression under both the same moved
number. The pass runs whether or not the first inventory moved, so one run
reports both.

`src/functional` classifies identically under both allocators today: 72
passing, the same two wrong answers — `functional/tls_align` and
`functional/tls_align_dlopen`, both of them a dropped constructor rather than
anything an allocator decides — and the same three excluded-reference. That
agreement across 72 running programs is what the pass exists to be able to
state.

Today 388 of 424 units pass (measured 2026-08-30, on one machine).
Seventy-eight are `src/api`: 78 of its 79 units compile against musl's headers
under both compilers, and one — `api/unistd` — is held out because musl
defines neither `_PC_TIMESTAMP_RESOLUTION` nor `_SC_XOPEN_UUCP`, which the
reference fails on too. The other 310 are runtime tests that link and run
green: `src/functional` is 74 passing against none failing and 3
excluded-reference; `src/math` is 168 passing and 31 excluded-reference;
`src/regression` is 68 passing against none failing and 1 excluded-reference.
Every subset passes every unit the reference can run, and **no unit of the
suite is in any state but `pass` or `excluded-reference`**: nothing is blocked
on a compile, nothing on a link, and nothing answers differently.

Most of that total is the reference's reach rather than Buster's, and it
arrived with the assembly stage: both archives hold musl's own x86-64 assembly
now, so `fenv` is real, `clone` is present and `setjmp` is not a trap. The
number the assembly stage itself reaches is 377; the nine above it are this
tree's, and seven of them are a fix — see the `LIBC_TEST_EXPECTED_PASSING`
note in `build.c`, which is where each step is written down.

The nine units with a sibling `.mk` are what this stage's newest counts are,
and seven of them pass. Both compilers build all four shared objects —
`functional/tls_align_dso`, `functional/tls_init_dso`,
`functional/dlopen_dso` and `regression/tls_get_new-dtv_dso` — and the two
programs that open one of them and had been waiting on it,
`functional/tls_init_dlopen` and `regression/tls_get_new-dtv`, run and match.
Two code models between them are why. Buster picks between all three ELF
thread-local models now — local-exec for a definition in an executable,
initial-exec for a declaration it does not define, general-dynamic under
`-fPIC` — so nothing here is behind a thread-local relocation `ld` refuses;
and `-fPIC` is the rest of the position-independent model, so a reference to
any other symbol another object could interpose goes through the GOT and a
direct call to one through the PLT. An unwind record was the last of those
references and the least obvious: an FDE named the function it describes,
where clang names `.text` plus an offset, and that is a PC-relative reference
to an interposable symbol like any other. `functional/dlopen` is
`excluded-reference`, because musl's loader saves a jump buffer around
`dlopen` and the reference dies on the same trapping `setjmp` the Buster
build does. The last two, `functional/tls_align` and `tls_align_dlopen`, were
neither code model: `tls_align_dso.c` is one `__attribute__((constructor))`
filling the table the test reads, nothing in this tree emitted `.init_array`,
and the attribute was accepted and dropped, so that object's `.text` came out
empty. Both pass since the attribute started reaching the object file and the
image (issue 771).

`LIBCTEST_BLOCKER` still prints nothing, and now neither does any
blocked-link or failing state: every unit in the suite compiles, links and
answers as the reference does under both compilers. The work list this stage
generates for itself is a list of missing components, and it is empty.

The last 22 blocked-compile units went together, 21 in `src/math` and
`functional/strtold`, when the x87 static-initializer folder stopped refusing
an overflowing literal and a division by zero. musl spells `INFINITY` as
`1e5000f` and `NAN` as `(0.0f/0.0f)` for a compiler that does not advertise
the GNU builtins, and every `long double` table libc-test writes opens with
one of them, so those units refused at their first element and all 22 pass
now. The state counts had understated that gap by three: 63 of the 199
`src/math` units failed to compile while only 21 were `blocked-compile`,
because an `excluded-reference` unit is classified by the reference's reach
and its own diagnostic never reaches a state count. `buster_compile_failed` on
the `LIBCTEST_SUBSET` line is the number to read for a compile gap, and it is
0 in every subset but `api` now.

The last two failing units, `functional/tls_align` and
`functional/tls_align_dlopen`, went with `__attribute__((constructor))`, and
they are worth keeping as the shape of a dropped feature: the attribute was
parsed and accepted, so nothing diagnosed it, and because the function that
carried it was static and nothing called it, the unused-static elimination
took the only function in the translation unit with it. The evidence was an
object with an empty `.text` rather than any diagnostic. Keeping the function
then exposed the gap behind it, which had never been reached: `__alignof__`
took an expression operand only for a compound literal, and that constructor
fills its table with `__alignof__(x)` over four thread-local objects. Every
one of the five units this stage started with is attributed, and no unit in
the suite is in any state but `pass` or `excluded-reference`.

`functional/fcntl` was the fourth of the original five to go, and it was a
lazy operand inside a call argument. Its child process exits on
`fcntl(fd, F_SETLK, &fl)==0 || (errno!=EAGAIN && errno!=EACCES)`, and the
`errno` reads came out ahead of the `fcntl` call that sets them, so the child
saw the value from before the call — 0, from the parent's own `TESTE` — and
reported the lock its parent held as not held. Nothing about `struct flock`,
the syscall wrapper or the fork was involved: Buster's own `fcntl.o` relinked
against the *reference* archive failed the same way, and the reference's
object against `libc-buster.a` passed, which named the one translation unit in
a single link rather than a bisect over members.

Two prepasses run over a statement before its expression is lowered — one
hoists calls (`c_ir_prepare_calls_discover`), one lowers parenthesized control
groups (`c_ir_prepare_control_expressions_step`) — and either one runs an
operand that only a taken branch should run. Only the call prepass had that
rule; they share one `CIrLazyOperandScan` now, and a prepass that jumps past a
group it will not enter folds the close it never sees back into the scan so
the depth it carries stays the group's own. A call argument is where this was
visible at all: an argument is lowered by the arithmetic core, which is what
runs the control prepass, while an assignment, an `if` or a `while` condition
reaches the condition machine directly and was always lazy — so
`f(x || (n = 1))` stored and `if (x || (n = 1))` did not.
`tests/basic_c_lazy_operand_argument.c` pins the whole class — both
short-circuit operators, both conditional arms, a call in a lazy operand, and
the eager groups that must keep running — under all four allocators.

Three of the original five went before it. `regression/sem_close-unmap`
and `functional/mntent` had one cause between them: neither `main` contains a
return statement, and reaching the `}` that terminates `main` returns 0
(C 5.1.2.2.3) where every other function's fall-off is undefined. The C
frontend terminated every non-void body's fall-off with the IR's unreachable —
`ud2` on x86-64, BRK on AArch64 — so both programs did all of their work
correctly and then died on the brace with SIGILL, exit status 132.
`sem_close-unmap` is nineteen lines ending in a bare `sem_post(sem);`, so
there was nothing else it could have been. `main` is the one function that
gets the implicit zero, decided once with its signature rather than by
matching a name at the terminator, and `tests/basic_c_main_implicit_return.c`
pins it under all four allocators: exit zero is reachable in that fixture only
by falling off the closing brace, so a trap faults and a bare `ret` exits with
what the last call left behind.

`functional/strftime` came in separately and is collateral of #719, which is
why the passing count had already moved once without a rebaseline. musl
formats every specifier through
`const char *__strftime_fmt_1(char (*s)[100], size_t *l, ...)`, which
`snprintf`s into `*s` and returns it, and `__strftime_l` calls it with `&buf`
of its own `char buf[100]`. The C frontend lowered `*p` on a pointer to an
array as an rvalue load, which copies the whole array into a frame temporary,
so the decayed argument named the copy, `snprintf` filled a buffer nobody
could read, and the returned pointer named a frame that was already gone: all
64 of the unit's checks reported a mismatch, most of them against the empty
string, the rest against the padding `__strftime_l` writes into its own output
before the `memcpy` that reads the stale pointer. An array lvalue is never
loaded (C 6.5.3.2p4). The bisect is the one this stage is for and it lands on
one object -- building musl's own `src/time/strftime.c` with the compiler from
before the fix and dropping it ahead of `libc-buster.a` puts the whole
transcript back -- and `tests/basic_c_pointer_to_array_place.c` pins the shape
under all four allocators: the pointee crossing a call boundary by decay and
the place surviving the return, beside the three store spellings
`tests/basic_c_packed_layout.c` already carries.

`functional/pthread_robust` and `regression/pthread-robust-detach` pass beside
it and are *not* what fixed them, which matters because the number moved by
four rather than by two. Both segfaulted inside `pthread_exit`, which walks the
robust list through `offsetof` on a null pointer. Without `__GNUC__`
<stddef.h> spells that as pointer arithmetic through a nested member, and
lowering `&(((type *)0)->a.b)` loaded the member instead of walking through it;
with `__GNUC__` musl takes `__builtin_offsetof` and never reaches the defect,
which is why these two moved at the predefine rather than at the fix. The
defect itself is described below and is fixed one commit later, so the claim
above that "both spellings mean the same offset" holds for a nested designator
too now.

`functional/setjmp` is the fourth, and unlike those two it is a fix: an
aggregate assigned to a `volatile`-qualified object of the same type produced
two IR types for one struct and the conversion was refused. libc-test's
`functional/setjmp` writes `volatile sigset_t oldset; ... oldset = set2;` at
its third assignment, which is the whole of it.

`functional/tgmath` was the last of the original five, and it went the way
`ide cc` predefines `__GNUC__`: in every dialect now, rather than only in a
GNU one. That is not a dialect question. Clang and gcc both report `__GNUC__`
beside `__STRICT_ANSI__` under `-std=c99`, because the macro says which
extensions the compiler implements and not which ones the dialect permits, and
without it the two compilers read *different* source out of one musl header
under the suite's own `-std=c99`. <tgmath.h> is where that shows: it puts
every `__typeof__` return cast behind `#ifdef __GNUC__` — its own header
comment says "the return types are only correct with gcc" — so each
type-generic macro took the type of the widest arm of its selection chain and
`sizeof pow(2.0, 0.5)` came back as `long double _Complex`, the return type of
`cpowl`. `tests/basic_c_type_generic_math.c` pins the machinery under all four
allocators, `-std=c99` included, because the `__GNUC__` half only has
something to check outside a GNU dialect.

Three frontend gaps sat behind that flip, each of them a construct musl only
reaches on the GNU path, so each was latent rather than new. `0 ? (t *)0 :
(void *)1` kept `t *` where C11 6.5.15p6 makes it `void *` — `(void *)0` is
one of the two spellings of a null pointer constant (6.3.2.3p3) and only the
integer one was recognized — and that conditional *is* musl's `__type1(c,t)`,
which is what made every return cast name its first type; the same fixture
carries it. A GNU attribute directly after a typedef name in a *block-scope*
declaration ended the specifier run and became the declared name, which is
musl's `typedef size_t __attribute__((__may_alias__)) word;` in twelve string
and allocator units (`tests/basic_c_local_typedef_attribute.c`). `offsetof`
with a subscripted member designator did not fold in a static initializer,
which is `ioctl.c`'s compatibility table
(`tests/basic_c_offsetof_subscript.c`). And the x87 `long double` folder knew
no calls at all, so `__builtin_inff()` and `__builtin_nanf("")` — what a
hosted <math.h> spells `INFINITY` and `NAN` as once the builtins are
advertised — refused where `1e5000f` and `(0.0f/0.0f)` already folded, which
put the same 21 `src/math` units and `functional/strtold` back into
blocked-compile the moment the predefine changed
(`tests/basic_c_long_double_static_special.c` now carries both spellings
against one set of expected bytes).

`regression/malloc-oom`, `regression/malloc-brk-fail`,
`regression/setenv-oom` and `regression/pthread_create-oom` used to sit here
too, hanging out the ten-second deadline once the allocator was out of memory,
and none of them was about the allocator. (`regression/fpclassify-invalid-ld80`
was a sixth, and went when the folder learned to produce the positive quiet
NaN.) Each of the four fills memory with libc-test's `t_memfill`, which mmaps
until the kernel refuses; musl's `MAP_FAILED` is
`((void *) -1)`, and an integer narrower than a pointer reached
INTEGER_TO_POINTER without being widened first, so the constant arrived as
`0x00000000ffffffff` and no caller of `mmap` could ever compare equal to it.
`t_vmfill` therefore mapped memory forever. The C frontend now widens ahead of
the conversion — sign-extending a signed operand — because all four backends
lower INTEGER_TO_POINTER as a plain register copy and LLVM's own `inttoptr`
zero-extends; `ir_canonical_conversion_valid` holds every producer to a
pointer-width operand so the ambiguous form cannot be built again.
`tests/basic_c_integer_to_pointer.c` pins it under all four allocators. The
bisect that found it is worth keeping: the hang reproduced with the
*reference's* own `malloc-oom.o` against `libc-buster.a`, which named the
archive, and dropping Clang-built `src/malloc` objects ahead of that archive
did not move it, which cleared the allocator and left `mmap`'s own return.

`LIBCTEST_BLOCKER` is empty for the first time. `__libc_start_main` (141),
`__syscall_cp` (107), `vfprintf` (142) and `__procfdname` (15) headed the list
in turn, then `lfind` and `lsearch` with one test each; every one of them is
now supplied. What the stage reports from here is wrong answers and the
`src/math` compile gap, not missing components — the list will come back the
moment a test reaches for something new, which is the point of generating it
rather than maintaining it.

For scale, one recorded run without libc-test left 26 MB behind: the
Buster pass spent 60,6 seconds of child time over 1349 units — 45 ms each — for
116,9 MB of preprocessed source, 4.306.806 lines and 5.167.422 tokens, and
produced 1344 archive members in 5.744.046 bytes against Clang's 1344 in
2.600.342. Both counts are the manifest minus its `crt/` and `ldso/` units,
which the startup-object and archive notes above keep out. The Buster archive
is the larger of the two, which is what an emitter that spills through the
frame rather than through registers looks like at this scale, and the two
shared objects repeat it: 2.922.384 bytes against 929.584 over the same 1347
members. The two archives take 0,71 seconds to write, the two shared links
0,26, and each probe link about 3 ms.

Adding libc-test takes the run to about 131 seconds wall and 160 MB (measured
2026-08-30, twice on one machine). The suite's 424 units cost 32,0 seconds of
compiler time across both compilers for 35,7 MB of preprocessed source,
1.122.642 lines and 3.523.458 tokens; the links cost 4,0 seconds and the runs
3,3. The runs used to dominate at 74,3 seconds, almost all of it the
reference's own structural hangs waiting out the ten-second deadline because
`clone` was architecture assembly and in neither archive; building musl's
x86-64 assembly into both ended that, and what is left is the compile.
`src/functional` under the second register allocator adds 5,0 seconds of that
compiler and child time — 3,3 compiling its 77 units, 0,5 linking them and 1,3
running them — and 17 MB of objects and programs. That is about four per cent
of the run and inside its run-to-run spread: 130,5 and 135,3 seconds wall with
the pass against 132,1 and 128,2 without it, on the same machine.

Generated headers, objects, archives, metrics and logs remain under
`build/musl-v1.2.6-<pid>/` and are not cleaned up on the way out, so delete the
directories of runs you are done with. Do not run `./build.sh generate` while a
harness run is in flight: it recreates `build/` from scratch, which takes the
in-progress run directory and the `ide` the run is invoking with it, and the
run carries on reporting the missing output files as compiler failures.

`build/build` commands: `generate`, `build` (default), `clang_analyze`, `test_cjson`, `test_zlib`, `test_lua`, `test_yyjson`, `test_stb`, `test_lz4`, `test_sqlite`, `test_sbase`, `test_doom`, `test_quickjs`, `test_musl`,
`cmake_profile_summary`, `ninja_log_summary`, `time_trace_summary`,
`time_trace_summary_self_test`, `test_timing_summary`,
`test_timing_summary_self_test`,
`import_assembly_metadata`, `test_self_host`, `test_all_combinations`,
`test_all_combinations_ci`; `self_host_from_existing` is an internal
build-driver worker command used only by the pooled artifact-fanout target.
The combination matrix shares one multi-config build tree across configurations
when their configure-time policy matches. Clang omits unsanitized Debug because
sanitized Debug provides the stronger coverage; it builds and runs unsanitized
Release plus sanitized Debug and Release. Non-Apple Clang configurations use
dedicated trees because their fuzz-runtime policy differs, while AppleClang's
two sanitized configurations share one cross-config Ninja graph. GCC and Zig
compile unsanitized Debug only and do not execute it; MSVC does the same on
Windows. Only optimized, unsanitized Clang/AppleClang builds use the requested
unity build; every other build uses split translation units. TCC is retained
only as the bootstrap compiler for `build.c` and is omitted from all
application/compiler combinations.
The matrix configures its compiler trees in parallel, then uses
`cmake/superbuild/CMakeLists.txt` for one outer `cmake --build` invocation.
Each shared compiler tree builds Debug and Release through one cross-config
Ninja process, so concurrent builds never write the same `.ninja_deps` or
.ninja_log`. One shared CMake job pool admits the outer compiler and test
commands, so a completed tree can release its slot to its tests while other
trees continue compiling. On hosts with four or fewer logical CPUs, it admits
one tree per CPU and sets every inner Ninja and `BUSTER_TEST_JOBS` quota to
one; the six-tree Windows matrix therefore runs four one-job compiler trees
at a time without nested oversubscription. Larger hosts retain the weighted
allocator: split trees share at least two logical CPUs per admission slot while
unity trees use one job. Clang tests then run concurrently in the same bounded
pool, with each tree's quota passed through `BUSTER_TEST_JOBS`; future multithreaded test work
must honor that limit. Application builds are multithreaded by default;
`./build.sh generate -DBUSTER_SINGLE_THREADED=ON` is the explicit serial
fallback. A single compile is serial throughout: the compiler library starts no
lanes of its own, so build-level concurrency is the only thing that has to be
budgeted. Trees are declared longest-first — sanitized Debug,
sanitized Release, the unity Release tree that also runs `clang_analyze`, trees
covering two configurations, then the rest — because Ninja admits ready edges
from a shared pool in declaration order and a fresh CI checkout has no
`.ninja_log` for its critical-path scheduler to learn from. Set
`BUSTER_MATRIX_DIRECT=1` only to diagnose the retained legacy scheduler,
`BUSTER_MATRIX_NO_TREE_ORDER=1` to restore the previous declaration order, and
`BUSTER_MATRIX_THREADS=<n>` to state a CPU budget instead of the detected one
(`get_nprocs()` ignores CPU affinity, so `taskset` alone cannot reproduce a
small runner's admission behavior). The last two exist so the ordering can be
A/B measured on one host. When artifact fan-out is enabled on the supported
desktop CI platforms, the canonical trusted Clang Release tree also gets a
self-host worker in this same pool. The build-driver boundary is mandatory:
capture provenance, clean the canonical Release producer, then start the outer
superbuild. The clean preserves the configured CMake/Ninja graph and cache while
forcing producer objects and links to rebuild under the captured inputs. The worker depends only on the canonical compile target,
consumes the producer's one-shot integrity-checked compiler/tool/cache/graph/
environment provenance record before validating and snapshotting the artifact,
runs the existing direct fixed-point workflow without starting an inner Ninja
process, and remains disabled for the direct scheduler and unsupported platforms.
Each self-host stage and the stage-2 benchmark are bounded at ten minutes
(`SELF_HOST_TIMEOUT_SECONDS`): the work is fixed and costs seconds, so a stage
that does not finish is a compiler that never will, and waiting on one wedges
a serialized CI runner for hours while Ninja buffers the edge's output and the
log says nothing. On expiry the child is killed and the run fails naming the
stage and its command line. Every other run waits indefinitely, because their
cost scales with what they are given.
The fixed-point pair continues to use the default FAST allocator, and the
existing non-Windows machine stage continues to compile and run its benchmark
with `-fregister-allocator=mir-stack`. That stage is followed by a canonical
compile-only gate: the stage-2 compiler builds `ide-stage2-none` with
`-fregister-allocator=none`. It does not run that output because the regression
this gate protects against is a canonical argument-capture crash while
compiling `ide.c`; launching a second benchmark would add CI work without
covering that path. NONE is the only allocator stage here that reaches the
canonical emission path; QUALITY exercises the same machine path already
covered by FAST/MIR_STACK and is covered by focused/all-mode tests, so another
full unity compile would add CI cost without distinct self-host coverage.
CI Release builds use `-O2`; local Release builds retain the toolchain default.
Local builds make the optimized tree profilable, which CMake's defaults do not:
`BUSTER_DEBUG_INFO` emits debug information in the configurations that carry no
`-g` (`Release`, `MinSizeRel`) and `BUSTER_FRAME_POINTERS` adds
`-fno-omit-frame-pointer` to every optimized configuration. Both default to off
under `--ci` and on everywhere else, and both are overridable with
`./build.sh generate -DBUSTER_DEBUG_INFO=ON|OFF -DBUSTER_FRAME_POINTERS=ON|OFF`.
`Debug` and `RelWithDebInfo` already carry `-g` from the CMake defaults, `Debug`
is `-O0` and keeps frame pointers anyway, and sanitized trees keep their own
reduced `-gline-tables-only` mode, so neither option disturbs them. MSVC is
excluded from the frame-pointer option because `/Oy` has no x64 meaning.
Measured on Clang 22/Linux x86-64: debug information does not change code
generation and costs compile time and artifact size only (unity Release
`ide.c` 37.3 s -> 46.5 s, `ide` 11.4 MiB -> 19.1 MiB); frame pointers do change
code generation but cost 0.065% of instructions on a unity self-compile
(29.5037 G -> 29.5227 G) with no wall-clock difference above run-to-run noise
and no change to the parser benchmark. CI opts out of both because it profiles
nothing and pays the compile time.
Clang static analysis runs only against unsanitized Release. Every Clang matrix
configuration runs `test_all`; GCC, Zig, and MSVC are compile-only, and platform
packages use their native test runner.
The one carve-out inside `test_all` is a **whole-table audit**: a module whose
result is a function of the generated metadata tables and the repository's
source text alone, so no compiler, configuration or optimization level can
change its answer. Those modules run on the same single canonical tree per
platform that already owns `clang_analyze` — unsanitized optimized Clang —
because re-deriving one identical answer in eight to ten configurations cost
more than any other single thing in CI. Mark such a module with
`table_audit` in the `test_descriptors` table of `src/buster/tests/test.c`;
`x86_64_completion_census_tests` is the current one. The superbuild opts a
tree out with `BUSTER_TEST_TABLE_AUDITS=0`, and **the default is on**, so a
bare `ide test`, a single-tree build, or any runner that does not set the
variable keeps full coverage. Never mark a module whose result depends on
generated code, the host, or the sanitizer.
Flag scope matters: `--sanitize`, `--fuzz`, `--lto`, `--ci`, `--time-trace`,
`--instrument`, `--cc <clang|gcc|zig|cl>` are accepted **only by
`generate`** for public workflows; `build` rejects them with an explicit
diagnostic. The internal `self_host_from_existing` worker is the narrow
exception for build-driver-supplied `--ci/--no-ci` and
`--fuzz/--no-fuzz`, and requires the captured provenance record.
TCC is reserved for compiling `build.c` through `build.sh`/`build.ps1` and
`generate --cc tcc` is rejected as an application compiler.
`--optimize`/`--no-optimize` are configuration shorthands for
Release/Debug and never create separate cached optimization state. `build`
accepts `--config <name>`, `--optimize`, `--target/-t <ninja target>`, and
`--verbose/-v`. Booleans have `--no-` twins; `--` passes the rest through to
Ninja (`build`) or CMake (`generate`).

The Clang-like `ide cc` driver accepts `-march=<model>` and
`-mcpu=<model>` (or their separated forms), ordered target-feature overrides
through `-mattr=+feature,-feature`, and x86 assembly dialect selection through
`-masm=att|intel`. CPU and feature options also accept separated values. CPU names use the canonical
spellings printed by `cpu_model_to_string_os`, such as `baseline`, `native`,
`haswell`, `znver5`, and `apple-m4`; incompatible target/model pairs are
diagnosed. `-v` reports the selected CPU, the sorted effective feature set,
and maximum native vector width. `-target`/`--target` strings are
`arch[-vendor][-os][-environment]`: the vendor and environment components stay
free-form, but a CPU model there is rejected in favor of `-march=`, and so is
anything past the fourth component. Both used to be dropped silently, which
left baseline code generation and no hint that the request was ignored.
Native x86-64 and AArch64 compilation uses the FAST register allocator at
every optimization level, including the default and `-O0`, while
`-fno-register-allocator` selects the canonical stack emitter. Advanced and
diagnostic callers may select `none`, `mir-stack`, `fast`, or `quality` with
`-fregister-allocator=<mode>`; when several allocator-affecting options are
present, the last one wins.
The allocators run on x86-64 under both System V and Win64, and on AArch64
everywhere but the PE-unwind targets, which still take the canonical path
whole. Win64 differs from System V in the file it allocates — RSI and RDI are
callee-saved there, so the allocator has seven callee-saved registers instead
of five and the vector class keeps only the volatile ZMMs — and in how a call
is built: the outgoing arguments and the callee's shadow space are written
into a fixed area at the bottom of the frame instead of pushed, because the
stack pointer must not move inside the body of a function whose unwind data
can only carry a frame-pointer offset up to 240 bytes. Its prologue pushes the
callee-saved registers before establishing the frame pointer for the same
reason. Shapes the Win64 subset does not build yet — variadic definitions and
calls, 128-bit integers, vector signatures, indirect (non 1/2/4/8-byte)
aggregate arguments, and dynamic stack allocation — fall back per function,
which `-v`'s `fallback_functions` and `CODEGEN_FALLBACK` lines report.
A `.s` input, or any input under `-x assembler`, is an assembly translation
unit rather than a C one. `assembly_unit_encode` (`assembly_unit.c`) is the
layer above `assembly_encode`: it interprets the directive vocabulary, tracks
one offset per section, resolves labels, and hands each instruction line to
the instruction layer beneath, and the driver turns its sections, symbols and
relocations into an `ObjectFile` like any other. The vocabulary is `.text`,
`.data`, `.bss`, `.rodata` and `.section`; `.globl`/`.global`, `.weak`,
`.hidden`, `.type` and `.size`; `.align`, `.balign` and `.p2align`; `.byte`,
`.short`/`.word`/`.hword`/`.value`, `.long`/`.int`, `.quad`, `.ascii`,
`.asciz`/`.string`, and `.zero`/`.skip`/`.space`; `.intel_syntax noprefix` and
`.att_syntax prefix`; and the `.cfi_*` family, accepted and dropped because it
describes unwinding rather than bytes. Anything else -- a directive the table
does not claim, or an operand form one of these does not cover -- is a
diagnostic naming the directive and its line, the way every other unsupported
construct here is reported rather than silently dropped.

Three things that layer owns rather than the instruction layer. Local numeric
labels: `1:` becomes a generated name and `1f`/`1b` resolve to the nearest
following or preceding definition in source order, and those names leave the
symbol table again once every reference to one is folded, the way GNU as drops
its own `.L` locals. A repeat or lock prefix alone on a line joins the
instruction on the next one. And a same-section PC-relative reference to a
label defined in the file is written into the bytes; only a cross-section or
undefined name becomes a relocation. `@PLT` is dropped: a static link resolves
such a call the same way it resolves a plain one. Sections keep their own
names -- `.init` and `.fini` are neither `.text` nor absent -- and a
hand-written section gets alignment 1, because `crti.o` and `crtn.o`
contribute one and two bytes to `.init` and any padding between them would
run as code.

One deviation from GNU as, and one refusal. A forward branch to a label is
always the near form, because the instruction layer sizes a statement before
the label is known and this assembler does not relax; the bytes are correct
and a few longer than GNU as writes. And a `.S` -- assembly the C
preprocessor runs over first -- is refused by name: this frontend's
preprocessor hands back C tokens, and `%rax`, `$1` and `1f` do not survive
that round trip, so the input is reported rather than mis-assembled.

`-emit-llvm` emits binary LLVM bitcode directly from canonical typed IR for C
inputs. It writes `<input>.bc` by default, accepts `-o` for a single
input, and rejects native objects, archives, libraries, frameworks, linker
arguments, `-E`, `-S`, and `-fsyntax-only`. The writer has no LLVM dependency;
see `LLVM_BITCODE.md` for its target metadata, API, and supported boundary.

Every hosted ELF link reads the shared libraries' own dynamic symbol tables.
`compiler_driver_elf_library_exports` looks `libc.so.6` and each requested
library up where the loader would — the `-L` paths, then the sysroot or host
`lib`/`usr/lib` roots, multiarch first — and rejects a file whose ELF machine
disagrees with the target, so a cross link never reads the host's own libc.
`compiler_driver_elf_dynamic_symbols` walks that table once and produces two
things.

The first is the defined global and weak **objects** with their addresses and
sizes, as `NativeDynamicDataSymbol` arrays, which `link.c` uses to reserve
copy-relocation slots: a slot stands for the library's object rather than the
one name the program spelled, so it carries every name the library exports at
that address, takes the library's own size, and is shared by two imported names
for one object. The alias set is what makes `extern char **environ` work. A
definition in the executable takes precedence over the library's for every one
of its names, and glibc stores the environment through `__environ` after
startup; an executable that defined only `environ` left that store in libc's
own storage while the program read a copy taken before startup ran, which is to
say null. This half is still collected only for a link with an undefined data
symbol, since a link with none has nothing to copy, and a library that cannot
be found or parsed leaves the pointer-sized slots the writer reserved before
alias sets existed.

The second is the **symbol version** of every defined entry, functions
included, read from the library's `.gnu.version` and `.gnu.version_d` into
`NativeDynamicVersionedSymbol` arrays. That half is collected on every hosted
ELF link, because versioning applies to functions and there is no cheaper way
to know: reading `libc.so.6` where nothing did before costs about 0,65 M
instructions, a tenth of a percent of the smallest hosted compile. It buys two
things in the x86-64 dynamic writer, and the AArch64 one through it:

- **A reference records the version it bound to.** `.gnu.version` carries one
  index per dynamic symbol, `.gnu.version_r` names per library the versions
  the image needs, and `DT_VERSYM`/`DT_VERNEED`/`DT_VERNEEDNUM` publish both.
  The alias names a copy slot defines are versioned too, because each of them
  is a name the library publishes. Without this the image binds by name to
  whatever the running glibc calls default, which is the versioning
  mechanism's whole purpose: `readelf -W --version-info` on a Buster
  executable now agrees with GNU ld's for the same program, `stat@GLIBC_2.33`
  included. Both sections are omitted when nothing needed a version, and the
  layout then collapses to exactly what it was before, so an unversioned
  library's image is byte-for-byte unchanged.
- **A name with no default version is refused** rather than linked, as
  `LINK_ERROR_SYMBOL_VERSION`. glibc publishes `sys_errlist` four times, once
  per historical layout, and every one of them is a non-default `name@VER`; an
  unversioned reference has nothing to bind to, GNU ld reports it undefined,
  and Buster linked it and let the loader pick. **Do not use `sys_errlist` as a
  Clang-differential fixture** — a harness that reads "Clang refuses, Buster
  accepts" as a Buster success measures nothing (issue #660).

Ninja targets: `ide`, `test_all` (on Android packages/runs the APK, on iOS
drives the simulator), `bench_all` (desktop only — runs `ide bench`),
`test_self_host` (Linux x86-64 and macOS), `run_ide`, `test_ide`, `debug_ide`,
`buster_shaders`, `apk` (Android), `clang_analyze`. Rendering backends and
shader compilation are retained as opt-in infrastructure and default off. The
Vulkan SDK (`VULKAN_SDK` env) is required only when Vulkan or Slang shader
compilation is explicitly enabled.

## Tests

- All tests run inside the `ide` executable; there is no external unit-test
  framework. From the repository root, run `ide test --verbose=1 --ci=1` or
  build the `test_all` target.
- Test modules live under `src/buster/tests/` as mirrored `*_test.c` and
  `*_test.h` pairs. `src/buster/tests/test.c` owns registration. Unity builds
  include implementations into the main translation unit; non-unity builds
  compile each test source independently.
- C frontend and driver fixtures live under `tests/` and use `.c`, `.h`, native
  object, archive, and shell-script inputs. Keep fixture paths relative to the
  repository root because tests intentionally exercise the real file loader.
- Dormant `.bbb` fixtures remain under `tests/` as preservation material. Do
  not register, compile, parse, benchmark, package, or execute them in the
  default build or CI until the custom frontend is deliberately reactivated.
- A new production module or public behavior must receive a focused module
  test. Frontend changes should cover preprocessing, parsing/diagnostics,
  semantic typing, canonical-IR lowering, and driver behavior as applicable.
- Keep test-only declarations behind `BUSTER_INCLUDE_TESTS`. Private structures
  shared with tests belong in a narrow `*_internal.h` seam rather than being
  exposed through a production public header.
- CI is defined under `.forgejo/`, not GitHub. Preserve Debug/Release,
  unity/non-unity, sanitizer/fuzz, self-host, and supported-platform coverage
  when changing build orchestration or the compiler pipeline.

## Benchmarking and diagnostics

- **`test_self_host` is the most trustworthy and complete compiler benchmark.**
  It exercises the full self-hosting IDE pipeline, including the trusted
  bootstrap, two complete unity-build IDE compilations, byte-identical stage
  verification, and the stage-2 benchmark. Use its end-to-end result as the
  primary completeness/fixed-point signal for compiler-throughput changes.
  For performance measurements, run the benchmark with the trusted
  Clang-compiled `ide` executable, whose generated host code has the best
  quality; self-compiled stage executables are useful for fixed-point
  validation, not as the default benchmark host.
- `ide bench` (also exposed through the `bench_all` target) reads
  `tests/basic_c_operations.c` and measures the complete C frontend path:
  preprocessing, syntax construction, semantic analysis, and canonical-IR
  lowering. It prints one `BENCH_C_FRONTEND` line with minimum and median
  latency plus source throughput. Run it from the repository root.
- **`STEP_INSTRUCTIONS` lines** report instructions retired for every build step
  that already reports wall time, printed straight after its `took ... seconds`
  line. The one to watch is `Self-host stage 1`, which is compile throughput on
  the complete unity translation unit: it is contention-immune and reproducible
  to a few thousand instructions, where wall time on an idle 16-thread desktop
  varies about 10% run to run. Compare it across commits — a change that only
  *adds* source can regress it sharply through a latent quadratic, which is
  invisible to test-time measurement. Linux-only: this needs hardware counters
  and there is no cheap portable equivalent, so nothing is printed elsewhere or
  when `perf_event_open` is unavailable, and its absence is never an error. One
  counter covers the process tree, so a step's number is exact only while it is
  the sole child running; that holds for the sequential self-host stages and
  CMake generation, but concurrent matrix steps overlap and must not be read as
  per-step totals.
- **The `SOURCE` table** gives `STEP_INSTRUCTIONS` a denominator: it reports
  what the C frontend read, in bytes, physical lines, sLOC, comments, blank
  lines, literals, and preprocessing tokens, each with its share of the whole.
  `ide cc -v` prints it, and the self-host stages pass `-v` so it lands in the
  log beside their instruction counts. The `unique` column covers the distinct
  files of the include closure, the `lexed` column every inclusion: a header
  with neither `#pragma once` nor a whole-file `#ifndef` include guard the
  preprocessor recognizes (see `CIncludeGuardTable` in `c_source.c`) is
  re-read and re-lexed at each `#include`, so the two columns' ratio is the
  unit's include amplification (currently 295 files/27,2 MB against
  326/27,3 MB; before the guard optimization the same tree read 448/29,4 MB).
  A `files lexed more than once` block under the table attributes the
  remainder per path — deliberately re-includable files like the builtin
  `stddef.h` and glibc's `bits/wordsize.h`, whose re-reads total 28 KB. `bytes on disk`/`physical lines` measure
  the files as written, `bytes scanned`/`lines scanned` what the lexer saw
  after carriage returns and line splices are folded out, and two partitions
  hold exactly: bytes are code plus comment plus whitespace, and lines are
  code plus comment plus blank once the `both` row is subtracted. A closing
  `preprocessed output` block measures the other side — the tokens actually
  handed to the parser, their spelling bytes, every spelling byte the run
  retained, macro expansions, and `#define` directives — because macro
  expansion multiplies the input by whatever factor the macros ask for, so
  parsing and lowering scale with those and not with the file sizes. The
  measurement is always on and costs 0.19% of stage-1 instructions (measured
  on Clang 22/Linux x86-64), because every source unit falls out of a branch
  the lexer already takes rather than a second pass over the bytes, and the
  output side is one linear sum over the finished token stream. The
  bootstrap's table does not match the self-hosted stages' and is not meant
  to: the bootstrap finds its host compiler's resource headers and the
  self-hosted stages fall back to the builtin ones, which is why the file
  table is populated lazily (see `map_entry` in `c.c`).
- **The `SELF_HOST stage <1|2> throughput` block** is the division done: its
  `workload` row reports bytes, LOC, SLOC and tokens; its `bandwidth` row
  reports MB/s, LOC/s and SLOC/s from the execution time shown immediately
  above it; and, where the platform exposes a hardware counter, its
  `instructions` row reports the total and the per-byte, per-SLOC and per-token
  ratios. These are the numbers to trend across commits, printed
  by `self_host_compare_action` beside the `SELF_HOST deterministic` line. `-v`
  prints the `SOURCE` table for a human;
  `ide cc -fsource-metrics=<path>` writes the same measurement as
  `<group>.<field>=<value>` lines for a program, each self-host stage writes
  one beside its executable, and build.c combines the relevant fields with
  the stage's own `STEP_INSTRUCTIONS` delta and wall time. A file rather than
  another stdout line, because capturing a compile step's output would stop its
  diagnostics from streaming as it runs. `bytes` is `lexed.translated_bytes`,
  `loc` is `lexed.translated_lines`, and `sloc` is `lexed.code_lines`; all
  three legitimately differ between the two stages for the resource-header
  reason above. The LOC/s and SLOC/s values divide those line counts by the
  stage's recorded wall time; MB/s uses decimal megabytes
  (1,000,000 bytes). `tokens` is `preprocessed.tokens`, which does not differ,
  and is the denominator to reach for when comparing the stages to each other.
  The ratios carry three
  decimals because a compile costs a few hundred instructions per byte, and
  whole units would quantize the series to about 0.3% — coarser than the
  regressions the counter exists to catch. Without a usable instruction
  counter the line still prints its units and omits the ratios. Only ever add
  keys to the metrics file: readers take the fields they know and must keep
  working against a newer compiler's file.
- **The stages' preprocessed token streams are a fixed point too**, gated
  beside the executable bytes. `test_self_host` fails when the two stages
  disagree on `preprocessed.tokens` or `preprocessed.bytes`, and when the
  executables differ it first says whether the token streams matched — which
  halves the search, since the parser and everything after it are a function
  of that stream alone. Nothing else is compared between the stages: bytes,
  sLOC, comments, `#define` directives and macro expansions all legitimately
  differ for the resource-header reason above (currently 6.088 vs 6.087
  `#define`s and 56.429 vs 56.426 expansions converging on the same 1.378.839
  tokens and 12.124.867 spelling bytes). Changing what the builtin resource
  headers declare can part the streams on purpose; that is the signal and not
  a false alarm, because the two stages are then no longer compiling the same
  program and byte-identical executables would be saying less than they
  appear to. Update the gate deliberately in that case — do not weaken it to
  the token count alone, since equal counts of differently spelled tokens are
  still different programs.
- **`ninja_log_summary <build-dir> [--limit N]`** and **`time_trace_summary
  <json-path>... [--limit N]`** (both new `build/build` commands, same
  shape as `cmake_profile_summary` — see `build.c`) are diagnostics for
  *where compile time goes*: the former reads `<build-dir>/.ninja_log`
  directly (only useful for multi-TU/Debug builds — Release is a single
  unity TU); the latter parses one or more clang `-ftime-trace` JSON files
  (enable via `--time-trace`) and reports the slowest `"Total *"` rollups
  clang itself pre-aggregates (`Total Frontend`, `Total Backend`,
  `Total InstantiateFunction`, ...), summed across every file given. These
  commands are diagnostics for humans to inspect when aggregate numbers
  suggest a regression, and can be run from CI or locally.
- **`test_timing_summary <test-log>... [--limit N] [--baseline <path>]
  [--update-baseline]`** answers *where test time goes*, the counterpart to
  the two commands above. It parses the `TEST_MODULE_TIMING` lines
  `library_tests()` already prints under `--verbose=1` (which every CI job
  passes) out of a saved matrix run or CI log, reports each module's total
  across the configurations in that run with its share of the whole, then the
  slowest individual (configuration, module) rows with their deltas against a
  stored baseline. `--update-baseline` records the current run.
  This exists because test cost accumulates silently: `x86_64_metadata_tests`
  reached 60% of all CI test CPU time before anyone noticed, and every number
  needed to catch it in week one was already printed on every run and
  discarded with the log.
  Two properties are deliberate and should be preserved. **It is a diagnostic,
  not a gate** — it has no thresholds and no CI failure condition, because
  wall-clock test time is far too noisy to gate on (identical code measured
  290.1 s and 319.8 s for the same matrix on an idle 16-thread desktop, a 10%
  spread); recording history first is the right order, and a gate would need a
  noise model that does not exist yet. **Series are per-runner and
  per-configuration and are never merged** — the same module measured 4.4 s in
  Linux Release, 138 s in sanitized Debug and 192 s on a sanitized Debug
  Windows runner, so a baseline records the runner it was taken on and refuses
  to be compared against another, every runner keys its own baseline (none is
  excluded, including shared or noisy ones), and rows are only compared within
  the same configuration. The runner name defaults to `<platform>-<arch>` and
  is overridable with `BUSTER_TEST_TIMING_RUNNER`; the default baseline path is
  `build/test-timing-baseline-<runner>.txt`. Configurations are recovered from
  the build commands the superbuild echoes ahead of each test block; timing
  rows with no command line in front of them are attributed to numbered
  `unknown:<n>` series rather than merged.
- **`tools/ci_time.py`** answers *what CI actually costs*, over a window rather
  than for one push. It reads the Forgejo Actions API and prints runner hours
  per runner (or per day, week, or branch) beside push latency — the two
  numbers that matter separately, since machine time is what the matrix burns
  and latency is what a push waits for:

  ```sh
  tools/ci_time.py --days 30
  tools/ci_time.py --days 30 --by week
  ```

  The 30 days to 2026-08-28 measured **403 runner-hours over 5,877 jobs** —
  `x86_64-windows-znver5` 131.0 h (median 5.7 min/job), `aarch64-macos-mini`
  113.9 h, `x86_64-linux` 102.1 h, `x86_64-linux-dedicated` 52.7 h, the gate
  3.3 h — and push latency over 1,435 runs of median 6.1 min, mean 8.1, p90
  17.0. Authentication reuses the git credential for the forge, so there is
  nothing to set up.

  The arithmetic is trivial and every trap is in the data, which is why this is
  a script and not a one-liner. **`task.updated_at` is not the end of the
  job**: Forgejo's log-retention sweep re-touches the row long afterwards in
  batches that share one end second (a whole day stamped `23:45:3x`), which
  makes a raw sum overstate a month by ~30x — a five-minute Windows job
  appearing to run for 33 hours. Two physical ceilings repair it: a job cannot
  outlive its run (clamp to the run's `stopped`, joined on `run_number ==
  index_in_repo`) and cannot outlive `ci.yml`'s `timeout-minutes`, which is why
  exact 7201-second rows are real timeout kills; the ~0.3% that survive both
  are imputed from the runner's median and counted in an `imputed` column
  rather than hidden. `actions/tasks` is the only per-job timing source —
  `actions/runs/{id}/jobs` returns `started_at` and `completed_at` as null
  here. And a paged walk must test the **head** of each id-descending page
  against the cutoff, never the tail: runs that never dispatched carry
  `0001-01-01`, which ends the walk hundreds of runs early and silently
  inflates every job whose run can no longer be found. `--self-test` covers the
  repair without touching the network.

  `limit` caps at 50 and the server ignores `Accept-Encoding: gzip`, so a cold
  month is 154 requests and 23 MB (~18 s); terminal rows are cached under
  `build/ci-time-cache.json`, which takes a repeat window to 2 requests and
  ~1 s. Do not raise `--jobs` past its default of 8 to go faster — 16 measured
  slower, the forge being the bottleneck while it is also serving runners.
- **`tools/cache_miss_survey.py`** answers *where the cache misses are*, the
  data-side counterpart to `tools/branch_miss_survey.py` below. It runs the
  stage-1 workload under `perf stat` for the hierarchy budget (L1d accesses
  and misses, how many fills L2 answers, the demand fills that reach DRAM,
  both TLBs, page faults), then takes one `perf record` per event and ranks
  symbols by miss share *beside their cycles share*, because the ratio is what
  separates a symbol that misses because it is big from one that is waiting on
  memory. Top symbols are broken down by source line through
  `llvm-symbolizer`, and `--dwarf` adds a sparser DWARF-unwound capture that
  names the exact `memcpy`/`memset` call sites — worth it here, where a fifth
  of every miss event has its leaf in libc. Three of its rules are the same
  ones this section states for `perf`, and the script encodes them so they do
  not have to be remembered: symbolize `sym+offset` rather than `-F srcline`,
  keep `--no-inline` on `perf script` (inline expansion costs 170 s against
  under a second here) and recover inline chains from `llvm-symbolizer`
  instead, and read a line table as a loop rather than an instruction, since
  AMD's precise sampling facility (IBS) is system-wide only and the script
  stays unprivileged. It also documents the trap that frame-pointer unwinding
  *skips* the caller of a `memcpy`, so an fp callchain names the call site one
  level out. Run it from the repository root on an idle machine after
  `./build.sh build --config Release -t ide`; a full `--dwarf` pass takes
  about 25 seconds and writes its captures and report under
  `build/cache-miss-survey/`.
- **The local Release tree is profilable as built.** `BUSTER_DEBUG_INFO` and
  `BUSTER_FRAME_POINTERS` are both on by default outside `--ci`, so
  `./build.sh build --config Release -t ide` produces a `-O3` binary that
  symbolizes to source lines and unwinds through `--call-graph fp`, the same
  cheap unwinding the sanitized Debug tree allows. Superluminal reads it
  directly. Record it like any other build:

  ```sh
  ./build.sh build --config Release -t ide
  perf record -F 999 -g --call-graph fp -o release.data — ./build/Release/ide bench
  ```

  The `perf script`/`llvm-symbolizer` rules below apply unchanged; the Release
  binary is a clang PIE like the Debug one. Pass
  `-DBUSTER_FRAME_POINTERS=OFF` when the point of the measurement is the
  frame-pointer cost itself, or when reproducing a CI Release number exactly.
- **Sampling the sanitized (ASan+UBSan) Debug tree with `perf` works.** It is
  the CI critical path, so it is the configuration most worth profiling. Record
  it exactly like any other build; there is no sanitizer-specific obstacle:

  ```sh
  ./build.sh generate --sanitize && ./build.sh build -t ide
  LD_PRELOAD= perf record -F 999 -g --call-graph fp -o asan.data \
      — ./build/Debug/ide test --verbose=1
  ```

  Four things must be right, and each one silently produces a *plausible but
  wrong* profile when it is not:
  - **Clear `LD_PRELOAD`.** With one inherited (NoMachine sets
    `LD_PRELOAD=/usr/NX/lib/libnxegl.so`), the shared ASan runtime is not first
    in the initial library list, the process aborts inside `ld.so` before
    `main`, and the capture is ~12 samples that are ~100% of *user-space* time
    in `ld-linux-x86-64.so.2` with no ASan symbols. That result is not a
    symbolization failure and not evidence that ASan defeats sampling — it is
    an empty profile of the dynamic loader. `CMakeLists.txt` already composes
    the runtime ahead of any inherited `LD_PRELOAD` for its own test targets;
    only ad-hoc command lines need the `LD_PRELOAD=` prefix.
  - **Run on a quiet machine.** A contended host produces the same visual
    signature — few samples, attribution smeared into the loader and the
    scheduler. Confirm the sample count and the `TEST_MODULE_TIMING` line agree
    with a serial run before reading anything into the histogram.
  - **`--call-graph fp` is correct and cheap here.** Debug is `-O0` and the
    sanitizer runtime keeps frame pointers, so frame-pointer unwinding resolves
    from inside `libclang_rt.asan` back into buster code. This matters because
    the sanitizer cost has to be charged to the buster function that provokes
    it; a leaf-only profile just says "memcpy".
  - **Do not use `perf script -F srcline` (or `-F ip,sym,srcline`) for line
    attribution.** It resolves the raw return address, which points *after* the
    call, so it reports the following line — measured on this tree it shifts
    call sites by 1–2 lines (real `c_test.c:7227` is reported as `7229`, real
    `7229` as `7230`) and prints `:0` for ~69% of frames. Extract
    `sym+offset` instead and symbolize the byte before the return address:

    ```sh
    perf script -i asan.data --no-demangle -F comm,ip,sym,symoff,dso
    # static vaddr = (readelf -sW addr of sym) + offset; then
    #   llvm-symbolizer --functions=linkage --demangle  <<< "CODE build/Debug/ide 0x<vaddr-1>"
    ```

    Use `sym+symoff`, not `-F dsoff`: `dsoff` is a **file** offset while
    `llvm-symbolizer` wants a **virtual** address, and for a clang PIE the two
    differ by `p_vaddr - p_offset` of the text segment (`0x1000` for
    `build/Debug/ide`). Feeding `dsoff` straight to `llvm-symbolizer` yields
    wrong-but-believable source lines. This is the same class of mistake as
    `perf report` mis-symbolizing buster-produced `ET_EXEC` images by
    `-0x400000`; clang-built binaries are PIE and symbolize normally once the
    right address space is used.
- **`tools/branch_miss_survey.py` ranks branch mispredictions by source line,
  not by symbol.** `perf record -e branch-misses` is not a precise event: the
  sample lands past the branch that caused it, so its histogram names the
  function and three audits (`2026-08-22b`, `2026-08-22d`) paid to discover
  that separately. Zen 3 and later carry the AMD Last Branch Record extension,
  so `perf record -j any,u` captures the last sixteen branches of every sample
  with a per-entry mispredict flag — the branch instruction's own address. The
  script records that over the stage-1 self-host compile by default, tallies
  the mispredicted records, symbolizes them through inline frames, and prints a
  ranking per function and per line with each row's estimated absolute misses:

  ```sh
  ./build.sh build --config Release -t ide
  tools/branch_miss_survey.py --repeat 3 --cross-check
  ```

  `--cross-check` re-profiles the same run with `branch-misses:u` and prints
  that share beside each function, which is the check that the address mapping
  is right: a branch record is a runtime address in a PIE and needs the
  `p_vaddr - p_offset` skew above, and dropping it silently reports the
  neighbouring functions. Profile any other workload by passing it after `--`.
  `--self-test` covers the address math without needing `perf`.
- **Validate any profiling method against a non-sampling ground truth before
  trusting it.** The cheap one here is direct timing: bracket the call sites
  under suspicion with `timestamp_take()`/`timestamp_ns_between()` and print
  through `arguments->show`, then compare against the sampled shares of the
  same process. Confirm the extracted return addresses too — each one must
  disassemble to the instruction immediately after a `call` to the callee the
  callchain names (`objdump -d --start-address=... --stop-address=...`).

## Performance audit notes

Audit history lives in `PERFORMANCE_AUDITS.md`, newest first — it is kept out
of this file because it is a growing record rather than a rule. **Read its
newest entry before any performance work**: it holds the reference numbers a
new measurement is compared against (stage 1 instructions, sanitized and
Release `ide test`, `ide bench` medians), the finds that were deliberately
left untaken, and the traps an earlier audit already paid for. Record a new
audit there, not here; this file keeps the method, that file keeps the
history.

An audit is a **new file**, `docs/performance-audits/<id>.md`, plus one line at
the top of the index in `PERFORMANCE_AUDITS.md`. Never append an entry into an
existing audit file and never rewrite one: the split exists so that two audit
branches open at once touch disjoint files, which is what the single prepended
history could not do. The index line is the only shared text, and
`.gitattributes` marks `PERFORMANCE_AUDITS.md` `merge=union` so concurrent
inserts keep both lines rather than conflicting — after such a merge, check
that the newest id is on top, because union does not know which line is newer.

The id is the **UTC timestamp at which the audit is recorded**,
`2026-08-22T140351Z` — ISO 8601 with the colons dropped, because Windows
forbids them in filenames. `tools/new_audit.py` mints one, creates the file and
inserts the index line; use it rather than typing an id by hand, because the id
is the one field two concurrent sessions can independently choose the same
value for. Audits before 2026-08-22 are named by date plus a sequence letter
(`2026-08-08k`), and that is precisely what collided: the letter is picked by
counting the day's existing entries, so two sessions auditing the same day
always picked the same letter, and three of the four PRs open when the history
was split had done exactly that. Those older names are historical — entries
cross-reference each other by them — and stay as written.

## Forge, issues, and pull requests

The forge is Forgejo at `https://code.buster14a.com/buster/buster`, not GitHub;
CI lives under `.forgejo/`. Talk to it with **`fj`**, the Forgejo CLI. It reads
the repository from the git remote, so run it from inside a checkout (or pass
`-C <path>` / `-r buster/buster`).

`fj` needs a token once per machine. The password half of the
`code.buster14a.com` line in `~/.git-credentials` is a valid API token, so
authentication is a pipe, not a browser round trip:

```sh
grep code.buster14a.com ~/.git-credentials \
  | sed 's|https://[^:]*:||; s|@code.buster14a.com.*||' \
  | fj auth add-token -H code.buster14a.com
fj whoami          # verify: davidgmbb@code.buster14a.com
```

`fj issue create "<title>" --body-file <path> --no-template`,
`fj issue search [-s open|closed|all]`, `fj issue view <n>`,
`fj issue comment <n>`, and `fj pr create --base main --head <branch>
--body-file <path>` are the whole working set; `fj pr search`, `fj pr status`
and `fj pr view` read the other side. Two things to know before scripting it:
the subcommand for listing issues is `search`, not `list`, and **omitting both
`--body` and `--body-file` opens `$EDITOR`**, which hangs a non-interactive
session — always pass a body file. Write the body as a file rather than a
shell string: backticks inside `$(cat <<EOF)` get command-substituted by zsh,
which has mangled a commit message before.

`fj` supersedes the older workarounds. `tea`'s login for this host has no
token, and the raw-`curl` recipe that went with it needed a browser
`User-Agent` to get past Cloudflare's `403 error code: 1010`; `fj` is not
subject to either problem.

**Issues are the task queue.** Work that is real but not being done right now
becomes an issue, not a paragraph in an audit that nobody will find — a chip
filed against a memory is invisible to the next agent, while an issue is
something a fresh session can pick up cold. Write the body as a **prompt**: what
is wrong and how it was diagnosed, the file and symbol names to start from,
the constraints and do-not-retries that earlier work already paid for, how to
validate the fix (which oracle, which harness, which counters), and a
definition of done. State what was measured and when, so a stale claim is
recognisable as stale; the tree moves fast enough that a count quoted without
a date is a trap. Issues #537-#549 are the current examples of the form.

**Push a rebase before you re-verify it.** A rebase onto a moved `main` is
followed by a full local pass — `test_all`, `test_self_host`, whichever compat
harness the change touches — and that pass takes longer than CI takes to start.
Push the rebased branch first, as long as the runners are not already saturated
with other work, so the matrix runs while the local pass runs; the two agree
almost always, and when they disagree you have both answers sooner. Force-push
with `--force-with-lease`, never a bare `--force`, so a branch someone else
advanced is not overwritten. This is a rebase rule, not a general one: a branch
whose content is still changing waits for the local pass, because a red CI run
on a commit you already know is incomplete tells nobody anything.

## Core rules

- **Code must stay human-maintainable.** Every major file opens with an
  orientation header — what it owns, its entry points, and, for large files,
  a layout map anchored to greppable definition names, never line numbers.
  Update the header in the same change that moves what it describes. A value
  that must agree across several sites (a chunk size that a capacity
  computation and an emitter both depend on, a limit one file checks and
  another file exploits) is a named constant, not a respelled literal; a
  genuinely one-off format field keeps its literal with a comment naming it.
  Comments state constraints the code cannot show; a claim that has not been
  verified against the code does not get written down.
- **One return per function.** A function has a single exit: compute the
  answer into one result variable, let control flow converge, and `return` it
  once at the end. Guard clauses, mid-loop `return`s, and per-case `return`s in
  a `switch` all become assignments to that variable followed by ordinary
  structured flow — an `else`, a loop condition, a `break`. Prefer restructuring
  the condition over adding nesting; where an early exit skipped work that is
  now merely wasted, keep it skipped with a `done` flag or a loop guard rather
  than reintroducing a second `return`. A single exit is what makes an epilogue
  — a cleanup, a trace point, an arena reset — impossible to leak past. `goto`
  to a shared tail is a second exit spelled differently; it is allowed only for
  the error-unwind ladders that already exist, never as a way to keep an early
  return. Returns in mutually exclusive `#if`/`#elif`/`#else` arms are one
  return — every configuration compiles exactly one of them — so a
  platform-dispatch body already satisfies this rule and does not need a result
  variable threaded through the preprocessor. A `switch` that used to return a
  value per case becomes an undecorated `Type result;`, one `result = ...` per
  case, and `default: BUSTER_TODO();` for the impossible case: `BUSTER_TODO`
  never falls through, which is what leaves `result` definitely assigned without
  a placeholder initializer that a real bug could hide behind. Do not initialize
  the result variable when every path already assigns it: `clang_analyze` reports
  that store as dead and fails the Release tree, and the rule's whole point is
  that the paths, not an initializer, decide the answer.
- `BUSTER_F_DECL` belongs on header declarations only. In a `.c` file, a
  module-local function is `BUSTER_GLOBAL_LOCAL`, and a function that a header
  already declares carries no macro at all — the header declaration is what
  gives it internal linkage in the unity build.

## Machine instruction selection and scheduling

- `MachineInstruction` is the 24-byte hot row. Keep static form, scheduling,
  memory, bundle, fixed-register, tie, early-clobber, implicit-physical, and
  implicit-resource facts in `MachineOpcodeInfo`, accessed through the
  `machine_opcode_*` helpers.
- `MachineFunction` owns CFG edges, block parameters, and incoming edge
  parallel-copy sources. Edge source `i` maps to destination block parameter
  `i`; keep these copies parallel through allocation so cycles are resolved as
  copies rather than serialized selector moves. Replay files include all three
  arrays and use the current replay version.
- Shared canonical-IR facts and the generated FAST/QUALITY rule decision tree
  live in `machine_select.{c,h}`, `machine_select_rules.h`, and
  `machine_select_generated.c`. Target selectors may retain custom ABI and
  complex lowering, but must consume shared facts instead of introducing a
  third permanent graph IR.
- x86 ADD/SUB/AND/OR/XOR/IMUL rows are three-operand machine SSA with operand
  0 tied to operand 1. Allocators satisfy the physical two-address constraint;
  selectors must not reintroduce a MOV plus mutable USE_DEFINE result.
- QUALITY scheduling remains pressure-first and deterministic. Pressure is
  counted per register class; metadata supplies barriers, memory membership,
  and vector scheduling membership while compatibility opcode classifiers
  cover legacy rows during migration.
- `-fPIC` is a code model, not an accepted flag. It reaches code generation as
  `CodegenModuleOptions.position_independent`, and generation resolves it for
  the target: x86-64 ELF, where the relocations it changes are the ones `ld`
  refuses in a shared object. A symbol another object could interpose --
  `ir_symbol_is_interposable`, which is external or imported linkage without
  hidden visibility -- has its address loaded out of its GOT slot
  (`R_X86_64_GOTPCREL`) instead of computed rip-relative, and a direct call to
  one is relocated `R_X86_64_PLT32` so the linker may route it through a
  procedure linkage entry. Internal and hidden symbols keep the rip-relative
  form, and a thread-local address is the thread-local model's to pick --
  `codegen_thread_local_model` reads the same flag and answers general-dynamic
  under it. The canonical emitter and the machine path make
  the same decision from the same predicate: the selector writes a
  `MachineSymbolReference` beside each call-target row and the module
  relocation is derived from it, so the four allocators cannot disagree. One
  object-writer decision follows from the model rather than from a relocation:
  an unwind record's function pointer is relocated against a local text symbol
  with the function's own offset, because an FDE naming a preemptible function
  is the same PC-relative reference to an interposable symbol that `ld`
  refuses in the body.
- `-fPIE`/`-fpie` stay accepted and inert, and that is a statement rather than
  an omission: every reference this compiler emits is already rip-relative, an
  executable's own definitions are not interposable, its references to another
  image's data are what the linker's copy relocation is for, and its own
  thread-local block is still the initial one -- so the
  position-independent-executable model asks for no code this compiler does not
  already produce. `-fno-pic` clears the model; `-fno-pie` clears nothing
  because nothing was set.
- The built-in linker resolves both forms for the image it writes, which binds
  every name in it: `PLT32` patches the same rel32 `PC32` does, and a GOT load
  is relaxed back into the `lea` it would have been (`link_x86_relax_got_load`),
  the same relaxation `ld` performs for a `GOTPCRELX` it can resolve. The ELF
  reader takes `R_X86_64_GOTPCREL`, `GOTPCRELX` and `REX_GOTPCRELX` as one
  kind for that reason, so a `-fPIC` object -- this compiler's or clang's --
  links here. An instruction shape the relaxation does not recognize fails the
  link by name rather than being rewritten. It relaxes the two indirect
  thread-local models back to local-exec for the same reason
  (`link_elf_relax_thread_local`).

- **C only.** No C++, no exceptions. `-fwrapv`, `-fno-strict-aliasing`,
  `-funsigned-char`.
- **No third-party code.** External code was deliberately removed from the
  tree. Do not add dependencies or vendor libraries.
- **Recursion is forbidden unless it is trivial and statically bounded.**
  Recursive algorithms must represent recursion in data, using an explicit
  stack, queue, or worklist rather than the C call stack. Never use recursive
  calls when the depth depends on source input, runtime data, or another
  unbounded structure.
- **Compiler control flow must not use callbacks or function-pointer dispatch.**
  Use direct calls, loops, switches, and explicit work structures. Function
  pointer values may model the program being compiled, but the compiler itself
  must not execute through them. The one infrastructure exception is the
  uniform SPMD entry passed to `lane_run`; work inside that entry still uses
  direct control flow and an explicit index or range.
- **Parallelism target: SPMD across threads, SIMD within each thread.**
  Implementation code is written multi-core-by-default in the lane model of
  Ryan Fleury's raddebugger (`lane_run`, `lane_index`/`lane_count`,
  `lane_range`, `lane_sync`, `lane_broadcast` in `<buster/lib/os.h>`): every
  lane of a gang runs the same code, narrows serial work with
  `if (lane_index() == 0)`, and splits parallel work by `lane_range` or an
  atomic take-index — not through job systems, task queues, or callbacks.
  Lane-style code must degrade to serial: `BUSTER_SINGLE_THREADED` builds and
  one-lane gangs run the identical path with no threads and no separate serial
  variant. Within a lane, prefer wide data parallelism in the style of Daniel
  Lemire's simdjson kernels — process data in blocks, classify with tables and
  bitmasks, iterate set bits — over per-element branching, and shape data the
  way Casey Muratori advocates: flat index-linked arrays in arenas transformed
  by batch passes, not pointer graphs walked element-at-a-time. Wojciech
  Muła's notes (`http://0x80.pl/notesen.html`; fetch over plain HTTP — the
  host's HTTPS certificate is broken) are the project-endorsed catalogue of
  these kernel shapes: before writing a byte- or word-granularity loop, check
  whether one of his notes already solves it branchless with masks, `vpshufb`
  or `vpermb` lookups, `vpternlogd`, or compaction. Start from "Modern
  perfect hashing for strings", "SIMD-ized check which bytes are in a set",
  "AVX512VBMI — remove spaces from text", "AVX512VBMI2 and packed varuint
  format", "Parsing decimal numbers" parts 1–2, "SIMD-friendly algorithms
  for substring searching", and "AVX-512 conflict detection". Parallel
  stages must stay deterministic — write results into slots indexed by work
  item, never by completion order — so the self-hosting fixed point stays
  byte-identical at any lane count. `lane_run` keeps a persistent worker gang
  on the calling thread context and reuses it across phases; do not add phase-
  local thread creation. Variable-duration work uses an atomic take-index,
  writes function- or item-local fragments into stable source-indexed slots,
  and merges them only after a lane barrier.
- **A global built on first use is prewarmed, never raced.** Several hot
  tables are derived once and read forever after through plain loads — the C
  frontend's character classes, compact lexer tables, punctuator dispatch and
  declaration keywords, the codegen ABI target cache, the font provider's
  resolved paths, and the x86 metadata decode plus the three caches over it.
  That shape is sound only while no other thread can be reading, so every such
  build states `BUSTER_CHECK_SERIAL_INITIALIZATION()` (`<buster/lib/os.h>`,
  always compiled in, over `os_is_only_live_thread()`), and every owning
  module publishes a prewarm entry point that fills it on the calling thread:
  `c_prewarm`, `codegen_prewarm`, `font_provider_prewarm`,
  `buster_x86_metadata_prewarm`, and `compiler_prewarm` for the compile
  pipeline as a whole. **Call the prewarm
  before `lane_run`** — x86 metadata separately, since no part of the compile
  path queries it and its prewarm costs a full table decode. A new lazily
  built global adds both the check and a line in its module's prewarm.
  Publish the flag *after* the state, never before: the x86 metadata decode
  needs two flags for this, one guarding re-entry from the validation it runs
  and one, set last, that every accessor tests. Spelling the character-class
  tables as constant initializers over a predicate macro would remove four of
  these outright, and was measured at **+178.8 M stage-1 instructions
  (+3.5%)** for 112 k extra preprocessed tokens — one predicate expansion per
  byte value per table. A real generator writing the bytes into source would
  not cost that; the preprocessor doing it at every compile does.
- **Microarchitecture tuning target: design for Zen 5's native 512-bit
  width; Zen 4 must break even, Zen 5 collects the upside.** The main
  development machine is a Ryzen 9 7940HS (Zen 4) and the CI x86-64 runners
  cover both generations (the Windows runner is a Zen 5 box), so
  single-thread throughput decisions — SIMD width, table sizes,
  branch-vs-cmov trades — are made for and measured on these cores.
  GNU-family builds compile with `-march=native`, so clang-built binaries on
  these hosts already have AVX-512 (VL/BW/DQ/VBMI/VBMI2) available. Write
  kernels at full 512-bit width: Zen 4 executes them double-pumped over its
  256-bit datapaths at AVX2-equivalent bytes per cycle with fewer
  instructions retired — no downclocking, no penalty, just no width upside —
  while Zen 5's native 512-bit units roughly double the same code's
  throughput. A kernel therefore has to justify itself at Zen 4's effective
  width and is validated there, but its shape (masks, compaction, permutes)
  is chosen for Zen 5. Watch the exceptions on Zen 4: the few instructions
  whose 512-bit form costs more than 2x the 256-bit form (check uops.info
  before leaning on exotic two-source permutes; `vpcompressb` is safe at
  ~9 cycles Zen 4 / ~5 cycles Zen 5), store-throughput-bound kernels (one
  256-bit store per cycle on Zen 4), and unaligned 64-byte loads — keep hot
  streamed buffers 64-byte aligned. Explicit intrinsics must stay behind
  feature/compiler guards with a scalar or SWAR fallback: MSVC builds carry
  no `-march`, aarch64 (macOS/Android/iOS) must keep building, and the
  self-hosted `ide cc` stages compile the same tree without vendor headers —
  performance is only ever quoted from clang-built binaries, so fallback
  paths need correctness, not speed.
- **Write 512-bit kernels in the vocabulary of `<buster/lib/simd.h>`, not in
  `<immintrin.h>`.** That header is a target-fixed list of AVX-512 operations
  — masked 512-bit loads and stores, byte comparisons producing a `Mask64`,
  `vpermt2b`, `vpcompressb` and its compacting store, byte-to-word widening,
  `vpternlogd`, lanewise arithmetic — with three implementations behind one
  spelling: `__builtin_buster_simd_*` for the self-hosted stages, host
  intrinsics for clang and gcc, and a scalar fallback for MSVC, AArch64 and
  pre-AVX-512 x86. `BUSTER_SIMD_512` says which one is in play; guard on it
  only where a *different algorithm* is worth writing, never merely to keep
  the tree building. It is deliberately not a portability layer — every
  operation names one instruction, and an abstraction wide enough to also
  describe NEON would lose exactly the operations that make these kernels
  fast. Adding to it means adding an `IrSimdOperation`, its arity in
  `ir_simd_operation_shape`, its validation, its EVEX lowering in
  `codegen_canonical_x64_simd_operation`, the builtin in `c_ir_simd_builtins`,
  its spelling in `c_symbol_predefined` (`c_source.c` — without it the
  self-hosted stages fail with "could not lower unbound identifier"),
  a fallback, and a case in `tests/basic_c_simd.c`; the shape table is the
  single source of truth that keeps a new operation from being half-taught to
  the pipeline. Two consequences are worth knowing before writing a kernel.
  **Everything in the header is a macro**, because `ide cc` lowers directly
  and runs no inliner — not even for `always_inline` — so a function wrapper
  would be a real call per SIMD operation in the self-hosted stages;
  arguments must therefore be free of side effects. A `Simd512` may still
  cross a call boundary by value where that is what the code wants: SystemV
  passes and returns it in a vector register and spills past the eighth, and
  a target whose vector registers are narrower than the value says so with
  `CODEGEN_ERROR_UNSUPPORTED_ABI` rather than encoding a register it does not
  have. And **masks are `u64`, not an opaque k-register handle**: the
  canonical backend allocates no registers, so a mask spills to its frame slot
  either way, the vector instructions pick it back up with a single `kmovq`,
  and mask shifts, Boolean combinations, `mask64_count` and `mask64_first_set`
  stay on the general-purpose ALUs, which retire more of them per cycle on
  Zen 4/5 than the k unit does.
- **SIMD C lexing method: the Validark lineage.** `c_lex_compact` in
  `frontend/c/c_source.c` draws on Niles Salter's (Validark's) Accelerated Zig
  Parser — local checkout `~/dev/Accelerated-Zig-Parser`, upstream
  `github.com/Validark/Accelerated-Zig-Parser` — plus validark.dev (start with
  `posts/deus-lex-machina` and
  `posts/eine-kleine-vectorized-classification`) and the three Utah Zig talks
  (YouTube `oN8LDpWuPWw`, `FDiUKafPs0U`, `NM1FNB5nagk`). When accelerating the
  C lexer, reach for these techniques first and keep new code compatible with
  their shapes. The core vocabulary: (1) per-64-byte-chunk classification —
  every class (whitespace, newline/CR, alpha/digit/underscore, quotes,
  backslash, slash, operator chars) becomes one comparison into a `k`-mask
  bitstring, all classes over the same chunk in lockstep sharing one load; (2)
  token extents by mask arithmetic — starts `x & ~(x << 1)`, ends
  `x & ~(x >> 1)`, then either cursor queries (shift by cursor, count trailing
  ones — one tzcnt replaces an unpredictable byte loop) or full vector
  compaction: `vpcompressb` an iota vector through the starts and ends masks,
  subtract, and every token length in the chunk materializes at once; kinds
  come from masked broadcasts into a kinds vector compressed by the same
  starts mask, interleaved with lengths on store; (3) charset membership via
  nibble-decomposed `vpshufb`/`vpermb` tables —
  `table[c & 0xF] & (1 << (c >> 4))` per lane, the upper-nibble powers-of-two
  vector shared across charsets, `vptestmb` folding the AND and test on
  AVX-512; (4) multi-character operators as a bit-channel `vpshufb` NFA: three
  per-position tables whose looked-up bytes AND together so each of the 8
  bit-channels legalizes one family of 2-/3-char sequences, plus a short
  effectively-branchless reconciliation of overlaps; (5) keyword and builtin
  recognition by perfect hash, never a memcmp ladder —
  `((len << 14) ^ first_two_bytes) * last_two_bytes >> 8` to 7 bits, Bagwell
  array-mapped compression (two u64 bitmaps + popcount rank) into a dense table
  of length-padded entries validated by a single wide compare, with
  compile/startup-time collision checks so editing the keyword set stays safe;
  (6) sentinels instead of bounds checks — a leading newline, trailing
  quote/NUL sentinels, and chunk-aligned overallocation let the hot loops drop
  every length test; (7) upper-bound allocation plus post-scan shrink instead
  of grow-and-check (the `2026-08-08k` tokenizer change is this trick alone,
  `-21.6%` bench instructions); (8) length-based token streams — kind + length
  with a 0-length escape to a wide length, no per-token start offsets or
  eagerly materialized line/column, offsets rebuilt by a running cursor and
  line numbers recovered by popcount over retained newline bitmasks.
  Escape-run parity uses the simdjson backslash algorithm (simdjson PR #2042
  has the current best form). All of it sits behind the guard rules of the
  tuning target above; scalar fallbacks keep the exact current semantics.
  `c_lex_compact` walks source in **item-aligned 64-byte windows** so no lexer
  state crosses a window: the item touching a window's last byte is deferred
  and rescanned by the next one, and shapes the masks do not model escape to
  the scalar single-item scanner `c_lex_scan_one`. C skips whitespace and
  comments instead of tokenizing them, so token ends need an explicit boundary
  mask rather than `starts >> 1`; lookahead loads are masked by the **file**
  bounds rather than the window's because a delimiter near the window end can
  be spelled from bytes the next window owns. Every SIMD lexer change must keep
  the differential gate asserting byte-identical agreement with the scalar
  reference over construct cases slid across the window boundary, items longer
  than a window, the real corpus at every window phase, and fuzz blobs over the
  full alphabet.
- **Warnings are errors** under a very large warning set (see
  `GNU_FAMILY_WARNINGS` in `CMakeLists.txt`), and code must stay clean under
  Clang, GCC, Zig cc, and MSVC. TCC is required only to compile/bootstrap the
  native `build.c` driver through `build.sh`/`build.ps1`; application code is
  not promised to compile with TCC. Avoid compiler-specific extensions unless
  guarded.
- **Idioms**: arena allocation (`<buster/lib/arena.h>`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `<buster/lib/base.h>`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Prefer function headers, declarations, statements, and
  similar constructs on one line; split them only when doing so is clearer.
  Match the surrounding file.
- Test implementations are not registered as modules; add new test pairs under
  `src/buster/tests/`, add
  the implementation to `BUSTER_TEST_SOURCES` and the header to
  `BUSTER_TEST_HEADERS` in `CMakeLists.txt`, and include both from
  `src/buster/tests/test.c` in the existing registration order (the
  implementation only in the `BUSTER_UNITY_BUILD` block).
- **Adding a module** (`foo.c`/`foo.h` under `src/buster/lib/`) takes three
  edits: (1) `buster_register_module(foo ...)` in `CMakeLists.txt`;
  (2) add `foo` to the `MODULES` list of `buster_add_executable(ide ...)`;
  (3) add `#include <buster/lib/foo.c>` to the `BUSTER_UNITY_BUILD` block at
  the top of `src/buster/apps/ide/ide.c` — optimized non-sanitized configs compile as
  unity builds, so forgetting this breaks ordinary Release builds.
- Headers are included as `<buster/lib/...>` or `<buster/tests/...>` (include
  root is `src/`).
  `compile_commands.json` is exported to `build/` by default.

## Platform and backend boundaries

- In rendering and windowing, keep platform-neutral policy and data flow in
  the module front door (`rendering.c`, `window.c`). Native API calls
  belong in the selected backend implementation.
- Rendering backends live in `src/buster/lib/rendering/*.c`; window backends
  live in `src/buster/lib/window/*.c`. These are implementation files
  included by their owning module, not standalone CMake modules, so do not
  register them or add them to the unity-build include list.
- Every backend `.c` includes its directory's `internal.h` before its
  implementation declarations. Those private headers own the shared types,
  helper declarations, and native headers needed for clangd to parse a backend
  independently; do not depend on declarations only earlier in the owning `.c`.
- Share CPU-side draw generation, font/texture orchestration, event-list
  ownership, and lifecycle policy. Keep device resources, synchronization,
  swapchains, native event translation, and native handles backend-specific.
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.

## C frontend and canonical IR rules

- The public frontend API is `compiler/frontend/c/c.h`. In non-unity builds the
  implementation is split across `c_source.c`, `c_parse.c`, and `c_gen.c`;
  `c.c` preserves the unity include order and diagnostic mapping.
- Keep the frontend pipeline explicit: source loading and preprocessing,
  parsing and semantic construction, then canonical-IR lowering. Do not add a
  parallel frontend-specific IR or route code generation around canonical IR.
- Invalid user input must produce structured C diagnostics and a failed driver
  result. Assertions and `BUSTER_TODO()` are for violated internal invariants,
  never ordinary syntax or semantic errors.
- Arena ownership is part of the API contract. Returned source, syntax,
  semantic, and IR structures may reference earlier-stage storage; callers must
  retain the translation-unit arena until every downstream consumer finishes.
- Zero-initialize aggregate tables before publishing a partially resolved type.
  Recursive and mutually dependent declarations can expose an aggregate while
  later members are still unresolved; an uninitialized `IrField` must never be
  mistaken for a valid type or source reference.
- Canonical IR owns format-neutral functions, blocks, instructions, values,
  symbols, source ranges, types, and relocations. Shared layers must not contain
  frontend entity IDs, parser AST pointers, or language-specific invalid
  sentinels.
- Validate IR before machine selection or Wasm emission. A diagnosed frontend
  failure must not publish an apparently valid partial function to codegen.
- **GNU's `__alignof__` takes an expression; `_Alignof` takes only a type
  name.** Both spellings reach the same fold in `c_gen.c`, and it resolved an
  expression operand only for a compound literal until libc-test's
  `tls_align_dso.c` reached it: the file fills a table with `__alignof__(x)`
  over four `__thread` objects, and every other shape refused with "could not
  lower logical expression core". The operand now goes through the resolver
  `sizeof v` already used, which answers the alignment of the operand's **own
  type** -- `__alignof__(arr)` over a `char[7]` is 1, not the 8 the expression
  type prediction would give the pointer it decays to in any other context.
  The prediction is still the last resort for both words, under the same
  guards: an inline aggregate definition and an object whose array type never
  mapped are refused rather than guessed at. `tests/basic_c_alignof_expression.c`
  is the fixture, and every value in it was compared against clang.
- **`void` is one byte, and an object of it is still refused.** GNU gives
  `void` a size and an alignment of one so that arithmetic on a `void *` steps
  by bytes, and clang and gcc both fold `sizeof(void)`, `sizeof(const void)`
  and `_Alignof(void)` to 1. This compiler folded 0, and the index that `p + 3`
  becomes is scaled by the pointee's layout size, so the pointer did not move
  at all -- a silently wrong address rather than a diagnostic, and `q - p` was
  refused outright because the divide by the element size would have been a
  divide by zero (#743). The answer lives in `c_parse_builtin_type_layout`,
  which is the one table **both layout engines** read: `c_parse_type_layout`
  folds `sizeof` through it during the parse and `c_ir_scalar_type` builds the
  `IrType` from it, so there is no second place to keep in step. Nothing
  downstream had to change, because every backend already scales an
  `IR_OPCODE_INDEX` by the element type's size and every question about `void`
  that is *not* its size is asked of `IR_TYPE_VOID` rather than of a zero.
  The size is an extension for that arithmetic and for `sizeof`, **not a
  licence to declare a `void` object**: C 6.7p7 wants a complete type and both
  reference compilers refuse `void v;`, `void a[4];` and a `void` member. Those
  refusals used to fall out of the zero size -- an aggregate whose layout never
  resolved, a local whose alignment was zero, a file-scope object that reached
  code generation and failed there naming `main` rather than the object -- so
  they are asked of the kind now, through `c_ir_type_is_void_object` in
  `c_gen.c`, at the two local-declaration sites, the global definition walk,
  and the aggregate member layout, which reports through the same
  one-per-type slot a rejected alignment specifier uses. The predicate
  descends array elements alone: a qualified copy keeps the base's kind, so
  `const void` answers the same, and a `void *` is a pointer and answers no.
  `tests/basic_c_void_size.c` pins every runtime answer under all four
  register allocators -- both orders of the addition, the two subtractions,
  `++`/`--`/`+=`/`-=`, and the qualified pointees -- reading each stepped
  pointer back through a live object so an address that folds correctly and
  lowers wrongly still fails; `c_test_void_object_refusals` pins both layout
  engines' number and the four refusals.
- An integer converted to a pointer reaches pointer width in the frontend,
  before `IR_CONVERSION_INTEGER_TO_POINTER`, sign-extending when the operand is
  signed. All four backends lower that conversion as a plain register copy and
  LLVM's own `inttoptr` zero-extends, so a narrower operand that arrives
  un-widened silently loses its top half: `(void *)-1` — musl's `MAP_FAILED` —
  came out as `0x00000000ffffffff` and hung four libc-test units for as long as
  it was expressible. `c_ir_emit_integer_to_pointer` is the one place that
  performs it, a null pointer constant is built at pointer width so it costs no
  instruction there, and `ir_canonical_conversion_valid` rejects a
  non-pointer-width operand outright. `tests/basic_c_integer_to_pointer.c`
  pins the answers under every allocator.
- `main` is the one function whose fall-off is defined: reaching the `}` that
  terminates it returns 0 (C 5.1.2.2.3), where every other non-void function's
  is undefined (C 6.9.1p12) and terminates with `IR_OPCODE_UNREACHABLE` — the
  `ud2` Clang and GCC also emit. `CIrSignature.returns_zero_at_end` decides it
  once, with the signature, from a file-scope declaration named `main` whose
  return type is `int`; the terminator does not match a name. Getting it wrong
  is silent until the program ends: `regression/sem_close-unmap` and
  `functional/mntent` in libc-test have no return statement anywhere and both
  ran correctly to their last statement before dying on the brace with SIGILL.
  `tests/basic_c_main_implicit_return.c` pins it under every allocator, and
  exit zero is reachable there only through the closing brace.
- `builder->size_type` and `builder->ptrdiff_type` are chosen against the width
  of the scalar type the lowering built, not against `program->data_layout`'s
  own `unsigned long` entry. The two can disagree: the layout comes from the
  *preprocess* result and the scalar types come from `c_lower_to_ir`'s `target`
  argument, so a caller that preprocesses with one target and lowers with
  another — every frontend test does, preprocessing with a default target —
  otherwise gets a `size_t` narrower than a pointer on an LLP64 host. Nothing
  noticed while that only reached arithmetic; the widening above notices
  immediately, which is how a platform-independent defect first showed up as
  one platform's CI failure.
  `c_test_pointer_width_integer_conversion` lowers the same source for an LLP64
  target and an LP64 one and checks both.
- `IrFunction.opcode_summary` answers *may this function contain opcode X*
  without a scan, and it answers only for the `IR_OPCODE_SUMMARY_TRACKED`
  list: an opcode outside that list is never recorded, so
  `ir_function_may_contain_opcodes` rejects a query naming one rather than
  reporting a confident absence. Adding a query means adding its opcode to
  the list in the same change. The summary is exact only for functions
  created by `ir_module_add_function` and filled by
  `ir_function_add_instruction`; IR whose rows were written straight into
  `instructions` reads as unknown and every consumer keeps a scan for it.
- Source diagnostics in shared layers use canonical `IrSourceRange` and
  `IrSourcePosition`. Do not reintroduce parser-specific source-range APIs into
  codegen, debug information, object writing, or the linker.
- A value defined by `IR_OPCODE_GLOBAL` is a *place*: its frame slot holds the
  object's address, so it is an eightbyte whether or not the object's type has
  a resolved layout. That is what lets C's `extern struct opaque object;` be
  addressed without ever being completed, and musl's `src/include/stdio.h`
  depends on it — it suppresses the definition of `struct _IO_FILE` and then
  declares `extern FILE __stderr_FILE`, so every `stderr` in the tree takes
  the address of an incomplete object. Codegen's two value-sizing loops
  therefore require a resolved layout of every value *except* a global place;
  requiring it of all of them cost eight musl units with an
  `INVALID_IR` blamed on whichever function happened to be first in the
  module.
- **An array lvalue is never loaded**, whichever expression names it. A named
  array keeps its place, and so does `*p` on a pointer to an array: C 6.5.3.2p4
  says the dereference designates the array object, and what follows either
  decays it or indexes it in place, neither of which reads anything. Loading
  one instead emits an `IR_OPCODE_LOAD` of the array type, which the code
  generator honours by copying the whole object into a frame temporary — so
  `(*p)[1] = v` indexed the copy and the store was dropped with no diagnostic
  (#719), while `p[0][1] = v` next to it worked. The expression walk in
  `c_gen.c` returns the place from its dereference arm for the same reason its
  address-of arm accepts one; `tests/basic_c_packed_layout.c` runs all three
  spellings of the store and `c_test_frontend_global_types` pins that no lowered
  function holds a load of array type. The decay is the half a libc trips
  over: musl's strftime writes into `*s` through `snprintf` and returns it from
  a `char (*s)[100]` parameter, so the copy took every formatted specifier with
  it and libc-test's `functional/strftime` failed all 64 of its checks.
  `tests/basic_c_pointer_to_array_place.c` pins that shape under all four
  register allocators.
- **An aggregate member the next `.` walks through is a place, not a value.**
  `((T *)p)->a.b` names b, and C 6.5.2.3 gives the route to it no read of its
  own. The expression walk in `c_gen.c` loaded `a` anyway and then recovered
  the place it needed from that load's own operand, so every answer was right
  and the copy was dead — but a dead copy is still a read of memory, and
  offsetof is written on the null pointer. A compiler that does not advertise
  `__builtin_offsetof` gets musl's other spelling,
  `((size_t)( (char *)&(((type *)0)->member) - (char *)0 ))`, so a member named
  through two accesses copied a whole object out of address zero. musl's
  `__pthread_exit` takes exactly that offset for every mutex on the robust list
  — `_m_next` is `__u.__p[4]` — and died there with SIGSEGV, which is what
  libc-test's `functional/pthread_robust` and `regression/pthread-robust-detach`
  reported as #737. Predefining `__GNUC__` in every dialect moved musl to
  `__builtin_offsetof` and closed both units before this rule landed, so the
  spelling that reaches the walk is now a program's own rather than a libc's:
  measured 2026-08-30, the libc-test classification is identical with and
  without this rule. The member arm keeps the place when the following token
  is a `.`, which is the same rule as the array arm beside it; a chain that
  ends at the aggregate still loads it, so a by-value read is unchanged.
  `tests/basic_c_member_chain_place.c` pins the offsets against a live object
  under all four register allocators, spelling the pointer form directly so it
  does not depend on which offsetof a header picks, and faults the way musl did
  if the copy comes back. The peek only sees the token after the member
  identifier, so a group hides the `.` that follows from it — `(*o).a.b` and
  `&(((T *)0)->a).b[i]` reach the next access with the load already emitted,
  the dereference arm having emitted the first one and the member arm the
  second (#741). The place `c_ir_emit_field_place_from_value` recovers from
  such a load therefore *drops* it, through the same
  `c_ir_recover_place_from_value` the address-of arm of `c_ir_apply_operation`
  uses for `&E`: the load must still be the last instruction emitted, which it
  is because the access that recovers from it is the next thing the walk does.
  A group is not a frame of its own — an ordinary one is a `C_CONDITIONAL_OPEN`
  marker on the same expression frame's operator stack — so nothing about this
  needs a place-or-value request threaded through the lowering machines.
  `c_test_frontend_global_types` pins that no lowered function holds a load of
  a struct or union type nothing reads, and that the by-value read beside it
  keeps the one it needs.
- **A value never carries a qualifier.** The frontend builds a qualified copy
  of a type wherever a qualifier is written, because a place, a pointee or a
  member has to carry it, and that copy keeps the base's kind and layout: it is
  one representation under two ids. Both load emitters and the store emitter
  therefore unqualify a `volatile` place the way they already unqualified
  `_Atomic` -- an lvalue conversion yields the unqualified type of the object
  (C 6.3.2.1p2) and an assignment converts to the unqualified type of the left
  operand (C 6.5.16.1p2) -- and `c_ir_emit_cast` treats a difference of only
  `volatile` as no conversion at all. The value ladder there spans the scalar
  kinds, so while a struct crossing the qualifier had to find an arm in it,
  `volatile sigset_t oldset = set2` was refused outright and every unit written
  around `setjmp` failed to compile (#735). Volatility itself never travelled on
  the type: a load and a store carry `volatile_access`, taken from the place's
  own flag, which is why none of this changes which accesses are volatile.
  `ir_types_differ_only_in_volatile` is the one predicate, and
  `ir_validate_canonical_module` admits exactly that difference between a plain
  `IR_OPCODE_LOAD` or `IR_OPCODE_STORE` and its place -- the pairing the atomic
  opcodes were always validated with. `tests/basic_c_volatile_aggregate.c` pins
  both directions of the qualifier under all four register allocators.
- Native lowering is `canonical IR -> machine IR -> scheduling/register
  allocation -> encoding`. Selection patterns and scheduling classes remain
  separate metadata domains even when they share instruction-form IDs.
- **A read-only object that carries a relocation is laid out with the writable
  data.** `const` is the frontend's answer and the object writer's read-only
  section is where it usually goes, but those bytes are written when the
  program is relocated, and in a shared object that write lands on a page the
  loader mapped read-only — a `DT_TEXTREL` the loader has to undo before it can
  process the relocation, and one musl's loader does not undo for the file it
  was itself started from. Clang answers the same question with a fourth data
  section, `.data.rel.ro`; this writer has three, so
  `codegen_global_is_read_only` in `codegen.c` folds "has a relocation" into
  the placement instead. Nothing in the C object model moves with it — writing
  through a `const` lvalue is undefined either way — and a static link, whose
  relocations are all resolved before the program runs, cannot tell the
  difference. `static const unsigned short *const ptable = table+128;` in
  musl's `__ctype_b_loc.c` is the shape, and `FILE *const stdout` is the one
  every program has.
- **`__attribute__((weak))` and `__attribute__((alias("target")))`** reach the
  object file, because musl publishes `malloc`, `free`, `errno` and most of
  its pthread surface as weak aliases of internal names. Weak is
  `IrSymbol.is_weak` and becomes `ObjectSymbol.weak`, which ELF writes as
  `STB_WEAK` and Mach-O as `N_WEAK_DEF`. COFF spells a weak definition as a
  selectany COMDAT, which needs a section per symbol while this model merges
  sections by kind, so a COFF object reads `weak` back but cannot write it and
  carries such a symbol as an ordinary external. That is the one gap of the
  three formats, and it predates aliases: `object.c`'s header states it. An
  alias is a pair in `IrModule.aliases` rather than a field on every symbol:
  it is a relation between two symbols rather than a property of one, and
  nearly every module has none. The object writer gives
  the alias its target's section, offset, size and kind and only its own
  binding, which is what Clang produces for the same source, and
  `ir_validate_canonical_module` requires the target to be a definition in
  that same module. The frontend keeps a static alias target alive -- an
  attribute names it, no identifier use does -- and diagnoses an alias whose
  target this translation unit does not define rather than emitting an
  import. Both attributes are read inside the declaration's
  `__attribute__((...))` list by `c_declaration_binding` in `c_gen.c`, never
  anywhere in its token range the way `section` and `asm` are matched: a
  marker attribute has no argument shape to recognise it by, and `weak` and
  `alias` are ordinary identifiers, so `int weak;` must stay a strong
  definition.
- **`__attribute__((constructor))` and `__attribute__((destructor))`** run a
  function before and after `main`. They are read out of the declaration's
  attribute list by the same `c_declaration_binding` walk as `weak` and
  `alias`, with the reserved `__constructor__`/`__destructor__` spellings
  beside the plain ones and the optional `constructor(101)` priority; the
  attribute may be written on any declaration of the function, so the flag is
  collected per entity the way `noreturn` is. Three consequences follow, and
  each was a separate hole before issue 771 closed them. A registered function
  is **reachable by definition** -- no expression names it -- so it is a root
  of the unused-static elimination in `c_gen.c`, without which a translation
  unit whose only function is a static constructor came out with an empty
  `.text`. The registration is a module-level list, `IrModule.initializers`,
  for the reason aliases are: it is a relation, not a property of a symbol,
  and nearly every module has none. And the two targets with no initializer
  array at all -- core Wasm, which starts one function of its own, and eBPF,
  which has no startup -- **diagnose** the attribute rather than dropping it.
- **`.init_array` and `.fini_array` are section kinds**,
  `OBJECT_SECTION_INIT_ARRAY` and `OBJECT_SECTION_FINI_ARRAY`, holding one
  pointer-wide slot per initializer with an `ABSOLUTE64` relocation against
  the function. ELF writes them `SHT_INIT_ARRAY`/`SHT_FINI_ARRAY`, Mach-O
  `__DATA,__mod_init_func`/`__mod_term_func` with the `S_MOD_*_FUNC_POINTERS`
  types, and COFF keeps this model's neutral names the way `.rodata` and
  `.tdata` already do. **Priority orders the whole program**, and the priority
  travels beside the array rather than in it. `ld` gets GNU's order off the
  section name -- every `.init_array.NNNNN` ahead of the unsuffixed
  `.init_array`, ascending -- and this model has one section per kind, so a
  translation unit's whole array is one section, its entries sorted into that
  order by `object_from_canonical_codegen_module` (ascending priority, an
  attribute that named none last, equal priorities in declaration order) and
  each entry's priority recorded beside it in
  `ObjectFile.initializer_priorities`, one `u32` per slot. Three places carry
  that array, and they are the same fact from different sides.
  `object_write_elf64` splits it back into one `.init_array.NNNNN` section per
  priority group, so an external linker orders two Buster objects exactly as
  it orders Clang's (issue #782). `object_read_elf64` recovers it from those
  section names -- the padded `.init_array.00101` written here and the
  unpadded `.init_array.101` Clang writes are both read -- and merges the
  sections of a kind in `ld`'s order rather than in section header order. And
  `link_initializer_arrays_order` sorts the *merged* array, stably, after
  `link_objects` has concatenated its inputs: without it a `constructor(101)`
  in the second object ran after an unprioritized constructor in the first,
  for Clang's objects as much as for this compiler's, which was issue #789.
  That sort moves each entry's relocation and any symbol defined at its slot
  with the entry, and it runs on the merged object rather than in
  `link_initializer_plan_build` so the Mach-O writer -- which keeps the arrays
  for dyld to walk instead of reading a plan -- gets the same order the
  entry-stub writers do. An input that states no priorities (the COFF and
  Mach-O readers, the assembler's objects) has every entry unprioritized,
  which leaves it in link order.
- **An image this linker produces calls its initializers from the entry
  stub.** There is no libc startup object in it -- the stub *is* the startup,
  which is why `link_x86_build_elf_entry_stub` exists at all -- so nothing
  would walk the arrays, and the linker already knows every entry's target at
  layout time. `link_initializer_plan_build` reads the two sections into a
  plan and hands back the object with them removed; each writer then emits one
  direct call per constructor before `main`, patched exactly like the call to
  `main` beside them. ELF passes argc, argv and envp to each constructor as
  GNU does; PE passes nothing, which is MSVC's `.CRT$XCU` contract and all
  that is available before the argv machinery runs. The **destructors** are
  not called where `main` came back: a program that reaches `exit` from inside
  `main` never comes back, and GNU runs a destructor either way, so the hosted
  stubs synthesize a **runner** past the trap that ends the stub -- one call
  per destructor and a return -- and register it with the C runtime before the
  constructors run. That is the position `__libc_start_main` hands `_dl_fini`
  to `__cxa_atexit` from, and it is what leaves the runner behind every
  handler the program registered itself (issue 781). The registration is
  `__cxa_atexit` on ELF and `_crt_atexit` on PE, because glibc's `libc.so.6`
  and Windows' `ucrtbase.dll` both keep plain `atexit` in a static library
  this linker does not read; `link_elf_hosted_exit_symbol` appends the ELF
  import beside `exit`, and both ELF dynamic writers have to agree on whether
  it is there because the AArch64 one re-derives the x86-64 one's import
  numbering. The freestanding ELF shape keeps its destructors inline after
  `main`: it has no `exit` to call and no runtime to register with, and a
  `-nostdlib` program that reaches the raw exit syscall runs no handler
  either. Two writers synthesize no entry point of their own, and they answer
  differently. **Mach-O** needs none: LC_MAIN hands `main` straight to dyld,
  which runs the main executable's `__DATA,__mod_init_func` before it enters
  `main` and its `__mod_term_func` in reverse on the way out, so that writer
  keeps both arrays, gives each the section type dyld dispatches on, and lets
  the loader call them (issue 779) -- which is why the merged array has to be
  in GNU's order before any writer sees it (issue 789), rather than only in
  the plan the other writers read. **UEFI** has no such third party -- a
  firmware image has no C runtime, and its entry is the firmware's call with
  the image handle and the system table -- so it still **refuses** a program
  with initializers, naming the first one, rather than placing an array
  nothing will call; `link_initializer_plan_empty` is that refusal. The
  objects it produces are correct and link through the system linker.
- **An undefined weak symbol resolves to address zero**, which is what a
  program asks for by declaring one: musl's startup takes the address of a
  weak hidden `_DYNAMIC` and reads zero to learn it is static. Which party
  answers is the question of whether the reference can be preempted, and the
  ELF writers decide it with `link_elf_symbol_resolves_to_zero` in `link.c`:
  a hidden reference, and any reference in a static image, is relocated
  against zero here, while a default-visibility one in a dynamic image stays
  an import entered in `.dynsym` as `STB_WEAK`, so the loader binds it when a
  shared library defines it and leaves it zero rather than refusing the image
  when none does. `link_elf_symbol_needs_dynamic_import` is the same question
  asked by the three places that choose between the static and dynamic
  writers and by the two that number imports; they must not disagree, because
  the AArch64 dynamic writer re-derives the x86-64 writer's import numbering.
  Merging inputs follows ELF: a symbol only references name is weak while
  every one of them is, and one hidden occurrence makes it hidden.
  A default-visibility reference is promoted to an import **only when a
  library the image names is known to define the name** with a default
  version, which is the same question `link_elf_symbol_version` answers for
  the version to record; asking the loader for a name nothing has is how such
  a reference came back as its own PLT thunk or copy slot. What knows is
  `compiler_driver_elf_dynamic_symbols`, which records every defined global
  and weak entry of every shared library on every hosted ELF link, functions
  included, and sets `exports_known`: `versioned_symbols` is the ELF export
  list, `exported_symbols` stays PE's. Absence is evidence only when **every**
  library was read — `link_elf_exports_complete` — because a library the
  driver could not open exports whatever it happens to export; a link missing
  one of them keeps the import it made before, which is also why a target
  whose libraries are never read, Android today, is unchanged.
- **A C library keeps some of its own names out of its shared object**, and
  this linker imports from the shared object alone, so it has to supply the
  rest itself. glibc puts `atexit` and `at_quick_exit` in libc_nonshared.a as
  one call apiece to `__cxa_atexit` and `__cxa_at_quick_exit`; UCRT puts the
  same two names in its import library as one call apiece to `_crt_atexit` and
  `_crt_at_quick_exit`. `link_elf_libc_runtime_object` and
  `link_windows_libc_runtime_object` build those stubs — weak, so a program's
  own definition wins, and a bare tail branch, so the C ABI hands the
  arguments through — over the shared `link_forwarding_runtime_object`. The
  driver adds either one **the way it selects an archive member**
  (`compiler_driver_archive_member_needed`): only when something references a
  stub and nothing defines it. That selection is the contract, not a
  refinement of it. The stub carries an undefined `__cxa_`/`_crt_` reference,
  so adding it unconditionally would put that import in every executable, and
  on a host with no readable `ucrtbase.dll` it would fail every Windows link
  outright. `link_windows_runtime_object` is the counterexample that has to
  stay separate: `_fltused` is a four-byte marker with no reference of its
  own, so it is added to every hosted Windows link unconditionally.
  `_onexit` is deliberately absent — it answers with the handler rather than
  with a status, so it cannot be a tail branch, and a stub that called and
  then chose would need Windows unwind data of its own.
- **`__attribute__((packed))` and `__attribute__((aligned(N)))`** decide object
  representation, so ignoring them is an ABI divergence rather than a missing
  optimization: a Buster-only program agrees with itself whatever it agrees on,
  and the disagreement only appears against another compiler's object or an
  offset a program computes by hand. `packed` and the aggregate's own
  `aligned` live in `CParseResult.aggregate_attributes`, a side table keyed by
  type index rather than a field on every `CType`: the population is a handful
  of aggregates against tens of thousands of types. A member's `packed` is a
  bit on `CMember`; an object declarator's `aligned` joins the specifier-level
  alignment specifiers in the one contiguous run `alignment_start`/
  `alignment_count` names, which is why the trailing scan runs immediately
  after the specifier one. `#pragma pack(N)` asks the same question -- the
  ceiling a member's alignment is clamped to -- and `packed` is that ceiling at
  one byte, so both feed one knob. **Two layout engines read it and must agree**:
  `c_parse_type_layout` in `c_parse.c` folds `sizeof`/`_Alignof` during the
  parse and `c_lower_to_ir` in `c_gen.c` builds the `IrType`. They disagreed
  about `#pragma pack` before this: the fold packed and the IR did not, so a
  folded size contradicted the object it sized.
  A packed bit-field takes the next bit rather than the next storage unit of
  its declared type, which is what Clang and GCC do and what makes
  `struct __attribute__((packed)) { int a : 3; int b : 30; }` five bytes. The
  unit such a field is *read* through is chosen after the aggregate's size is
  known, by sliding it back until it lies inside the object -- a
  read-modify-write through a unit hanging off the end would clobber the next
  object -- and where the object is too small for the declared type to sit
  anywhere, by narrowing the unit to the smallest power of two that covers the
  field: `struct __attribute__((packed)) { char c; int b : 5; char t; }` is
  three bytes, so `b` is read through the byte at offset one, which is again
  what Clang and GCC do. **The narrowed width lives on `IrField.access_size`**,
  zero meaning the declared type's size, and `ir_field_access_size` is what
  every reader asks: the load and the read-modify-write in `c_gen.c`, the four
  constant-initializer folds there, the `IR_OPCODE_AGGREGATE` selectors in
  `machine_x86_64.c` and `machine_aarch64.c`, and the two canonical emitters in
  `codegen.c`. It is also the one place a `LOAD` or `STORE` may disagree with
  its place's type, which `ir_place_narrow_bit_field_access` is what validation
  admits it through. **A field whose bits cross every unit that fits has no
  single-unit access even then**, which is every width whose byte count is not
  a power of two: `union __attribute__((packed)) { long long b : 40; }` is five
  bytes and there is no five-byte load. `access_size` then carries the *span*
  rather than a unit, and `ir_field_access_pieces` decomposes it into the
  descending powers of two that cover it -- five is four plus one, seven is
  four plus two plus one, and nine, the widest span there is, is eight plus
  one. Clang writes the same access as `i40` and lowers it to the same
  sequence. Every reader walks the pieces: the frontend assembles a load out of
  them and read-modify-writes each one in turn, reaching the bytes past the
  first through a `unsigned char*` taken from the member place, and the
  constant folds deposit the bits one byte at a time because no integer is nine
  bytes wide. The `IR_OPCODE_AGGREGATE` selectors decline a span that is not a
  single unit and the canonical emitters take it, which is a per-function
  fallback the statistics already count. **A `_Bool` bit-field is extracted
  like any other**: its declared type is not an integer, so it used to skip the
  extraction and hand back the whole `_Bool` unit -- every neighbour's bits with
  it, normalized to one, which is why a `_Bool` that was the only set bit still
  read correctly -- and the write clobbered them. The unit it is read and
  written through is the unsigned integer of the same width, because the shift
  has to see the raw byte. A zero-width bit-field keeps aligning to its declared
  type even inside a packed aggregate, which is also what Clang and GCC do.
  **A zero width belongs to the *unnamed* bit-field alone**: C requires a named
  one to be at least one bit wide (C23 6.7.3.2p4) and both reference compilers
  refuse `int b : 0;`, where accepting it laid out a member that occupies no
  bits and can still be assigned and read back (issue #710). The width is
  checked in `c_lower_to_ir` where the constant expression is folded, so the
  expression spelling `int b : 1 - 1;` is refused with the literal one rather
  than only the spelling the parse fast path folds. The report shares the
  one-diagnostic-per-type budget with the rejected alignment specifier -- they
  are one `definition_rejection` slot whose kind travels with the message --
  and the definition still lays out, the way a rejected alignment specifier
  still hands back an alignment, so the program hears about the member it wrote
  rather than about a type that never got a layout.
  On AArch64 the accesses this reaches land at whatever byte offset packing
  chose, and the scaled unsigned-immediate load/store addresses only multiples
  of its own width, so `codegen_canonical_a64_memory_operation_base` falls back
  to the unscaled form -- `LDUR`/`STUR`, any byte offset in a nine-bit field --
  and materializes the address beyond that. It used to fail the whole
  compilation instead, which `struct __attribute__((packed)) { char a; int v :
  32; }` already reached before any of this.
  **A bit-field is not a field for System V's unaligned-field rule.** Its
  declared type is not what it occupies -- `int value : 20` at bit eight of a
  packed record occupies twenty bits inside the first eightbyte, not four
  bytes at an offset no `int` would sit at -- so
  `ir_system_v_abi_classify_bit_field` merges INTEGER into each eightbyte the
  field's *bits* fall in and leaves the declared type out of it, which is what
  the psABI writes and what clang and GCC compile. Asking the declared type
  instead sent every such record to memory, so the five bytes of
  `struct __attribute__((packed)) { char lead; int value : 20; char tail; }`
  came back through a hidden pointer where System V returns them in `rax`
  (issue #721); a program agreed with itself and disagreed with the object
  next to it. An *unnamed* bit-field is padding for this and contributes no
  class at all, which is observable beside a float: clang returns
  `struct { float f; int : 20; }` in `xmm0` and the same record with the field
  named in `rax`. The class is always INTEGER, since C admits no bit-field of
  floating type. AArch64 never asked: its aggregates up to sixteen bytes take
  INTEGER parts by size once `ir_homogeneous_float_abi` declines them.
  **A union member starts at bit zero whichever kind it is**, so a union sizes
  to the bits its widest member *occupies* rather than to that member's
  declared type: `union __attribute__((packed)) { char c; int b : 5; }` is one
  byte. The unpacked spelling needs no arm of its own -- rounding the size up
  to the alignment its declared type asks for is what gives it its four bytes
  back -- and the unit the field is read through is chosen by the same slide a
  struct's is, so the one-byte union reads its five bits through a byte
  (issue #706). Sizing it from the declared type is also what hid the
  single-unit refusal above from unions: a widest bit-field of 17 to 24 or 33
  to 56 bits sizes the union to a width no power-of-two unit fits inside --
  three bytes hold no unit covering 24 bits -- and is diagnosed for the same
  reason the struct spelling of those widths is.
  **A unit is never written whole.** Sliding and narrowing are what make a unit
  reach bytes it does not own: it can cover an ordinary member -- the four-byte
  unit of `struct __attribute__((packed)) { char c; int a : 5; int b : 7; char
  t; }` starts at offset zero, where `c` is -- and two units can share a byte,
  because packing narrows one field's unit and not the next one's. So every
  writer of a bit-field is a read-modify-write, including the one inside an
  aggregate initializer, where the members are materialized into a zero-filled
  slot and it is tempting to treat the accumulated word as the whole unit: the
  canonical emitters spell it `OR mem, reg` and the two `IR_OPCODE_AGGREGATE`
  selectors seed the accumulator with a load of the unit rather than with zero.
  Ordering the members differently does not substitute for it -- a whole-unit
  store loses whichever neighbour ran first, and two overlapping units lose one
  of themselves whatever the order (issue #705).
  A bit-field declarator carries a list of its own in exactly one place, *after*
  the width -- Clang rejects `int b __attribute__((packed)) : 5` -- so
  `c_type_parse_aggregate_segment_step` trims the width's token range with
  `c_parse_trailing_attribute_start`, the helper the parenthesized-declarator
  path already uses. Left untrimmed the list is part of the constant expression
  and the width never folds, which loses the aggregate's whole layout while
  `sizeof` still answers (issue #693). It is the third spelling of the packed
  bit-field layout above, and reaches the same narrowed unit as the other two. The trimmed tokens stay inside
  `[declarator_start, declarator_end)`, which is the range the per-declarator
  `packed` and `aligned` scans read, so the attribute reaches the layout with no
  second pass.
  **`aligned` written on a typedef is a different question**: it sets the
  alignment of the type the name declares rather than raising a declaration's,
  which makes it the one spelling that *lowers* an alignment without `packed`
  -- `typedef int pair __attribute__((aligned(2)))` is two-byte aligned in
  Clang and GCC alike. The request lives in `CParseResult.type_alignments`, a
  side table keyed by type index for the reason `noreturn_function_types` is
  one, and it keys on a *copy* of the type the declarator arrived at:
  `typedef int cache_line __attribute__((aligned(64)))` names the one builtin
  `int`, so marking that would realign every `int` in the translation unit.
  The copy carries `has_unqualified_type`, which is what gives both layout
  engines one place to read the natural alignment from and keeps the alias
  compatible with what it aliases. **Which of the two positions it is written
  in decides how many names it reaches**: after the declarator it belongs to
  that declarator alone, and among the specifiers it belongs to the
  declaration's type, so `typedef int __attribute__((aligned(16))) t5, t6;`
  aligns both names. That position is also where `_Alignas` is a constraint
  violation rather than a request -- a typedef declares no object for a
  declaration's alignment to apply to, and Clang and GCC both refuse
  `typedef _Alignas(16) int t;` -- so the run is *partitioned* by spelling
  rather than rejected by position, which had dropped the whole declaration
  (issue #715). `c_parse_typedef_alignment_run` in `c_parse.c` does that for
  both the file-scope and the block-scope parser, reading the spelling back
  out of the token stream with `c_alignment_specifier_is_standard`, and it
  rewinds the specifier table with the records it drops so the
  declarator-position ones each parser appends next stay contiguous with what
  survives. A **function** keeps the position rejection whole: neither
  reference compiler raises a function's alignment through it.
  **A qualifier cannot take the request away**, and a qualified copy points
  *past* the alias at the type it strips to, so `c_parse_add_qualified_type`
  gives the copy its own record rather than
  leaving the layout engines to walk a chain that no longer names the alias --
  `const cache_line` folded `_Alignof` 4 where Clang and GCC answer 64, in
  every position and with no diagnostic (issue #714). `_Atomic` applied to an
  aligned alias is the exception, and it is the one place the two references
  disagree: Clang gives `_Atomic cache_line` the alignment an atomic of that
  width gets and GCC keeps the alias's, so the record is inherited only when
  the step does not add `_Atomic`. **Two places build that type and one rule
  answers for both**, `c_parse_atomic_drops_type_alignment`: the copy a
  declaration makes goes through `c_parse_add_qualified_type`, while a type
  name in an expression is resolved during lowering by
  `c_ir_type_name_prefix` in `c_gen.c`, which builds no `CType` at all and so
  reached the aligned alias's own `IrType` and kept the request the typedef
  spelling had already dropped -- one type answering `_Alignof` 64 written
  inline and 4 written through a typedef of it, which is two layouts for one
  object across two translation units (issue #726).
  `c_ir_atomic_over_aligned_alias` is the lowering half, and both of that
  resolver's spellings ask it: `_Atomic` as a qualifier before or after the
  name, and the `_Atomic ( T )` operator, whose branch reads its operand's
  typedef out of the tokens because an alias and the type it aliases can map
  to one `IrType`. It rebuilds from the alias's *unqualified* type exactly as
  the type-mapping pass builds the typedef spelling, so
  `c_ir_add_qualified_type`'s dedup hands back the very `IrType` that spelling
  mapped to; the qualifier spelling runs before the pointer run because
  `_Atomic cache_line *` qualifies the pointee. The `_Atomic` shapes in
  `tests/basic_c_packed_layout.c` are written twice, once each way, and pin
  the pair rather than only the number; they stay out of the cross-linked
  `basic_c_packed_layout_shapes.h`, whose other half is whichever host
  compiler the platform has and where a GCC host answers the alias's number.
  A *qualified* copy is built where the qualifier
  is written, which is after the aggregate that embeds it, so the
  scalar seed in `c_lower_to_ir` is cleared for every recorded type: the
  mapping round that lays the aggregate out would otherwise read the seed's
  natural alignment before the alias branch replaced it.
  On the System V side one more rule follows:
  "contains unaligned fields" there means unaligned for the field's *natural*
  alignment, so `struct { char tag; pair value; }` is passed in memory even
  though `value` sits where its type asked. `IrTypeLayout::natural_alignment`
  carries that, and it is zero for every type nothing lowered.
  It is also the only way an **array element can end up over-aligned**, which
  Clang and GCC both refuse and so does this: an element has to be addressable
  at its own alignment in every slot, so its size has to be a multiple of that
  alignment, and `cache_line a[2]` puts the second element four bytes into a
  sixty-four-byte alignment. The scan is at the end of the type-mapping rounds
  in `c_lower_to_ir`, over a settled table, which is what makes one report per
  array type automatic and reaches a typedef no object ever names; it is
  skipped whole on an empty `type_alignments`, because every other type is
  sized at a multiple of its alignment by construction -- an aggregate's own
  `aligned` rounds its size *up*, so `struct __attribute__((aligned(16))) { char c; } a[2]`
  is thirty-two bytes and stays well-formed (issue #703). The report is counted
  on the *bound record* rather than on the type, because a qualified array and
  a typedef of an array are copies carrying the same bound, and one report per
  written `[N]` is what Clang produces; two identical declarations that intern
  to one array type therefore report once where Clang reports twice. Two
  spellings reach that report down a different road (issue #713), because
  neither reaches the type table as a `C_TYPE_ARRAY`. A parenthesized
  declarator's `cache_line (*p)[2]` builds one only once the syntax scan stops
  reading a top-level `(` after an identifier as the parameter list of a
  function that identifier names -- see the declarator note below. An array
  type name in an expression, `sizeof(cache_line[2])` and the compound literal,
  is resolved during lowering and never reaches the table at all, so
  `c_ir_type_name_suffix` records the earliest offending bracket on the builder
  and `c_lower_to_ir` makes one report from it at the very end, after every
  declaration has been lowered: that resolver runs inside speculative attempts
  that are rolled back and revisits the same tokens many times, so keying on
  the token index is what makes the report independent of the order the
  attempts run in, and it has no bound record to count on. It records without
  refusing the type, the way the settled-table scan reports without refusing
  one: the report is what refuses the translation unit.
- **`_Atomic T` is a type built from T, not a qualified T, and its layout says
  so.** Clang pads it up to the next power of two and aligns it there, so a
  value the `__atomic` builtins could reach lock-free has an instruction that
  covers it: there is no three-byte atomic access and there is a four-byte one.
  `_Atomic` of a three-byte record is four bytes aligned four, of a five-byte
  one eight aligned eight, of a twelve-byte one sixteen aligned sixteen, and a
  zero-sized aggregate still takes a byte; every atomic scalar ends up aligned
  to its size, which is the same sentence that answers `_Alignof(_Atomic
  cache_line)` above and what moves `_Atomic _Complex double` from eight-byte
  alignment to sixteen. The ceiling is the target's maximum lock-free width,
  `TargetDataLayout::atomic_max_width` -- 128 bits everywhere here but wasm64
  and BPF, where it is 64 -- and a type wider than that keeps T's own layout,
  so the rule is not "round every aggregate up": `_Atomic` of a seventeen-byte
  record is seventeen bytes aligned one in Clang too. GCC pads nothing and
  raises the alignment only where the size is already a power of two, so this
  is the second place the two references disagree and Clang is the oracle for
  both (#731). `c_atomic_promoted_layout` in `c_parse.c` is the one rule and
  both engines ask it: `c_parse_type_layout` at the seed, at the exit that
  answers a builtin kind outright, and at an atomic branch of the solve that
  answers for every kind a qualified copy can carry -- ahead of the branches
  that would lay that copy out from its own kind -- and
  `c_ir_add_qualified_type` for the copy the mapping pass builds. An **alias
  over an atomic type** replaces the alignment and keeps the padding, so the
  aligned-alias branch runs first and takes only the promoted size from the
  rule: `typedef at3 t __attribute__((aligned(32)))` is four bytes aligned
  thirty-two. A **type name** builds the atomic type as well, in each of the
  three spellings that reach one: `c_ir_type_name_prefix` qualified only an
  aligned alias before, which was invisible while `_Atomic T` was laid out like
  T and became `sizeof` 3 written inline against 4 through a typedef the moment
  it was not. The fourth spelling is `_Atomic` written in
  front of a `struct`, `union` or `enum` keyword, and it reaches the type
  through the declaration-specifier run rather than through a resolver of its
  own: `c_parse_atomic_type_specifier_at` is what tells the qualifier apart
  from the `_Atomic ( T )` specifier, a `(` right after the keyword being the
  only difference, and the prefix scans stop only at the specifier. Stopping
  at both left the qualifier uncollected, so the aggregate branch handed back
  the tag's own type and `_Atomic struct three` was `sizeof` 3 against the
  other three spellings' 4 -- and, worse than a number, an assignment to such
  an object was an ordinary aggregate copy where the program asked for an
  atomic store (#761). `const struct S` and `volatile struct S` ride that same
  run and always reached the type through it. Two scans ask the question: the
  declaration-specifier run in `c_type_parse_scalar_step`, whose existing
  completion applies whatever the run collected, and
  `c_parse_machineless_base_type`, the operand walk the enum-constant
  evaluator uses because the type-parse machine is already running the body it
  is folding for -- that walk dropped the leading run before the tag and the
  trailing run after it alike, which failed a `const struct S` operand outright
  rather than mis-sizing it. That walk had no branch for the `_Atomic ( T )`
  spelling either, so it resolved nothing for a tag, a typedef name and `int`
  equally, and the enumerator that spelled it failed -- which fails the enum
  type and leaves every enumerator beside it undeclared for the rest of the
  file (#784). The specifier's operand is a whole type name rather than a base
  type, so the walk resolves it the way it resolves the outer one -- base type,
  then `c_parse_machineless_declarator_suffixes`, the pointer/qualifier/array
  chain shared with the sizeof operand walk -- and then applies the refusals
  `c_type_parse_scalar_step` diagnoses at `C_DIAGNOSTIC_INVALID_ATOMIC_TYPE`:
  an array, a function, `void`, or an already-qualified operand. It refuses
  silently, because the walk has a fallback -- the caller tries the operand as
  an expression next -- and a diagnostic would land in the caller's throwaway
  copy of the parse result; an enum constant that does not fold reports the way
  every other one does. `_Atomic(_Atomic(int) *)` is legal C, so the nesting
  follows the source and the levels go on an explicit stack
  (`C_PARSE_MACHINELESS_ATOMIC_LEVELS`, eight) rather than on the C call stack;
  past it the operand does not fold, which is where every depth of it stood
  before, and the machine-bearing path folds it at any depth either way. On
  the argument side the promotion moves nothing *on System V*: a promoted
  four-byte record is one INTEGER eightbyte where the three-byte one already
  was. It does move something on Win64, where the class is a function of the
  size alone -- four bytes ride a register and three are passed indirectly --
  so the classification is made from the promoted type rather than from the
  record on every convention, and that is what makes an argument position
  carry the atomic type; see the parameter bullet below.
  The **LLVM bitcode writer** maps an atomic type onto its
  operand's LLVM type, which is exact for every atomic scalar -- `_Atomic int`
  and `int` are one type -- and short by the padding for an aggregate, so an
  atomic type whose `layout.size` exceeds its operand's gets a record of its
  own instead: the operand followed by a byte array of the padding, which is
  what Clang writes as `{ %struct.three, [1 x i8] }` (#767). The operand is
  then the atomic type's only dependency, so `llvm_bc_type_dependencies_ready`
  answers for it directly rather than from the fields the qualified copy shares
  with it -- otherwise the table walk builds the copy as a plain struct before
  the operand it is meant to wrap has an id, and the padding is lost.
  `tests/basic_c_atomic_bitcode.c` holds the three positions a type is built
  for, and the native object it also runs pins the sizes the bitcode has to
  agree with.  `tests/basic_c_packed_layout.c` pins the promoted sizes
  next to the one-byte aggregate that agrees without them, once for each
  engine, and they stay out of the cross-linked
  `basic_c_packed_layout_shapes.h` for the same reason the shapes above do.
- **A qualifier decides nothing about how a value is passed.** `_Atomic T` and
  `volatile T` take T's argument class on every convention: the qualified copy
  carries T's kind and T's fields, `_Atomic` changes only the size and the
  alignment, and neither qualifier introduces a class of its own. So a record
  holding an `_Atomic int` is the INTEGER eightbyte its `int` spelling is, a
  record holding an `_Atomic float` beside a plain one is still two floats --
  SSE on System V, a homogeneous float aggregate on AAPCS64 -- and an atomic
  record passed by value is classified from the record it is built from, which
  no fixture can pin yet: an atomic aggregate parameter fails code generation
  in every spelling (#786), and the one spelling that compiles does so only
  because it is not reaching the type at all (#761), so a fixture written that
  way would pass while testing nothing. The wrapped shape --
  `struct { char c; _Atomic(struct pair) v; }` -- reaches the same walk and is
  in the pair. `ir_abi_unqualified_type` in `ir.c` is the one step that says
  so, and the AAPCS64 homogeneity walk is its only caller: System V already
  reads the leaf kind, which a qualified copy keeps.
  **This is the one place the oracle is not followed, and it is a decision
  rather than an oversight** (#763). Measured 2026-08-30 against Clang 22.1.8
  and GCC 16.2.1: GCC classifies every one of these shapes exactly as the
  unqualified spelling on x86-64 (its AArch64 answer was not measurable on the
  machine that measured this -- no cross-GCC). Clang sends any record
  *containing* an atomic member, and any atomic record, to MEMORY -- but only
  on System V x86-64; the same Clang passes a record *containing* an atomic
  member in registers on Win64, on AAPCS64 and on Darwin AArch64. An atomic
  *record* passed by value shows the fallthrough in a third shape on Win64
  (measured 2026-08-30): Clang expands it into one argument per member, the
  padding byte among them, and returns it through `sret`, where the four-byte
  record it is built from would ride one register both ways. Its AArch64
  homogeneity test declines separately, so `struct { _Atomic float a, b; }`
  rides X0 there where `struct { float a, b; }` rides S0/S1. Both refusals are
  the same accident: `X86_64ABIInfo::classify` asks `Ty->getAs<RecordType>()`
  and `isHomogeneousAggregate` asks for a builtin type, an `AtomicType` is sugar
  over nothing, and each walk falls through to its "everything else" tail. The
  psABI and AAPCS state no such rule, `volatile` reaches neither refusal, and
  Clang contradicts itself across three of its own conventions, so following
  it would mean writing a rule neither document states and disagreeing with
  GCC everywhere and with Clang on every convention but one. The cost is real
  and is recorded rather than hidden: a `struct { char c; _Atomic int v; }`
  argument sits in a register on this side of a System V x86-64
  translation-unit boundary and in memory on Clang's.
  The same reading fixed a divergence from *both* references: identity in the
  homogeneity walk was a type id, so `struct { float a; volatile float b; }`
  was an integer pair where Clang and GCC both keep the two-register
  aggregate. `tests/basic_c_atomic_abi_shapes.h` and the callee/caller pair
  around it pin all of it -- both halves through this compiler under every
  allocator, mixed with a real GCC on System V x86-64 (the host compiler
  cannot stand in: on that convention Clang is the half that disagrees), and
  mixed with Clang on AArch64 under qemu, where the one shape Clang answers
  differently leaves through `ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA` and its
  `volatile` twin stays behind to pin the same mechanism. Both directions were
  verified to fail when the rule is taken away: the Clang-paired System V link
  fails at the first record, and restoring the type-id identity fails the
  Clang-paired AArch64 link at the `volatile` shape.
- **An atomic aggregate is loaded and stored as one integer access of its
  promoted width**, which is what the promotion above exists for: a three-byte
  record is read and written through four bytes, and the padding the promotion
  added is written as zero, because Clang copies the value through a zeroed
  temporary and that is the oracle. The widths the canonical emitters lower
  that access at are one, two, four and eight bytes on both targets, plus
  sixteen on x86-64 where `cx16` gives them `CMPXCHG16B` -- the sequence
  `_Atomic __int128` already used, which now also takes any aggregate the
  promotion padded into the same width. The machine selectors decline the
  aggregate shapes and the function falls back to the canonical emitter, which
  the fallback statistics already count, so all four allocators answer the same
  bytes. Anything wider would need a `libatomic` lock and there is none here,
  so lowering refuses it with a diagnostic naming the width rather than leaving
  code generation to fail internally (#762). The refusal is
  `c_ir_atomic_aggregate_accesses_lowerable` in `c_gen.c`, and it runs over the
  *finished* body rather than where the access is built, because an operand is
  lowered as a value before an expression that only wanted its address recovers
  the place and drops the load again: refusing at the emit site rejects
  `&object.atomic_member`, which performs no atomic access at all and is what
  `tests/basic_c_packed_layout.c` writes over its seventeen-byte atomic
  member. AArch64 has no 128-bit lock-free access
  here -- `_Atomic __int128` does not lower there either -- so a sixteen-byte
  atomic aggregate is one of the shapes that refusal covers, and the refusal is
  therefore target-dependent where the layout rule above is not.
  `tests/basic_c_atomic_aggregate.c` runs the bytes under every allocator with
  Clang's answers baked in, including the padding.
- **A parameter and a return value of an atomic aggregate carry the atomic type
  itself, and what converts between it and the record is an object rather than
  an instruction.** Every access in between carries the unqualified type -- a
  load of an atomic place yields the record and a store takes one, which the IR
  validates rather than merely allows -- but an ABI position cannot: the
  `IR_OPCODE_ARGUMENT` type is validated against the signature's parameter type,
  and code generation classifies an argument from the type the instruction
  carries, so a record-typed value there would be classified from three bytes
  where the object is four. The two are therefore two aggregates of two sizes
  meeting in one expression, which no ladder in `c_ir_emit_cast` spans, and
  every spelling of the type failed code generation with an internal message
  rather than a diagnostic (#786). `c_ir_atomic_aggregate_pair` in `c_gen.c`
  recognizes the pair and `c_ir_emit_atomic_aggregate_conversion` converts
  through a slot of the atomic type, written through one view and read back
  through the other: widening is the settled atomic store -- one integer access
  of the promoted width, which is what zeroes the padding -- followed by a
  plain read of the whole slot, private storage nothing else can observe, and
  narrowing is that pair reversed. Both accesses are the settled ones above, so
  a promoted width past the target's lock-free ceiling is refused by the same
  walk over the finished body rather than failing inside code generation, and
  nothing in the IR, in validation or in code generation had to change. The
  parameter and return round trips are in `tests/basic_c_atomic_aggregate.c`
  in all four spellings and in both positions, with Clang's bytes -- the
  padding the promotion added among them -- baked in the way the rest of that
  fixture bakes them.
- **A top-level `(` right after an identifier** is the parameter list of a
  function that identifier names in `T f(int)`, and a parenthesized declarator
  in `T (*p)[2]`, whose `T` is the last word of the declaration specifiers.
  The syntax scan that finds a declaration's name has no typedef table to tell
  those two identifiers apart, and does not need one: a parameter is a
  declaration, so a parameter list can never begin with a `*`, and a group that
  does is a declarator group whatever precedes it. Without that,
  `T (*p)[2]` and `struct s (*p)[2]` were read as functions named `T` and `s`
  and the declaration each really makes was dropped whole -- no definition
  emitted, no diagnostic, and a `sizeof(*p)` that folded nothing. The redundant
  `T (p);` is the one shape still left: it needs the typedef table, since
  `int f(x);` with `x` a typedef name is a real function declaration.
- `__typeof` is accepted alongside `__typeof__` and `typeof`, because musl's
  `weak_alias` macro is written with it. **A function declared through a type
  name rather than a parameter-list declarator** -- `extern __typeof(f) g;`,
  or the same through a typedef -- has no function-name token, and that token
  is what the declaration kind is otherwise decided by. `c_analyze_semantics`
  in `c_parse.c` therefore reclassifies on the resolved type: a declarator
  whose type is a function is filed `C_DECLARATION_FUNCTION` with the
  parameter range taken from the `CType`, because the declarator has no
  parameter list and only the type knows the parameters. Everything
  downstream reads the declaration -- `c_ir_build_function_name_index` indexes
  it, `c_ir_function_signature` gives it an arity, and the entity is a
  `C_ENTITY_FUNCTION` -- so the name is callable and not merely addressable
  (issue #641). The same handoff from type to declaration is what the
  parenthesized declarator path and the block-scope function declarator in
  `c_parse_declaration_type` already do.
  `c_parse_entity_kind_redeclares` in `c_parse.c` is what keeps this spelling
  and an ordinary prototype one entity, which in musl every published name
  has.
- **A `typeof` operand is typed twice, by two engines, and both have to
  answer.** `c_ir_sizeof_operand_type_attempt` in `c_gen.c` types the operand
  of a `typeof` written in an *expression* -- a cast, a compound literal --
  and `c_type_parse_sizeof_step` in `c_parse.c` types the one written as a
  *declaration's specifier*, because the parse is what decides a declaration
  exists at all. A specifier that resolves to nothing declares no name, so the
  reported error is "use of undeclared identifier" at the first *use*, naming
  neither the `typeof` nor its operand; a `typeof` bug reads as a missing
  declaration (issue #760). The parse-side walk carried two gaps that the
  gen-side one did not: no unary `*` or `&` at all -- so `__typeof__(*(double
  *)0)` resolved to nothing, prefixes over a plain identifier chain being the
  only ones `c_parse_direct_expression_type` handles -- and a conditional
  merge that compared type ids, so two pointer arms of different types
  resolved to nothing either. `c_parse_conditional_pointer_type` now applies
  C11 6.5.15p6 in its own order, and both engines agree: a *null pointer
  constant* on either side yields the other operand's type, and only if
  neither is one does a `void *` operand pull an object pointer to `void *`.
  Two object pointers with incompatible elements are what clang reports as
  `-Wpointer-type-mismatch` and still types as `void *`, so
  `*(0 ? (double *)0 : (char *)0)` is `void` and the declaration on it is
  refused for an incomplete type -- clang's own diagnostic -- rather than
  never being seen. `c_parse_range_is_null_pointer_constant` answers the
  spelling half: it gates on the arm's type (integer, or pointer to `void` --
  the constant walk strips casts, so without the gate `(double *)0` would
  answer yes), strips leading pointer casts by shape, since a parenthesized
  group whose last token is `*` is never a parenthesized expression, and folds
  the rest through `c_parse_integer_constant_range` *machineless*, the caller
  already being inside a type-parse frame. That composition is exactly musl's
  `__type1(c,t)`/`__type2(c,t1,t2)`, whose outer cast is
  `(__typeof__(...) *)` and whose machineless base-type reader cannot resolve
  it -- hence the by-shape strip. `tests/basic_c_typeof_conditional.c` runs
  both macros under all four allocators and
  `c_test_typeof_conditional_type` pins the resolved types themselves.
- **A failed call blames the call, not the declaration.** A direct call's
  callee token is not an identifier *use*: it resolves through the
  call-target index, so the parser records no binding for it and
  `c_ir_identifier_entity` misses. The lowering failure in
  `c_ir_lower_expression_core_step` looks the name up before reporting, and
  keeps the identifier's own token index rather than the one the call arm
  advanced to the closing parenthesis, so an object called as a function is
  reported as one instead of as an unbound identifier. A name nothing
  declares still reports as unbound, which is what a SIMD builtin missing
  from `c_symbol_predefined` looks like.
- **A call to a function declared `()` supplies the parameters itself.**
  Before C23 an empty parameter list is no prototype at all, so the arguments
  take the default argument promotions and the *call site* is the signature.
  `IrType.is_unprototyped` marks such a declaration's type, and
  `c_ir_unprototyped_call_type` in `c_gen.c` gives the `IR_OPCODE_FUNCTION`
  reference the call's own parameter types plus a trailing `...` -- Clang's
  model, which declares `void die()` as `void (...)` and types each call site
  `void (i32, ...)`. The `...` reaches only System V x86-64, where it sets the
  AL vector count that a callee which really is variadic reads;
  `int printf(); printf("%f", x);` is that program. Every argument stays
  *named*, so Darwin AArch64 passes them in registers rather than on its
  variadic stack, and a definition is untouched: `int main()` is still an
  ordinary zero-parameter body with no register save area.
  `ir_validate_instruction` therefore lets a function reference disagree with
  its symbol's type when that type is the unprototyped one and the return
  types agree, and Wasm64 refuses such a call, because it types every call by
  the callee's declared signature. C23 made `()` mean `(void)`, so the marker
  is never set in that dialect and the call is refused as an arity error --
  which every dialect now reports by naming the callee and its parameter
  count rather than as "could not prepare C calls" (issue #666).
- The generic JIT loads already-produced host-native objects and resolves
  explicit bindings. It is not a second source-language compiler and must stay
  independent of frontend semantic structures.
- The command-line driver accepts C source/preprocessed C plus native
  objects/archives where the selected action permits them. Unknown languages,
  retired module-root options, and unsupported source extensions must fail
  explicitly rather than being forwarded or guessed.
- **`long double` is 80-bit x87 on System V x86-64, and it is memory-only.**
  Transport, the four arithmetic operators, negation, the six comparisons,
  truth conversion, and the conversions to and from the narrower floats and
  every integer width all lower, and a variadic argument takes the sixteen-
  byte, sixteen-aligned overflow slot the ABI requires. Every one of those is
  a *closed* x87 transaction: it loads its operands from frame slots, operates,
  stores the result back, and leaves the x87 stack as empty as it found it, so
  no value is ever live in an ST register across a machine instruction and the
  register allocators need no x87 class. Machine selection therefore refuses an
  f80 function outright and it falls back per function to the canonical
  emitter, which is where the whole vocabulary lives
  (`codegen_canonical_x64_emit_f80_*` in `codegen.c`). Do not touch the x87
  control word outside `codegen_canonical_x64_x87_truncate_begin/end`: its
  default extended precision is exactly what `long double` wants, and only a
  float-to-integer conversion may switch the rounding field, for one store.
  An **aggregate** carrying an f80 payload takes one of two paths, and which
  one is the ABI classification's answer, never a walk of the fields. System
  V's merger algorithm orders its rules equal, NO_CLASS, MEMORY, INTEGER, x87,
  SSE — **INTEGER beats x87** — so musl's `union ldshape`, an f80 overlaid
  with `struct { uint64_t m; uint16_t se; }`, classifies as two INTEGER
  eightbytes and rides two general-purpose registers, which is what clang
  compiles it to (`{ i64, i64 }`) and is not what a literal "an x87 class
  makes the value MEMORY" reading produces. A shape the merger cannot
  reconcile — `union { long double; unsigned long; }`, whose X87_UP tail is
  then unaccompanied, or `union { long double; double; }`, or anything past
  two eightbytes — goes to memory whole, byval in and sret out. Neither asks
  anything of the x87 stack: their bytes are copied. Only a classification
  that really carries an X87/X87_UP part needs the x87 vocabulary, and
  `ir_abi_value_has_x87_part` is the predicate every gate asks. Objects of
  more than one `long double` — `long double v[2]`, local or global — lower
  the same way, because the array is memory-class and its elements are reached
  one f80 at a time. `tests/basic_c_long_double_aggregate.c` covers the
  semantics under all four allocators; the ABI itself is only pinned by
  `tests/basic_c_long_double_aggregate_{caller,callee}.c`, linked against the
  host compiler in both directions, because a caller and a callee this
  compiler produced agree with each other whatever they agree on.
  Reading one back out of a `va_list` admits exactly the two shapes the
  argument side does, and for the same reason. The X87/X87_UP pair classifies
  into memory, so a variadic `long double` is never in the register save area
  whatever the argument counters hold: `va_arg` realigns the overflow cursor
  to sixteen, takes the slot, advances past all of it, and copies the payload
  through the x87 stack, which is what leaves the destination's six padding
  bytes zeroed. An opaque aggregate reads back through the ordinary eightbyte
  path. `tests/basic_c_va_arg_long_double.c` pins both under all four
  allocators, including a read through a `va_list *` and one past a `va_copy`
  — the spellings musl's `pop_arg` uses, and the ones the canonical emitter
  alone has the x87 vocabulary for.
  A *static* x87 initializer is folded rather than emitted:
  `c_ir_ext80_fold_initializer` in `c_gen.c` evaluates a constant expression
  over numeric literals — `+ - * /`, unary sign, parentheses — straight into
  the 64-bit significand and sign/exponent pair the object writer stores, for a
  scalar global, a function-local static, and each x87 element of an aggregate.
  Every leaf rounds in its own declared type and every operation rounds in the
  common real type the usual arithmetic conversions pick, which is what
  reproduces C's per-operation rounding and makes the bytes Clang-identical; a
  decimal fold would not, because `1/LDBL_EPSILON` is exactly 2^63 only once
  musl's spelling of the epsilon has rounded to 2^-63 first. Overflow becomes
  an infinity, underflow a signed zero, and the invalid operations — infinity
  minus infinity, infinity times zero, infinity over infinity, zero over zero —
  the default *positive* quiet NaN whatever the operand signs were, which is
  the answer Clang's own folder gives and the one
  `c_ir_fold_float_invalid_operation` gives the same expression inside a
  function; the two have to stay together, because a disagreement prints `nan`
  from one and `-nan` from the other. Those last two matter because musl spells
  `INFINITY` as `1e5000f` and `NAN` as `(0.0f/0.0f)` for a compiler that does
  not advertise the GNU builtins, so they are what a `long double` table in
  libc-test is made of. An operation between two *integer* operands is refused
  on purpose: it does not round at all, and `1/3` would need its own truncating
  semantics rather than 0.5 — which is also why `0/0` between two integer
  literals is a diagnostic here rather than the NaN its floating spellings fold
  to, Clang rejecting that initializer outright. So is an addition that aligns
  a genuine subnormal against a near-maximum operand, which spans more of the
  x87 exponent range than the folder's bignum holds — a zero operand is taken
  as the exact identity instead of aligned, so only a real subnormal reaches
  that edge. `tests/basic_c_long_double_static_initializer.c` pins the finite
  arithmetic and `tests/basic_c_long_double_static_special.c` everything from
  the infinities out, both against bytes read out of Clang's own object.
  Still refused with a source diagnostic: a fixed wide-float parameter of a
  variadic *definition* (the SysV `va_start`
  register-save area does not account for it), an aggregate whose
  classification carries an X87 class without being the ABI-proven single-f80
  shape, and every wide float on a target whose `long double` is not this
  format.
- **A module-level `__asm__` block emits into the module's text through
  `codegen_emit_global_assembly` in `codegen.c`.** It interprets the
  directives itself — `.text`, `.byte`, `.p2align`, and the symbol directives
  `.globl`/`.global`, `.weak`, `.hidden`, `.type` and `.size` — and hands
  every instruction it does not have a fixed encoding for to `assembly_encode`,
  the same assembler the inline-assembly path uses, one line at a time. That
  is where relocation support comes from: a `call sym` or a
  `lea sym(%rip),%reg` against a name the block does not define becomes a
  `CodegenModuleRelocation`. The assembler's PC-relative addend already carries
  the distance from the relocated field to the end of its instruction, and the
  module vocabulary's does not — the object writer subtracts four on the way
  out — so the emitter adds those four back rather than restricting the shapes
  it accepts. A symbol the block names does not need a C declaration: the
  emitter adds one to `IrProgram.symbols` when the search misses, which is what
  lets a startup object define `_start`. Two things follow from the AT&T/Intel
  dialect being x86-only: the emitter passes `ASSEMBLY_SYNTAX_DEFAULT` for
  every other target, and the assembler's AArch64 vocabulary is the bootstrap
  control-flow set, so an AArch64 block gets `bl`, `brk`, `ret` and `nop` and
  not an ADRP/ADD page pair. A block that fails reports through
  `CodegenModule.failed_in_assembly` and the block/line beside it, which is what
  keeps the driver's diagnostic off the next C function in the file. The
  *inline*-assembly arm of `codegen_generate_canonical_module_attempt` shares
  the last two of those: once its template is substituted,
  `codegen_emit_inline_assembly` walks the result a line at a time and hands a
  leading-dot line to the same directive table and everything else to the same
  `codegen_global_assembly_encode_instruction`, so a `lea sym(%rip),%0` in a
  GNU asm template becomes the same relocation. What the two do not share is
  where a new symbol's name may point: an inline template is a copy the
  code-generation attempt owns and the retry rewinds, so the name recorded is
  the one in the instruction's IR literal (`codegen_assembly_durable_name`).
  Labels are refused inside a template rather than defined, because a template
  is emitted once per instruction rather than once per file.
- The Wasm64 backend consumes canonical IR directly. Unsupported ABI or
  instruction shapes must be diagnosed; never silently fall back to a native
  backend.

## Repository map

Top level:

| Path | Contents |
|---|---|
| `build.c`, `build.sh`, `build.ps1` | Native build driver and its bootstrap scripts. |
| `CMakeLists.txt` | Compiler detection, warnings, sanitizers, module graph, targets, and platform packaging. |
| `src/buster/apps/` | Command-line application entrypoints and standalone tools. |
| `src/buster/lib/` | Runtime, platform, compiler, assembler, linker, JIT, and retained UI/rendering libraries. |
| `src/buster/tests/` | In-process unit/module tests. |
| `tests/` | C frontend, driver, object/archive, fuzz, and CI-script fixtures. |
| `tools/` | Python generators, scanners, and measurement scripts run by hand; outside the build graph. |
| `.forgejo/` | Forgejo CI workflows and scripts. |
| `PERFORMANCE_AUDITS.md` | Index of the append-only measurement history; one line per audit. |
| `docs/performance-audits/` | One file per audit, named for its id; older entries may describe components that no longer exist. |
| `WASM64.md` | Direct core Wasm64 target contract and usage. |
| `LLVM_BITCODE.md` | Direct LLVM bitcode output, driver usage, emitter API, validation, and current limitations. |
| `docs/uefi-target.md` | Freestanding UEFI target contract, driver usage, firmware ABI, image layout, relocations, and limitations. |
| `build/` | Generated output. Never edit or archive it as source. |

Compiler (`src/buster/lib/compiler/`):

| Path | Contents |
|---|---|
| `frontend/c/c.h` | Public C frontend API. |
| `frontend/c/c_source.c` | Source loading, lexing, preprocessing, includes, macros, and source metrics. |
| `frontend/c/c_parse.c` | Parsing, declarations, scopes, types, semantic analysis, and diagnostics. |
| `frontend/c/c_gen.c` | Target-aware lowering from analyzed C into canonical IR. |
| `frontend/c/c.c` | Unity-build aggregator for the three C frontend implementation files. |
| `ir/model.h`, `ir/ir.{c,h}` | Canonical typed IR model, construction, validation, and printing. |
| `assembly/` | Standalone x86-64/AArch64 assembly parsing, metadata, semantics, and encoders. `assembly_unit.{c,h}` is the whole-file layer above them: sections, directives, labels, and relocations. Generated metadata stays under `assembly/generated/`. |
| `codegen/machine*.{c,h}` | Machine IR, instruction selection, scheduling, ABI lowering, and target-specific emission. |
| `codegen/register_allocator_*.c` | Fast and quality register allocators. |
| `codegen/codegen.{c,h}` | Canonical-IR-to-native-code orchestration and codegen statistics. |
| `debug/`, `dwarf/`, `codeview/`, `pdb/` | Canonical debug model and platform debug-format emitters. |
| `object/object.{c,h}` | Format-neutral object model plus ELF64, COFF, and Mach-O readers/writers. |
| `link/link.{c,h}` | Section merging, symbol resolution, hosted native executable linking, and imports-free PE32+ UEFI application output. |
| `jit/jit.{c,h}` | Host-native in-process object loader with explicit imports and W^X finalization. |
| `wasm/wasm.{c,h}` | Direct canonical-IR-to-core-Wasm64 emitter using Memory64. |
| `gpu/gpu.{c,h}` | Target parsing, deterministic command planning/execution, tool discovery, temporary ownership, and artifact validation for external SPIR-V, NVPTX/PTX, AMDGCN/HSA, Metal AIR/metallib, and DXIL pipelines. |
| `llvm/bitcode.{c,h}` | Dependency-free canonical typed-IR to binary LLVM bitcode emitter. It writes the bitstream directly, preserves deterministic value numbering, records target metadata, and diagnoses unsupported IR instead of routing through textual LLVM IR. |
| `driver/driver.{c,h}` | Clang-like C command-line parsing and end-to-end preprocess/compile/assemble/object/link dispatch, including hosted and freestanding UEFI links, plus direct LLVM bitcode output and isolated external GPU-pipeline orchestration. |

Applications:

| Path | Contents |
|---|---|
| `src/buster/apps/ide/ide.c` | Headless `ide` executable: `cc`, `test`, `bench`, fuzzing, and x86-64 completion census. The name is retained for build compatibility. |
| `src/buster/apps/disk_builder.c` | Standalone disk-image builder; not part of the default CMake target. |
