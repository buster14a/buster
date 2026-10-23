# Shared-library TLS measurements, 2026-10-23

The measured compiler is source
`faf1e95d0fccdcdfb98257dc9b02a16a9ba04fd5` (PR #316). The common-workload
baseline is `19fa4ff26a1b7e7450d9999a9d0524a131a890bd`, whose compiler sources
are identical to main `829fc6d16342920f678b27ad443db3d91737bfef`.
Both binaries were built with Clang 23.1.1 on the approved Ryzen 7 9700X.
The TLS compiler remained unchanged throughout these measurements.

The subsequent integration merges main
`2740ff1d59bce335747bdcfd7d9994ce4426c320`, including its debug layout and
command-owned benchmark lane group. The figures below describe only the
frozen binaries above; they are not measurements of that integrated tree.

`campaign-plan.tsv` records each invocation and its captured status;
`writer-microseconds.tsv` transcribes all 216 workload/publication rows,
with every timing column in microseconds. The raw logs remain authoritative.
They retain the tools' original whitespace, including trailing spaces in
some lock-status and disassembly lines.
`raw/final-binary-identity.txt` rechecks the compiler and helper hashes after
the final TLS process, at 16:27:51 UTC.

The host ran Linux `6.18.50-2-lts`, the performance governor and enabled boost.
Single-lane measurements use CPU 2; eight-lane measurements use CPUs 0–7.
The benchmark writes to `/tmp` on tmpfs. Every invocation independently takes
`~/buster-fast-bench/exclusive.sh` with owner `codex-linker-tls85`, allowing
CI and other sessions their turn between invocations. Lock waiting is outside
all recorded times and hardware counters.

`raw/host-runtime-provenance-2026-10-23.txt` records the runtime file hashes,
file timestamps and current host settings captured at 14:40:39 UTC. This is
a capture during the campaign, not a pre-campaign inventory. The runtime
files' recorded modification and change times precede the campaign; those
timestamps do not replace a pre-campaign hash receipt.

The command common to the records is:

```sh
perf stat -e cycles,instructions -x, -o COUNTERS.csv \
    ./tls85-measure taskset -c CPUS COMPILER \
    benchmark-executable --only shared OPTIONS
```

`tls85-measure.c` is the original C helper used to collect `wait4` peak RSS
and user/system CPU time. It was built separately under the shared lock with
`cc -O2 -Wall -Wextra tls85-measure.c -o tls85-measure`. Hardware counters,
CPU time and peak RSS cover the complete benchmark command, including fixture
generation, binding, all writes, the copying floors and result validation.
For conditioned cases, the counters and CPU times also include the scalar
conditioning intervals.
The writer's printed times cover only each complete `executable_write` call;
its phase times subdivide that call. Each printed phase is its own median,
so the phase medians need not sum to the whole-writer median.

For each workload, an initial sizing write is followed by one warm-up and
41 measured writes per publication mode for outputs at most 4 MiB, or 11
measured writes for larger outputs. A process
measures both replacement and new-file publication. Each configuration is
repeated in three separately locked processes. The common baseline/candidate
order and the TLS vector/exact order reverse in repetition two. The seed is 1.
These are warmed repeated writer measurements: the existing benchmark keeps
one bound `Link` across writes after its initial write. It does not restore
every original atom byte and derived column between samples. Both common
baseline and candidate use that same lifecycle; these figures do not measure
fresh input generation or binding.

The common workload uses `--functions 1,64,2000,180000` on both binaries.
TLS workloads add `--tls general` or `--tls local`, and the exact twin adds
`--exact`. That setting also selects the writer's existing exact paths; its
comparison does not isolate the new TLS work. Steady single-lane runs use all
four sizes; runs with
`--after-scalar 100` and eight-lane runs (`--lanes 8`) use 64 and 180,000.
The scalar conditioning happens before every timed sample, on each lane,
and its time is excluded from the writer's printed times.

Each TLS worker adds a distinct TLS symbol and a callable ABI probe. Those
symbols alias at most 64 backing objects so the existing initial-exec
workload fits the loader's static TLS reserve. The command loads the output
and checks every probe outside the timed writer region. This measures symbol,
GOT-pair and fixup scaling; it does not measure initialization of a large TLS
storage block. Independent correctness fixtures cover distinct TLS objects,
mixed initial/general-dynamic references and threads created before loading.
Half of the general-dynamic definitions are preemptible and half are hidden;
the local-dynamic definitions are hidden. Their output sizes and dynamic
relocation counts consequently differ, so comparisons between the two models
are workload observations rather than before/after regressions.

For `S` symbol entries including the null entry and `L` writer lanes, the two
new shared-writer scratch arrays reserve `8 * L * (floor(S / 64) + 1) + 4 * S`
bytes, before arena alignment and small count fields. They are allocated for
shared outputs even when no general-dynamic reference occurs. Whole-command
peak RSS also includes generated inputs, binding, output copies and runtime
loading; it must not be attributed entirely to these arrays.

The frozen source passed `test-link`, `test-executable`,
`test-dynamic --cross-check`, `test-static --cross-check`,
`test-object-writer --cross-check`, `test-foreign --cross-check` and the
call-graph check on the locked host. Its compiled shared-library suite had
2,689 checks and zero failures. Hosted CI run `39928038749` also passed for
the exact measured commit.

The `raw/` directory preserves the command output and `perf stat` CSV records,
including any shared-lock waiting messages. Those messages describe scheduling;
they do not contribute to either the process counters or writer times. A
`*.status` file records the corresponding remote command's exit status when
that receipt was captured. The first candidate command's SSH receipt was lost
when its local waiting cell stopped; its complete benchmark output, final
resource-use line and counters were retrieved afterward. Its status is not
inferred from that output.

`weak.c` and `weak-loader.c` retain the original hidden undefined weak TLS probe
used to check the reference toolchains. They are separate from the compiler's
regression fixture. The source deliberately does not dereference the address
returned by `__tls_get_addr`. The shared-library regression log records the
compiler's explicit refusal of this case.

## Common-workload comparison

Each cell is the median of three process medians in microseconds, followed
by their minimum–maximum range. Positive change means a longer candidate time.

| Functions | Publication | Baseline, us (range) | Candidate, us (range) | Median change |
| ---: | --- | ---: | ---: | ---: |
| 1 | replace | 8.0 (7.9–8.6) | 7.8 (7.7–8.7) | -2.50% |
| 1 | new file | 7.4 (7.4–7.4) | 7.4 (7.2–7.4) | +0.00% |
| 64 | replace | 13.0 (12.9–13.7) | 13.0 (13.0–13.9) | +0.00% |
| 64 | new file | 11.8 (11.8–12.0) | 11.8 (11.7–12.0) | +0.00% |
| 2,000 | replace | 180.3 (178.3–181.9) | 183.1 (179.1–183.4) | +1.55% |
| 2,000 | new file | 162.5 (162.2–164.9) | 164.4 (163.2–165.7) | +1.17% |
| 180,000 | replace | 21534.6 (21528.6–21676.6) | 21607.4 (21492.6–21648.0) | +0.34% |
| 180,000 | new file | 19479.1 (19471.1–19755.4) | 19562.7 (19540.4–19580.1) | +0.43% |

The largest output is identical in size and census on both revisions:
63,181,112 bytes, 185,452 atoms, 2,342,790 fixups, 180,011 dynamic symbols,
335,635 `.rela.dyn` entries and 242 chunks. Its candidate median is 0.34%
longer for replacement and 0.43% longer for a new file. Both ranges overlap
the baseline ranges. The 2,000-function medians are 1.55% and 1.17% longer,
also with overlapping ranges. These repetitions establish neither a speedup
nor a timing difference larger than the observed process spread; they do not
exclude a small added cost.

Whole-command peak RSS medians are 253,312 KiB baseline and 255,384 KiB
candidate, a 2,072 KiB increase. Their ranges are 253,192–253,320 KiB and
255,272–255,464 KiB respectively. The cycle medians are 4,627,904,275 and
4,644,376,791; retired-instruction medians are 12,827,421,244 and
12,763,054,279. Both counters ran for 100% of their recorded duration in
each process. These whole-command figures include the workload preparation
and copying floors described above.

## TLS-model steady single-lane comparison

These are whole-writer vector/exact comparisons on the same candidate source,
not before/after TLS-kernel measurements. The exact option also changes
existing writer paths. Each cell again gives the median and range of three
process medians in microseconds. Negative change means a shorter vector time.

| TLS model | Symbols/probes | Publication | Vector, us (range) | Exact, us (range) | Vector change |
| --- | ---: | --- | ---: | ---: | ---: |
| General dynamic | 1 | replace | 8.1 (7.9–8.9) | 9.9 (9.7–10.6) | -18.18% |
| General dynamic | 1 | new file | 7.4 (7.3–7.4) | 9.3 (9.1–9.4) | -20.43% |
| General dynamic | 64 | replace | 19.3 (19.3–20.3) | 24.5 (24.5–25.2) | -21.22% |
| General dynamic | 64 | new file | 18.1 (17.9–18.1) | 23.2 (23.1–23.4) | -21.98% |
| General dynamic | 2,000 | replace | 343.9 (336.9–344.8) | 432.2 (431.2–433.3) | -20.43% |
| General dynamic | 2,000 | new file | 315.3 (314.5–316.7) | 409.3 (408.1–409.7) | -22.97% |
| General dynamic | 180,000 | replace | 42549.6 (42472.2–42673.7) | 50936.3 (50903.4–51022.2) | -16.47% |
| General dynamic | 180,000 | new file | 40424.8 (40362.9–40721.8) | 48400.5 (48257.9–48440.5) | -16.48% |
| Local dynamic | 1 | replace | 8.1 (7.9–8.8) | 9.8 (9.7–9.8) | -17.35% |
| Local dynamic | 1 | new file | 7.5 (7.5–7.5) | 9.2 (9.2–9.7) | -18.48% |
| Local dynamic | 64 | replace | 18.9 (18.5–20.0) | 23.7 (23.7–24.0) | -20.25% |
| Local dynamic | 64 | new file | 17.6 (17.4–18.1) | 22.5 (22.4–23.2) | -21.78% |
| Local dynamic | 2,000 | replace | 321.1 (319.5–340.1) | 409.6 (408.1–423.1) | -21.61% |
| Local dynamic | 2,000 | new file | 302.2 (301.7–318.2) | 389.8 (388.0–393.0) | -22.47% |
| Local dynamic | 180,000 | replace | 38246.0 (38148.0–39318.7) | 45730.4 (45448.3–45750.7) | -16.37% |
| Local dynamic | 180,000 | new file | 36447.5 (36165.1–37228.5) | 43957.7 (43394.8–44427.2) | -17.09% |

All sixteen vector/exact ranges are disjoint in these repetitions. At 180,000
symbols/probes, vector whole-writer medians are 16.4–17.1% shorter than exact.
This supports the candidate's vector writer path for these workloads; it does
not assign that reduction solely to the added TLS handling.

The 180,000-symbol general-dynamic output is 93,509,416 bytes with 365,516
atoms, 2,702,790 fixups, 450,012 dynamic symbols, 605,636 `.rela.dyn`
entries and 357 chunks. The local-dynamic output is 79,255,016 bytes with
365,516 atoms, 2,882,790 fixups, 360,012 dynamic symbols, 335,637
`.rela.dyn` entries and 303 chunks. Every command checked all ABI probes.
The backing-object pool remains bounded at 64, as described above.

## Single-lane writer after scalar conditioning

The same writer is measured after 100 ms of scalar fixup classification
before each timed sample. The scan is excluded from writer time; these
measurements do not directly measure clock frequency or a transition cost.
The table again reports the median and observed range of three process
medians, using `--after-scalar 100` on both paths.

| TLS model | Symbols/probes | Publication | Vector, us (range) | Exact, us (range) | Vector change |
| --- | ---: | --- | ---: | ---: | ---: |
| General dynamic | 64 | replace | 19.9 (19.5–19.9) | 25.1 (25.0–25.1) | -20.72% |
| General dynamic | 64 | new file | 18.5 (18.2–18.7) | 23.4 (23.4–23.5) | -20.94% |
| General dynamic | 180,000 | replace | 42915.4 (42771.8–43807.7) | 51933.4 (51157.1–52008.1) | -17.36% |
| General dynamic | 180,000 | new file | 40868.7 (40591.1–42002.9) | 49724.5 (49033.6–49781.0) | -17.81% |
| Local dynamic | 64 | replace | 19.1 (19.0–19.1) | 24.3 (24.2–24.5) | -21.40% |
| Local dynamic | 64 | new file | 17.6 (17.6–17.6) | 22.7 (22.6–22.9) | -22.47% |
| Local dynamic | 180,000 | replace | 38342.2 (37870.2–38761.7) | 46189.1 (45918.5–46707.5) | -16.99% |
| Local dynamic | 180,000 | new file | 36327.4 (36080.1–36631.1) | 44623.3 (44262.0–45152.2) | -18.59% |

All eight vector/exact ranges are disjoint in these repetitions. For the
180,000-symbol cases, vector whole-writer medians are 17.0–18.6% shorter
than exact. The comparison still includes the writer's preexisting vector
paths and the same bounded TLS backing-object pool. It does not isolate a
TLS kernel or establish a frequency effect.

All twelve command status receipts are zero. All twenty-four workload
blocks report one lane and successful checks of every TLS probe; their
output sizes and symbol/relocation counts match the steady workloads.
Whole-command resource counters include the conditioning intervals.

## Eight-lane writer observations

These vector-path runs use eight lanes on CPUs 0–7, three fresh processes
per TLS model. The table compares their process medians with the completed
one-lane steady block above. The one-lane and eight-lane blocks were run
separately, rather than alternated; the ratios describe these observations
and do not isolate lane count from time-varying host conditions.

| TLS model | Symbols/probes | Publication | One lane, us (range) | Eight lanes, us (range) | Observed throughput ratio |
| --- | ---: | --- | ---: | ---: | ---: |
| General dynamic | 64 | replace | 19.3 (19.3–20.3) | 23.4 (23.2–23.8) | 0.82× |
| General dynamic | 64 | new file | 18.1 (17.9–18.1) | 19.6 (19.6–19.8) | 0.92× |
| General dynamic | 180,000 | replace | 42549.6 (42472.2–42673.7) | 26019.8 (25870.0–26112.4) | 1.64× |
| General dynamic | 180,000 | new file | 40424.8 (40362.9–40721.8) | 22795.2 (22398.1–23347.3) | 1.77× |
| Local dynamic | 64 | replace | 18.9 (18.5–20.0) | 22.2 (21.5–23.2) | 0.85× |
| Local dynamic | 64 | new file | 17.6 (17.4–18.1) | 19.2 (19.1–19.7) | 0.92× |
| Local dynamic | 180,000 | replace | 38246.0 (38148.0–39318.7) | 22413.9 (22263.8–22694.5) | 1.71× |
| Local dynamic | 180,000 | new file | 36447.5 (36165.1–37228.5) | 19097.8 (18411.5–19469.2) | 1.91× |

At 180,000 symbols/probes the observed throughput increase is about
1.64–1.91× using eight cores. This is well below linear scaling.
Every eight-lane median for the 64-symbol cases exceeds every corresponding
one-lane median. All six command receipts are zero and all twelve workload
blocks check every TLS probe. Their output censuses match the corresponding one-lane files.
These results do not close the separate linker scaling work.
