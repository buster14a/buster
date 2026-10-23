# Lengths-only SIMD for branch sizing (#305)

This change removes an encode-and-discard pass from branch sizing. The vector
path computes 16 instruction lengths at once, prefix-sums them, and stores
instruction offsets. It reads only each form's flags, shares the encoder's
length rules, and does not construct or store machine-code bytes. The scalar
twin uses the independent scalar encoder rules. Branch sizing drops its
15,424-byte encoded scratch allocation, 1,024-record chunking and offset
rebasing; the caller encodes the sized records once.

The target is Zen 5. Development and the repeated comparisons below ran in
the cloud, **not on Zen 5**. They establish a useful optimization on this
available host, not a Zen 5 throughput ceiling. The PR's ordinary 9700X
qualification is the target-machine acceptance route; its results belong in
the PR, with links to the exact run. No agent work ran on that physical host.

## Revisions and environment

- Baseline: `fc45e674d13d9257d66c02b05d78adf85ddae7ea` in
  [buster14a/buster](https://github.com/buster14a/buster/commit/fc45e674d13d9257d66c02b05d78adf85ddae7ea).
- Candidate: the source hashes in [source-and-binary.sha256](source-and-binary.sha256).
  The source changes are in the commit containing this record.
- Cloud CPU: AMD EPYC 9V74, family 25/model 17, KVM. Nine visible vCPUs;
  cgroup CPU quota is eight CPU equivalents. Single-worker timing uses CPU 2.
  These vCPU identifiers do not establish physical-core topology.
- Module and scaling binaries: Clang 19.1.1, `-O3 -march=native -mmwaitx
  -fwrapv -funsigned-char -fno-strict-aliasing`. Full compiler builds additionally
  use their normal debug and warning flags. The temporary cloud build adds
  `-Wno-bitwise-instead-of-logical` after `-Wall -Werror`, and `-mmwaitx`;
  no build flags were changed in the repository.
- [cpu.json](cpu.json), [cpu-quota.txt](cpu-quota.txt),
  [clang-version.txt](clang-version.txt), and [uname.txt](uname.txt) retain
  host identities. Hardware performance counters were unavailable, so the
  cloud results use `CLOCK_MONOTONIC_RAW`; reported zero cycles/IPC are not data.
- Every build or timing process in this experiment uses the same
  `/tmp/buster-simd-305.lock`. Timing does not overlap other work from this team.

## Single-worker producer and consumer

The old and new production sources were compiled with the **same enhanced
benchmark harness**. The baseline copy differs at exactly one call: the old
`branch_sizing_lengths` takes `columns.encoded`; the new one does not.
Each subprocess uses `--samples 21 --seed 1`, one `--only` row and one
`--measure`. Five fresh before/after process pairs were scheduled, reversing
their order on even-numbered pairs. Each sample processes approximately
262,000 records, with one warmup sample before recorded samples. The benchmark
uses `seed + row index`, even when `--only` selects one row.

The table reports the median of complete process-pair medians, in
**nanoseconds per instruction record**. For an even number of retained
processes the two central values are averaged. A larger speedup means greater
throughput. Exact pair counts, ranges and median paired ratios are in
[cloud-summary.tsv](cloud-summary.tsv).

| Workload | Initial lengths: before → after | Branch sizing: before → after | Sizing + encoding: before → after | Combined speedup |
| --- | ---: | ---: | ---: | ---: |
| tier0-40 | 2.684 → 1.523 | 4.667 → 2.503 | 7.506 → 5.452 | 1.38× |
| tier0-250 | 1.839 → 1.012 | 2.594 → 1.842 | 4.412 → 3.656 | 1.21× |
| tier0-2000 | 1.668 → 0.814 | 2.760 → 1.604 | 4.346 → 3.332 | 1.30× |
| tier0-mix | 2.140 → 1.145 | 3.166 → 2.214 | 5.345 → 4.266 | 1.25× |
| optimized-2000 | 1.638 → 0.880 | 2.736 → 2.057 | 4.777 → 3.596 | 1.33× |

`size_encode` measures sizing and the immediate final encode in one interval;
it is not a sum of separate medians. Both the old and new encoding calls
produce bytes and offsets, without fixup-end collection. Branch restoration
and input warming occur outside this module interval. Scratch acquisition and
all sizing phases are inside it. Errors and mismatched totals invalidate the
sample. Read-only disassembly inspection confirmed that both sizing and final
encoding calls remain in the measured binary.

This is a synthetic compiler-shaped producer/consumer benchmark, not whole-C
compilation. The unchanged `x86_encode` control varies from 0.91× to 1.14× by
ratio of process medians across rows; its mixed-function row is 1.01×.
The cloud's variability limits fine distinctions. The much larger length-pass
improvement is repeated across every row. Do not attribute the control's
movement to an encoder optimization: the full encoder is unchanged.

### Incomplete reports

Some subprocesses did not retain a complete stdout report despite the shell
batch completing. No cause was established. Their timings are unusable, and
their entire before/after pair is excluded whenever either report is missing.

An incomplete `tier0-250/size_encode` pair and five incomplete scaling pairs
were rerun in full, in the same order. Original retained output is in
[incomplete-cloud.txt](incomplete-cloud.txt). The final snapshot still has
four incomplete module pairs and two incomplete scaling pairs, excluded from
all summaries. The final module uses 3–5 complete pairs per case; scaling uses
4–5. No result was excluded on the basis of its measured speed.

[ab-cloud.txt](ab-cloud.txt) and [scaling-cloud.txt](scaling-cloud.txt) preserve
the final report snapshot, with a filename marker even for an empty report.
The incomplete pair list is [incomplete-pairs.json](incomplete-pairs.json).

## Aggregate worker throughput

[scaling.c](scaling.c) compiles unchanged against both source trees.
Each worker pins itself before first touch and owns an independent arena and
generated pool: 262,219 records in 2,883 functions, seed 1, 1,064,719 encoded
bytes per pass. Before timing it checks all sizing outputs and compares every
encoded byte and offset with the scalar encoder. Five warmup passes precede
a barrier. Each worker then completes 200 passes.

Parent wall time includes branch restoration, sizing, final encoding, error
and length checks, lightweight output sampling and the completion barrier.
It excludes pool construction, full reference checking and teardown. Output
stores remain observable through a compiler memory barrier. All runs retain
the same full reference checksum `29cbeeb814da6666`; before/after timed
checksums agree at matching worker and pass counts.

One worker uses CPU 2. Multiple workers use CPUs 1 through N; the mostly
sleeping coordinator uses CPU 0. Work per worker remains constant, so these
are aggregate independent-function throughput / weak-scaling measurements,
not a fixed-job latency experiment.

| Workers | Complete process pairs | Before, million records/s | After, million records/s | Throughput ratio |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 4 | 175.7 | 222.8 | 1.27× |
| 2 | 4 | 289.4 | 397.8 | 1.37× |
| 4 | 5 | 541.1 | 652.8 | 1.21× |
| 8 | 5 | 883.3 | 1077.7 | 1.22× |

At eight workers the retained process medians range from 873.7–899.1 million
records/s before and 1,042.2–1,199.1 million after. The 1.22× median ratio
demonstrates a useful aggregate gain under this VM's eight-CPU quota.
It does not identify the Zen 5 all-core limit or prove whole-compiler scaling.

## Short calls, width choices and lighter phases

[tiny.c](tiny.c) checks counts 0, 1, 4, 8, 15, 16, 17, 31, 32 and 40 against
the scalar encoder before timing. Three rotated process sets, each with
15 samples of 64 passes through 4,096 calls, compare the old lengths helper
with the new one. Both wrappers are `noinline` and aligned to 64 bytes.
Inputs come from the repository's compiler-like generator, seed 305.
[tiny-cloud.txt](tiny-cloud.txt) records all 60 process reports.

No nonempty tiny-call case regressed. Selected medians, ns/call:

| Records | Before | After |
| ---: | ---: | ---: |
| 0 | 2.788 | 2.666 |
| 1 | 53.092 | 38.015 |
| 4 | 37.124 | 20.535 |
| 16 | 34.749 | 18.747 |
| 17 | 77.090 | 46.798 |
| 40 | 94.083 | 56.067 |

The full encoder's production entry already uses its 16-record implementation
for short calls; 4- and 8-record variants are benchmark alternatives, not
automatic short-call dispatch.

[width.c](width.c) compares the retained 16-record loop with 2× and 4× loop
unrolling. These are successive 16-record groups, not a different arithmetic
lane width. [width32.patch](width32.patch) and
[width64.patch](width64.patch) are the entire source differences. Each process
uses 262,144 generated records, seed 305, three warmups, and 15 samples of
eight traversals. Three rotated process sets cover compiler-like and
vector-heavy mixtures and function sizes 40, 250, 2,000 and 262,144.

The width screen used `/usr/bin/clang-19 -O3 -march=native -fwrapv
-funsigned-char -fno-strict-aliasing -Wall -Werror -Wno-unused-function
-ffunction-sections`, with `-Wl,--gc-sections`. Its first include directory
contains the selected encoder copy, followed by the candidate's `src`.
It did not use `-mmwaitx`. The tiny-call screen used Clang 19 with the same
flags plus `-mmwaitx -Wno-bitwise-instead-of-logical`, selecting the baseline
include tree with `-DBUSTER305_BASELINE` and the candidate without that define.

The 2× version improved the largest streams by roughly 5–6% but generally
lost on smaller functions; the 4× version did not provide a consistent gain.
The 16-record loop was retained. [width-cloud.txt](width-cloud.txt) contains
every report, including the losing variants. These cloud choices still need
Zen 5 qualification.

[conditioned-cloud.txt](conditioned-cloud.txt) records three candidate-only
processes per case, 11 samples each, with `--after-scalar 100`. Each recorded
sample follows at least 100 ms of the corresponding scalar work, outside
the timed interval. Median ns/record:

| Workload | Initial lengths | Sizing + encoding |
| --- | ---: | ---: |
| tier0-40 | 1.323 | 4.551 |
| tier0-2000 | 0.878 | 3.036 |
| tier0-mix | 1.195 | 4.037 |

These check the candidate after a lighter phase; they are not a claim about
Zen 5 frequency transitions. No before/after conditioning ratio is reported:
the old scalar sizing path used the vector encoder for lengths, whereas the
new scalar path uses `x86_measure_scalar`.

## Correctness and code generation

[validation-cloud.txt](validation-cloud.txt) retains these completed checks:

- Full encoder suite, all five modes, including existing GNU-as-checked
  digests. New length checks cover 136,752 generated form/operand cases,
  300,000 random instructions, FS/GS and fixup markings, all-NONE streams,
  zero and excessive counts, vector/chunk boundaries and rebased partitions.
- Length-only guard tests: counts 0–199 and 1,023–1,025, all 16 dword
  alignments, protected input/output page edges and canaries.
- Full branch-sizing tests: 1,500 random functions / 5,157,349 records,
  rel8 boundaries, convergence/worklists, memory exhaustion and guard pages.
- GNU as 2.47 independently assembled 404 generated functions:
  **404 compared, 404 identical**, including 98,920 long branches.
- Full encoder suite with Clang 18.1.3 AddressSanitizer and
  UndefinedBehaviorSanitizer, `-O2 -fno-sanitize-recover=all`.
  `ASAN_OPTIONS=detect_leaks=0` disables only leak checking.
- Final full Clang 19 build, call-graph tests and whole-build call-graph
  check: 30 translation units, 15,343 functions, zero unexpected cycles.

The installed GNU as 2.42 also failed on the unmodified baseline's newer
APX/AMX forms. The oracle was updated by building the official
[binutils 2.47 archive](https://ftp.gnu.org/gnu/binutils/binutils-2.47.tar.xz)
out of tree, without changing tests, expected output, system binaries or
production dependencies. Archive SHA-256:
`154ab23b60070e8f27013c22977f1129425d67d1e8acd6e13010e617811e4cff`.
The built `as-new` SHA-256 is
`a769e8fd95747c089094e53bc0dcd5868bed0ba39c2e316109c1782f2b73cccf`.

Clang 19 `-march=znver5` assembly was inspected without executing that binary
on this cloud host. The main group has four ZMM record loads, 16 direct form
word loads, 16 template-flag dword loads, and one ZMM offset store; it has no
vector spills, gather, scatter, compression or machine-code stores. An empty
pointer identity assembly expression prevents Clang from reusing widened
record loads and extracting each form through a vector-to-integer move.
The byte-prefix sum uses a 128-bit multiply/shuffle, and each 16-record
group totals at most 240 bytes. The final partial group is copied into a
bounded zeroed array and only live offsets are stored.
The wrapper and retained function-only listing (excluding initialization and
constant tables) are [measure-probe.c](measure-probe.c) and
[measure-znver5.s](measure-znver5.s).

[AMD's Zen 5 optimization guide, publication 58455](https://docs.amd.com/v/u/en-US/58455_1.00)
was consulted for architectural guidance. No LLVM-MCA prediction is treated
as a Zen 5 measurement.

## Existing exact-target baseline

The exact baseline passed
[9700X run 39918626782](https://github.com/buster14a/buster/actions/runs/39918626782/job/120298143545)
on 2026-10-23, with Ryzen 7 9700X, Clang 23.1.1 and the runner's exclusive
machine lock. [baseline-9700x-metadata.json](baseline-9700x-metadata.json)
identifies the exact revision, job, source log ranges and benchmark methods.
The host, encoder and branch-sizing excerpts are retained beside it.

That run's old branch benchmark reports cycles, not nanoseconds; it has no
combined sizing/encoding row. Its `tier0-mix` pool uses seed 5 and differs from
the seed-1 scaling pool. Compare candidate qualification against matching
CI rows and pool statistics. Interleaved CI measurements are qualification
context, not identical to the isolated cloud timing method.

## Reproduction

Use separate checkouts at the baseline SHA and the candidate. The following
commands are shell orchestration; all workload and measurement code is C.
Run CPU-consuming commands under the host's shared lock and use an allowed
CPU. Substitute paths for the local checkouts.

```sh
base=/path/to/buster-base
candidate=/path/to/buster-candidate
evidence="$candidate/docs/measurements/x86-lengths-2026-10-23"

# Apply only the old internal helper signature to the enhanced harness.
sed 's/branch_sizing_lengths(records, count, offsets, vector)/branch_sizing_lengths(records, count, offsets, columns.encoded, vector)/' \
    "$candidate/src/buster/compiler/branch_sizing_benchmark.c" > /tmp/baseline-benchmark.c

clang-19 -O3 -g -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing \
    -Wall -Werror -I"$base/src" /tmp/baseline-benchmark.c "$evidence/benchmark-main.c" -o /tmp/bench-before
clang-19 -O3 -g -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing \
    -Wall -Werror -I"$candidate/src" "$candidate/src/buster/compiler/branch_sizing_benchmark.c" \
    "$evidence/benchmark-main.c" -o /tmp/bench-after

taskset -c 2 /tmp/bench-before --only tier0-mix --measure size_encode --samples 21 --seed 1
taskset -c 2 /tmp/bench-after --only tier0-mix --measure size_encode --samples 21 --seed 1
taskset -c 2 /tmp/bench-after --only tier0-mix --measure size_encode --samples 11 --seed 1 --after-scalar 100

clang-19 -O3 -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing \
    -Wall -Werror -Wno-bitwise-instead-of-logical -pthread -I"$base/src" "$evidence/scaling.c" -o /tmp/scaling-before
clang-19 -O3 -march=native -mmwaitx -fwrapv -funsigned-char -fno-strict-aliasing \
    -Wall -Werror -Wno-bitwise-instead-of-logical -pthread -I"$candidate/src" "$evidence/scaling.c" -o /tmp/scaling-after
/tmp/scaling-before 8 200 tier0-mix
/tmp/scaling-after 8 200 tier0-mix
```

Repeat module rows/measurements and 1/2/4/8-worker runs in fresh, alternating
before/after processes. Validate that both reports exist and contain the
expected measurement before including a pair. The raw snapshots retain
per-process values and within-process p10/p90; summaries do not imply tighter
confidence intervals than the recorded VM variability.

All implementation, harnesses and generated inputs here are original to this
repository. Clang, GNU as and Linux perf are external tools, not imported
implementations or production dependencies.
