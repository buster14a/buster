# Evidence for the PR #139 follow-up

The [audit](../../2026-09-06T125122Z.md) is the interpretation of these files.
Compiler production sources remain at `a8454d84`; no throughput optimization
is accepted. Generated corpora, compiler binaries and build trees are omitted.
Log/disassembly trailing whitespace is normalized for presentation; patch
files retain their exact unified-diff context, including blank context lines.

| Files | Meaning |
| --- | --- |
| `kernel-notes.md`, `kernel-results.json` | Final isolated Zen 4-class VM results and limits, including negative populations. |
| `kernel-manifest.json`, `kernel-provenance.json` | Source hashes, corpus counts, compiler/CPU information and measured artifact hashes. Observed temporary paths are provenance, not paths required for replay. |
| `kernel-check.log`, `kernel-timing.log`, `regimes/*.log`, `*.asm` | Differential results, seven alternating pairs with checksums, synthetic distributions and inspected assembly. |
| `literal-reuse-experimental.patch` | **UNLANDED** three-site compiler candidate plus its focused fixture. All three sites have zero frozen-stage-1 hits. |
| `literal-census-instrumentation.patch` | Temporary direct counters applied after that candidate solely for the census; never proposed as production code. |
| `literal-census-*.log`, `literal-ab-result.txt` | Actual dynamic site counts and byte-identical frozen-input pairs. |
| `literal-results-manifest.json`, `literal-validation-notes.txt`, `validation-excerpts.txt` | Candidate/baseline artifact hashes and complete passing gate summaries. Full test logs are represented by excerpts plus original hashes and lengths. |
| `instructions.log`, `*-profile.log`, `*-survey.log` | Measurement failures. `<not supported>` is not zero and does not establish a valid profile. |
| `initial-measurement-attempts.json` | Initial commands/statuses, before llvm-symbolizer installation. The separate survey logs are the subsequent attempts with tool preflight satisfied. |
| `hide-before.c`, `hide-after.c`, `*.readelf.txt` | Successful source-order dependency reproducer for future SPMD work. |
| `layout-probe.c` | Compile-time checks against actual backend structures, not duplicated definitions. |

For kernel replay, build current Debug non-unity objects through `./build.sh`
and follow [the opt-in harness instructions](../../../../tools/bench_pr139_simd/README.md).
Its output directory must be private to the run. Never compare two current
source trees as if they were a frozen-input compiler A/B.

The literal experiment applies to an isolated checkout based on `a8454d84`:

```sh
git apply --check docs/performance-audits/evidence/2026-09-06T125122Z/literal-reuse-experimental.patch
```

Its three-site counters belong inside `c_lower_to_ir` and report at completion.
The targeted fixture gives four hits at each site; the frozen stage-1 corpus
gives none. Reproducing that zero does not require another performance trial.
Do not promote the candidate without a representative workload that exercises
it and the required instruction A/B and full correctness gates.

To reproduce the source-order observation with the saved baseline compiler,
compile each `hide-*.c` fixture with `cc -c -fPIC -v` and inspect
`readelf -Wr -s <object>`. Both succeed and end with hidden `foo`, but the
later call's relocation and function size depend on whether the directive was
already emitted. This is not a report of existing serial nondeterminism.
