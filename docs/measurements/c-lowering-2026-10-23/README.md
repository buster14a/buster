# Selective lowering qualification, 2026-10-23

This records the standalone emission planner introduced by PR #325 for #129.
The compiler sources are from `5a9e05048c7c4494491bf2eb62d9f326cf940218`;
`3335607dc130e93e1fc520bf0b8d83cf44065f63` adds its CI benchmark invocation.
Neither revision supplies the complete semantic source producer or backend
integration. These results do not measure actual bodies lowered or `main.c`
compile time saved.

The required [9700X CI run 39935948376](https://github.com/buster14a/buster/actions/runs/39935948376)
passed with Clang 23.1.1. It tested merge
`35690a73d0262e81bef8ea768d94fe6c0de9239d`, combining PR head `3335607` with
main `635434f2a4d153c6de1dd2f1149055c4507c6f46`. The runner hooks held the
shared host lock for the job.

- `test-lowering --clang`: 3,990,695 checks, zero failures, 14 manually
  modeled C fixtures, all 56 required Clang object-symbol comparisons.
- Parser corpus regression: 24 repository translation units, 17,343,750
  tokens, all accepted by Clang, zero mismatches or scalar/vector differences.
- No-recursion check: 32 translation units, 16,605 functions, 90,780 calls,
  zero disallowed cycles; the three pre-existing allowed cycles remain.
- All six lowering benchmark shapes passed their independent result checks.

[The raw CI benchmark output](ci-benchmark.log) retains the single process's
21-sample medians for vector selection, scalar selection and preparation plus
vector selection. The workflow pins this smoke run to its first allowed CPU.
These are unconditioned single-operation observations. They establish neither
sustained vector timing nor repeatability across processes. The vector path
was faster on the small-live-set, chain, cycle and large cases and slower on
fanout and duplicate-heavy cases; no general vector-speedup claim follows.
Preparation is included in its own measurement, but there is no
preparation-plus-scalar comparison.

The separate target build has SHA-256
`4c634879d239980d271ddb60cd64f3f44c18c685457ef0756b759e7c2b77d5d2`.
Every file in [the source manifest](source.sha256) matched the target tree.
Both that normal build and a focused AddressSanitizer/UndefinedBehaviorSanitizer
build completed the same 3,990,695 checks and all 56 Clang comparisons with
zero failures. Build and test commands acquired the shared lock separately.
The sanitizer compile used Clang 23.1.1 with `-O1 -g -march=native -fwrapv
-funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function
-fsanitize=address,undefined -fno-omit-frame-pointer -pthread`. Its entry point
included `lane.c`, `c_lowering.c`, `c_lowering_test.c` and
`c_lowering_benchmark.c` and called `run_c_lowering_tests(argc - 1, argv + 1)`.

The exact [focused entry point](sanitizer-main.c) is retained here. On the
approved host, from `/home/david/buster-fast-bench/codex-lowering-129`, the
frozen checkout was `tree/` and this entry point was `lowering-main.c`.
The sanitizer commands were:

```sh
EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh \
  clang -O1 -g -march=native -Itree/src -fwrapv -funsigned-char \
  -fno-strict-aliasing -Wall -Werror -Wno-unused-function \
  -fsanitize=address,undefined -fno-omit-frame-pointer -pthread \
  lowering-main.c -o lowering-sanitize
EXCLUSIVE_OWNER=codex-126-lowering \
  ~/buster-fast-bench/exclusive.sh ./lowering-sanitize --clang
```

The requested-oracle failure control ran the normal binary with
`test-lowering --clang /bin/false` under its own shared lock. It exited 1,
reporting 57 expected failures and zero Clang comparisons. The shell accepted
only that exact exit status. [The diagnostic output](clang-negative.log)
records the failed comparisons; no successful external check is inferred from
this deliberately failing run.

## Repeated target measurements

The repeatability campaign uses the same frozen normal executable above on
physical `benchpress`, pinned to CPU 2. [Host metadata](host.txt) records the
performance governor, CPU 2 sibling list (`2`) and `perf_event_paranoid`
setting (`-1`). Every retained measurement has hardware counts. Each process acquires
the shared `exclusive.sh` lock separately and yields to CI. A pass consists of
these three commands, in this order, repeated for three passes from the frozen
`tree/` directory:

```sh
EXCLUSIVE_OWNER=codex-126-lowering ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 ./build/compiler benchmark-lowering
EXCLUSIVE_OWNER=codex-126-lowering ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 ./build/compiler benchmark-lowering --only vector --before-vector 100
EXCLUSIVE_OWNER=codex-126-lowering ~/buster-fast-bench/exclusive.sh \
  taskset -c 2 ./build/compiler benchmark-lowering --only vector --after-scalar 100
```

Each process measures all six deterministic synthetic shapes. After one warm-up,
it reports the median of 21 single-operation samples; the default run rotates
the order of vector, scalar and preparation-plus-vector measurements. The
conditioned runs execute their specified path for 100 ms on a separate prepared
plan immediately before each vector measurement. Both the measured output and
the final conditioning output are checked outside the measured interval.

The default samples are unconditioned. The vector-conditioned and
scalar-conditioned samples measure selection after those specific histories;
they are not a complete frontend workload. Preparation includes reservation,
first touch, validation, copying and CSR construction followed by vector
selection. There is no preparation-plus-scalar comparison. Hardware counts
include clock reads and exclude kernel/hypervisor work; wall time includes
first-touch costs. Workspace figures are allocated planner-column capacities,
including scratch/output alignment, rather than a count of bytes touched.
They exclude the arena's extra virtual reservation and input/oracle storage.

CI and other owners' approved work can run between processes, so these repeats
include inter-process variation under that schedule. The fixed ordering is not
a randomized paired experiment. Source files and the executable remain frozen;
later main integrations and their fresh CI builds are separate correctness
evidence, not the executable measured by this campaign.

All nine processes exited successfully, completing at **2026-10-23 13:59:11
UTC**. The final executable hash still matched the frozen hash above. All six
graph inventories and selected/skipped counts matched across the nine logs;
every measured output passed the benchmark's independent result checks.
All 148 source/build-manifest entries were also rechecked on the target after
the campaign and still matched.

The [process median record](benchmark-process-medians.tsv) retains all 90 measurement
rows with nanoseconds, cycles, instructions and traversal-work counts.
[The summary](benchmark-summary.tsv) gives the median, minimum and maximum
of each metric across the three process medians, not a pooled 63-sample
distribution. [Graph metadata](benchmark-graphs.tsv) records definitions,
references, roots, selected/skipped functions and objects, and workspace
capacities. Instruction counts were identical across all three repeats of
each condition/shape/variant.

The table cells below are **median (minimum–maximum), in nanoseconds**, across
the three process medians. Three repeats describe the observed spread in this
campaign, not a confidence interval.

| Shape | Default vector | Default scalar | Prepare + vector |
| --- | ---: | ---: | ---: |
| intrinsic | 942 (892–1,312) | 2,275 (2,274–3,316) | 28,744 (28,172–40,897) |
| fanout | 12,183 (12,183–12,203) | 8,677 (8,375–8,686) | 41,869 (41,828–42,029) |
| chain | 35,677 (35,597–36,729) | 47,980 (47,529–50,204) | 116,329 (115,677–116,679) |
| cycle | 35,587 (35,326–35,607) | 47,890 (47,539–50,114) | 116,198 (115,426–117,040) |
| duplicates | 21,610 (21,390–21,651) | 5,901 (5,881–5,911) | 491,773 (491,772–493,024) |
| large | 192,701 (191,679–193,834) | 256,882 (256,321–257,013) | 769,484 (764,574–769,493) |

| Shape | Vector after 100 ms vector | Vector after 100 ms scalar |
| --- | ---: | ---: |
| intrinsic | 871 (862–872) | 872 (871–872) |
| fanout | 12,163 (12,142–12,173) | 12,233 (12,212–12,463) |
| chain | 35,035 (35,026–35,056) | 35,055 (35,006–35,286) |
| cycle | 35,006 (34,966–35,046) | 35,277 (34,996–36,198) |
| duplicates | 21,390 (21,119–21,400) | 21,480 (21,230–21,490) |
| large | 191,018 (184,165–191,630) | 183,815 (183,545–184,647) |

In these default selection measurements, vector and scalar time ranges are
separated: vector is faster for intrinsic, chain, cycle and large, while fanout
takes 1.40 times and duplicates 3.66 times the scalar median. The short
unconditioned intrinsic case has substantial wall-time spread. Conditioning
changes the history before selection; its effects must be read with the
reported ranges rather than treated as a universal speedup. Neither graph
selection nor preparation measures body checking, body lowering, backend work,
or real `main.c` compile time saved.

The raw outputs below retain every benchmark line. Only the wrapper's
lock-wait/owner display lines were removed; no result or diagnostic was filtered.

| Process repeat | Unconditioned | Vector-conditioned | Scalar-conditioned |
| --- | --- | --- | --- |
| 1 | [default 1](benchmark-qualified-default-1.log) | [before-vector 1](benchmark-qualified-before-vector-1.log) | [after-scalar 1](benchmark-qualified-after-scalar-1.log) |
| 2 | [default 2](benchmark-qualified-default-2.log) | [before-vector 2](benchmark-qualified-before-vector-2.log) | [after-scalar 2](benchmark-qualified-after-scalar-2.log) |
| 3 | [default 3](benchmark-qualified-default-3.log) | [before-vector 3](benchmark-qualified-before-vector-3.log) | [after-scalar 3](benchmark-qualified-after-scalar-3.log) |

The semantic handoff and outstanding source/corpus acceptance are described
in [the lowering guide](../../c-lowering.md).
