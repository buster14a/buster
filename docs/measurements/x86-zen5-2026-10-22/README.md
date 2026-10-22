# Original Zen 5 observations, 2026-10-22

These files contain original measurements made by this repository's C harness,
not an imported instruction-timing dataset.

## Source and host

- [Successful CI run 39758665284](https://github.com/buster14a/buster/actions/runs/39758665284),
  [job 119757044571](https://github.com/buster14a/buster/actions/runs/39758665284/job/119757044571).
- PR head: `9b9f43734d5b8264c4e91fdc94e494c9bf0653c2`.
- Actual CI checkout: `7ecd2739e88d543c32b62138df6cede4703cc7a8`, merging that
  head into `9713624349981908171fae1eb73d0d2bf05e351e`.
- The model and harness sources are byte-identical to measurement-source
  revision `d2bbeaf03ad6c2be5fb9b0a2f4abcaeed703069d`. Numeric observations were
  inserted after these runs; the recorded model-source hash therefore refers
  to the original unpopulated table, not the later populated file.
- [SHA-256 identities](source-and-binary.sha256) identify the measured
  executable and the two source files. Generated-loop FNV-1a hashes in each
  TSV match across all three processes and audit their exact instruction bytes.
- AMD Ryzen 7 9700X, CPUID `00b40f40`, microcode `0xb404038`, Linux
  `6.18.50-2-lts`, Clang `23.1.1`, performance governor, CPU 2 pinned. Its SMT
  sibling was offline (`thread_siblings_list` was `2`). Perf permissions were
  `-1`; pinned groups reported no multiplexing. The runner's job hook held the
  same exclusive lock used by interactive sessions throughout the CI job.
- Three processes ran from 10:22:15 through 10:22:22 UTC, each with
  `measure-x86-zen5 --passes 11 --iterations 20000`. Each loop contains 240
  target instructions; mixes add 240 blockers. The six experiments rotate
  order within every process.

## Data and reduction

[Process 1](process-1.tsv), [process 2](process-2.tsv) and
[process 3](process-3.tsv) retain the complete command output with only the
CI timestamps removed. There are 342 observation rows per process: 57 cases
times six experiments. Each row contains an eleven-pass median and the
minimum and maximum cycle observation, not just a selected best pass.

[summary.tsv](summary.tsv) combines those files deterministically:

1. Check that all cases/experiments appear exactly once in each process and
   their generated-code hashes agree. Retired instructions, after the empty
   loop subtraction, must be within 0.0001 of one per target or two per mix.
2. For each experiment, take the median of its three process medians.
3. Use `independent` for throughput and retired macro-ops. Multiply by 256
   and round to the nearest integer to populate their Q8 columns. Preserve
   the raw observations here; for example, a shift's 0.33475 cycles becomes
   86/256, while IMUL's 0.33336 becomes 85/256.
4. For GPR latency, take the larger of the destination and source-chain
   medians, round to a whole cycle, then multiply by 256. The observed chains
   are within 0.0003 cycles of that whole number. Zeroing XOR and immediate
   moves have no GPR input dependency, so their latency stays unknown
   (`65535`) instead of becoming a fractional throughput value.
5. Leave flag-result latency, load increments, executed micro-ops and pipes
   unknown. None follows from the retired-macro-op counter or from these
   limited mixtures.

The widest span between the three independent-process medians is 0.0535%
of their median. This is repeatability of these exact fixtures, not a bound
on predictions for other instruction layouts or complete programs. In
particular, some six-chain blocks run faster than twelve-chain blocks;
their register choices and instruction-byte layouts differ. These are
observed block throughputs, not claims about an isolated execution-port limit.

The IMUL control illustrates why the mixtures alone cannot identify pipes:
its twelve-chain stream reaches 0.33336 cycles/instruction, six chains take
0.50003, and IMUL plus ADD takes 0.50027 cycles/pair. The 0.5-cycle bound is
already present with six dependent IMUL chains. A second group of IMULs
raises the pair to 0.66670 cycles, but this still does not uniquely assign
physical pipes.

## Lookup observations

[lookup.txt](lookup.txt) preserves one process per mode from the same CI run.
Each uses 21 timed passes over 65,536 hot IDs, repeated 1,024 times per pass.
The interleaved median was 0.1807 ns/ID for the scalar twin and 0.0165 for
AVX-512 (10.95 times faster). Vector-only medians were 0.0150 ns/ID sustained
and 0.0170 after 200 ms of scalar work before each pass. The recorded ranges
are within-process extrema, not between-process confidence intervals.

This first lookup run used the unpopulated observation table; the generated
code still loads both table registers and executes its permutes. The final
CI configuration repeats each mode in three processes with the populated
table. The PR's subsequent CI log records that verification. These timings
exclude record-to-ID resolution and say nothing about a compiler consumer
or generated-program speed.
