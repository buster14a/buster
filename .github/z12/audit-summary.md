## Decision and scope

**Not accepted as a performance improvement.** This is a measured but inconclusive prototype for [#130](https://github.com/buster14a/buster/issues/130), under [#128](https://github.com/buster14a/buster/issues/128), in [draft PR #377](https://github.com/buster14a/buster/pull/377). No allocator correctness defect or physical Zen 5/Zen 4 speedup is claimed. #351 edit sorting remains separate.

Baseline/main: `ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae`. Source candidate: `820cb87376eaa626ae593238ef6fbaaabfc2c992`; tree `93ee044324a732e92ed5aff0e872235c3bd94b47`. Timing preceded publication and labels the candidate `ef99-plus-patch-a52c38d386375ffc`; its recorded tree exactly matches the source commit. Patch SHA256: `a52c38d386375ffc64b7e08370ef9d2c75d5ce68172fea72e3edea38c934d9ed`.

Current main already has held/dirty masks, sparse set-bit walks, the live-owner inverse map, and merged #135's guarded full-mask helper. The limit is still 48; historical full-file scanning is not the current premise. #135's old shared-Zen-4 timings did not establish a gain and are not current evidence. Current machine/SIMD/parallelism/benchmarking/workflow guides and the latest baseline audit, `2026-09-09T163649Z`, were read. Pre-direct-SSA census numbers were treated only as leads. Historical Forgejo #590/#601 map to GitHub #37/#46; current numbers were not remapped.

Initial paginated inventory recovered 137 open issues and two PRs; source publication rechecked 14 then-open PRs and #130 comments. #362 and #369 share the machine test file but their reviewed aggregate/constraint hunks are separate. #135's patch/comments and the exact owner-helper closed-PR search were read. Every historical discussion and all broad closed-search results were not exhaustively reviewed.

`machine_fast_owner_contains` specializes contract retention and pending blockers in `machine_fast_conform_edge`, and dirty-edge membership in `machine_fast_placement_build_prepassed`. Sparse membership stops at the first match. Dense AVX-512 eligibility reuses the full-mask helper. Split duplicate removal still needs every match and is unchanged. Serial bind/spill/LRU policy, ties, placements, row sizes and edit sorting are unchanged. No new primitive, allocation, owner/death mirror or maintained index is shipped. The live-owner inverse map does not directly answer a different saved contract/snapshot bank; a second inverse map was not added without maintenance evidence.

The existing guarded owner tests gain 2,818 Boolean differential checks: null/empty, all exact tails through lane 63, sparse/alternating/dense masks, duplicate/high-bit owners, first/last hits and misses. Full-mask and placement/picker regressions remain intact. There is no parallel harness. New AVX2/full-width comparisons and death-index batching were not prototyped; the measured populations below do not establish a useful full-width population.

## CI evidence and reproduction

All compilation, tests and diagnostic execution occurred in repository CI. Local work was source/diff editing and analysis of downloaded artifacts. Custom builds used two jobs and separate trees; no destructive generation overlapped a build in the same directory.

Successful native experiment job: [34424477857 / 102706676170](https://github.com/buster14a/buster/actions/runs/34424477857/job/102706676170). Its run is red because a separate validation job lost its driver when generation cleared `build/`, before candidate compilation. Corrected validation uses `$RUNNER_TEMP`.

Artifact `z12-experiment-34424477857-1`, ID `10132210799`, SHA256 `6f22143f57e802fa75af39ddfb7543b842ce64713b030a1d786be476ac212899`, retains native samples, paired uncertainty, metadata, commands, frozen inputs/output hashes, telemetry, compiler flags, code sizes and diagnostic census. Retention is seven days, not permanent.

The trusted driver ran these commands sequentially on both pinned trees:

```sh
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable \
  build.c -o "$RUNNER_TEMP/z12-build"
"$RUNNER_TEMP/z12-build" generate \
  --build-directory build/throughput-ci-compiler --ci --cc clang -- \
  -DBUSTER_INCLUDE_TESTS=OFF -DBUSTER_UNITY_BUILD=OFF \
  -DBUSTER_BENCH_ALLOCATIONS=OFF -DCMAKE_LINKER_TYPE=DEFAULT
"$RUNNER_TEMP/z12-build" build \
  --build-directory build/throughput-ci-compiler --config Release -t ide -- -j2
```

The first timing job's driver was `candidate/build/build`, outside its generated `build/throughput-ci-compiler`; it used identical generate/build options above. From the candidate root it ran:

```sh
./build/build bench_throughput self-test
./build/build bench_throughput run \
  --baseline "$GITHUB_WORKSPACE/baseline/build/throughput-ci-compiler/Release/ide" \
  --candidate "$PWD/build/throughput-ci-compiler/Release/ide" \
  --baseline-id ef99ec72cf0f17c8818a1d4b4c106fd5908b5eae \
  --candidate-id ef99-plus-patch-a52c38d386375ffc \
  --output ../evidence/throughput --profile ci --mode all \
  --pairs 20 --warmups 2 --timeout 120 --cpu auto \
  --require-identical-output --pmu
```

Host: Ubuntu 26.04.1 image `20260907.131.1`, Clang 21.1.8, reported AMD EPYC 7763 under Microsoft full virtualization, four vCPUs/two reported cores/two threads per core. Timing affinity CPU 0; SMT active. This is not verified physical Zen 5 or Zen 4. The host flags do not include AVX-512. `perf_event_paranoid=4`; PMU replays returned errno 13. Cycles/instructions/branches/misses and work-normalized PMU counters are unavailable, not zero. Allocation-instrumented replays and allocator phase timings were not collected.

The retained compile command has `-O3 -DNDEBUG` followed by repository `-O2` (effective O2), `-march=native`, `-fwrapv`, `-fno-strict-aliasing`, `-funsigned-char`, and existing strict warning/error flags. Timing builds disable tests, unity, sanitizers, LTO and allocation instrumentation; correctness builds are separate. Complete CMake caches and `compile_commands.json` are retained. Compiler files are both 17,455,568 bytes; baseline SHA256 `7d4c8483c6cb68d377b58c2604e025aa4aa15364ec614dc79c71627b94f1610b`, candidate `3e124bf44d76537865b4ee1a5c3bbb84c18b35b7ddffde7ded1ab3f50212d363`.

## Paired generated-corpus results

Six frozen inputs, modes none/mir-stack/fast/quality, two guard rounds, 20 alternating pairs per round: 1,920 measured compiler-process observations, with two warmups per variant/job separate. Timing is new-process launch through wait completion on a warm filesystem, not Clang building Buster or generated-program runtime. PMU and owner diagnostics run separately. Native harness self-tests and exact A/B output identity across all 24 series/all measured observations passed.

**19/24 series are inconclusive; five detect no substantial regression; zero confirmed regressions. No speedup is established.** Existing guards were unchanged: wall growth must exceed 15% and 2 ms, or RSS 20% and 16 MiB, with family alpha 1%, corrected per-test significance and confirmation in both rounds. Coarse nonregression is not small-gain acceptance or equivalence.

Times are independent medians. Paired change is the native harness's geometric combination of per-round paired-median ratios, not the ratio of those independent medians. Intervals are native per-round paired uncertainty, not a new statistical method.

| Series | Baseline / candidate wall ms | Paired change | Round 0 ratio interval | Round 1 ratio interval | Baseline / candidate RSS MiB |
| --- | ---: | ---: | --- | --- | ---: |
| tiny_startup/fast | 22.719 / 23.263 | +0.11% | [0.816815, 1.186811] | [0.935979, 1.222599] | 23.34 / 23.31 |
| large_function/fast | 44.935 / 40.442 | -0.34% | [0.808500, 1.150954] | [0.809278, 1.098594] | 29.95 / 29.96 |
| many_functions/fast | 38.939 / 33.270 | -0.07% | [0.719519, 1.484753] | [0.730587, 1.316546] | 28.89 / 28.89 |
| symbol_table/fast | 35.109 / 35.735 | +0.22% | [0.835279, 1.195583] | [0.819052, 1.416136] | 30.99 / 31.26 |
| control_flow/fast | 76.834 / 70.167 | -0.37% | [0.821404, 1.067675] | [0.939174, 1.085986] | 39.76 / 39.76 |
| backend_pressure/fast | 156.959 / 152.362 | -0.41% | [0.881961, 1.048831] | [0.883517, 1.110586] | 56.17 / 56.18 |
| control_flow/quality | 68.770 / 69.630 | +0.33% | [0.978361, 1.232846] | [0.768041, 1.230223] | 39.76 / 39.72 |

Only control_flow/fast and backend_pressure/fast in this table pass the coarse margin; the other three passing series are backend_pressure/none, mir-stack and quality. All shown intervals include 1. Full CPU/source-normalized throughput tables remain in the native artifact. For example control_flow/fast child CPU medians are 76.4375 / 69.9570 ms, not a causal speedup estimate.

Frozen inputs, seed 20260907, ci scale 1:

| Input | Bytes / lines / functions | SHA256 |
| --- | --- | --- |
| tiny_startup | 133 / 5 / 1 | `4aff03a3b92f04efeff63a3e020ca55b8d98ede8488abeda6974fd62eacb90de` |
| large_function | 51085 / 1029 / 1 | `b1f9fcec7b85a8ccf0489f5b43c95d4dee29cc9ec4fea5b07d053694315fe6cf` |
| many_functions | 39898 / 2049 / 512 | `baf3a60857bffc10dd484591a8f8d11d86888afaeac6e1615143d216016af0c4` |
| symbol_table | 280189 / 4101 / 1 | `f1bee39d36c032a699f75936ac8c926a69ca66fd54fa86658228c5fa5759da20` |
| control_flow | 235398 / 3329 / 64 | `2d7c13661910bbcc01c3465fdb9a724b6e65598033dc60de1dfa3602bfcc4497` |
| backend_pressure | 711531 / 19202 / 64 | `ff18c7018039abf36ff0ad499facd66a9e91f768b264e171579f491550ffb51d` |

## Current query populations

The temporary probe uses a 6,240-byte per-function counter array, not shared mutable global state. It is absent from both timing compilers and the PR. Counters measure site/population/calls/hits/prefix reads; outputs are checked against uninstrumented references. No production maintenance cost is hidden.

Only control_flow in the generated corpus invokes these consumers: per FAST/QUALITY replay, 12,416 empty contract queries, 1,024 one-owner misses and 1,024 two-owner first-hit queries. Full sparse reads total 3,072 versus 2,048 early-exit reads. The other five inputs exercise none of the four consumers. This narrow corpus is not evidence about all compiler workloads.

A [frozen Buster-unity diagnostic](https://github.com/buster14a/buster/actions/runs/34425323578/job/102709218066) separately compiles identical candidate sources with baseline, candidate and instrumented baseline compilers in FAST and QUALITY. The tracked-file hash manifest is unchanged before/after. Input metrics: 314 unique files, 30,025,201 unique bytes, 371,308 unique physical lines, 3,266,123 preprocessed tokens. This is not a paired self-host timing experiment; self-built compilers are not the measured subjects.

| Mode / site | Calls | Hits | Maximum population | Full sparse reads | Early-exit prefix reads |
| --- | ---: | ---: | ---: | ---: | ---: |
| FAST / contract retention | 277261 | 14858 | 10 | 125645 | 110290 |
| FAST / pending blocker | 1 | 1 | 2 | 2 | 2 |
| FAST / dirty edge | 206 | 0 | 10 | 453 | 453 |
| QUALITY / contract retention | 815578 | 32769 | 10 | 280014 | 251507 |
| QUALITY / pending blocker | 1 | 1 | 2 | 2 | 2 |
| QUALITY / dirty edge | 518 | 0 | 11 | 1090 | 1090 |
| QUALITY / split mask, unchanged | 184 | 3 | 4 | 113 | 108, hypothetical only |

FAST contract retention is 76.80% empty; QUALITY 80.46%. Early exit avoids 15,355 / 28,507 contract-owner reads respectively (12.22% / 10.18% of those query reads), not that fraction of whole-compiler work. Pending blockers and dirty edges save no reads here. Split duplicate removal still requires the complete mask. No population reaches 16; these inputs do not establish useful full-width SIMD work. No extra inverse-map/SoA maintenance was excluded from a claimed gain.

Each variant used the same frozen source root and this command:

```sh
"$COMPILER" cc -Isrc -Ibuild/throughput-ci-compiler/generated \
  -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0 -g \
  -fregister-allocator="$MODE" -fsource-metrics="$METRICS" -v \
  src/buster/apps/ide/ide.c -o "$OUTPUT" -lm
```

All three outputs are byte-identical within each mode. FAST SHA256: `095ce972ee584d5d265decb83014e806a8f4a2fe141064af3cfc59783e446fa4`; QUALITY: `c4c5c698d9d907fe8a0843e95e6f072a696a5c0a2ace4f47539eeed6657aefe6`. Cross-mode identity is not claimed. Artifact `z12-unity-census-34425323578-1`, ID `10132458626`, SHA256 `94c8b2f211de1b8bb2f4a27eb779c74279713c4b0021288e093fe434c4065213`, retains full histograms, original probe logs, commands, metrics, source hashes, emitted files and disassemblies.

## Emitted compiler code and budget

Paired-build `.text`: 3,274,245 -> 3,274,133 bytes (-112); `.rodata`: 13,269,092 -> 13,269,108 (+16). Executable size is unchanged. `machine_fast_conform_edge`: 1,849 -> 1,664 bytes; `machine_fast_placement_build_prepassed`: 18,476 -> 18,556. Production data/BSS/TLS and row sizes are unchanged. The predeclared <=1 KiB `.text` growth budget is met in this pilot; RSS guards were not raised.

The first objdump mistakenly used two `--disassemble` options, capturing only placement. The unity job uses one command/file per symbol for both compilers. Scalar contract-loop inspection shows baseline TZCNT/CMP/SHLX/CMOV/OR/BLSR building a full match mask; candidate TZCNT/BLSR/load/conditional comparisons stop at a match. The candidate introduces a data-dependent branch and final compare; fewer reads do not imply faster execution. Conform-edge stack allocation shrinks 0x58 -> 0x38 with the same six saved general registers. This is static code evidence, not dynamic spill/PMU evidence. No AVX-512 useful-lane, mask-transfer or Zen 5 instruction-throughput claim follows.

## Correctness and remaining acceptance

Exact source SHA 820cb passed [corrected validation](https://github.com/buster14a/buster/actions/runs/34424636843/job/102707215132):

```sh
"$RUNNER_TEMP/z12-build" generate --cc clang --ci --linker DEFAULT
"$RUNNER_TEMP/z12-build" build --config Release -t test_all -- -j2
"$RUNNER_TEMP/z12-build" test_self_host --config Release
"$RUNNER_TEMP/z12-build" test_self_host_audit --config Release
```

This includes full regressions/new guarded cases, ordinary self-host/alternate-backend gates and repeated verified generations. [Normal PR CI](https://github.com/buster14a/buster/actions/runs/34424734932) and [normal throughput](https://github.com/buster14a/buster/actions/runs/34424734995) were also requested. Complete final platform/sanitizer/mode/configuration results are not asserted by this entry. A later document-only commit has a new submitted SHA and must have its own checks inspected.

The unity replays/byte comparisons passed, but that run is red because `python3 tools/new_audit.py --check` rejects unchanged historical entry `2026-08-24T131821Z`: it opens with a heading rather than the required id/parenthetical. The same file exists on the pinned baseline. No historical audit or checker was changed to conceal this failure.

Keep #377 draft. Outstanding: physical Zen 5 paired end-to-end acceptance on representative frozen inputs including Buster unity; separate Zen 4 nonregression; final-SHA supported SIMD/MSVC/AArch64/platform/sanitizer/mode gates; and final memory/portability acceptance. Repository write access and hosted labels do not establish physical hardware access; the connector could not enumerate repository runners. This inconclusive prototype is not a completed #130 optimization. No confirmed losing speedup was accepted, and #351 sorting/death-index batching/general allocator rewrites remain outside scope.
