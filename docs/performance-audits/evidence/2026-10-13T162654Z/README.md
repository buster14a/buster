# Frozen self-host profiling evidence

See the [audit](../../2026-10-13T162654Z.md) for interpretation and limitations.

## Identities and files

- Baseline: `e0d55a13095724aaa7380cf478cbe4b7a6873458`.
- Final code: `5c6eafbaacb0a4d7cc0ef6a88751ee653d4c3c86`.
- `original.callgrind.gz`: supplied capture, original source revision unknown.
- `profiles/baseline-compatible.cg.gz`, `baseline-repeat.cg.gz`: two baseline runs.
- `profiles/final-1.cg.gz`, `final-2.cg.gz`: two final runs, identical Ir.
- `profiles/proof.cg.gz`: retained neutral output-proof-only experiment.
- `profiles/combined.cg.gz`: first combined candidate before restoring the
  scalar helper's rejection of out-of-range enum values.
- `profiles/baseline.cg.gz`: rejected host-CRT/generic-runtime combination,
  with exit-127 reason in its log; this is not a successful baseline sample.
- `native-raw-receipts.tar.gz`: all native observations and stdout/stderr,
  including the failed explicit-`:u` A/A receipt. The five summaries are also
  available as separate JSON files. Unqualified request names were used for
  the successful receipts; raw CSV verifies the observed user-only scope.
- `binary-identities.json`: every compiler and output identity. All successful
  A/B output files have SHA-256
  `f52d13ffb732fcbb114ead0877d12291633c3f01392c7708d3a5c5a84d3afbe1`,
  42,570,440 bytes. Both native and simulated runs agree.
- `frozen-input-sha256.json`: frozen `src/` and generated-header identities.
  The compile input is the baseline tree for every arm.
- `native-build-plan.json`: exact Clang argument arrays for the separate
  tests-off native production compilers.
- `validation/`: compressed test/build logs. The sanitizer run is on
  `e6c001a48d22bc50bce774186d1acdf5166378cc`; final Release runs are on the
  final code revision. No hosted result is implied.
- `core-triage.json`, `core-commands.json`, `valgrind-host-true.log`: core
  hashes/notes and the loader SIGILL control. The core contents are not uploaded.
- `runtime-SHA256SUMS`, `host-metadata.txt`, `*-elf-notes.txt`: generic CRT
  identity, runtime image/host identity, and the host-v4/generic-baseline
  ELF ISA declarations. Startup object contents are not vendored.

## Reproduce the attribution

Decompress a profile, then use the retained parser:

```sh
gzip -dc profiles/final-1.cg.gz > /tmp/final-1.cg
python3 analyze_callgrind.py /tmp/final-1.cg /tmp/final-1.json
```

The parser asserts every exclusive event total. `study-summary.json` compares
both runs per arm, records spreads and retains the relevant call edges.

## Reproduce the compilers and frozen input

Use separate clean worktrees at the revisions above. For each checked subject,
use the repository build driver (the full revision stamp is required):

```sh
./build.sh generate -DBUSTER_NATIVE_TARGET=x86-64-v2 \
    -DBUSTER_INVESTIGATION_REVISION="$(git rev-parse HEAD)"
./build.sh build --config Release -t ide
```

Retain the trusted Clang compiler and its unity object before building another
revision. Freeze `src/` from the baseline revision and its generated headers
in one directory; both arms compile that same input directory. `generate`
recreates its selected build tree, so never use it while another consumer
is running there. The native production subject uses the same trusted Clang
Release command, with `BUSTER_INCLUDE_TESTS=0` and `-march=native`; the complete
argument arrays, source paths and revision definitions are retained in
`native-build-plan.json`.

For the simulated subject on this CachyOS host, relink each unchanged unity
object with the generic runtime's startup objects:

```sh
clang -B/absolute/path/to/generic-crt/ -O3 -DNDEBUG -fuse-ld=mold \
    /absolute/path/to/ide.c.o -o /absolute/path/to/ide-profile -lm
```

The generic image used here is
`docker.io/archlinux/archlinux@sha256:5bf8d86cc2bc6a8406b5cefc7cbafb185202f8fa5faae5ef5e6e9b2424ca42a8`,
local image ID
`6ccfb63f72ebe103a4f290f1f2937eab6fc32e5df1ecc7ef091bb3fb2fbf130f`.
Copy its `/usr/lib/Scrt1.o`, `crti.o`, and `crtn.o` to the generic-CRT directory;
verify the retained hashes. Use that image for both runs. The host Valgrind
3.25.1 launcher and `/usr/lib/valgrind` were mounted read-only into it. The
full work directory was mounted at `/study`, with `/study/frozen` as cwd;
network was disabled and core size limited to zero.

Inside that runtime, the per-arm command is:

```sh
valgrind --tool=callgrind --cache-sim=yes --branch-sim=yes \
    --I1=32768,8,64 --D1=32768,8,64 --LL=16777216,1,64 \
    --callgrind-out-file=/study/profiles/SAMPLE.cg \
    /study/bin/ARM-profile cc -Isrc -Ibuild/generated \
    -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v \
    -fsource-metrics=/study/profiles/SAMPLE.metrics \
    src/buster/apps/ide/ide.c -lm -o /study/profiles/SAMPLE-self
```

Run each arm twice; compare full output bytes. The runtime is needed on this
host because a trivial native `true` already faults in the AVX-512 loader;
changing Buster's compile ISA alone does not fix the loader or CRT boundary.

## Native counters, wall time and RSS

Build the existing repository `tools/performance_evidence.c` as documented
in the benchmarking guide. Run from the frozen source directory. All native
comparisons here inherit CPU-0 affinity from `taskset -c 0`. Use one warmup
and six measured pairs for A/A, then one warmup and ten measured A/B pairs.
The same compiler argument suffix is used in every run:

```text
cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g -v
-fsource-metrics=../native-production.metrics src/buster/apps/ide/ide.c -lm
-o ../native-production-self
```

For work counts, use `compare --claim work-reduction --tool perf-stat`,
`--events task-clock,instructions,cycles,branches,branch-misses`, exact
`--baseline`/`--candidate` paths and revision flags, and a fresh `--out`
directory. For elapsed time and resources, use a separate
`compare --claim native-runtime --tool time` campaign. Neither is Zen 5
evidence. The wall-time campaign is inconclusive; it is retained unchanged.

## Independent ABI projection oracle

Compile `layout_probe.c` once against the frozen baseline's `target.c` and
once against the final source, using matching include/generated directories:

```sh
clang -O2 -ffunction-sections -fdata-sections -fwrapv \
    -fno-strict-aliasing -funsigned-char -DBUSTER_UNITY_BUILD=0 \
    -I/absolute/source/src -I/absolute/generated \
    layout_probe.c /absolute/source/src/buster/lib/target.c \
    -Wl,--gc-sections -o /absolute/output/layout-probe
```

Each executable writes 1,512 complete 280-byte records. Redirect each output
to its own file and compare with `cmp`; `abi-layout-comparison.json` retains
the independent result hashes and byte counts. These are old-versus-new
implementations, not two APIs backed by the same new helper.
