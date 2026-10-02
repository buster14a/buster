<!-- buster-ci-build-classification-v1-2026-10-02 -->
# Build-time classification: Debug / Release / sanitizer / fuzz / compiler / OS / architecture

**Evidence addendum, 2 October 2026.** This classifies the existing measured build intervals; it does not introduce new performance samples or alter historical evidence.

## Scope and source

- A3 combined checks: [run 36991068435](https://github.com/buster14a/buster/actions/runs/36991068435), attempt 1.
- C4 split checks: [run 36996168725](https://github.com/buster14a/buster/actions/runs/36996168725), attempt 1.
- Both measurement sources: `e424b387fcb51b00c1d19e87e2d369ae8a212792`; source tree `b2ab099b499f7d5c8e91de92312f270c475c0a8f`.
- Original report, all 27 raw artifact ZIPs, manifests and replay remain in the [unchanged archive](../INDEX.md), index commit `1db6dfa892bd08945a1cccaa9473530c05a19762`, data commit `999f03babd32d1e97758ea6f0c3a30dae63a1551`.
- Classification: **26 desktop archives, 44 build trees, 46 logical configuration rows; 22 trees / 23 rows per sample.** The separate analyzer archive is hash-verified but excluded from build totals.

## Measurement definitions

**All times below are summed build-phase elapsed task-minutes unless marked seconds. They are not workflow wall time, runner occupancy, billed minutes or CPU time.** Concurrent tree intervals overlap. Each marginal table is a different partition of the same total: do not add the sanitizer, compiler, OS and configuration tables to each other. Percentages use only build time as the denominator, unlike the original generation/build/test percentage table.

`build` is the existing native observer's compile/link/pre-test-build/producer-clean interval. Self-host validation, generation, test payload, post-test work, setup, queue delay, non-desktop jobs and standalone analysis are outside these build totals. In-process compiler/reference/link operations inside the test payload remain test time in this phase-level view. See the [pinned phase contract](https://github.com/buster14a/buster/blob/e424b387fcb51b00c1d19e87e2d369ae8a212792/docs/ci-matrix-phases.md).

Configuration and flags come from the plan, phase summary, selected coverage row IDs and retained CMakeCache.txt, not job names. A release/checks shard label is not treated as the actual build configuration. `sanitizer=on/off` means recorded BUSTER_SANITIZE policy, not an independently reconstructed per-object instrumentation census. `fuzz_support=on/off` means BUSTER_FUZZ_AVAILABLE: building fuzz support is not time spent executing a fuzzing campaign. Compiler is the host build toolchain; OS/architecture are the desktop lane identity, with exact compiler target/hash/version retained in the raw plan and regenerated records.

**macOS shared builds stay in `Debug+Release (shared)`.** Both configurations occur in one measured build tree. Assigning the whole interval to each configuration would double-count; dividing it 50/50 would invent attribution. The category is known; the split of that shared build interval is not measured.

Observed category differences are not isolated estimates of the cost of enabling sanitizers/fuzzing, compiler speed rankings, architecture performance or an accepted split-layout speedup. Hardware, worker allocation, coverage, unity policy, configuration and contention differ. No unsupported/unselected combination is fabricated as a zero-duration build.

## Totals

| Sample | Trees | Build seconds | Build task-minutes |
|---|---:|---:|---:|
| A3 combined | 22 | 3893.006979 | 64.883449650 |
| C4 split | 22 | 2367.750329 | 39.462505483 |

## Configuration

| Configuration | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| Debug | 34.027 | 52.44% | 17.845 | 45.22% |
| Debug+Release (shared) | 5.057 | 7.79% | 5.144 | 13.03% |
| Release | 25.800 | 39.76% | 16.473 | 41.74% |

## Configuration x sanitizer x fuzz support

| Configuration | Sanitizer | Fuzz support | A3 min | A3 build share | C4 min | C4 build share |
|---|---|---|---:|---:|---:|---:|
| Debug | off | off | 28.014 | 43.18% | 15.500 | 39.28% |
| Debug | on | on | 6.012 | 9.27% | 2.345 | 5.94% |
| Debug+Release (shared) | on | off | 5.057 | 7.79% | 5.144 | 13.03% |
| Release | off | off | 3.430 | 5.29% | 2.813 | 7.13% |
| Release | off | on | 6.932 | 10.68% | 7.325 | 18.56% |
| Release | on | off | 15.438 | 23.79% | 6.336 | 16.05% |

## Sanitizer policy

| Sanitizer | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| off | 38.376 | 59.15% | 25.638 | 64.97% |
| on | 26.507 | 40.85% | 13.824 | 35.03% |

## Fuzz-support policy

| Fuzz support | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| off | 51.939 | 80.05% | 29.792 | 75.49% |
| on | 12.945 | 19.95% | 9.670 | 24.51% |

## Compiler implementation

| Compiler | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| Apple Clang | 6.870 | 10.59% | 6.313 | 16.00% |
| Clang | 29.999 | 46.24% | 17.649 | 44.72% |
| GCC | 12.228 | 18.85% | 6.887 | 17.45% |
| MSVC | 1.756 | 2.71% | 0.829 | 2.10% |
| Zig cc | 14.031 | 21.63% | 7.785 | 19.73% |

Clang-family totals (including Apple Clang): **36.869 minutes / 56.82% in A3; 23.962 minutes / 60.72% in C4**. Do not add those family totals to the vendor-split compiler table. MSVC is the plan's `cl` compiler; Zig cc uses Zig's C compilation driver.

## Operating system

| OS | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| linux | 24.423 | 37.64% | 11.958 | 30.30% |
| macos | 13.027 | 20.08% | 12.225 | 30.98% |
| windows | 27.434 | 42.28% | 15.280 | 38.72% |

## Architecture

| Architecture | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| aarch64 | 26.296 | 40.53% | 20.034 | 50.77% |
| x86_64 | 38.588 | 59.47% | 19.428 | 49.23% |

## OS x architecture

| OS | Architecture | A3 min | A3 build share | C4 min | C4 build share |
|---|---|---:|---:|---:|---:|
| linux | aarch64 | 11.349 | 17.49% | 5.847 | 14.82% |
| linux | x86_64 | 13.073 | 20.15% | 6.111 | 15.49% |
| macos | aarch64 | 13.027 | 20.08% | 12.225 | 30.98% |
| windows | aarch64 | 1.919 | 2.96% | 1.962 | 4.97% |
| windows | x86_64 | 25.514 | 39.32% | 13.317 | 33.75% |

## Unity policy (additional control)

| Unity | A3 min | A3 build share | C4 min | C4 build share |
|---|---:|---:|---:|---:|
| off | 54.521 | 84.03% | 29.325 | 74.31% |
| on | 10.362 | 15.97% | 10.138 | 25.69% |

## Full joint classification

The 22 rows below describe every classified build tree in each sample. Each row joins OS, architecture, compiler, configuration, sanitizer and fuzz policy; unity is retained as an additional control. All times are **seconds**, not minutes. Exact artifact/tree IDs and integer microseconds are in [builds.csv](derived/builds.csv).

| OS | Architecture | Compiler | Configuration | Sanitizer | Fuzz support | Unity | A3 build s | C4 build s |
|---|---|---|---|---|---|---|---:|---:|
| linux | aarch64 | Clang | Debug | on | on | off | 116.334189 | 43.290750 |
| linux | aarch64 | Clang | Release | off | on | on | 174.072120 | 172.061187 |
| linux | aarch64 | Clang | Release | on | off | off | 185.752233 | 74.211553 |
| linux | aarch64 | GCC | Debug | off | off | off | 105.868270 | 29.451565 |
| linux | aarch64 | Zig cc | Debug | off | off | off | 98.931653 | 31.784671 |
| linux | x86_64 | Clang | Debug | on | on | off | 104.110695 | 38.619004 |
| linux | x86_64 | Clang | Release | off | on | on | 100.841231 | 124.682231 |
| linux | x86_64 | Clang | Release | on | off | off | 271.779595 | 122.271646 |
| linux | x86_64 | GCC | Debug | off | off | off | 159.239231 | 38.831083 |
| linux | x86_64 | Zig cc | Debug | off | off | off | 148.426000 | 42.256120 |
| macos | aarch64 | Apple Clang | Debug+Release (shared) | on | off | off | 303.391235 | 308.614230 |
| macos | aarch64 | Apple Clang | Release | off | off | on | 108.825564 | 70.172172 |
| macos | aarch64 | GCC | Debug | off | off | off | 216.790876 | 211.057389 |
| macos | aarch64 | Zig cc | Debug | off | off | off | 152.627156 | 143.661338 |
| windows | aarch64 | Clang | Release | off | off | on | 96.978133 | 98.600856 |
| windows | aarch64 | MSVC | Debug | off | off | off | 18.170709 | 19.136511 |
| windows | x86_64 | Clang | Debug | on | on | off | 140.294954 | 58.807432 |
| windows | x86_64 | Clang | Release | off | on | on | 141.017277 | 142.759450 |
| windows | x86_64 | Clang | Release | on | off | off | 468.756453 | 183.650628 |
| windows | x86_64 | GCC | Debug | off | off | off | 251.757216 | 133.854647 |
| windows | x86_64 | MSVC | Debug | off | off | off | 87.160411 | 30.601014 |
| windows | x86_64 | Zig cc | Debug | off | off | off | 441.881778 | 249.374852 |

## Toolchain versions recorded in these artifacts

Both samples retain the same displayed version strings for their respective OS/architecture/compiler cells. This does not establish identical CPU features or performance conditions. Full target triples, compiler hashes and raw version output remain in each native plan; regenerated build_records.json retains the target, binary hash and version banner.

| Compiler / platform | Observed version |
|---|---|
| Upstream Clang, Linux and Windows | 23.1.2; revision 85ac560262434c9ccfc0c183ec22d4138ed647fb |
| Apple Clang, macOS | 21.0.0, clang-2100.1.1.101 |
| GCC, Linux | Ubuntu 15.2.0-16ubuntu1, GCC 15.2.0 |
| GCC, Windows x86-64 | MinGW-Builds x86_64-posix-seh-rev1, GCC 15.2.0 |
| GCC, macOS | Homebrew GCC 15.3.0 |
| Zig cc | Zig 0.16.0 |
| MSVC, Windows x86-64 / AArch64 | 19.51.36260; distinct x64 and ARM64 targets |

## Findings and interpretation

1. **Debug-only builds are the largest A3 configuration bucket: 34.027 minutes / 52.44%.** Most of that is unsanitized GCC/Zig/MSVC portability work: 28.014 minutes, compared with 6.012 minutes of sanitized, fuzz-enabled Clang Debug build work. This is build accounting; the original long Debug test payload is a separate metric.
2. **Release sanitizer build work is substantial:** 15.438 minutes in A3 and 6.336 minutes in C4, across Linux x86-64/AArch64 and Windows x86-64. The largest A3 single build interval is Windows x86-64 sanitized Clang Release, 468.756453 seconds. That is observed cost under that schedule, not a measured sanitizer premium.
3. **Windows x86-64 contributes 25.514 build task-minutes in A3**, but its GCC, Zig and Clang cells do different work and have different quotas. The Windows AArch64 artifact population has only Clang Release and MSVC Debug here; the smaller total is not proof that ARM builds the same matrix faster.
4. **Fuzz-support-on totals cannot be called fuzzing time or fuzz overhead.** They include three unsanitized Release trees plus three sanitized Debug trees per sample. The on/off columns are not a factorial experiment.
5. **Shared macOS build attribution remains an explicit measurement boundary**, not missing data silently discarded. The full interval contributes once to OS/compiler/sanitizer totals and once to the joint configuration bucket.
6. The existing #709 / #1826 / #2120 / #2119 owners remain unchanged. This addendum creates no duplicate optimization, instrumentation or sanitizer issue and does not reopen #2033 or #1885. It does not alter the archived analyzer correction or historical qualification status.

## Validation actually performed

The published backup artifact was downloaded and its outer SHA-256 and contained published evidence ZIP were verified against the archived receipt. Classification was repeated on both delivered and published evidence ZIPs; all eight regenerated outputs match byte-for-byte. All 27 raw artifact hashes were checked against the retained published digests inside the hash-pinned original evidence bundle. All 44 classified trees agree across native plan, summary, selected coverage row IDs and configured CMake cache. The 46 logical configuration rows are unique across sibling jobs and match the executed row sets. Every requested marginal and joint grouping reconciles exactly in integer microseconds to A3 3893006979 and C4 2367750329. No build interval is counted twice within a grouping.

**12 offline classifier checks passed**, including corrupt outer bundle rejection, incomplete summary rejection, source mismatch, negative build interval, cache-policy disagreement, missing-tree conservation failure, invalid/duplicate configuration, shared-build accounting, compiler vendor/family separation and fuzz/sanitizer independence. These are Python data-analysis controls, not Buster compiler tests or a full independent native-journal/assertion qualification replay. Unknown CPU time/RSS and historical unmatched host conditions stay unknown.

No Buster compilation, test suite, analyzer, benchmark, cloud CI dispatch, laptop or 9700X execution was performed for this classification. Only local cloud-sandbox parsing and classifier tests ran. No production source, workflow, default, acceptance policy or original archive member was changed.

## Reproduce and inspect

The [parent evidence.zip](../evidence.zip) already retains the raw ZIPs. Run from this directory:

```sh
python3 -B classify_builds.py --bundle ../evidence.zip --out derived
python3 -B classify_builds_test.py
```

The classifier accepts the unchanged delivered ZIP or published normalized ZIP by outer SHA-256. It reads data only and never executes archived commands/binaries. Set BUSTER_CI_CLASSIFICATION_BUNDLE to an alternate location for the tests. It uses only Python's standard library and explicit exceptions (its validation does not depend on assert statements).

Generated outputs: build_records.csv/json (44 fully attributed trees), build_by_dimension.csv, build_by_all_dimensions.csv, build_by_configuration_sanitizer_fuzz.csv, build_by_os_architecture_compiler.csv and validation.json. The compact published derived/builds.csv retains all 44 build observations with exact artifact/tree IDs. Richer compiler/source/cache metadata is reproducible from the unchanged raw evidence rather than duplicated into every report table.

**License/provenance:** Buster first-party terms remain unselected/unresolved, per [LICENSES/README.md at the measured source](https://github.com/buster14a/buster/blob/e424b387fcb51b00c1d19e87e2d369ae8a212792/LICENSES/README.md) and #621. This adds first-party diagnostic accounting only; no external implementation, dependency or asset was imported. Compiler version classification is not a new audit of upstream compiler distributions.
