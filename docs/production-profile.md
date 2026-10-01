# Production Clang PGO/LTO profile

`production_profile` is the reproducible, opt-in production-throughput build.
It does not change the ordinary developer, Debug, Release, sanitizer, fuzzing,
or CI defaults.

## Prerequisites

Run from a clean Git checkout on Linux or macOS with one LLVM toolchain on
`PATH`:

- Clang;
- LLD;
- `llvm-profdata`;
- `llvm-readobj`;
- CMake and Ninja as required by the normal build.

The command accepts explicit paths for Clang, `llvm-profdata`, and
`llvm-readobj`. The compiler executable is hashed and the PGO-use CMake
configuration rejects a profile produced by a different compiler binary.

## Clean-checkout workflow

```sh
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable \
  build.c -o build/build

./build/build production_profile \
  --output build/production-profile \
  --benchmark-profile smoke \
  --pairs 8 \
  --warmups 1
```

The output must be beneath the checkout's real `build/` directory and may
not contain `.` or `..` path components. Use `--clean` only to replace
such an output; the containment check runs before recursive deletion.
`--no-benchmark` keeps profile generation, the final build, section
inspection, and correctness validation while omitting the comparison matrix.

The workflow builds these controlled Release variants with the same Clang,
LLD, source tree, native target, tests, unity setting, and frame-pointer
policy:

1. current Release defaults;
2. explicit `-g0`;
3. `-g0` plus ThinLTO;
4. `-g0` plus PGO;
5. `-g0` plus PGO and ThinLTO.

Two additional instrumented trees are used only for training: `instrumented`
(no LTO) and `instrumented-lto` (ThinLTO).

## One profile per LTO mode

Clang's IR PGO hashes each function's control flow at the point where
instrumentation runs. The ThinLTO pre-link pipeline reaches that point after
different simplification than a non-LTO build; for example, it skips IPSCCP
function specialization. A profile trained without LTO therefore fails a
ThinLTO use build with `function control flow change detected (hash
mismatch)` under `-Werror=profile-instr-out-of-date` once a function's
pre-instrumentation shape diverges between the two pipelines (issue #2082).

Each PGO-use tree consumes only the profile trained in its own LTO mode:

| Use tree | Instrumented tree | Profile root |
|---|---|---|
| `pgo-lto` (production) | `instrumented-lto` | `profile/` |
| `pgo` (comparison) | `instrumented` | `profile-no-lto/` |

The mode is part of each training contract and fingerprint, and the manifest
repeats it as `build_lto=ON|OFF`. A PGO-use configure fails when that value
differs from the tree's `BUSTER_LTO`.

## Training contract

Each training runs the repository-owned deterministic throughput harness with its
`ci` corpus, all compiler modes, one paired sample, one warmup, output
identity checks, and regression gating disabled. The instrumented compiler is
used on both sides so the run is a workload, not a performance comparison.
`LLVM_PROFILE_FILE` uses a bounded `%m` pool and `llvm-profdata merge`
produces one `merged.profdata` per profile root.

Each `training-contract.txt` fixes and hashes:

- source commit and tree;
- Clang executable and identity;
- `llvm-profdata` executable and identity;
- target identity;
- workload profile, modes, sample count, warmup count, artifact, and build
  policy, including `build_lto`.

Each `manifest.txt` binds the contract fingerprint, merged-profile hash,
source revision/tree, compiler hash, and LTO mode. A PGO-use configure is a
hard error when the profile or compiler hash differs, the checkout is dirty,
the commit or tree differs, the expected fingerprint differs, or the LTO mode
differs; the build fails when Clang reports out-of-date instrumentation.

The output root must be fresh. This prevents a raw profile or build tree from
a previous run from being admitted silently.

## Validation and evidence

The final compiler is
`build/production-profile/pgo-lto/Release/ide` (or the equivalent selected
output root). The workflow:

- inspects its section table with `llvm-readobj` and rejects DWARF/debug
  sections;
- builds and runs the `test_all` correctness target in the PGO+LTO tree;
- benchmarks Release against `-g0`, LTO, PGO, and PGO+LTO with
  `--require-identical-output`;
- writes every command/status and captured identity/inspection output under
  `evidence/`;
- writes the final paths, hashes, validation status, and benchmark status to
  `summary.txt`.

For a longer measurement use `--benchmark-profile ci` or
`--benchmark-profile full` and increase `--pairs`. Keep training unchanged so
profiles from separate runs have the same declared workload contract.

## Focused contract test

```sh
./build/build production_profile_self_test
```

This checks strict integer parsing, output containment, command option
validation, training fingerprint stability, and debug-section detection
without performing expensive compiler builds.
