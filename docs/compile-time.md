# Where sqlite3.c's compile time goes

**RSS accounting correction (#865).** The historical RSS figures below retain
their original `getrusage`/`wait4` scope; they have not been remeasured. Those
peaks can include the launcher's pre-exec address space. This does not establish
that any particular historical run was contaminated. Current `--time-stages`
uses a separate approximate post-exec peak, while subprocess peaks are labelled
`wait4`. See the [RSS contract and regression](peak-rss.md).

This is [#518](https://github.com/buster14a/buster/issues/518)'s attribution of
`compiler -c sqlite3.c` on the Ryzen 7 9700X, against tcc: how long the whole
process takes, which of the compiler's stages take that time, and which
functions inside them. It also records why linking has no part in that figure,
and the follow-ups that rank the work to close the gap.

## Against tcc, clang -O0 and gcc -O0, per token (#146)

[#146](https://github.com/buster14a/buster/issues/146)'s headline measurement:
the same `-c` of the frozen corpus's `sqlite3.c`, with the 9700X job's options
(#518, below), timed with `tcc -c`, `clang -O0 -c` and `gcc -O0 -c` by one
`measure-compile` run, its 21 rounds interleaving the five compiles in a
Williams design's order, pinned to CPU 2, under the 9700X's lock, on
9 November at `85680a84` (main `cb608c1f` with the harness's references and the
report's token count). [The measurement
record](https://github.com/buster14a/buster-performance/tree/claude/146-compile-time-record/docs/measurements/compile-time-references-146-2026-11-09)
has the whole output, the scripts and every executable's version and hash:
tcc 0.9.28rc 2026-01-17 (the CI step's pin), Arch's clang 23.1.1-1 and gcc
16.2.1 (20260810, with GNU as 2.47). Each reads the system's glibc headers;
buster reads them under its default configuration. The cycles and instructions
are perf's user-mode counters, inherited by gcc's `cc1` and `as`. The
preprocessed tokens, 716,136, are `--time-stages`' `tokens`, the same divisor
for every compiler.

| compiler | wall ms | user cycles | user instructions | ns per token | cycles per token | instructions per token | wait4 peak |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| buster `-c` | 116.5 (115.8 to 119.3) | 557.8 M | 1,259.0 M | 162.7 | 778.9 | 1,758 | 237,912 KiB |
| tcc `-c` | 42.3 (41.9 to 48.1) | 218.4 M | 537.6 M | 59.1 | 304.9 | 751 | 13,304 KiB |
| clang `-O0 -c` | 567.2 (565.6 to 620.3) | 3,045.5 M | 6,645.3 M | 792.0 | 4,252.7 | 9,279 | 216,524 KiB |
| gcc `-O0 -c` | 1,921.4 (1,916.4 to 1,930.3) | 10,486.4 M | 24,261.3 M | 2,683.1 | 14,643.0 | 33,878 | 324,024 KiB |

- **2.75 times tcc** (74.2 ms apart), down from 4.44 at `1677a3b` (#518, below).
- **4.87 times faster than clang `-O0`** and **16.5 times faster than gcc
  `-O0`**, which retire 5.3 and 19.3 times buster's instructions.
- **Against #115's goal**, 15 ms for a whole Tier 0 compile, about 120 cycles
  per token: the compile takes 889 core cycles per token, 7.4 times the goal.

The stages, from the `--time-stages` processes (medians of 21 runs). Their
cycles per token are TSC ticks at the report's 3,792.8 MHz, and core cycles at
the processes' average clock, 5,493.4 MHz: their cycles in both modes per wall
millisecond ("How it is measured", below). Stages under 0.1 ms are left out;
the preprocessor's and the lowering's phases are under their stages.

| stage | ms | share | TSC ticks per token | core cycles per token |
| --- | ---: | ---: | ---: | ---: |
| setup | 0.261 | 0.2% | 1.4 | 2.0 |
| preprocess | 9.425 | 8.1% | 49.9 | 72.3 |
|  · read | 0.634 | 0.5% | 3.4 | 4.9 |
|  · lex | 2.482 | 2.1% | 13.1 | 19.0 |
|  · refine | 1.565 | 1.3% | 8.3 | 12.0 |
|  · directives | 0.340 | 0.3% | 1.8 | 2.6 |
|  · main loop | 4.360 | 3.8% | 23.1 | 33.4 |
| parse | 7.202 | 6.2% | 38.1 | 55.2 |
| check-input | 44.122 | 38.0% | 233.7 | 338.5 |
|  · its expressions' walk | 11.883 | 10.2% | 62.9 | 91.2 |
| check-expressions | 10.328 | 8.9% | 54.7 | 79.2 |
| check-declarations | 2.006 | 1.7% | 10.6 | 15.4 |
| static-data | 0.431 | 0.4% | 2.3 | 3.3 |
| check-program | 20.786 | 17.9% | 110.1 | 159.4 |
| select | 0.941 | 0.8% | 5.0 | 7.2 |
| lower | 10.642 | 9.2% | 56.4 | 81.6 |
|  · prepare | 1.017 | 0.9% | 5.4 | 7.8 |
|  · statements | 6.886 | 5.9% | 36.5 | 52.8 |
|  · bodies | 1.363 | 1.2% | 7.2 | 10.5 |
|  · validate | 1.328 | 1.1% | 7.0 | 10.2 |
| tier0 | 5.325 | 4.6% | 28.2 | 40.8 |
| emit-setup | 1.021 | 0.9% | 5.4 | 7.8 |
| branch-sizing | 0.542 | 0.5% | 2.9 | 4.2 |
| encode | 0.396 | 0.3% | 2.1 | 3.0 |
| unwind | 0.471 | 0.4% | 2.5 | 3.6 |
| code-atoms | 0.620 | 0.5% | 3.3 | 4.8 |
| publish-data | 0.452 | 0.4% | 2.4 | 3.5 |
| object-build | 0.462 | 0.4% | 2.4 | 3.5 |
| object-write | 0.178 | 0.2% | 0.9 | 1.4 |
| span | 115.959 | | 614.1 | 889.5 |

The checker's four stages take 77.2 ms, 66.6% of the span and 593 core cycles
per token; preprocessing and parsing 128, and the backend, from `select` to the
object's write, 161. tcc's whole compile, 305 user cycles per token, is less than the
reader's stage alone.

## Results

The 9700X job measured these at `1677a3b` (#520), which it merged into main
`6b2c922`:
[run 40421327819, job 121916266526](https://github.com/buster14a/buster/actions/runs/40421327819/job/121916266526),
step "Attribute sqlite3.c's compile time to the compiler's stages, against tcc".
The figures are medians of 11 runs. [The measurement record](https://github.com/buster14a/buster-performance/tree/2dddeb09612d54f243cf0521e6bb937cfe2f80f7/docs/measurements/compile-time-518-2026-10-27)
holds the step's whole output and the job's log: every run's samples, the stage
table, perf's counters and profile, and the revision, flags and hashes.

- **4.4 times tcc.** `-c` takes 189.6 ms against tcc's 42.7 ms, 146.9 ms apart.
  #136's speedtest1 record measured 194.8 ms and 42.2 ms at `e04462c`, in
  another session.
- **The checker is two thirds of it.** Two stages take more than half the
  compile: the checker's reader (`check-input`, 43%) and the lowering to the
  typed stream (`lower`, 14%).
  - The checker's stages together take 67% (127 ms).
  - Preprocessing and parsing take 10% (19 ms).
  - The backend, from the lowering to the encoded atoms, takes 21% (40 ms).
  - The object's build and write take 0.3% (0.6 ms).
- **The gap to tcc is work, not stalls.** The compile retires 4.2 times tcc's
  user instructions (2,258 M against 538 M) at about the same IPC (2.55 against
  2.48).
- **Memory adds a sixth.** Peak RSS is 728 MB, 55 times tcc's 13 MB and about
  1 KB per preprocessed token. Clearing those pages takes 30 ms of system time.
  The kernel's `clear_page_erms` is the single hottest function in the
  profile, at 15.9% of the samples.

### The whole process

Both compilers took the same command line, pinned to CPU 2. The rows from wall
time to faults are `measure-compile`'s, whose processes ran in a rotating
order. The counters are `perf stat -r 11`'s.

| | buster `-c` | tcc `-c` | ratio |
| --- | ---: | ---: | ---: |
| wall time | 189.6 ms (189.0 to 191.6) | 42.7 ms (42.4 to 43.1) | 4.44 |
| user time | 161.0 ms | 39.3 ms | 4.10 |
| system time | 29.9 ms | 3.3 ms | 9.1 |
| peak RSS | 727,784 KB | 13,308 KB | 54.7 |
| page faults (perf) | 1,388 | 2,779 | 0.50 |
| instructions, user | 2,258.3 M | 537.6 M | 4.20 |
| cycles, user | 884.7 M | 216.5 M | 4.09 |
| instructions, kernel | 39.6 M | 22.4 M | 1.77 |
| cycles, kernel | 167.5 M | 13.2 M | 12.7 |
| branch misses, user | 5.89 M | 2.10 M | 2.80 |

The page faults are few for 728 MB because most pages arrive as 2 MB
transparent huge pages. Clearing them still costs: kernel cycles are 15.9% of
all the compile's cycles, and 5.7% of tcc's.

### The stages

`--time-stages` took this report, the medians of 11 runs. In each run the
process's wall time was within 0.71 ms of the clock's span: exec, the loader,
`main`'s dispatch and exit, 0.63 ms at the median. The option cost 0.196 ms
(0.10%) of the default's median wall time.

| stage | ms | share | system ms | peak KB added |
| --- | ---: | ---: | ---: | ---: |
| setup | 0.434 | 0.2% | 0.000 | 6,236 |
| preprocess | 9.771 | 5.2% | 0.000 | 46,660 |
|  · read | 0.818 | 0.4% | | |
|  · lex | 2.508 | 1.3% | | |
|  · refine | 1.451 | 0.8% | | |
|  · directives | 0.468 | 0.2% | | |
|  · main loop | 4.530 | 2.4% | | |
| parse | 9.633 | 5.1% | 3.364 | 105,708 |
| type-table | 1.667 | 0.9% | 0.587 | 38,916 |
| check-input | 81.169 | 42.9% | 11.550 | 324,996 |
| check-expressions | 20.026 | 10.6% | 0.019 | 53,316 |
| check-declarations | 5.053 | 2.7% | 0.188 | 10,368 |
| static-data | 1.380 | 0.7% | 0.000 | 2,048 |
| check-program | 15.674 | 8.3% | 0.015 | 14,336 |
| select | 2.018 | 1.1% | 0.000 | 2,048 |
| backend-setup | 0.779 | 0.4% | 0.000 | 6,656 |
| lower | 25.675 | 13.6% | 6.476 | 104,768 |
| tier0 | 5.038 | 2.7% | 0.000 | 128 |
| emit-setup | 2.212 | 1.2% | 0.181 | 2,048 |
| branch-sizing | 5.598 | 3.0% | 0.000 | 0 |
| encode | 0.363 | 0.2% | 0.000 | 0 |
| unwind | 0.114 | 0.1% | 0.000 | 0 |
| code-atoms | 0.188 | 0.1% | 0.000 | 2,240 |
| publish-data | 0.550 | 0.3% | 0.000 | 0 |
| object-build | 0.384 | 0.2% | 0.000 | 0 |
| object-write | 0.189 | 0.1% | 0.000 | 0 |
| release | 1.351 | 0.7% | 1.269 | 0 |
| span | 189.181 | | | |

`debug` (`-g`'s DWARF) and `executable` (the direct path's link, since #778
its `runtime`, `executable` and `write` stages) are zero in `-c` without `-g`,
and are left out. The stages' medians add up to 189.27 ms,
the span's median 189.18.

### The functions

`perf record -F 20000` of 5 default compiles, by DSO:
- the compiler, 78.0%;
- the kernel, 18.6%;
- libc, 3.3%.

The functions above 0.8% of the samples:

| function | share | stage |
| --- | ---: | --- |
| `clear_page_erms` (kernel) | 15.85% | first touches, every stage |
| `check_input_expressions` | 11.64% | check-input |
| `check_input_run` | 6.77% | check-input |
| `check_input_resolve_range` | 5.96% | check-input |
| `check_program` | 3.91% | check-program |
| `check_lower_drain` | 3.91% | lower |
| `check_graph_run` | 2.87% | check-expressions |
| `check_program_lower` | 2.44% | lower |
| `validate_instructions` | 2.09% | branch-sizing (`x86_validate`) |
| `check_program_emit_timed` | 1.68% | emission |
| `check_input_add` | 1.56% | check-input |
| `tier0_generate` | 1.41% | tier0 |
| `check_action_compute` | 1.40% | check-expressions |
| `check_lower_class` | 1.34% | lower |
| `check_input_intrinsic_initialize` | 1.16% | check-input |
| `tokenize_c_raw_no_separators` | 1.05% | preprocess (lex) |
| `check_driver_reports` | 1.04% | check-declarations |
| `check_graph_action` | 1.01% | check-expressions |
| `check_graph_scalar_node` | 0.99% | check-expressions |
| `parse_translation_unit` | 0.99% | parse |
| `check_control_drain` | 0.90% | check-program |
| `check_program_select_mode` | 0.85% | select |
| `check_literal_number_plain` | 0.84% | check-input |
| `check_input_declarator_step` | 0.84% | check-input |

## The bottlenecks, ranked

Each upper bound is the time the item would save if its cost went to zero.

1. **The checker's reader**, 81.2 ms (42.9%): 70 ms of user time and 11.6 ms
   of first touches. tcc's whole compile retires 538 M instructions; this
   stage alone retires about 43% of the compile's 2,258 M, nearly twice tcc's
   total.
   - Its hottest functions walk every token or expression:
     `check_input_expressions`, `check_input_resolve_range` and `check_input_run`'s
     own pre-passes.
   - Upper bound: about 70 ms of user time.
   - Follow-up: [#529](https://github.com/buster14a/buster/issues/529), which
     closed with the stage at 47.3 ms ("#529's outcome" below);
     [#1671](https://github.com/buster14a/buster/issues/1671) carries the rest.
     [#113](https://github.com/buster14a/buster/issues/113)'s deferred bodies
     are its multicore route.
2. **Memory first touched**, 728 MB of peak RSS.
   - The stages that add it: the reader's 325 MB (about 85 token-sized columns),
     the parser's 106 MB, the lowering's 105 MB, the expressions' 53 MB and the
     preprocessor's 47 MB.
   - Cost: 29.9 ms of system time, 15.9% of the compile's cycles, all of it the
     kernel clearing pages.
   - Upper bound: about 30 ms, with the cache misses of sparse columns besides.
   - Follow-up: [#528](https://github.com/buster14a/buster/issues/528), which
     took 115 MB of it and 17% of the kernel's cycles, #570 46 MB more,
     [#572](https://github.com/buster14a/buster/issues/572) 121 MB more and
     [#628](https://github.com/buster14a/buster/issues/628) 150 MB more, to
     240 MB, with 23% fewer kernel cycles ("Memory first touched", below);
     [#1701](https://github.com/buster14a/buster/issues/1701) has what is left.
3. **The checker's graph and program passes**, 35.7 ms (18.9%):
   `check-expressions` 20.0 ms and `check-program` 15.7 ms, with no system
   time.
   - Upper bound: 35.7 ms.
   - Follow-up: [#530](https://github.com/buster14a/buster/issues/530), which
     took 8.7 ms off them: [below](#the-checkers-graph-and-program-passes-530).
4. **The lowering to the typed stream**, 25.7 ms (13.6%): 19 ms of user time,
   five times Tier 0's compile of the stream it produces (5.0 ms).
   - Upper bound: about 19 ms of user time; its 6.5 ms of system time is #528's.
   - Follow-up: [#531](https://github.com/buster14a/buster/issues/531).
     [#151](https://github.com/buster14a/buster/issues/151)'s per-function jobs
     are its multicore route.
   - Since: [#570](https://github.com/buster14a/buster/pull/570) took the stage
     from 25.6 to 17.5 ms and its system time to zero (this clock on the 9700X,
     against main at `ad408cf2`). Tier 0's check of each lowered function, about
     a sixth of what is left, is
     [#569](https://github.com/buster14a/buster/issues/569).
5. **`x86_validate` on every Tier 0 record**, about 5 ms (2.7%).
   - `branch_sizing_validate` validates each function's records before sizing
     them, which makes the branch-sizing stage (5.6 ms) 15 times the encoder's
     (0.36 ms). `--verify` exists for that check.
   - Upper bound: about 5 ms.
   - Follow-up: [#532](https://github.com/buster14a/buster/issues/532).

The rest are small: preprocessing (9.8 ms) and parsing (9.6 ms) have their own
issues (#117, #126, #451); Tier 0 takes 5.0 ms; the object's build and write
take 0.6 ms.

**Across the stages (#2524).** At `d96b2e73` the function bodies are 87% of the
compile's user cycles, and the stages from `check-input` to `lower` spend about
1,600 cycles on each expression node, which five of them visit in turn.
[The node machine's design](node-machine.md) is the change for that: one visit
per node from the parser's tree to the typed stream, with this pipeline as its
twin. It has the measurements, by stage and by node, and the estimate.

**At eight workers (#2536).** At `4c8c3c9c` eight compiles of `sqlite3.c` at
once cost each of them 1.89 times the cycles of one alone, for the same
instructions: 295 MB/s on the 9700X where eight times one compile would be 594.
All of the difference is memory: the stages' records of the whole unit come
from DRAM where the third-level cache is shared by eight, and the kernel's work
for 190 MB of pages triples.
[The all-core path's design](all-core-path.md) is the change for that: the unit
compiled one window of external declarations at a time, from its tokens to its
code, in memory the next window reuses, with this order as its twin. It has
the inflation by stage, what each design in hand removes of it, and the model.

## Memory first touched (#528, #572)

Three allocations touched far more memory than they used.
- [#528](https://github.com/buster14a/buster/issues/528)'s two changes took
  115 MB off the compile's peak RSS, and 17% of its kernel cycles.
- #570, the lowering's work for [#531](https://github.com/buster14a/buster/issues/531),
  takes 46 MB more with the third. #528's branch had made that change first,
  and left it to #570.

[#572](https://github.com/buster14a/buster/issues/572) then took 121 MB more off
the checker ("The checker's columns", below). The objects are the same bytes.

The remaining parser scratch work in [#628](https://github.com/buster14a/buster/issues/628)
separates the production vector workspace from the kernel benchmark's nine
traversal arrays (36 bytes per token). The C driver requests `tree_only`; other
clients build final traversal columns after popping the vector workspace.
Those unused arrays had created a hole before live K4 slots and fallback
scratch. The obsolete candidate stack, another six bytes per token, had no
consumer either. The benchmark explicitly reserves its traversal arrays;
production retains the borrowed arena's mark and initializes every value it
reads, including on dirty pages and arena copies. This reduces the scratch
span; its RSS and system-time effect requires separate whole-process
measurement and is not implied by the virtual byte count.

The reader's zero-default `asm_ids` output column is also omitted when the
parser's roles prove there is no assembly statement group. Declaration linkage
labels contain `asm` keywords but need no IDs: the pinned SQLite input has 45
such keywords and no statement groups. Its 716,157 tokens otherwise require
2,864,632 bytes of IDs. The scalar reader keeps a dense zero column as an oracle;
real assembly statements keep the original allocation, capacity and diagnostics.
Every reader uses a zero-default accessor or a guarded vector load, and deferred
ID remapping preserves an absent column. No arena freshness or high-water mark
is assumed.

The [frozen main416 qualification record](https://github.com/buster14a/buster-performance/blob/32b96dd2a12c12c7aaf25be3639a31b7fc87cdd9/docs/measurements/unclaimed-perf-2026-10-28/issue-628/ASM-COLUMN-RESULTS.md)
measured about 8 MiB lower native peak RSS for the parser and empty-ID changes
together on the pinned SQLite input. Wall and system-time improvements were
not established; the record's compiler, source, controls and remaining scope
are explicit.

The reader fills thirteen mandatory retained uint32 output columns whose
empty value is `UINT32_MAX` with bounded all-one byte fills,
including each column's sentinel. Requested scalar parsing keeps the original
interleaved initialization loop as an independent oracle, and the six
reader-internal NONE stores keep their existing ownership and initialization.
(Twelve of those columns are sparse since, and need no fill; the reader's six
are filled a column at a time too: "The sparse output columns", below.)
Optional zero-default columns retain their existing NULL guards. Dirty and
borrowed output storage is always explicitly cleared; no fresh-page or arena
high-water assumption is made. The column layout and allocated byte count do
not change.

The [frozen native record](https://github.com/buster14a/buster-performance/blob/dfc33ffce07a0d04cdcbe9d435bd6ebb1fd7af8d/docs/measurements/retained-none-fill-2026-10-28/README.md)
measures 6.7 to 7.0 percent higher whole-process throughput for its 800k-byte
table in both role orders, with 6.18 percent fewer retired user instructions.
Peak RSS is unchanged and system time does not improve. The small SQLite
timing observation remains a cost screen without code-placement qualification;
the broader #628 memory/system-time goal remains open.

### Before and after

These are the 9700X's medians, pinned to CPU 2 under the host's lock:
- **Before:** #520's head `ea673e4b`, main `ad408cf2` with the stage clock.
  Its peak RSS was 674 MB rather than the 728 MB above, measured at `1677a3b`
  before main's later changes.
- **#528:** the type table's and the parse's changes on it (round 3 of the
  record).
- **Measures:** `measure-compile` in both orders, each compiler the other's
  reference (21 rotated rounds each), and `perf stat -r 11` of each, twice.
  [The measurement record](https://github.com/buster14a/buster-performance/tree/c5ffd6cd6f90abca60273f8bc72321c2bc5ad478/docs/measurements/memory-528-2026-10-27)
  holds the logs, the scripts and the trees' identities.

| | before | #528 | change |
| --- | ---: | ---: | ---: |
| wall time | 190.1 ms (189.5 to 193.3) | 186.3 ms (184.8 to 187.9) | −3.9 ms (−2.0%) |
| peak RSS | 674,216 KB | 558,936 KB | −115,280 KB (−17.1%) |
| minor faults | 1,531 | 2,092 | +561 |
| task-clock (perf) | 189.7 ms | 186.1 ms | −1.9% |
| cycles, kernel | 155.2 M | 129.5 M | −16.6% |
| cycles, user | 896.9 M | 902.2 M | +0.6% |
| instructions, user | 2,227.3 M | 2,227.1 M | 0.0% |

- **Reverse order:** base against #528 measured 190.3 ms against 186.0 ms.
- **Second `perf stat` pair:** 190.1 ms against 186.2 ms, kernel cycles −17.1%.
- **With lane 0's writer too** (round 2): 184.0 ms against 190.1 ms, and a
  peak RSS of 512,576 KB.
- **Profile.** `clear_page_erms` fell from 14.8% of the samples to 12.2%.
- **User cycles.** They rose with glibc, mostly its `memset`: from 3.9% of the
  samples to 5.1%. The reader's columns now take pages that the parse left out
  of the caches, and zeroing them reads each line before writing it, where the
  kernel had just cleared fresh pages.

The stages that changed, from each compile's `--time-stages` medians:

| stage | ms before | ms after | peak KB added before | peak KB added after |
| --- | ---: | ---: | ---: | ---: |
| parse | 9.55 | 9.44 | 105,704 | 97,556 |
| type-table | 1.67 | 0.04 | 38,916 | 128 |
| check-input | 80.26 | 78.22 | 324,612 | 261,408 |
| check-program | 15.79 | 15.84 | 14,336 | 10,240 |

### The allocations

- **The type table** (#528). Its columns each have room for four types a token,
  far apart, and a unit fills a little at each one's start: `sqlite3.c`'s
  716,380 tokens make 3,434 types. In huge pages, each column's first touch
  cleared 2 MB. The C driver's table is now in base pages
  (`arena_reserve_small`).
- **The parser's temporaries** (#528). The vector path's arrays but the tree's
  (63 MB of `sqlite3.c`'s) and the postorder's passes (14 MB) were in arenas of
  the parser's own, held to the end of the compile. The checker's reader then
  touched 169 MB of fresh scratch of its own.
  - A parse's temporaries now go in `ParseOptions.scratch`, each step's popped
    after it ([the parser guide](c-parser.md)).
  - The C driver passes its checker's scratch, whose reader takes the same
    pages again.
- **Lane 0's writer** (#570). A lane's writer has some thirty columns with room
  for any of the program's bodies: 11.3 million entries, where the largest body
  takes 18,935. Lane 0's were in the caller's scratch past what the checker
  touched, and each column's start cleared a 2 MB page: 46 MB of the `lower`
  stage on one lane, as the pinned compile runs.
  - The other lanes' have been in base pages since #151
    ([the lanes guide](function-lanes.md)).
  - In #528's branch, all of lane 0's scratch in base pages instead made Tier
    0's arrays fault page by page, 0.6 ms slower; the writer's columns alone
    don't.

### The checker's columns (#572)

Four of the checker's allocations held memory that few entries used, or that
nothing read:
- **The reader's rarely written columns.** `check_input_run` kept all of its
  token columns in the checker's scratch, whose pages the later stages take
  again. On `sqlite3.c` the reader touched 186 MB of it, where no later stage
  touches more than 72 MB, so the scratch stayed at the reader's size to the
  end of the compile.
  - The 30 columns that few tokens have an entry in, the scope stacks and the
    binding table (room for four events a token, of which `sqlite3.c` fills
    the first 167,456) are now in an arena of the reader's own, which it unmaps
    when it returns.
  - Its pages are new and zero: the columns need no clearing, and take memory
    only where the walk writes them. The walk reads most of them at every
    token, which maps the kernel's huge zero page, so the arena's pages are
    huge ones but for the scope stacks' and the enumeration constants', which
    only their own tokens read.
  - The reader's scratch is 70 MB, under the parse's and `check_program`'s.
- **The graph's values.** The expression graph had a 96-byte `CheckValue` for
  each of `sqlite3.c`'s 343,555 nodes, which only `VALUE` nodes read: 33 MB.
  The values are now the `VALUE` nodes' alone, in node order, which a 4-byte
  slot per node finds ([the checker guide](c-checker.md)).
- **The program pass's controls.** `check_program`'s labels, task worklist and
  controls had room for 8 entries a token in the scratch, 300 MB, of which
  each body takes a few at the start. Their starts, and the columns pushed
  after them, took pages past the reader's, which the scratch kept. They are
  now in an arena of `check_program`'s own.
- **#113's environment.** The C driver no longer publishes it
  (`check_input_compile`): only the deferred reader's tests and the corpus's
  comparisons read it.

These are the 9700X's medians, pinned to CPU 2 under the host's lock, of
`main` at `d8805cb7` and #572's head `f74414b9`: `measure-compile` in both
orders, each compiler the other's reference (21 rotated rounds each), and
`perf stat -r 11` of each, twice.
[The measurement record](https://github.com/buster14a/buster-performance/tree/703e034725e79fed5574e42b0be96b3af4cae9bf/docs/measurements/memory-572-2026-10-27) holds the logs, the scripts and the trees'
identities.

| | `main` | #572 | change |
| --- | ---: | ---: | ---: |
| wall time | 147.06 ms (145.98 to 147.92) | 140.26 ms (139.74 to 141.33) | −6.80 ms (−4.6%) |
| peak RSS | 514,000 KB | 393,140 KB | −120,860 KB (−23.5%) |
| minor faults | 2,251 | 2,265 | +14 |
| task-clock (perf) | 147.24 ms | 139.93 ms | −5.0% |
| cycles, kernel | 120.3 M | 108.5 M | −9.8% |
| cycles, user | 695.9 M | 667.3 M | −4.1% |
| instructions, user | 1,556.3 M | 1,558.0 M | +0.1% |

- **Reverse order:** `main` against #572 measured 146.83 ms against 140.33 ms.
- **Second `perf stat` pair:** 146.7 ms against 139.5 ms, kernel cycles −9.9%.
- **Integrated base, round 3.** Main `92c025e7` against the merged head
  `da352e29`, 21 rotated rounds in each order: 147.074 → 140.812 ms
  (−4.26%) and 146.980 → 141.056 ms in reverse order. Peak RSS is
  512,868 → 392,000 KB (−23.57%), with 512,864 → 392,024 KB in reverse.
  The first `perf stat -r 11` pair gives 692.359 → 671.525 M user cycles,
  120.167 → 108.781 M kernel cycles and 1,552.457 → 1,554.177 M user
  instructions (+0.11%). The second pair confirms lower user/kernel cycles.
  Objects are byte-identical. `round3.log`, its script and both source
  manifests are retained in the record. These are the named integrated
  snapshots; their gains are not added to other checker PRs' measurements.
- **Instructions.** The memsets the reader no longer makes took 6.8 M off the
  user instructions (round 1). The leaves' slots added 8.5 M, nearly all where
  clang's code around the graph's accesses moved, by an instruction-sampled
  profile on the laptop: `check_graph_add` is no longer inlined into
  `check_input_add`, and the reader's expression walk and the barrier's node
  step grew. The cycles fell all the same: the leaves no longer write 33 MB.

The stages that changed, from each compile's `--time-stages` medians:

| stage | ms before | ms after | peak KB added before | peak KB added after |
| --- | ---: | ---: | ---: | ---: |
| check-input | 63.96 | 58.20 | 259,488 | 204,248 |
| check-expressions | 15.56 | 14.72 | 53,508 | 0 |
| check-declarations | 5.33 | 5.53 | 10,240 | 2,252 |
| check-program | 12.31 | 12.18 | 12,288 | 14,340 |
| the clock's span | 146.49 | 139.77 | | |

- **The reader's arena.** It is gone before the compile's peak. On
  `sqlite3.c` it holds 61 MB when the reader returns, most of them huge pages
  of the columns it wrote, and their clearing is most of `check-input`'s
  system time. The expressions' output then takes their place in the RSS, so
  `check-expressions` adds nothing to the peak.
- **`check-declarations`.** Its 0.2 ms more is of the order of its spread
  between builds in #530's measurements (5.25 to 5.56 ms).
- **The reader alone** (round 1 of the record, the first commit): 142.1 ms
  against 147.3 ms, a peak RSS of 420,904 KB against 513,052 KB, kernel
  cycles −6.2% and user cycles −2.6%.
- **Profile.** `clear_page_erms` fell from 14.1% of the samples to 13.0%,
  and libc's, its `memset` mostly, from 4.6% to 3.6%.
- **The graph run.** `benchmark-checker-source`'s vector path took 36.42 ns a
  body node against 39.98 ns, its leaves now read one after another.

### What's left

At about 393 MB, by arena (smaps at the compile's end, on the laptop), as #572
left them; "The binding table, the fresh output and the stream's columns",
below, has what #628 changed since (the stream's values among it, now columns):
- **The driver's arena, 120 MB.** It holds:
  - the reader's output columns, which the later stages read, about 58 MB.
    Most are filled with `NONE` or zero for every token, though few tokens
    have an entry;
  - the expression graph, about 29 MB, 15 MB of it the `VALUE` nodes' values.
- **The checker's scratch, 88 MB.** The reader's 70 MB, and 18 MB of the
  parse's temporaries that lie past it.
- **The checker's output arena, 72 MB.** Most of it is the expressions'
  stream, with a 96-byte value per node (33 MB), which the later stages read
  (`stream->values`, on 128 lines of 15 files).
- **The front end:** the preprocessor's 47 MB, and the parser's 13 columns,
  each in an arena of its own: about 31 MB by their sizes.

Not at the peak, the reader's own arena's 61 MB of huge pages are still most of
`check-input`'s system time. [#628](https://github.com/buster14a/buster/issues/628)
followed up on these: "#628's outcome", below, has where it left them.

### The binding table, the fresh output and the stream's columns (#628)

Three of those allocations touched memory that held nothing:
- **The binding table.** Its nine event columns each have room for four events
  a token, of which the walk writes a prefix (`sqlite3.c`'s 167,456 of 2.86
  million) and reads no further. In the reader's arena's huge pages each
  column's first touch cleared one or two 2 MB pages, 11 of the arena's 34.
  The table is now in the arena's base pages, with the scope stacks: about
  4.5 MB of pages (1,126 more faults) where it took 22 MB.
- **The zero-default output columns.** The C driver's reader
  (`check_input_compile`) says that no copy of its arena pushed past its
  length. Where that arena's pages past its length are still the kernel's zero
  (`arena_untouched`: an owned mapping that no push reached past its length),
  the reader no longer writes zeros over its 17 zero-default output columns
  and the graph's `warning_suppressed`.
  A column the walk never writes then takes no memory: the later passes read
  the kernel's zero page there. That covers most of `sqlite3.c`'s byte columns
  and `expression_regions`. Every other caller, and a reused, popped or poisoned
  arena, keeps the clear. The `NONE` columns keep their fills.
- **The stream's values.** `CheckStream.values` had a 96-byte `CheckValue` for
  every node, of which `sqlite3.c`'s 323,557 nodes use little: 36% have an
  object and 12% anything past it (a constant's payload, an address
  constant's offset, a bit-field's place, a fault).
  - The fields every node has are now columns: type, flags, object, category
    and constant kind, 14 bytes a node.
  - A node with more keeps its whole value as a row of `payloads`, which
    `payload_rows` finds. On `sqlite3.c` the columns and row indices take
    5.8 MB and the rows under 4 MB, where the values took 31 MB.
  - The lowering reads the columns it prefetched before, and the batch path's
    classification loads the type column 16 nodes at a time where it gathered
    from 96-byte records.
  - Readers take values through `check_stream_value` and its fields' accessors
    ([the checker guide](c-checker.md)).

On this repository's 4-core cloud VM, without Zen 5's vector set (the scalar
host build, `--lanes 1`), `-c` of the frozen `sqlite3.c` peaks as follows, with
each build's object the same bytes by both paths:

| build | peak RSS | check-input's peak added |
| --- | ---: | ---: |
| main `f152e63` | 399,772 KB | 256,276 KB |
| binding table and fresh output | 395,680 KB | 234,204 KB |
| and the stream's columns | 377,332 KB | 233,992 KB |

- **The binding table** cuts `check-input`'s addition by 18 MB, and **the
  fresh output** cuts it by 4 MB more. The compile's peak is later than the
  reader's, so only the fresh output's empty columns showed there at first.
- **The stream's columns** take 18 MB more off the end of the compile, where
  the peak then is. The reader's peak is now within 4 MB of the end's.
On the 9700X, the `compile_time` step of the PR's run (#518's measurement:
`measure-compile`, 11 rotated runs, and `perf stat -r 11`, pinned to CPU 2) at
`f243b507` merged into main, against the same step of three runs of the same
morning, PRs whose changes don't reach the default `-c`'s checker or its
lowering (#1040's, #177's and #179's heads, on main 18 to 24 minutes later
than this PR's merge; their medians' median):

| | same-day runs | #628 and #529 | change |
| --- | ---: | ---: | ---: |
| wall time | 134.485 ms | 132.079 ms | −1.8% |
| peak RSS (`wait4`) | 400,108 KB | 381,072 KB | −19,036 KB (−4.8%) |
| task-clock (perf) | 133.49 ms | 131.30 ms | −1.6% |
| cycles, kernel | 109.9 M | 103.2 M | −6.1% |
| cycles, user | 630.4 M | 624.8 M | −0.9% |
| instructions, user | 1,399.4 M | 1,444.0 M | +44.6 M (+3.2%) |
| page faults | 1,997 | 3,114 | +1,117 |
| `clear_page_erms`' share of the samples | 14.56% | 13.00% | |
| check-input | 58.820 ms | 57.277 ms | −1.54 ms |
| check-input's peak added | 241,324 KB | 221,392 KB | −19,932 KB |
| check-expressions | 13.681 ms | 14.054 ms | +0.37 ms |
| check-program | 13.896 ms | 14.160 ms | +0.26 ms |
| lower | 14.157 ms | 13.556 ms | −0.60 ms |

- **The memory.** The peak falls by the stream's and the binding table's
  share, and the kernel's cycles by 6%. The page faults grow by the binding
  table's base pages: 4 KB faults where it took 2 MB ones.
- **The instructions.** 44.6 M more, 3.2%, are the accessors'. A value read
  through `check_stream_value` is built from the columns, or copied from its
  row, where it was one copy. `check_stream_type` and its kin test for a
  hand-built stream's `dense_values` first. The check and the program pass
  take 0.37 and 0.26 ms longer. The lowering, which waited on the stream's
  96-byte records, takes 0.60 ms less, and so does every stage that clears
  fewer pages. Making the hot readers read the columns directly, and the
  graph run's stores cheaper, is what is left of this change's cost.
- **The other runs' code.** The three runs built other PRs' heads on a
  slightly later main (link-foreign, docs, tests and #1010's x86 builtins,
  which `sqlite3.c` doesn't call). Their three medians agree within 1.2 ms of
  wall time and 0.2 M instructions. The comparison is no A/B of one build,
  but its differences are larger than that spread.

### Pages held for nothing: the parse's tail, the graph, the rare columns, the scratch (#628)

On main `92252c3` (after the section above), `-c` of the frozen `sqlite3.c` on
this repository's scalar-host VM (`--lanes 1`) peaked at 379.6 MB. The peak
came at two moments almost alike: at the end of the reader (367.8 MB, its own
arena still mapped) and in the backend (379.6 MB). Measured page by page
(`mincore` for what was mapped, `MADV_DONTNEED` for what that freed), much of
what both held was pages that no stage would read again, or that held no
entry:
- **The parse's tail.** The parse's temporaries take the checker's scratch
  first, then the reader takes their pages again for its own columns, as far
  as those reach: 77 MB of `sqlite3.c`'s. K4's stack in memory, last of the
  parse's buffers and touched at its slots' ends, kept three huge pages at 90
  to 96 MB, and the statement pass's levels one at 82 MB. The buffers are now
  pushed in the order of where they're touched: K4's slots first, the levels
  and the twin statements' records last. The vector path leaves the statements'
  13.9 MB alone, so that on the 9700X nothing the parse touches lies past the
  reader's columns (on this VM, where the twins run, the statements' first
  pages still do).
- **The expression graph.** Its 13 columns have room for a node a token, of
  which `sqlite3.c`'s graph holds 42% (323,557 of 774,038). Pushed one after
  another, each column's unused tail shared its huge pages with the next
  column's used start: 39 MB of huge pages for 17 MB of nodes. Each column of
  a huge page or more now starts one (`check_graph_column`), and the byte
  columns come first, beside the reader's output columns' end: 26 MB.
- **The reader's rare columns.** Its arena of rare columns had them all in huge
  pages but the scope stacks and the enumeration constants' values. Most take
  a few hundred spread tokens on `sqlite3.c`, so that each took every huge page
  of its column: 52 MB for under 4 MB of entries. The columns that only the
  tokens writing them read (tags, sizeof types, specifiers, designators, the
  initializers' finishes, the parameter shapes) are now in the arena's base
  pages, where they take only the pages written: 1,880 more faults, and 15 MB
  less at the reader's end. Those that the walks read at every token of a kind,
  where nothing was written, stay in huge pages, whose reads take the huge zero
  page: in base pages, they faulted 9,600 pages on reads alone.
- **The sparse marks.** The expressions' walk's classes read seven sparse
  columns at every token, 16 at a time, to say whether one has an entry there
  (`check_input_expression_sparse`): 28 bytes a token. A bit a token now says it
  (`CheckInputReader.sparse_marks`), kept by every write of those columns, and
  the classes' vector path reads its word for 64 tokens. The cached constants,
  the `va_arg` calls, the designators and the variable arrays' bounds, which
  the walk then tests only where a mark says, move to the base pages too. The
  scalar twin reads the columns, and counts every mark that they contradict
  (`CheckInput.visit_misses`, which the tests require to be 0). On this VM only
  the twin runs (`BUSTER_SCALAR_PATH`), which reads the four columns at every
  token: 2,600 more faults on the zero page here, and none on the 9700X.
- **The scratch through the backend.** `check_program` takes 51 MB of the
  scratch, and the backend after it 24 MB, while their outputs and the
  lowering take pages of their own. The 26 MB past `check_program`'s columns,
  the parse's and the reader's, stayed resident through the backend's peak.
  `check_program` now gives them back after it pushes its columns
  (`arena_discard`, `lane.c`), which costs no fault: no later stage reaches
  them.

On this VM, with the objects the same bytes by both paths (three runs of each,
which agree within 40 KB; the reader's end is the high-water mark when
`check-input` ends, with its arena of rare columns still mapped):

| build | peak RSS | at the reader's end | page faults |
| --- | ---: | ---: | ---: |
| main `92252c3` | 379,600 KB | 367,808 KB | 5,186 |
| the parse's buffers, the graph's columns and the rare columns | 367,292 KB | 340,256 KB | 7,008 |
| and `check_program`'s discard | 340,796 KB | 340,540 KB | 7,005 |
| and the sparse marks | 340,784 KB | 336,160 KB | 9,620 |

- **The parse's buffers and the graph's columns** took 1.4 and 10.3 MB off
  the reader's end in a measurement of each alone, and **the rare columns**
  15 MB. The backend's peak, 27 MB above the reader's end then, was the
  compile's.
- **The discard** takes the backend's peak below the reader's end, which sets
  the compile's again, within 0.3 MB of the end of `check_program`.
- **The sparse marks** take 4.4 MB more off the reader's end, the four columns'
  huge pages, and the end of `check_program` is the compile's peak, 4.6 MB
  above it. Here the twin's reads of the four columns are 2,600 faults on the
  zero page, which the vector path doesn't make.

A column that `check_program` pushes before the discard keeps the scratch's
earlier pages under it, whatever it writes. #1154's dropped ranges, room of
8 bytes a token for a prefix of a few kilobytes, kept 5.7 MB of them on
`sqlite3.c`, and the lowering and the emission's setup set the peak past the
checker's again, 4 MB higher on main `703ae9f5` than on `b70deb47`. They are
pushed past the discard now (#628): on the 9700X, main `9a74fc37`'s peak is
295,084 KiB and the change's 291,288, with every object the same bytes and the
times flat ([the
record](https://github.com/buster14a/buster-performance/tree/cfb3faec63e81977de79432fe72727f524b5f17d/docs/measurements/dropped-ranges-628-2026-11-03),
with the bisect).

**On the 9700X.** The `changes` job now selects the compile-time attribution
when the checker's reader, graph, parser, program or arena sources change, so
this PR's job ran the step's own `measure-compile --runs 11` on the frozen
`sqlite3.c` with `-march=znver5`. Main's latest successful scheduled run of the
same step (`ebee03e`) against this PR's merge (`f74aa39f`):

| build | wall | user | system | peak RSS | check-input | check-expressions | check-program | lower |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| main `ebee03e` | 134.574 ms | 111.333 ms | 22.938 ms | 401,724 KiB | 58.902 ms | 14.033 ms | 13.824 ms | 14.125 ms |
| this PR `f74aa39f` | 117.485 ms | 100.340 ms | 16.720 ms | 325,528 KiB | 49.142 ms | 14.281 ms | 10.066 ms | 13.565 ms |

The kernel's cycles (`perf stat`'s `cycles:k`) were 110,131,476 on main and
93,768,014 on this PR. Its minor faults rose from 2,139 to 5,098, the rare
columns' base pages and the discard's first touches, for the 76,196 KiB lower
peak. The PR's merge includes main's later checker changes, so this table is
the final branch against main's latest successful scheduled run; the VM table
above is the isolated PR change.

What the reader's end still holds for little, besides the scratch and the
front end, which their first users fill (the parse's temporaries, then the
reader's columns, to 77 MB of `sqlite3.c`'s scratch):
- **The `NONE` output columns.** Twelve of the reader's output columns hold
  under 3% entries on `sqlite3.c` (`label_targets` 797, `control_owners` 3,012,
  `declaration_bodies` 16,655, `case_ranges` none), but spread over every page,
  so that each takes its 2.8 MB whatever its pages: 33 MB. The walk reads most
  of them back by token (the initializers' and the parameters' 12 to 18 times),
  so that records in their place, written as the walk goes, need its lookups to
  change, and #592's passes' too. They are sparse columns now (below).
- **The graph's leaves.** Their 96-byte values take 14 MB for 144,720 leaves, of
  which the identifiers' carry little but a type and an object: columns and
  payload rows, as the stream's values have, would take about 8 MB less. They
  are 32-byte records now ("The graph's leaves", below).
- **The byte columns.** Twelve zero-default output columns of a byte a token,
  8.3 MB, written at declarations, most of whose bits would fit one byte. Seven
  of them are one now ("The declaration bits", below).
- **The `attributes` column.** A pointer a token, 5.7 MB, for 2,457 tokens'
  attributes, and four passes read it whole. It is a wide sparse column now
  ("The attributes column", below).

### The sparse output columns (#628)

On main `3ce71ec3`, `-c` of the frozen `sqlite3.c` (716,136 tokens) peaked at
323.5 MB on the 9700X, at the end of `check-input`, which added 171 MB of it.
Twelve of the reader's output columns took 34.4 MB of that, a value a token
each, for 57,349 entries:

| column | entries | 16-token blocks with one |
| --- | ---: | ---: |
| `declaration_bodies` | 16,655 | 8,917 |
| `parameters` | 13,789 | 6,777 |
| `parameter_next` | 5,934 | 3,837 |
| `initializer_begins`, `initializer_ends` | 4,182 each | 3,502 each |
| `initializer_nodes` | 3,982 | 3,318 |
| `control_owners` | 3,012 | 1,285 |
| `body_declarations` | 2,674 | 2,671 |
| `string_ids` | 2,132 | 1,445 |
| `label_targets` | 797 | 780 |
| `evaluation_nodes`, `case_ranges` | none | none |

The entries lie on most of each column's pages (on 155 to 604 of its 700
base pages, but for the two empty ones), so neither base pages nor a value for
NONE that a fresh page holds would have saved much of their memory. And the
reader filled all twelve with NONE before the walk, 34.4 MB of stores.

They are sparse columns now (`c_check_sparse.c`):
- **The layout.** A column is a directory, an entry for each 16 tokens, of
  blocks of 16 values in a pool that the unit's columns share. Block 0 holds
  16 NONEs and is never written, so a directory's zero entry reads NONE for its
  16 tokens. A fresh output arena's directories are as the kernel mapped them,
  so the columns need no fill (`check_input_run`'s `output_zero`); otherwise
  only the directories are cleared. On `sqlite3.c` the columns take 36,034
  blocks, 2.3 MB, and the directories 2.1 MB: 4.5 MB in place of 34.4 MB.
- **Reads and writes.** A read is two loads (`check_sparse_get`), and 16 tokens'
  values two blocks' and a permute (`check_sparse_vector`), at any start, which
  the program's and the lowering's scans of 16 tokens at a time take. A write at
  a token whose 16 have no block takes the pool's next; a write of NONE there
  takes none. The pool has a block for each directory entry of each column, which
  a write takes once at most.
- **Lanes.** The deferred reader's lanes write their own tokens of the same
  columns at once, and two lanes' tokens can share 16. There a write claims the
  empty directory entry, then takes, fills and publishes the block; another
  lane's write waits for it. A read of a claimed entry reads block 0, which is
  right, as the reading lane hasn't written its own token there yet.
  - The lanes take blocks one after another, so two lanes' first fills can fall
    in one fresh 2 MB region of the pool. As with the rare columns
    (`check_input_rare_claim`, #764, #847), the first lane to fill a block in a
    region claims it and faults it in, and the others wait
    (`check_sparse_region`), so no two lanes clear a huge page for it at once.
  - A shared pool's directories are cleared on the reading's own thread, so
    their pages are in place before the lanes start.
- **The deferred merge** maps `string_ids` at its entries by
  `check_sparse_entries`, which skips 16 tokens without a block with one load,
  and loads a block's values in its lane's range alone: a neighboring lane
  writes the others of a block that straddles the range's end.

The reader's own six NONE columns (`activate`, `first_value` and the others), in
the scratch, are filled a column at a time on the vector path too, as the
output's two dense ones are (`check_input_output_none`); the scalar twin keeps
its one loop over every token.

`test-checker`'s scans test checks the columns against columns of a value a
token at every count to 200: random writes, every token's value, every 16
tokens' vector at every start, the masks and the entries' lists by both paths,
with the directory ending at a `PROT_NONE` page. Three lanes then write one
shared pool's columns at once, interleaved tokens of the same blocks, and scan
and write their own ranges, which start and end inside blocks: every value, every
block taken once, and every region's claim settled. The tests that build a
`CheckInput` by hand build its sparse columns from their values.

On the 9700X, CPU 2 under the host's lock, main `dac5b770` against the change
merged with it (`67f7bf51`), with every object the same bytes, by both paths, on
8 lanes and with `-g`: 8 interleaved rounds of `perf stat -r 3`, and
`measure-compile --runs 11` of each with the other as its reference ([the
record](https://github.com/buster14a/buster-performance/tree/a012932ca4ca85bca5783d65245ac9837942d02c/docs/measurements/sparse-columns-628-2026-11-02)
has the raw outputs, the receipts and the scripts):

| measure | main | sparse columns |
| --- | ---: | ---: |
| task clock | 117.07 ms | 116.07 ms |
| user cycles | 555.8 M | 555.5 M |
| user instructions | 1,274.2 M | 1,272.1 M |
| kernel cycles | 93.6 M | 88.0 M |
| peak RSS (`wait4`) | 323,720 KiB | 296,856 KiB |
| `check-input` | 49.07 ms | 47.35 ms |
| `check-input`'s peak added | 171,256 KiB | 144,504 KiB |

- **The time.** Every round of the 8 has the change 0.8 to 1.5 ms faster, nearly
  all of it the kernel's: the fills' and the pages' clearing it no longer does.
  An earlier round on main `3ce71ec3` (with `93f2085f`, the same code) had it
  2.0 ms faster, with 1.1% fewer user cycles, which this round doesn't repeat:
  a placement's difference, as `sqlite3.c`'s `-c` is known to show.
- **The peak** is still the reader's end, 26,864 KiB (8.3%) lower.
- **The shared pool's claims** (`3950f15b`, after the review), measured the same
  way against `dac5b770`: task clock 117.07 → 115.90 ms, kernel cycles 93.8 →
  89.3 M, peak 323,608 → 296,856 KiB, objects the same bytes. User
  instructions are 0.75 M more than `67f7bf51`'s: the directories' acquire loads
  and the exhaustion test.

### The attributes column (#1167)

On main `b70deb47`, `CheckInput.attributes` held a pointer a token: 5.7 MB on
`sqlite3.c`, for the 2,457 tokens that have attributes, whose entries lie on 219
of its 1,399 base pages and so, in the output arena's huge pages, on all of
them. Four passes read it whole, 8 pointers at a time: the asm labels' list of
attributed tokens (`check_input_asm_attributed_tokens`, 0.81% of `-c`'s
cycles), the late attributes' masks, the driver's attribute warnings and the
emission's attributed declarations.

It is a wide column of the sparse pool now (`CheckSparseWide`):
- **The layout.** A directory entry for each 16 tokens names two consecutive
  blocks of the pool, which hold the 16 tokens' values of 8 bytes, and the zero
  entry blocks 0 and 1, both NONEs, which a pool with a wide column keeps. The
  column holds each pointer complemented, so that a null pointer is its NONE:
  a token without attributes takes no block, and a fresh arena's directory
  needs no fill. Reads and writes go through `check_input_attributes_at` and
  `check_input_set_attributes_at`.
- **Lanes.** The deferred reader's lanes take its blocks by the pool's claims,
  as the other columns' are taken (`check_sparse_take`, of two blocks here).
- **The scans.** Each of the four skips 256 tokens without attributes with a
  load of their 16 directory entries (`check_sparse_wide_next`) and tests a
  group's values 8 at a time (`check_sparse_wide_group`); the scalar path, their
  twin, visits every group and reads each token's value.

`test-checker`'s scans test checks wide columns against columns of a 64-bit
value a token at every count to 200, beside a column of the same pool: every
token's value, every group's bits and the next group from every start, and the
entries' lists, by both paths, with the directory ending at a `PROT_NONE` page.
Its lanes test writes a wide column of the shared pool at once too.

On the 9700X, main `b70deb47` against the change on it (`ebd8cf4e`), measured as
the sparse output columns were above, with every object the same bytes by both
paths, on 8 lanes and with `-g` ([the
record](https://github.com/buster14a/buster-performance/tree/41457e6769eff22baa23243b55a0787f707f3d74/docs/measurements/attributes-column-1167-2026-11-03)):

| measure | main | wide `attributes` |
| --- | ---: | ---: |
| task clock | 114.26 ms | 113.30 ms |
| user cycles | 546.0 M | 541.3 M |
| user instructions | 1,276.4 M | 1,275.8 M |
| kernel cycles | 87.3 M | 87.2 M |
| peak RSS (`wait4`) | 291,208 KiB | 287,104 KiB |
| `check-input` | 45.73 ms | 44.96 ms |

- **The time.** Every round of the 8 has the change 0.6 to 1.4 ms faster. The
  4.7 M fewer user cycles are about the asm labels' 0.81%, but this is one
  placement each.
- **The peak** is 4.1 MB lower. It isn't the reader's end any more: a map of
  each push's resident pages on the laptop puts `VmRSS` at 290.9 MB there and at
  260.5 MB once the reader's arena of rare columns is released, and
  `check-expressions` and `check-declarations` take it to the peak, 297.6 MB.
  What the reader keeps to the peak lowers it; the rare columns, which their
  few writes spread over about 30 MB of huge pages, cost that much clearing
  instead (in base pages, the reader's reads of them take 6,800 more faults).

### The declaration bits (#1167)

Seven of the reader's byte columns held a flag or two at its declarations:
`defined`, `tentative`, `static_objects`, `registers`, `thread_local`,
`compound_objects` and `linkage`. At the peak, each took its 700 KiB of huge
pages on `sqlite3.c`, the empty ones too, since the written ones share their
pages. They are one byte a token now, `CheckInput.declaration_bits`: a bit each
(`BUSTER_CHECK_BITS_DEFINED` and the others) and the linkage, 0 to 2, in the top
two. `check_input_bit`, `check_input_set_bit`, `check_input_linkage` and
`check_input_set_linkage` read and write them, and the scans test a bit of 64
tokens' bytes at once (`check_input_scan_bit`, `check_input_scan_linkage`), as
they tested the columns' bytes. The expressions' walk gathers a declaration's
byte once for its three flags. `declaration_kinds`, `function_flags`,
`explicit_abis` and the constant warnings' two columns stay columns of their own.

On the 9700X, main `703ae9f5` against the branch merged with it (`49f066ba`),
both changes, measured as the attributes' change was, with every object the same
bytes ([the
record](https://github.com/buster14a/buster-performance/tree/3dd0b4bfce2982eae0fb2ccb42be8a537d89c951/docs/measurements/attributes-column-1167-2026-11-03),
`round5/`):

| measure | main | both changes |
| --- | ---: | ---: |
| task clock | 111.59 ms | 109.25 ms |
| user cycles | 530.4 M | 520.7 M |
| user instructions | 1,204.8 M | 1,204.1 M |
| kernel cycles | 88.0 M | 85.5 M |
| peak RSS (`wait4`) | 295,172 KiB | 287,028 KiB |
| `check-input` | 44.37 ms | 43.15 ms |

Every round of the 8 has the branch 1.8 to 2.8 ms faster. The peak is 8.1 MB
lower, of the 12 MB that #1167 bounded for the two changes. Main's own peak had
risen 4 MB since `b70deb47`, by its changes between.

### The graph's leaves (#628)

On main `ec5f0f6b`, a map of each push's resident pages (the attributes
column's record's `peak-map.patch`, on this repository's 4-CPU cloud VM, one
lane, with huge pages on as the 9700X has them, `BUSTER_HUGE_PAGES=1`) finds
three peaks of `-c sqlite3.c` almost alike:
- the reader's end, 283.5 MB, with its arena of rare columns, 31 MB, still
  mapped;
- `check-declarations`' end, 286.1 MB, with the graph run's outputs;
- the backend's, with `check_program`'s and the lowering's columns: 278.8 MB
  with `-w`, but 297.0 MB with the step's options. `sqlite3.c`'s one warning
  (#1339's, which the scalar path and clang don't give) makes the preprocessor
  record its history (`pp_history_record`): it preprocesses the unit again, and
  the replay keeps 16.6 MB to the end, 12 MB of it its output tokens, which only
  its comparison with the unit's output reads.

All three hold the parser's columns (16 MB), the graph's leaves (14.3 MB), the
preprocessor's output tokens (12.5 MB) and its file's windows (11.5 MB). The
leaves were a 96-byte `CheckValue` each, 144,720 of them: 24,071 have more than
the stream's columns hold (#1022), 23,991 of those only an integer (a
literal's or an enumerator's value), and 80 more than an integer of 64 bits.

They are 32-byte records now (`CheckGraphLeaf`, `c_checker.c`):
- **The layout.** A record holds the value's type, flags, object, overload set,
  the low 64 bits of its integer, category and constant kind. A value with more
  (`check_graph_leaf_rest`: a wider integer, a floating payload, an address's
  offset, a bit-field's width or place, a fault) also takes a whole `CheckValue`
  row of `leaf_payloads`, the record's `row`. A leaf takes a row at its first
  write at most, so that the rows are at most the leaves: the column has room
  for a row a node, of which only those written take memory. On `sqlite3.c`
  the records take 4.6 MB and the rows 7.5 KB.
- **Reads and writes.** `check_graph_leaf_value` gives a leaf's whole value,
  and `check_graph_leaf_type`, `check_graph_leaf_flags`,
  `check_graph_leaf_object` and `check_graph_leaf_overload_head` its record's
  fields. `check_graph_set_leaf` writes a leaf the first time, as the reader's
  walk and its coded classes write each once, without reading what its storage
  held; `check_graph_update_leaf` writes over one in the row it has. A
  hand-built graph may hold whole values in `dense_leaves`, as a stream may hold
  `dense_values`, and every accessor reads either.
- **The graph run** writes a leaf that is its record alone, without a row, an
  integer or an overload set (an identifier's, most of them), into the stream's
  columns with no value in memory between them.
- **Lanes.** The deferred reader's parts write their leaves in ranges of their
  own (`leaf_offset`), and their rows in the same ranges of the rows. The merge
  moves a part's rows after the graph's and renumbers its records' rows
  (`check_deferred_expression_leaves`), a scalar loop that only runs where both
  have rows.

`test-checker`'s `check_test_leaf_records` writes random values, each field zero
or not and some with more than a record, into records and into dense leaves,
their twin, over storage that earlier writes left dirty; writes a quarter again;
then appends a part written in its own range as the merge appends it. Every
leaf must read back the same, whole and by its fields, with at most a row a
leaf.

On the VM, main `ec5f0f6b` against the change, three runs each, which agree
within 8 KiB, with every object and diagnostic the same bytes by both paths:

| pages | measure | main | records |
| --- | --- | ---: | ---: |
| huge (`BUSTER_HUGE_PAGES=1`) | peak RSS | 297,024 KiB | 290,836 KiB |
| | `check-input`'s peak added | 145,328 KiB | 139,188 KiB |
| base (the VM's default) | peak RSS | 227,988 KiB | 218,908 KiB |
| | minor faults | 72,411 | 70,154 |

The peak is the backend's in both, 6.2 MB lower with huge pages, where the
records take three of them and the rows one, against seven for the values; in
base pages the 9.1 MB of the values' pages that the records don't touch.

On the 9700X, CPU 2 under the host's lock, main `ec5f0f6b` against the change
(`9cfc0f50`), built with `BUSTER_HOST_ARCH=znver5`, measured as the attributes'
change was: 8 interleaved rounds of `perf stat -r 3`, then `measure-compile
--runs 11` of each with the other as its reference, all of them under one hold
of the host's lock (about 20 seconds), and one `--time-stages` run of each under
another. Every object is the same bytes by both paths, on 8 lanes and with
`-g`.

| measure | main | records |
| --- | ---: | ---: |
| task clock | 138.86 ms | 138.28 ms |
| user cycles | 676.1 M | 674.1 M |
| user instructions | 1,636.3 M | 1,637.8 M |
| kernel cycles | 94.7 M | 93.0 M |
| peak RSS (`wait4`, `measure-compile`) | 293,592 KiB | 289,976 KiB |
| peak RSS (`--time-stages`) | 293,660 KiB | 289,888 KiB |
| its growth past the process's start | 291,792 KiB | 284,176 KiB |
| `check-input`'s peak added | 149,916 KiB | 143,840 KiB |
| `check-input` (`measure-compile`) | 59.71 ms | 59.22 ms |

- **The time.** Every round of the 8 has the change 0.3 to 1.0 ms faster; the
  kernel's cycles are 1.7% fewer, the user instructions 0.1% more (the
  accessors).
- **The peak.** `check-input` adds 6.1 MB less, as on the VM, and the stages
  after it about as much as main's do, so that the process grows 7.6 MB less past its
  start. But on `sqlite3.c` the change's process starts 3.8 MB larger at its
  first mark than main's (5,712 against 1,868 KiB), which an empty file's
  doesn't show (2,016 against 1,868 KiB) and the VM doesn't either (5,556
  against 5,588 KiB), and nothing the change writes runs by then: the whole
  peak is 3.8 MB lower.

What the peaks still hold for little, after this:
- **The scratch's high-water.** 90 MB at the reader's end, and to the end of
  `static-data`, where `check_program` and the backend use 57 MB of it. Two
  things reach its top: the parse's buffers (`parse_vector_buffers_reserve`'s
  statements, to 83 MB) and the reader's deepest temporaries (the deprecated-use
  pass's, to 86 MB, which #1530 changes). Below them the reader keeps its own
  columns, 64 MB, its traversal's 13.5 MB among them.
- **The rare columns**, 31 MB at the reader's end ("Pages held for nothing",
  above).
- **The replay's output tokens**, 12 MB for a unit with any diagnostic. Its
  collected diagnostics read its tokens' origins, so only the token columns
  could go after its comparison.
- **The stream's conversions**, 32 bytes a node, 10 MB from `check-expressions`
  through the backend, where a leaf's is its type.

### The expansion history's replay (#628)

A unit whose diagnostics need the macro expansions' history records none while
it is preprocessed (`PPOptions.defer_history`) and, when a reader first asks for
it (`pp_history_ensure`), preprocesses itself again with the history on: a
replay, which must give the same output, token for token.
- **Every unit replayed.** #1455 (`f4272a46`) made a macro's number after a
  comma a candidate for `__clang__`'s `_Clang` reading
  (`parse_clang_scope_numbers`), which only a C23 attribute list's comma can
  be. The first candidate asks for the history, so every unit with a macro's
  number in a call's arguments replayed its preprocessing at the parse: on the
  VM, `sqlite3.c`'s `-c` parse added 12.5 MB, and the compiler's own `main.c`'s
  `-fsyntax-only` parse took about 250 ms and 140 MB more. A comma's candidate
  now counts only past the unit's first `[[`, which an attribute list's comma
  follows; both paths track it, the vector path as the bits from the first token
  right after a `[[`. `test-parser` checks that a unit without one keeps its
  history deferred by both paths, and that one with a `[[` before such a comma
  asks for it.
- **The replay's output.** A replay kept its output token columns to the unit's
  end, though nothing reads them after its comparison with the unit's output:
  12 MB of `sqlite3.c`'s, in the backend's peak, for a unit with any diagnostic.
  They are released once the replay is taken; its origins, which a collected
  diagnostic's group may own, stay.
- **The diagnostics.** With the history recorded at the parse, main's vector
  path doesn't give #1339's false `-Wempty-body` at `sqlite3.c:74649`, since
  the warning reads the history when it's there. With the change, it gives it
  again, as before #1455, until #1547 fixes the warning: its objects are the
  same bytes, and the `--scalar` path's diagnostics too.

On the VM, main `67cd05ca` against the change, `-c sqlite3.c` with huge pages
(`BUSTER_HUGE_PAGES=1`), three runs each, which agree within 12 KiB:

| measure | main | the change |
| --- | ---: | ---: |
| `parse`'s peak added | 97,716 KiB | 79,600 KiB |
| peak RSS, with `-w` | 297,664 KiB | 279,552 KiB |
| peak RSS, with the warning | 297,732 KiB | 279,620 KiB |

### Huge pages held for under a megabyte (#672)

Each mapping's resident memory at the compile's peak, on this repository's VM
with the arenas' huge pages (`BUSTER_HUGE_PAGES=1`) and without them
(`BUSTER_HUGE_PAGES=0`), at main `ec5f0f6b`: 297.0 MB against 228.0 MB. Every
arena's mapping was logged with its call stack and matched to
`/proc/self/smaps` at each `--time-stages` mark ([the
record](https://github.com/buster14a/buster-performance/tree/bc07aad4fd01b54bf7a00c332e29b21a939551a0/docs/measurements/page-policy-672-2026-11-06),
`diag/`). The checker's three arenas hold 38 MB of the 69 MB difference (the
driver's arena 27 MB, its output arena 7.8 MB, its scratch 3.5 MB), which
#628 follows. Four arenas outside the checker each took a huge page or two for
under a megabyte:

| arena | huge pages | base pages |
| --- | ---: | ---: |
| the type table's index (`check_driver_unit`) | 2,112 KB | 128 KB |
| the file cache's records arena (`file_cache_arenas`) | 2,112 KB | 268 KB |
| the file cache's bytes arena | 2,112 KB | 376 KB |
| the thread's scratch (`scratch_begin`) | 4,160 KB | 1,048 KB |

The thread's scratch took two: a large file's windowed preparation
(`pp_prepare_windows`) pushes three arrays of a word for every 64 of its
bytes, 3.5 MB for `sqlite3.c`, and touches each one's start. These now start
with base pages: the index's first 4 MB (`BUSTER_TYPE_INDEX_SMALL_BYTES`,
65,536 buckets), the two file cache arenas' first 2 MB
(`BUSTER_FILE_CACHE_SMALL_BYTES`) and the scratch's first 4 MB
(`BUSTER_SCRATCH_ARENA_SMALL_BYTES`). On the VM the peak falls from 296.8 to
288.3 MB, for 386 more faults, with the object the same bytes. On the 9700X an
experiment build of the change, whose worktree's source the record keeps as
captured after the runs, peaked
alone at 287.2 MB against main's 293.7 MB, with 396 more faults. Its observed
medians were 138.7 against 139.9 ms, from three unpaired runs each, which
establish no difference in time.

### Partly used huge pages, and three changes not taken (#628)

A map of each push's resident pages at the three peaks of `-c sqlite3.c`, with
huge pages and with base pages (`BUSTER_HUGE_PAGES=1` and `=0`, the peak map
above, on the VM), finds about 30 MB of 2 MB pages that their pushes touch in
part, in all three. On #1574's tree (the VM, huge pages) the peaks are within
3 MB of each other: 277.3 MB at the reader's end, 279.9 MB at
`check-declarations`' end and 278.0 MB in the backend. A change lowers the
compile's peak only by what it takes off all three, or off the highest down to
the next. Three changes were measured and aren't in:
- **The reader's sparse output in base pages.** The sparse columns' directories
  and pool (`check_sparse_reserve_wide`) and the graph's leaf rows
  (`leaf_payloads`), each with room for a value a token or a leaf, of which the
  reader writes few, went to an arena of base pages that the C driver reserved
  for each unit (#1584's first commit, `b6708929`).
  - **The VM.** Every peak was 6.0 MB lower on #1574's tree, and on main
    `faef1f98` the peak was 281,776 KiB against 287,848, for 1,575 more faults.
    The leaf rows alone changed nothing: the pool's pages are the 6 MB, and its
    faults.
  - **The 9700X.** Main `faef1f98` against the change (`dd32e723`): 7
    interleaved rounds, each compile under its own grant of the lock, with every
    object the same bytes. The peak was 288,328 KiB against 290,936, only
    2.6 MB lower. The task clock was 137.11 ms against 136.31, slower in each
    of the 7 rounds, with the same user instructions, 1,522 more faults and 1%
    more kernel cycles; `check-input` took 0.94 ms more
    ([the record](https://github.com/buster14a/buster-performance/tree/7626df3211c5cde011c00cb818af2698e6976b57/docs/measurements/sparse-output-628-2026-11-07)).
- **The rare columns in base pages**, for a reading in source order
  (`check_input_sparse_arena`, as a large unit's deferred reading has them).
  The reader's end fell 17.7 MB on the VM, for 6,800 more faults in
  `check-input` and the same system time there. `static-data`'s end stayed the
  peak.
- **The stream's conversions in 16 bytes a node** (its first two operands'),
  with the third operand's and the stored value's (667 and 3,818 of 323,557
  nodes) in two wide sparse columns. The column fell from 10.1 to 5.1 MB. But
  the sparse pool's room, a block for each directory entry, starts a 2 MB page
  for 0.5 MB of blocks, and the output arena's later pushes start other 2 MB
  pages. The peak fell 0.2 MB.

What would lower all three is in the huge pages that columns of unknown length
touch in part: the graph's node columns (about 8 MB, a node a token of room,
42% of it used) and the preprocessor's token columns (5.6 MB). Each needs its
count, or a bound close to it, before it is pushed, so that its columns can
sit side by side. Base pages for them trade faults for the memory, as the
sparse output's did. The file's windows, the third, now go after the run
(below).

### The files' token columns, and `-Wempty-body`'s replay (#628)

The file cache's token columns (`FileCacheArenas.columns`: the kinds, starts
and values, 9 bytes a token, and the leading-space words after the kinds) held
11.5 MB of `sqlite3.c`'s, from preprocessing to the end, in all three peaks.
Nothing reads them after the run: the parser, the checker and the diagnostics
read the output, the ranges, and the files' bytes and line tables. The
checker's driver now gives them back once the run is over
(`pp_release_file_columns`, `docs/c-preprocessor.md`'s "Deferred history"),
and a later replay for the history prepares the files again.

That made a replay dearer, and `sqlite3.c`'s compile with the `compile_time`
step's options replayed: not for a diagnostic, but for `-Wempty-body`'s
indentation check (#1339). sqlite defines `NDEBUG`, so glibc's `assert` is
`(__ASSERT_VOID_CAST (0))`. A `for (...);` followed by an `assert`
needs the column of the `assert`'s invocation, which only the history gave:
the whole unit was preprocessed again in `check-program`, for that one column.
`pp_output_invocation_place` now gives it from the file's bytes when the two
file tokens around the expansion hold one invocation alone, and the history
answers anything else (`--scalar` always asks it).

On the VM with huge pages (`BUSTER_HUGE_PAGES=1`), main `4d6562be` against
the change, `-c sqlite3.c`, five runs each, the objects the same bytes
(medians):

| measure | main | the change |
| --- | ---: | ---: |
| peak RSS, the step's options | 269,604 KiB | 255,464 KiB |
| `check-program`, the step's options | 59.3 ms | 46.5 ms |
| minor faults, the step's options | 6,282 | 5,902 |
| peak RSS, `-w` | 269,604 KiB | 255,464 KiB |
| `check-program`, `-w` | 19.4 ms | 18.8 ms |
| minor faults, `-w` | 5,832 | 5,900 |

The peak map's three plateaus (the instrumented build of "Partly used huge
pages", on main `53854a7c`, with the change applied) fall from 270.1, 270.9
and 269.8 MB to 255.9, 256.6 and 250.0 MB: the reader's end, `static-data`'s
end and the backend. The backend falls further since it no longer holds the
replay's history. `static-data`'s end is now the compile's peak, 0.7 MB above
the reader's.

On the 9700X, main `4d6562be` against the change (`fb2b4af1`), each compile
under its own grant of the lock, the order alternating by round
([the record](https://github.com/buster14a/buster-performance/tree/fd7993d1a82e4c3e2ead2fe7cbd97cc3e01db725/docs/measurements/file-columns-628-2026-11-07)),
every object the same bytes in six modes and the diagnostics too (medians):

| measure | main | the change |
| --- | ---: | ---: |
| task clock, the step's options (7 rounds) | 131.06 ms | 124.95 ms |
| user instructions | 1,403.6 M | 1,323.8 M |
| kernel cycles | 93.81 M | 86.72 M |
| `check-program` | 31.22 ms | 25.18 ms |
| page faults | 6,265 | 5,845 |
| peak RSS | 272,556 KiB | 258,172 KiB |
| task clock, `-w` (5 rounds) | 109.22 ms | 108.96 ms |
| kernel cycles, `-w` | 89.27 M | 86.27 M |
| peak RSS, `-w` | 272,560 KiB | 258,172 KiB |

With the step's options the change is faster in each round, by 4.77 to 6.44 ms:
its 80 M fewer instructions are the replay's. With `-w`, which never replayed,
the time is the same.

### The scratch after the reader, and the rare columns' pages (#628)

After the change above, on the VM's peak-map build, the reader's end is 255.9 MB,
`static-data`'s end 256.6 MB and the backend 250.0 MB. Four variants that lower
them were measured on the 9700X against it (`fb2b4af1`), each compile under its
own grant of the lock, every object and diagnostic the same bytes
([the record](https://github.com/buster14a/buster-performance/tree/a760df46a6040b235b6ee365da81cd1b23fa6698/docs/measurements/scratch-rare-628-2026-11-07),
with the variants' patches). None is in, since each raises the kernel's cycles:

| variant | peak RSS (KiB) | task clock (ms) | kernel cycles (M) |
| --- | --- | --- | --- |
| the whole scratch given back after the reader and the common warnings | 258,172 → 257,824 | 124.04 → 126.16 | 85.41 → 100.53 |
| the same, with the rare columns in base pages, populated | 258,208 → 239,876 | 124.43 → 125.19 | 87.64 → 104.94 |
| the rare columns alone | 258,168 → 258,172 | 124.17 → 122.71 | 87.16 → 89.19 |
| the rare columns, the scratch given back past 96 bytes a token | 258,172 → 248,564 | 124.29 → 123.25 | 86.81 → 88.89 |

- **The scratch.** After the reader it holds the parse's and the reader's pages,
  dead, to `static-data`'s end: giving them all back takes that plateau from
  256.6 to 174.1 MB on the VM. But `check-expressions`, `check-declarations`,
  `static-data` and `check_program` push about 64 MB of it again before
  `check_program` gives it back, and fault those pages in again, from huge pages
  the kernel clears: 15 M kernel cycles. Keeping 96 bytes a token past the
  scratch's length costs nothing, and still takes that plateau to 233.5 MB. The
  common warnings never pop what they push (21.7 MB of `sqlite3.c`'s), which the
  backend then pushes past; popping it lets the backend reuse those pages.
- **The rare columns** (`check_input_sparse_arena`'s token columns) hold 46 MB
  of huge pages at the reader's end. In base pages, populated by one
  `MADV_POPULATE_READ`, the reader's end falls 17.7 MB and `check-input` takes
  1.6 ms less, but the kernel's cycles rise 2.0 M. Of the columns' 6,673 pages
  the reader writes 688 and only reads 5,985 more, each of which the populate
  maps to the zero page. The reads that reach nearly every page are the
  expressions' walk's classes, which load `specifier_finishes` and `builtins` at
  every token, and the reads at every declaration of the specifier jobs, the
  storage, the bases and the VLA scopes' ends; for `sqlite3.c`, seven of the 16
  columns are never written. A mark a token of the columns' writes, as the
  reader's sparse marks are (`check_input_sparse_mark`), would let those reads
  skip what nothing wrote, so that the columns could be base pages that fault
  only where written, with no populate.

### The rare columns read only where written (#628)

What the section above found to make the rare columns pay is in, with its two
free scratch changes:
- **The scratch after the reader** goes back past 96 bytes a token beyond its
  length (`check_driver_discard_scratch`), which the stages up to
  `check_program` push again, and the common warnings pop what they push.
- **The rare columns** of a reading in source order are base pages, with a bit
  per 64 bytes of them that every write's claim sets
  (`CheckInputReader.rare_marks`). The vector path's hot reads (the
  expressions' classes, the declarations' first tokens, the builtins' count)
  give 0 where no bit is set, without touching the page: a scalar read reads
  the column's first entry and clears it, and a vector load reads a static line
  of zeros (`check_input_rare_line`). The scalar path reads every entry, and
  counts the 64 bytes a mark misses (`check_input_rare_verify`,
  `CheckInput.visit_misses`). A page's bit would do
  for the reads, but the tests' small fixtures put several columns in a page,
  where one column's write hid another's missing mark: with a claim left out
  on purpose, `test-checker` found none by pages and 212 by 64 bytes.

A tracer that protected the columns' pages and logged each first touch
counted the pages `sqlite3.c`'s compile maps there: 6,660 before the marks,
of which about 6,170 were first read, and 884 after, 685 first written and
199 first read, nearly all by two byte gathers (of `static_initializers` and
`declaration_flags`, in the expressions' batches), which the marks don't
gate.

On the VM with huge pages, main `4e5abcb0` against the change's first version
on it: the peak is 255,160 → 249,208 KiB, for 893 more faults, with the objects the same
bytes. The backend's end is now the compile's peak.

On the 9700X, main `872cb8fe` against the change (the record's `pr4`), each
compile under its own grant of the lock, the order alternating by round
([the record](https://github.com/buster14a/buster-performance/tree/a27a3d5175cdbbc10f95231b055bd2c564d51420/docs/measurements/rare-marks-628-2026-11-07),
with the earlier versions' patches), every object the same bytes in six modes
and the diagnostics too (medians):

| measure | main | the change |
| --- | ---: | ---: |
| task clock, the step's options (9 rounds) | 122.03 ms | 120.43 ms |
| user cycles | 591.42 M | 582.72 M |
| kernel cycles | 86.08 M | 84.25 M |
| `check-input` | 46.70 ms | 44.77 ms |
| page faults | 5,840 | 6,650 |
| peak RSS | 258,196 KiB | 248,284 KiB |
| task clock, `-w` (5 rounds) | 107.02 ms | 105.72 ms |
| kernel cycles, `-w` | 86.33 M | 84.62 M |
| peak RSS, `-w` | 258,200 KiB | 248,284 KiB |

The change is faster in each round, by 1.06 to 2.39 ms, and by 0.75 to 2.20 ms
with `-w`. Its 810 more faults are the base pages', which fault 4 KiB at a
time.

**A load masked to no lanes.** The first three versions of the gate cost 4.0
to 5.0 ms each on the 9700X, for the same peak. All of it was in the
expressions' classes' pass, which took 5.3 ms rather than 0.48: it masked its
vector loads of `builtins` and `specifier_finishes` to no lanes where no bit
was set, ten of them every 64 tokens. A masked-off lane doesn't fault its page
in, and at a page that isn't mapped such a load cost far more than a load.
Timed alone, a load a page over 16,384
base pages, in TSC ticks a load:

| load | ticks |
| --- | ---: |
| every lane, at written pages | 35.5 |
| no lanes, at written pages | 37.5 |
| no lanes, at pages a read mapped to the zero page | 8.4 |
| no lanes, at untouched pages | 174.1 |

Since no fault maps those pages, every later load there costs the same again.
The gated loads now read `check_input_rare_line`'s static line of zeros
instead, and the classes' pass takes 0.43 ms.

### The preprocessor's output before the backend (#628)

After the change above, the backend's end was the compile's peak. A map on the
VM (main `6b4e178f`, instrumented as in "Partly used huge pages", whose peak
it kept within 0.1 MB), with huge pages and one lane (the 9700X's runs are pinned
to a CPU; the VM's four CPUs give four lanes, whose arenas add 28.7 MB at the
backend's `lanes` stage), put the stages' ends at:

| stage's end | resident (KiB) |
| --- | ---: |
| the reader | 240,196 |
| `static-data` | 237,516 |
| `check-program` | 233,552 |
| `lower` | 249,840 |
| `emit-setup`, the peak | 251,888 |

At the peak, the pushes' pages were the checker's graph (34 MB from
`check_graph_initialize`, 29.6 MB from `check_graph_run`), the program
(28.4 MB), the reader's output (24.6 MB), the parser's token and tree columns
(16 MB) and the preprocessor's output (12.5 MB). Each made unreadable before
the selection, in turn, showed what the backend reads: the selection reads the
tree's parents and lowering the parser's token columns, but nothing reads the
preprocessor's output but `-g`'s line table and the diagnostics.

So a unit without errors whose history is still deferred gives the output
back before the backend (`pp_release_output`, `docs/c-preprocessor.md`'s
"Deferred history"), and a reader asks for it first (`pp_output_ensure`): a
diagnostic records the history, whose replay gives it back, and one that
doesn't, as an undefined reference's system-header test, has it from a replay
without history. The tests, run with every unit's output unreadable, found
the readers that didn't ask: the unparsed literals' diagnostics of a failed
parse, which the release now leaves alone, and the undefined reference's
test.

On the VM, the same build and options: the peak is 251,968 → 239,848 KiB, and
the reader's end is the compile's peak again, the backend's end 1 MB below
it. The objects are the same bytes, with `-Wall -Wextra`, `-g` and `--lanes
8` too. With `-Wall -Wextra`, `sqlite3.c`'s 25 warnings record the history,
and its peak (272,208 KiB) is the replay's; `-g` records it from the start.

On the 9700X, main `73fcf006` against the change, each compile under its own
grant of the lock, the order alternating by round
([the record](https://github.com/buster14a/buster-performance/tree/dfef704959b5b7079e77724bdf7897bef2ff531d/docs/measurements/output-release-628-2026-11-07)), every object the same bytes in six modes
and the diagnostics too (medians):

| measure | main | the change |
| --- | ---: | ---: |
| peak RSS, the step's options (9 rounds) | 248,644 KiB | 239,824 KiB |
| task clock | 120.46 ms | 120.49 ms |
| user cycles | 581.98 M | 581.99 M |
| kernel cycles | 84.97 M | 84.97 M |
| page faults | 6,656 | 6,655 |
| peak RSS, `-w` (5 rounds) | 248,640 KiB | 239,816 KiB |
| task clock, `-w` | 105.19 ms | 105.10 ms |
| kernel cycles, `-w` | 84.15 M | 84.26 M |

The peak falls 8.8 MB, to the reader's end. The time and the kernel's cycles
are the same, within the rounds' spread: the release only gives the pages back
earlier than the exit would, and the backend faults in nothing it wouldn't
have.

### #628's outcome

[#628](https://github.com/buster14a/buster/issues/628) started from #617's
393 MB. Its changes, each in a section above:
- the binding table in the reader's base pages, no fills over a fresh output
  arena, and the stream's values as columns and payload rows (#1022);
- the parse's tail, the graph's columns, the rare columns' pages, the sparse
  marks and `check_program`'s discard of the scratch (#1076);
- the twelve sparse output columns (#1166), and #1167's attributes column and
  declaration bits;
- the graph's leaves as records and payload rows (#1562);
- the expansion history's replay run only past a unit's first `[[`, and its
  output given back once compared (#1574; #1584 records three changes measured
  and not taken);
- the parser's scratch without the benchmark's traversal arrays, and no
  assembly IDs without an assembly statement (this section's opening);
- the files' token columns given back after the run, and `-Wempty-body`'s
  invocation place without the replay (#1647);
- the scratch given back after the reader past what the next stages push again,
  and the rare columns in base pages, read only where written (#1684);
- the preprocessor's output given back before the backend (#1695).

As #518's step measures them, on the 9700X (`measure-compile --runs 11`
against tcc, then `perf stat -r 11` of the compiler, CPU 2), main `2a852f20`,
#617's merge, against main `94e166ec`, #1695's, each run under its own grant
of the lock, in the order base, main, main, base
([the record](https://github.com/buster14a/buster-performance/tree/e835986b41c233f2667060745926ed126159eb95/docs/measurements/step-628-2026-11-07)):

| measure | `2a852f20` | `94e166ec` |
| --- | ---: | ---: |
| peak RSS | 390,188 / 390,172 KiB | 239,844 / 239,840 KiB |
| wall, `measure-compile`'s median | 135.157 / 134.956 ms | 119.813 / 119.710 ms |
| system time, its median | 16.484 / 19.608 ms | 13.257 / 17.038 ms |
| kernel cycles | 107.68 / 108.15 M | 82.99 / 82.33 M |
| user cycles | 639.98 / 640.54 M | 576.76 / 577.24 M |
| minor faults | 2,220 / 2,221 | 6,760 / 6,760 |

Each cell gives both of a tree's rounds. The peak is 38.5% lower and the
kernel's cycles 23%, and the system time, which `getrusage` gives coarsely, is
lower in each order. The faults are the base pages', which fault 4 KiB at a
time where a huge page was cleared whole.

Every peak in the compile is now within a few megabytes of the others: the
reader's end, `static-data`'s end and the backend's end (the map in "The
preprocessor's output before the backend"). What they all hold is what is
left: the reader's scratch columns, about 88 MB at its end, the graph with room
for a node a token, of which `sqlite3.c` uses 42%, the parser's columns, which
the selection and the lowering read, and the reader's output. Two options
still add their own: with `-Wall -Wextra`, the history's replay for the
warnings adds 26 MB in `check-program` (the VM: 269 MB against 237 MB), and
with `-g`, the debug rows and line table 26 MB in the backend (268 MB).
[#1701](https://github.com/buster14a/buster/issues/1701) has them.

## The checker's graph and program passes (#530)

**Measured against `main` on the 9700X.** Medians of 15 interleaved
`--time-stages` runs of each build, pinned to CPU 2, each build first, second
and third in 5 rounds: `main` at `a774acd2` (#520 merged), its merge into
#530's branch (`2163d68a`), and the branch's next commit (`1989a51d`, which
numbers the static objects' symbols with #442's kernels). Each build's object
is the same bytes, by both paths.
[The measurement record](https://github.com/buster14a/buster-performance/tree/a44531fbbb1606de7a018b6018864c8d4d94b1b8/docs/measurements/checker-passes-530-main-2026-10-27)
holds every run, the scripts and the builds' identities; the stages are its
`balanced/` repeat, and perf stat's counters 11 runs of each build.

| | `main` | merge | change | `1989a51d` | change |
| --- | ---: | ---: | ---: | ---: | ---: |
| `check-expressions` | 20.550 ms | 15.823 ms | -23.0% | 15.753 ms | -23.3% |
| `check-declarations` | 5.250 ms | 5.440 ms | +3.6% | 5.559 ms | +5.9% |
| `check-program` | 16.024 ms | 12.085 ms | -24.6% | 12.141 ms | -24.2% |
| the clock's span | 157.948 ms | 149.474 ms | -5.4% | 149.682 ms | -5.2% |
| user instructions (perf stat) | 1,751.0 M | 1,555.3 M | -11.2% | 1,555.4 M | -11.2% |
| user cycles (perf stat) | 738.2 M | 690.2 M | -6.5% | 689.3 M | -6.6% |

- **Ranges.** The two stages' ranges don't overlap: 20.2 to 21.2 ms against
  15.3 to 16.7, and 15.6 to 16.3 against 11.9 to 12.2.
- **The other stages.** Each is within 0.2 ms of `main`'s median, but for
  `check-input`, 0.60 and 0.72 ms longer, though it runs before any code #530
  changes: that is [where the code falls](profiling.md#comparing-two-versions).
- **The first capture.** The record's first capture of the same builds, whose
  script repeated the same five orders in each hold of the lock (each build
  first in 3 or 6 of its 15 rounds), gave the two stages within 0.2 ms of
  these, and `check-declarations` 0.34 and 0.38 ms longer than `main`'s.

**Why `check-declarations` took 0.2 to 0.3 ms longer.**
- **The same work.** Its checks of the initializers and its report of the
  warnings (`check_initializers_run`, `check_init_value`,
  `check_init_objects_open`, `check_driver_reports`) ran the same instructions
  in both builds, in more cycles (perf's samples on a Zen 4 laptop).
- **What `main`'s scan left in the caches, 0.15 ms.** `main`'s report scan
  read every node's 96-byte value at the end of the graph run, the whole
  33 MB column, and these checks read values next. The new scans read the
  diagnostics' and contexts' bytes, and a value only where a node can report.
  A build of #530's later head (`d32d593f`) that reads every value again at
  the end of the graph run when an environment variable is set ran 30 rounds
  beside `main`, one executable either way. With the reads, `check-expressions`
  took 1.18 ms more and `check-declarations` 0.15 ms less.
- **The rest came with the builds.** Without the reads, that build's
  `check-declarations` was 0.04 ms longer than `main`'s, though its scans are
  the merge's.

**The first measurement.** Against #520's branch merged into the `main` of
the time (`373ce21b`), #530's first head (`0b5af444`) took `check-expressions`
from 20.262 to 15.515 ms (-23.4%), `check-program` from 15.936 to 12.235 ms
(-23.2%) and the clock's span from 190.517 to 181.889 ms (-4.5%), with 8.8%
fewer user instructions ([its record](https://github.com/buster14a/buster-performance/tree/2f36c27c24ffcea2317721c0337544d025f98fb8/docs/measurements/checker-passes-530-2026-10-27)).
`main` has since cut `check-input` (#529) and the lowering (#531).

**The barrier wasn't the cost.** On the frozen `sqlite3.c`'s 343,555 graph nodes
(the first record's laptop counts):
- **The barrier's nodes.** 203,218 (59%) took the scalar barrier
  (`check_graph_scalar_node`). By operator:
  - leaves, 70,518 (35%);
  - casts, 28,795;
  - `->`, 24,623;
  - assignments, 13,910;
  - calls, 13,685.
- **The regions.** The other 140,337 sat in 45,283 reorderable regions,
  26,823 of them a single node.
- **The vector path.** It was slower than its scalar twin:
  `benchmark-checker-source` put it at 75.2 against 70.3 ns a body node on a
  Zen 4 laptop. Its regions' batches folded integers in vectors for 1.4% of
  the nodes, and still ran every node's action.

So batching the barrier's interning (`type_derive_batch`) would have batched
little: a node's cost was the action's own machinery, the same on either path.
That is a 384-byte `CheckAction`, its 96-byte operand values, the result and
its store.

**What changed.**
- **The graph run** (`c_checker.c`):
  - **Leaves.** A leaf without an overload set reads no operand and interns no
    type, so the vector path writes its record without the action
    (`check_graph_leaf`). That is 45% of the body nodes. The records go in
    node order, since a separate pass of leaves wrote each output line twice.
  - **Validation.** It checks 16 nodes a block with AVX-512
    (`check_graph_validate_vector`), with the old loop's step as its exact twin.
  - **Scans.** The reports, warnings and resource scans read only the nodes
    that can act.
  - **Overloads.** Their checks are skipped in a graph without candidates.
- **The program pass** (`c_check_program.c`):
  - **Each body's own columns.** A body builds its children lists, labels,
    case targets, stops and repeats in one pass over its own tokens, just
    before its statements read them (`check_control_children`). Before, the
    unit's columns were built, and cleared in seven memsets, before every
    body: on the laptop the unit's lists took 10.7 M ticks and the memsets
    6.7 M, of the pass's 85.0 M.
  - **Scans.** Its other unit-wide scans test 16 tokens or nodes (or 64 roles)
    at a time and run the per-token code on the hits, in the same order:
    the bodies and initializers, the cases, the variable-length arrays'
    declarations, the reached builtins and the static objects, whose symbols
    #442's kernels number (`check_program_needed_number`).

On `sqlite3.c`'s 319,193 body nodes the graph run went from 55.7 to 40.9 ns a
node on the 9700X (308 to 227 cycles), the means of two runs of `main` and of
the merge (55.2 to 40.2 ns in the first measurement).

**What is left.**
- **Validation's reads.** Most of the validation's time is the leaves' records
  it reads, two cache lines of each 96-byte record. Since #572 they are the
  `VALUE` nodes' alone, one after another, where the column had a record for
  every node.
- **The actions.** The rest of the graph run is the actions and their
  per-node structures, on either path.
- **The program pass.** Most of what remains of it is the bodies' builds and
  their reference walks, one statement at a time.

## The reader's sparse walks (#529)

The completed 9700X comparison of main `92c025e7` with `60732582`, pinned to
CPU 2 under the shared host lock, is retained in the [measurement record](https://github.com/buster14a/buster-performance/tree/703e034725e79fed5574e42b0be96b3af4cae9bf/docs/measurements/check-input-529-walks-2026-10-27).
Eight alternating `perf stat -r 3` rounds give these medians for the frozen
SQLite `-c` configuration:

| measure | main | sparse walks |
| --- | ---: | ---: |
| task clock | 146.340 ms | 141.725 ms |
| user cycles | 690.795 M | 664.729 M |
| user instructions | 1,552.457 M | 1,471.594 M |
| check-input, 11 stage runs | 63.682 ms | 58.927 ms |
| direct reader, pinned corpus, 11 passes | 53.912 ms | 49.375 ms |
| peak RSS, main-first process comparison | 512,872 KB | 510,820 KB |

The whole-process wall medians are 146.462 → 142.098 ms in one order and
147.174 → 142.364 ms in reverse. Every object is unchanged on the scalar
path and on one/eight lanes; the record also retains all 16 passing target
checker suites. The visits column adds 706,240 bytes to the reader's
scratch-written extent, while its retained output extent is unchanged.
These measurements precede integration of #572's storage and #186's
traversal; they are not a measured sum of those independent improvements.

**The next-mark scan's loads.** The walk in source order finds each next
marked token with `check_input_resolve_next`. It loaded the 64 bytes of
`visits` from the token after the current one, a masked load that crossed a
cache line for nearly every token it visited: 92 of 95 sampled loads on the
9700X (the 29 October record, 0.85% of `-c`'s cycle samples). It now loads
the aligned 64 bytes that hold that token, with the bytes below it and from
the walk's stop on masked off and not read (`check_input_scan_nonzero_from`),
and steps a line at a time after that. `visits` starts on a cache line, so no
load crosses one. The walk visits the same tokens, and `test-checker` checks
both functions against a scalar reading of columns that end at a `PROT_NONE`
page.

The same PR's #628 changes also shorten `check-input` itself: the reader no
longer writes zeros over its fresh output columns, and the binding table's
pages are base pages, of which it clears a fifth of what it did ("The binding
table, the fresh output and the stream's columns", above).

### The per-token passes outside the walks (#529, 6 November)

**Where the stage went.** `perf record` of five `-c` compiles on the 9700X
(CPU 2, 20 kHz), at main `ec5f0f6b`. The samples were mapped to their inlined
frames with `llvm-symbolizer`.
- **The stage.** `check-input` took 59.5 ms of the 138.8 ms span.
  - #1489's #1530, not yet merged, takes it to 50.5 ms by itself. Most of that
    is `check_input_deprecated_uses`, 5.7% of the compile's samples.
- **The rest of `check_input_run`.** Its own 7.5% was its inlined passes over
  every token.
- **Walks and steps.** These are flat: `check_input_resolve_range` takes 4.8%,
  with no line above 0.35%, and the declaration steps 4.4%.

This change takes the per-token passes outside the two walks. Each pass's
scalar path stays its vector path's exact twin:

| pass | share of `-c`'s samples | now |
| --- | ---: | --- |
| `check_input_declaration_starts` | 0.83% | one pass over the roles, 64 tokens at a time, where it wrote the column three times and kept the struct and enum stack a token at a time |
| `check_input_asm_labels`' two kernels | 0.45% | each 16 tokens' objects or declarations tested against a filter of the labelled set in two registers (`CheckInputFilter`); only those that pass gather their bits |
| `check_input_count_tokens` | 0.33% | the names compared only where the same kind of filter of the sought names passes |
| `check_input_system_tokens` | 0.31% | 16 tokens' levels gathered at once |
| the enumeration lists (`check_input_enum_records`) | about 0.27% | the enumerators and the `enum` keywords visited alone |

**The enumeration lists' pages.** Four passes read every token's kind and every
token's tag. `tag_types` is a column of the rare columns' arena that only a
tag's keyword writes, so reading all of it mapped each of its base pages. The
other passes touch the same pages as before: the declaration-start column is
still written whole, and the filters are registers. So the stage's 526 fewer
faults below are this column's: its 700 base pages, less those its tags write.

**Measured on the 9700X.** The PR's 9700X job (run 42281737894, job
128118533298) compared main `ec5f0f6b` with the change merged into it
(`3f3fe0ac`). [The record](https://github.com/buster14a/buster-performance/tree/4d5a27b8e8285e4c4eeb857ec42c93cb2419f4c3/docs/measurements/check-input-529-passes-2026-11-06)
keeps the job's log, the profiles' samples and their breakdowns, and an A/B of
its own, each run under its own hold of the lock:
- **Rounds.** 5 runs each, over four placements of the checker's unit and two
  copies of each executable.
- **The control.** A second copy of main ran in the same rounds.
- **The objects.** Both builds' objects are the same bytes (`cmp`).

| measure | main | change | difference |
| --- | ---: | ---: | ---: |
| wall time, 40 runs | 140.452 ms | 137.472 ms | −2.12% (−2.37% to −2.05% by placement) |
| user cycles | 679.854 M | 663.132 M | −2.46% |
| user instructions | 1,636.44 M | 1,606.84 M | −1.81% |
| minor faults | 6,619 | 6,093 | −526 |
| `check-input`, 11 stage runs | 59.862 ms | 57.201 ms | −2.66 ms |
| `check-input`'s minor faults | 3,905 | 3,379 | −526 |

The control's wall time was within 0.09% of main's, and its cycles within 0.15%.

**What is left in the stage**, at 57.2 ms:
- the expressions' coded walk, 13.5 ms (#1038, closed by #1478);
- the walk in source order, about 6.5 ms;
- the declaration steps, about 6 ms;
- the binding answers, about 2.4 ms;
- the reader's fills of its scratch columns (#628);
- what #1530 removes.

### The binding answers and the deprecated marks (#529, #1489, 7 November)

**Where the stage went.** At main `53854a7c`, `check-input` took 48.2 ms of `-c sqlite3.c`'s 129.9 ms span on the
9700X. These are shares of the compile's cycles samples, by inlined frame:
- **The walk in source order** (`check_input_resolve_range`): 4.95%.
- **The expressions' coded walk**: 3.76%, plus the functions of its actions.
- **The declaration steps**: about 5% together.
- **`check_input_binding_answers`**: 1.26%. Of that, 0.65% is `check_input_unused_events`; `check_input_binding_answer`
  adds another 0.70%.
- **`check_input_deprecated_marked`**: 0.69%.

The walks' and the steps' cost is spread over their lines, and their branch misses don't account for it. The coded
walk has 5.6% of the compile's misses and the walk in source order 2.5%.

#1642 and #1646 change the two passes outside the walks. Each change keeps its scalar twin: given no list, the
binding passes visit every event, as before.

| pass | before | now |
| --- | --- | --- |
| `check_input_deprecated_marked` | gathered each declaration's object and mark, 16 at a time | tests 16 declarations at a time against a filter of the marked keys in two registers (`CheckInputFilter`), and gathers only for those that pass; it skips the scan of the objects when no key is marked |
| `check_input_unused_events`' three passes | every binding event, with a branch on its operation | only the declare events, listed once by `check_input_operation_events`, which compares 64 operations at a time and compress-stores the matches' indices |
| the answers loop | every event | only the use events, from a list built the same way |
| the walk that marks declarations used | back to the chain's start | stops at a declaration already marked |
| `chain_marked`, `chain_definition` | filled whole | cleared only at the declare events the passes visit |

The deprecated marks' `latest` column is cleared by walking the attributed tokens again, not by a second fill.

**The splice.** #1642 also made `check_input_expression_values`, the expressions' walk's splice of its children's
values, branch-free. Those branches are 0.57% of the compile's misses. The walk's actions took 10.39 ms with the
branch-free form, against 10.02 to 10.03 ms without it, in every comparison. So #1646 puts the branches back, and
the function's comment records the measurement.

#1175 had found neither of its branch-free forms helped ("The checker's rare tests and their layout", below). There,
clang made a branch of the conditional store, and a shared sink slot would race between lanes. #1642's form stores the
link through a selected pointer, into a local where there is no chain, so no lanes share a slot. It measured slower
all the same.

**Measured on the 9700X.** Each A/B was 8 interleaved rounds of `perf stat -r 3` on CPU 2, then `measure-compile` each
way. Every run held the lock on its own, with a receipt. Every build's object is the same bytes.
[The record](https://github.com/buster14a/buster-performance/tree/26334fb1ebb0b405d8cbad5bf3ba5dc5858f0ddd/docs/measurements/check-input-529-answers-2026-11-07)
keeps the profiles, the `measure-compile` of each step of #1642 and the receipts.

| against main `53854a7c` | wall time | user cycles | user instructions | `check-input` |
| --- | ---: | ---: | ---: | ---: |
| #1642 (`9ce56d36`) | −1.21% | −1.23% | +0.58% | 48.20 → 47.46 ms |
| #1646 (`6d1f7cfc`) | −1.45% | −1.57% | −0.19% | 48.07 → 47.12 ms |

By symbol, `check_input_binding_answers` went from 1.16% to 0.87% of the cycles, and `check_input_deprecated_marked`
from 0.73% to 0.27%.

**What is left in the stage**, at 47.1 ms:
- the expressions' coded walk, about 13 ms, of which its actions are 10 ms;
- the walk in source order, about 6.7 ms;
- the declaration steps, about 6.6 ms;
- the binding answers, about 1.9 ms;
- the reader's fills of its scratch columns (#628).

The walks and the steps are flat profiles, so what is left of them needs a change to the walks themselves. Trimming
another pass around them won't reach it.

### #529's outcome (7 November)

**Done when.** #529 asked for the 9700X step to show the stage's time down, with the objects unchanged, and for
this file to record it.

| 9700X step, `-c sqlite3.c` | `check-input` | span |
| --- | ---: | ---: |
| at the filing (`1677a3b`) | 81.2 ms | 189.2 ms |
| at its close (#1646's PR job, run 42371769735) | 47.3 ms | 126.6 ms |

Every change on the way kept the objects the same bytes. The close's step compares them with `cmp`, and its log is in
[the outcome record](https://github.com/buster14a/buster-performance/tree/cf5e1d1023573ad61ba91c707fc3a0e53177fe33/docs/measurements/check-input-529-outcome-2026-11-07).
The sections above record each step.

**The issue's work items:**
1. **Profile the stage.** Each section above starts from a 9700X profile by function and line.
2. **Separate the per-token work from the per-declaration and per-expression work:**
   - **The full-token pre-passes** are folded: #568's single counting pass, then #1563's vector passes.
   - **The expression graph's construction** is data-parallel (#1038, closed by #1478).
   - **Skipping the system-header declarations nothing uses** was measured and not taken. See below.
3. **The multicore route** is #113's deferred bodies on lanes, outside this issue.

**The system headers' declarations.** Measured on the 9700X, each run under its own hold of the lock:
- **The unit.** `sqlite3.c`'s system-header lines are about 6.4% of its preprocessed tokens.
- **The ceiling.** Compiled alone as a unit, those lines cost the reader 3.85 ms, against 0.018 ms for an empty unit and
  46.4 ms for the whole preprocessed unit. So a skip could take at most 3.8 ms, 8.1% of the stage.
- **The declarations.** The unit's own lines name 114 of the 2,683 system declarations.
- **Why the default mode doesn't skip them.** Skipping would stop the reader diagnosing those declarations: a malformed
  system header's errors, and the constructs it refuses there. The default mode checks every declaration as clang does
  ("Every body is parsed and checked by default" in [the checker's design](c-checker.md)), and #155 allows a skip only
  behind `-flazy-system-headers`.
- **The opt-in mode today.** It skips only unreferenced system bodies, and its plan makes `check-input` slower: 73.0 ms
  against 47.4 ms, for the same object (#1364).

So item 2's skip is settled by the default mode's contract, with its ceiling measured; a skip of declarations would
belong to the opt-in mode, whose plan must first stop costing more than it saves (#1364).

**Past the done-when,** #1671 tracks the stage's remaining time: the two walks and the declaration steps, 26 ms of the
47.3 ms, with flat profiles.

## The walk in source order's plain and ignored visits (#1671)

**What bounds the walks.** One `perf record` of cycles, instructions, branch misses and fills from memory, by symbol,
on the 9700X ([#1671](https://github.com/buster14a/buster/issues/1671#issuecomment-6707704992)):
- **The walk in source order** (`check_input_resolve_range`) runs at about 2.4 IPC, with 1.5% of the compile's
  branch misses.
- **The coded walk** runs at about 3.0 IPC.
- **The declarator step** runs at about 2.9 IPC.

So the walks are bound by the instructions they retire, not by their branches or loads. The walk in source order
retired about 290 instructions a visit.

**Its visits, by class.** A probe of `-c sqlite3.c` counted them:

| visit | count | now |
| --- | ---: | --- |
| a plain name: an identifier operand, marked for its syntax alone, of no syntax bit | 124,581 of 254,391 | the label's test and the name's step alone |
| a token an earlier step ignored, marked for its syntax alone, not a return's or a for's | 40,290 | passed over |
| another token marked for its syntax alone | 55,876 | the general visit |
| a visit with another mark's reason | 33,643 | the general visit |

**Why the two paths are exact.**
- **A plain name.** Each of the walk's tests is false at it except the label's and the name's own:
  - the tests of the other marks' reasons, by its mark;
  - the tests of roles, kinds and syntax bits, by its own;
  - C89's implicit declaration, by the standard, so a C89 unit's names take the general visit.
- **An ignored token.** Every step tests that its token isn't ignored, except a return's and a for's. The label's test
  would only set again the mark, which holds a bool.
- **Shared code.** Both paths call the two steps as functions of their own, `check_input_resolve_ignored` and
  `check_input_resolve_name`.
- **The twin.** The scalar path visits every token and makes every test.

**What the visits left do.** Every visit the general path still takes acts:
- declarations' starts, blocks, conditions, casts and returns;
- scopes' ends and activations.

**Measured on the 9700X.** Main `cb021b04` against #1676's head, in 8 interleaved rounds, then `measure-compile` each
way. Every run held the lock on its own. The objects are the same bytes.
[The record](https://github.com/buster14a/buster-performance/tree/8bac1f50af1a9e5885304a223e6e7942ac3ef617/docs/measurements/check-input-1671-walk-visits-2026-11-07)
keeps the receipts, each build's profile and the probe.

| measure | main | head | difference |
| --- | ---: | ---: | ---: |
| wall time | 122.510 ms | 121.830 ms | −0.56% |
| user cycles | 595.868 M | 592.436 M | −0.58% |
| user instructions | 1,320.752 M | 1,302.454 M | −1.39% |
| `check-input` | 47.115 ms | 46.908 ms | −0.21 ms |

**Instructions against cycles.** The head retires 18.3 M fewer instructions per compile, about 110 a visit the two
paths take. `check_input_resolve_range` went from 5.58% to 4.60% of the cycles. The compile's cycles fell by about a
fifth as much as its instructions: the tests a visit no longer makes were cheap and well predicted.

## The declaration steps' frames without attributes (#1671)

**Where the declaration steps' time goes.** Probes summed `__rdtsc` deltas by class over `-c sqlite3.c`'s declarators.
They ran off the 9700X, so their shares are what count, not their values:

| declarator | count | TSC cycles per call |
| --- | ---: | ---: |
| a name | 5,431 | about 200 |
| pointers and a name | 3,834 | about 330 |
| empty or pointers alone, mostly casts' type names | 9,342 | about 200 |
| a function's, with its parameters | 5,914 | about 4,000 |

The function declarators take about 80% of the declarators' time. Within them:
- **Attribute lists.** 1,895 lists, mostly glibc's prototypes', are read at about 3,000 cycles each: about 20%.
- **Parameters' specifiers.** The 14,783 parameters' specifiers take about 11%.
- **Type warnings.** `check_input_attribute_type_warnings` takes about 10%.
- **The rest.** About 60% goes to each parameter's bookkeeping and the function type.

**The change.** 40,403 of the 42,404 frames, the declarations' and the parameters', have no attribute list. Two steps
now return at once there:
- **`check_input_attribute_type_warnings`** walked the type to its function before loops over no list. Without lists it
  warns of nothing and marks nothing.
- **`check_input_declarator_abi`**, called twice per frame, walked the type again. With no list,
  `check_input_attribute_requested_abi` gives no ABI, so `check_input_attribute_abi_origin` keeps the origin and
  `check_input_attribute_abi_with_override` keeps the type.

**Measured on the 9700X.** Main `07b78e0f` against #1680's head, in 8 interleaved rounds, then `measure-compile` each
way. Every run held the lock on its own. The objects are the same bytes.
[The record](https://github.com/buster14a/buster-performance/tree/97d4731b20dc4a334c49381ed6dade2473f932dd/docs/measurements/check-input-1671-declarators-2026-11-07)
keeps the probes and the receipts.

| measure | main | head | difference |
| --- | ---: | ---: | ---: |
| wall time | 121.740 ms | 121.490 ms | −0.21% |
| user cycles | 590.785 M | 589.415 M | −0.23% |
| user instructions | 1,304.696 M | 1,299.420 M | −0.40% |
| `check-input` | 46.614 ms | 46.388 ms | −0.23 ms |

## The coded walk's actions inlined into its loop (#1671)

**What the walk does.** A probe of `-c sqlite3.c` counted it
([#1671](https://github.com/buster14a/buster/issues/1671#issuecomment-6710430485)):
- **451,790 actions** over 695,362 postorder positions, making 316,456 nodes. Half the actions (229,679) splice no
  values.
- **The largest classes:** identifiers 95,402; lists 74,297; terminators 54,508; literals 44,184; binaries 42,766.
- **Flushes:** 3,592 before a visit, 87 of a full batch, 1 at the end.

**Where main's cycles went**, by source line on the 9700X, in shares of the whole compile:

| part | share |
| --- | ---: |
| each action's call frame | about 2.3% |
| the preparation: the compaction's gather, the identifiers' gathers, the literals | about 2.4% |
| `check_graph_record_nodes` | 0.78% |
| the flush's writes and gathers | about 0.6% |
| the batch's adds | 0.57% |
| the value splice | 0.56% |
| each class action's own body | 0.17% at most |

**The call frame.** `check_input_expression_coded` was a call per action, not inlined into the chunk's loop. Each paid:
- four arguments pushed on the stack after a `vzeroupper` (about 60% of `check_input_expressions_coded_range`'s samples);
- the prologue and epilogue;
- spills.

The flush only looked large off the host, where the VM's page faults in the walk (6,821) put it at 36% of the actions.

**The change.**
- **The untimed loop** takes the action always inlined, with the identifier and literal cursors in registers.
- **The hot class actions.** Once the action was inlined, clang called the identifier's, literal's and terminator's
  actions instead, so those three are always inlined too.
- **The timed loop** (`PPOptions.expression_timing`, `benchmark-checker-bindings`) calls a wrapper, so the body is
  inlined once.
- **Nothing the actions do changes.** The scalar path, the walk's visit at every token, stays their twin.

**Measured on the 9700X.** Main `1c966613` against the head `6c1b3ac8`, in 8 interleaved rounds, then
`measure-compile` each way. Every run held the lock on its own. The objects are the same bytes.
[The record](https://github.com/buster14a/buster-performance/tree/19c50f5aeda77b78d9d70f48ad3ea9326aadc064/docs/measurements/check-input-1671-coded-inline-2026-11-07)
keeps the probe, main's profile by line and instruction, and the receipts.

| measure | main | head | difference |
| --- | ---: | ---: | ---: |
| wall time | 121.810 ms | 120.990 ms | −0.67% |
| user cycles | 591.072 M | 586.604 M | −0.76% |
| user instructions | 1,298.686 M | 1,278.616 M | −1.55% |
| `check-input` | 46.480 ms | 45.673 ms | −0.81 ms |
| `expressions.actions` | 10.075 ms | 9.436 ms | −0.64 ms |

## The coded walk's flush records its nodes 16 at a time (#1671)

**What it replaces.** Each flush of the coded walk's batch called `check_graph_record_nodes`, which recorded the
batch's nodes one at a time: about 0.8% of `-c sqlite3.c`'s cycles on the 9700X, about 15 cycles a node. It records:
- each node's purity (`check_graph_pure`);
- the validation's counters.

`check_graph_run` takes these in place of a validation.

**The change.** The flush's vector path records its 16 nodes in the step that writes them
(`check_input_batch_record`), as `check_graph_validate_block` already computes purity:
- **Each node's own part.** The operator's class from `check_graph_classes`' table (made once a walk), and the requested
  type, builtin or void.
- **A leaf's part.** Its type and overload head, gathered from its record by its slot.
- **Operands before the 16** are gathered from `recorded_pure`. Those among them are taken in node order.
- **The counters**, by popcounts and reductions.
- **Around it.** The nodes a visit made before the batch are recorded first. The scalar path keeps
  `check_graph_record_nodes` as the twin.

**The checks.**
- `test-checker` compares every source fixture's recorded facts with both validations.
- A probe that validates every graph found `sqlite3.c`'s 309,214 nodes' recorded purity and counters equal to the
  validation's.

**Measured on the 9700X.** Main `219e7e9b`, with #1692, against the head `38152245` (this change merged with it), in 8
interleaved rounds, then `measure-compile` each way. Every run held the lock on its own. The objects are the same bytes.
Against main `73fcf006`, before #1692, it measured −0.56% wall and −0.67 ms of `check-input`.
[The record](https://github.com/buster14a/buster-performance/tree/ee69c4a5186a0308078c67be14623f4baefd9ecd/docs/measurements/check-input-1671-coded-record-2026-11-07)
keeps both A/Bs' receipts, each build's profile and the probe.

| measure | main | head | difference |
| --- | ---: | ---: | ---: |
| wall time | 119.460 ms | 118.450 ms | −0.85% |
| user cycles | 580.282 M | 575.560 M | −0.81% |
| user instructions | 1,288.534 M | 1,279.602 M | −0.69% |
| `check-input` | 43.966 ms | 43.183 ms | −0.78 ms |
| `expressions.actions` | 9.660 ms | 8.868 ms | −0.79 ms |

**A refinement that didn't pay.** The recording's gathers then took most of the flush's samples:
- a leaf's type and overload head from its record;
- its operands' purity.

A version replaced them, keeping a leaf's purity as a batch byte written with the leaf and taking the operands lane by
lane in node order. Against this change it was no faster:
- `check-input` was 43.11 against 43.12 ms;
- user cycles were −0.27%, lower in only 5 of 8 rounds;
- user instructions were +0.41%.

The gathers had overlapped with the flush's other work, so the change keeps them. The record holds both A/Bs.

## The return flow's library-noreturn answers by name (#1709)

**What cost.** The return-warning facts (`c_check_return_flow.c`) leave a direct call to a known, non-noreturn
function unknown when its name is a catalogue entry with the `r` attribute (`abort`, `exit`, `longjmp`…), whose
noreturn depends on builtin policy rather than the declaration's flag. Main searched the catalogue by name for every
such call: in `-c sqlite3.c`, 13,142 searches of about 2,200 distinct names. `builtin_intrinsic_lookup` was 1.00% of
the compile's cycles, all of it from `check_program_return_call`.

**Now.** Only 16 of the catalogue's 3,775 entries carry `r`. `check_program_return_facts` interns their spellings
once (`intern_find`) and marks their ids in a byte per identifier (`check_program_return_library_names`); a call reads
its name's byte. The answer depends on the interned name alone, so the facts are the same.
- **The twin.** The search (`check_program_return_library_search`) stays: it answers when the scratch arena has no
  room for the bytes, and `test-checker` compares the bytes with it over every catalogue name, near misses and the
  predefined names.
- **The table's drift.** The same test checks that the table of 16 names is exactly the catalogue's `r` entries, in
  its order, so a regenerated catalogue that changes the set fails there.
- **A first step, kept in the record.** Memoizing each name's search still made 2,182 searches, and the A/B gained
  0.78%.

**Measured on the 9700X.** Main `829eb71a` against `56143cc3`, in 8 interleaved rounds, then `measure-compile` each
way. Every run held the lock on its own. [The record](https://github.com/buster14a/buster-performance/tree/7e65d1a31fa2d399d5c92c98f2e5ee53e97765f6/docs/measurements/check-program-1709-return-library-2026-11-07) keeps the receipts, the three builds' profiles and the
probe.

| measure | main | head | difference |
| --- | ---: | ---: | ---: |
| wall time | 118.620 ms | 117.420 ms | −1.01%, lower in 8 of 8 rounds |
| user cycles | 576.707 M | 569.578 M | −1.24% |
| user instructions | 1,279.591 M | 1,272.399 M | −0.56% |
| `check-program` | 24.625 ms | 23.601 ms | −1.02 ms |

`builtin_intrinsic_lookup` is gone from the profile. The frozen corpus's objects are the same bytes, and sqlite
3.50.4's `-c -g -Wall -Wextra` objects and diagnostics are the same bytes as main's on the vector path and with
`--scalar`.

## What remains of check-input (#1671)

After #1692 and #1694, `check-input` is 43.15 ms on `-c sqlite3.c` (main `829eb71a`, the 9700X). A study found no
remaining part worth more than about 0.3 ms by local changes.
[The record](https://github.com/buster14a/buster-performance/tree/72bc1dd97d260c9b7e04ddaffcde4ad72a0eae71/docs/measurements/check-input-1671-remaining-2026-11-07)
keeps the profiles, the probes and the A/Bs below.

**Where its time is,** in shares of the compile's cycles:

| part | share | profile |
| --- | ---: | --- |
| the coded walk | about 6% | its loop's spills, the flush's recording, the value splice and the dispatch, each 0.7–0.8% |
| the walk in source order's own lines | 4.3% | no line above 0.27% |
| the declaration steps | about 4.9% | flat |

**What was ruled out, by measurement:**
- **The declaration steps in vector passes.** The file-scope declarations hold 70% of the steps' time, and each is
  distinct. Their per-parameter bookkeeping is branching and binding calls; its stores are about 0.04 ms in all.
- **Caching the specifiers.** 96.5% of `check_input_specifiers`' 42,308 calls repeat an earlier call's tokens, but
  they are one or two tokens long, and each still writes its tokens' ignored marks. The bound is about 0.3 ms.
- **The expressions' graph built by passes.** #1038's numbering alone takes 3.21 ms for 323,623 nodes, as much as the
  class actions it would replace, before any column is written.
- **The coded walk's rare class actions out of line.** The instructions were identical: clang already kept them out of
  line.
- **The walk's label marks computed ahead.**
  - Gathered a line at a time, they cost +0.28% cycles: the gathers sat on the walk's path.
  - Made before the walk without gathers, they removed 5.5 M instructions and no cycles: the per-visit tests were cheap
    and well predicted.
  - So batching the plain names' visits, 17% of the walk's time, wasn't tried.

**Outside check-input.** check-program's `check_program_return_call` looks every direct call's callee up in the builtin
catalogue by name: 13,142 binary searches on `sqlite3.c`, 0.95% of the compile.

## The passes after checking (#592)

#641 removes more token-domain work from the same handoffs. Initializer plans keep only their ascending
`declaration_tokens`; `check_initializers_plan_index` returns an index plus one by a bounded lower bound, with zero
for an absent token, including the empty unit. The plans and their tokens are immutable after successful planning.
The source adapter, VLA/compound-literal lowering and x86 diagnostic consumers all use that lookup.

The warning producers maintain counts while they write or clear warnings: declaration-time constant rows,
the final expression-warning rows, and ignored asm labels. The diagnostic collector omits an empty column and
sizes its ordering arrays from those counts. Prepared flags distinguish completed producer metadata from a
hand-built input or stream, which retains the full scan. Mutating a completed warning column requires updating
its count through the producer helper or clearing that column's prepared flag; the metadata is part of the
immutable source snapshot. Randomized dense-oracle tests exercise both cases, including suppression and
speculative constant-call restoration.

Where static-data preparation already ran, the program consumes its declaration keys instead of selecting
the same tokens again. Syntax-only users retain the original selection. The emission initializes `defined`
only at symbol objects, its only readable domain, and scans static-storage declarations instead of initialized
automatic locals. Attribute collection selects nonnull pointers before testing declaration identity; its
retained-object bytes are initialized only at the collected objects. Parameters remain in this collection,
so their alias/retain refusal is preserved. The current warning-range index updates the count in its existing
scalar or masked blocks, including deferred merges and speculative restoration, without a new final pass.

[Frozen 9700X qualification](https://github.com/buster14a/buster-performance/blob/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-641/target/README.md)
retains only this first slice: its 2,852 SQLite objects and 1,760 small-corpus pairs matched the base bytes.
The post-check construction/handoff/consumer sum fell from 32.320 to 31.489 ms, with about 2.1 MiB less RSS,
but whole-compile changes stayed within the A/A dispersion. A second compact selection-metadata slice was
rejected after ablation. Selection maps and attributed-declaration lists remain future #641 work. These
measurements apply to the frozen revisions in that report; this refreshed main-321 stack claims correctness
and representation savings, with no new whole-compile speedup attribution.

**Measured against `main` on the 9700X.** Medians of 15 interleaved
`--time-stages` runs of each build, pinned to CPU 2, the order rotating by
round: `main` at `8ab9a208` and #592's head (`d4be6a47`), both built as the
9700X job builds (`BUSTER_DEBUG_INFO=0`). Each build's object is the same bytes,
by both paths.
[The measurement record](https://github.com/buster14a/buster-performance/tree/c5bd1be7700cb65d9b2f68df244579cd1cd06646/docs/measurements/sparse-passes-592-2026-10-27)
holds every run, the scripts and the builds' identities; perf stat's counters
are 12 runs of each build, in turn.

| | `main` | head | change |
| --- | ---: | ---: | ---: |
| `check-declarations` | 5.350 ms | 2.895 ms | -45.9% |
| `static-data` | 1.522 ms | 0.446 ms | -70.7% |
| `select` | 1.390 ms | 0.881 ms | -36.6% |
| `emit-setup` | 1.426 ms | 1.240 ms | -13.0% |
| the four | 9.688 ms | 5.462 ms | -43.6% |
| the clock's span | 146.656 ms | 142.028 ms | -3.2% |
| user instructions (perf stat) | 1,556.0 M | 1,452.8 M | -6.6% |
| user cycles (perf stat) | 692.5 M | 667.9 M | -3.6% |

- **Ranges.** No stage's ranges overlap: `check-declarations` 5.19 to 5.59 ms
  against 2.81 to 3.01, `static-data` 1.46 to 1.55 against 0.43 to 0.46,
  `select` 1.32 to 1.44 against 0.85 to 1.00, `emit-setup` 1.40 to 1.48 against
  1.21 to 1.28, and the span 145.9 to 147.2 ms against 141.3 to 143.1.
- **Counters.** The instructions are the same in every round of each build
  (1,555.99 M against 1,452.81 M); the cycles' rounds run 690.5 to 695.6 M
  against 665.2 to 670.2 M.

**What they did.** Each went over every token of the unit (716,380 in the
frozen `sqlite3.c`), or cleared token-sized columns, for entries a few
thousand tokens have.

**What changed.** Each stage finds its entries with the reader's AVX-512 scans
(`check_input_scan_*`, 8, 16 or 64 tokens at a time; the scalar path takes
every token) and visits only them:
- **The diagnostics' report** (`check_driver_reports`). It filled a 16-bit
  column of every token's warnings and a 64-bit one of its first report, then
  walked every token. It now gathers the warnings as keys of their token and
  flags, from the tokens with attributes, the constants' warnings, the nodes'
  warnings and the excess elements, and the reports as keys of their token and
  order; sorts both by token (`c_bindings_sort`); and walks only the tokens
  with a pragma, a warning, a packed member's address or a report, with the
  cursors a walk of every token keeps, which a test keeps as its oracle.
- **The initializers** (`check_initializers_run`). One list of the
  declarations with initializers (`check_init_declarations`) serves its
  checks, the constant objects' table and its plans, in place of three passes
  over every token; the excess elements' tokens are put in order only where
  there are any.
- **The static data** (`check_data_prepare`). It finds the static
  declarations as keys of their object and token, sorted by object, and works
  out each object's last declaration, definition and alignment per object, in
  place of three token-sized columns and two passes over every object id. Its
  result keeps the keys and the static objects with their last declarations
  (`check_data_latest`), not a token-sized column of them.
- **The selection** (`check_program_select_mode`). The body that holds a label,
  a block-scope static or a builtin is found by a search of the bodies'
  openings (`check_program_token_body`), rather than read from a column filled
  over every body's tokens; the roots and inline rules come from the data's
  static declarations, per static object, rather than from every declaring
  token into two object-sized columns.
- **The emission's setup.** `check_program_object_access` finds the defining
  tokens in the `defined` and `tentative` bytes (bits of one byte since #1167),
  rather than reading four columns at every declaring token; the attributes'
  passes visit only the declaring tokens that have attributes.

**What is left.**
- **The initializers' walks.** Most of `check-declarations` is now
  `check_init_walk`, the walk of each initializer's own tokens. On a Zen 4
  laptop's profile a quarter of its samples were `check_init_string`'s search
  of all the unit's string literals for each string element, which
  [#593](https://github.com/buster14a/buster/issues/593) makes linear.
- **The emission's attributes.** `emit-setup`'s passes still read the
  attributes of every declaring token, to find those that have any.
- **Dense maps.** `declaration_plans` (by declaration token), and the
  selection's `object_definitions`, `needed` and `object_symbols` (by object)
  remain dense in this change. The selection owns `object_definitions` and
  `needed` as scratch; the lowering and emission also use `object_symbols`.
  `declaration_plans` has only a few readers, indexed by declaring token.
  Compacting these maps and changing those readers together is tracked in
  [#641](https://github.com/buster14a/buster/issues/641).

## Token-wide work for sparse constructs (#877)

[#877](https://github.com/buster14a/buster/issues/877) found about 4% of `-c sqlite3.c` spent in passes that visit
every token for constructs that the unit has a handful of, or none. Its label table (#917) and runtime-name declarations
(#920) merged on their own. This change handles the rest of its items and the attributes comment's reader sweeps.
Every change keeps both paths' objects and diagnostics byte for byte.

- **Selections** (`check_input_selections`). In a unit that spells `_Generic` or `__builtin_choose_expr` (the pass
  doesn't run otherwise), the pass visited every token in postorder to find the closes whose first child is one of them.
  Its body is now `check_input_selection`, for one close.
  - **Vector path.** `check_input_selection_positions` compares 16 tokens' names at a time with the two names. Each
    such keyword maps to its parent, of which it must be the first child, and sets a bit at that close's postorder
    position. The pass visits those bits' closes in order, which is the order the walk would visit them in, so a
    selection's operands are still selected before it.
  - **Scalar path.** The walk. The kernel's own scalar path tests every close as the walk does: its twin in
    `test-checker`.
  - **Fallback.** Without room for the bits (`(count + 63) / 64` words of scratch), the vector path walks.
- **`check_program`'s columns.** `vla_first`, `allocation_first`, `allocation_next`, `vla_blocks` and `vla_saves`, and the
  allocations' pass over every token, are made only where the unit may have a variable-length array
  (`check_program_variable`): where it has a bound (`vla_count`), or its type table has a variable-length array's type.
  The condition is exact, since `check_action_runtime_size` needs such a type. `asm_statements` is made only with asm
  statements, and `region_first` only with regions. Each reader tests the column's presence first, as
  `check_lower_prepare` already did for `vla_identities`. A jump's search for the blocks it leaves
  (`check_control_leave`) is skipped without the variable-length arrays' columns. Each column is 4 bytes a token, 2.9 MB
  for `sqlite3.c`. Six of them were filled whole, 17 MB together; `allocation_next` took address space only.
  Then `plan_first` (each statement's first initializer plan, 4 bytes a token) and `initializers` (each initializer
  node's declaration, 4 bytes a node) are written at few entries. A bitset marks them (`planned`, `initialized`: a bit of
  each), and the readers test it, rather than reading a column cleared at every entry (`check_program_bit`).
- **The lowering's tables.** `switch_locals` is set to none only at the cases' owners, its only reads. `local_first` is
  set to none only at the bodies' openings, where it is read, and at the locals' bodies, where it is written. Both used
  to be filled at every token. `local_map` is read for any object an expression names, a global's included, and must
  read as none for all but the body's own. So preparation fills it with none, 2.9 MB for `sqlite3.c`, and its readers go
  through `check_lower_local_of`.
  - **#1067's bitset.** #1067 cleared only the bodies' parameters' and locals' entries and marked them in a bitset
    (`locals`), which every read tested first.
  - **Back to the fill.** On the 9700X that test cost the lowering more than the fill did, so #1580 restored the fill
    ("On the 9700X, matched", below).
- **The asm labels** (`check_input_asm_labels`). The reader swept the `attributes` column three times for its
  attributed tokens: for any label, for the labelled objects, and for the register variables' labels (which moved
  here from `check_input_asm_finish`, in the same order).
  - **One list.** `check_input_asm_attributed_tokens` now lists those tokens once, testing 8 pointers at a time.
  - **Sparse states.** The objects' states and first labels are kept only for the labelled objects, which a bitset
    marks, rather than cleared for every token (9 bytes a token).
  - **The uses.** Before the pass over the uses reads an identifier's object and state, it tests whether the
    identifier's declaration is one of a labelled object's: a bitset over the tokens
    (`check_input_asm_labelled_tokens`), which a gather of 16 tokens' words reads (`check_input_asm_labelled_uses`).
    Since #529, both kernels gather only for the 16 tokens whose values pass a filter of the labelled set
    (`CheckInputFilter`; "The per-token passes outside the walks").
  - **Twins.** Each kernel's scalar path tests every token.
  - The emission's own sweep of `attributes` and the column itself (#628) are unchanged. Both are sparse since #1167
    ("The attributes column", below).

`test-checker` checks the selections' kernel against a reading of random forests of up to 1,100 tokens, and the asm
labels' three kernels against a reading of random columns. Every column ends at a `PROT_NONE` page, and both paths
run. The existing program fixtures of variable-length arrays (jumps out of their blocks among them), asm statements and
statement expressions run through the columns, and every unit without them skips them. The bitsets are checked against
readings of their own too, each ending at a `PROT_NONE` page: `initialized` against the tokens' initializer nodes on
random columns; `planned` against a walk of random initializers' parents to their statements, whose lists must be their
plans in order and which may write no other entry of `plan_first` or `plan_next` (`check_program_plans`), with as many
plans as tokens, fewer, and none. Every program fixture's preparation must leave `local_map` none at every object
(#1580).

**On the 9700X, matched.** [The record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/sparse-constructs-877-2026-11-06)
([buster-performance#305](https://github.com/buster14a/buster-performance/pull/305)) compares each pull request with
its base on `-c` of the frozen `sqlite3.c`, with the `compile_time` step's options.

- **Placements.** Each tree is built at eight offsets of the checker's code (`BUSTER_CHECKER_OFFSET`, backported to
  these trees).
- **Holds.** `measure-compile`'s stages ran in three holds of the lock. `time-runs`, over the offsets and fresh copies
  with the base again as the floor, ran in five.
- **Objects.** Every build wrote the same object.

| | this change (#1025) | the bitsets (#1067) |
| --- | --- | --- |
| wall, `time-runs`, by round | −3.03% to −3.17% | −0.01% to +0.17% (the base against itself: −0.13% to +0.14%) |
| user cycles, by round | −3.54% to −3.77% | −0.02% to +0.25% (the base against itself: −0.08% to +0.18%) |
| instructions | −26.8 M (−1.90%) | +2.96 M (+0.21%) |
| `check-input` | 58.97 → 57.06 ms (−3.2%) | 55.91 → 55.76 ms (−0.3%) |
| `check-program` | 13.78 → 11.85 ms (−14.1%) | 10.16 → 10.09 ms (−0.7%) |
| `lower` | 14.28 → 14.19 ms (−0.7%) | 13.45 → 13.61 ms (+1.2%) |

**This change (#1025).** Its `check-input`, `check-program` and wall are below the base's at every offset: all 64
pairs of offset medians. Every `time-runs` round's range over the placements excludes zero. Its `lower` is within the
placements.

**The bitsets (#1067).** `check-program` is below the base's in 62 of 64 pairs. But `lower` is above in 53 of 64 pairs
and in every round. Every read of `local_map` tested the bitset first, which cost more than the 2.9 MB fill it
replaced. That fill is a `rep stosb` on the 9700X, a handful of instructions, while the tests added 3.0 M.

**#1580** restored the fill. The record's second campaign measured that on main `5fb062a4`, against main and against a
branch-free read of the bitset:

- **The fill.** `lower` is 0.07 ms (0.7%) faster in every round, with 1.27 M fewer instructions. As a whole process it
  is within main against itself.
- **The branch-free read** gains nothing.

The lowering is cheaper on today's main: 10.4 ms, against 13.5 ms at #1067's merge. The bitset's cost shrank with it.

**Instructions.** Callgrind counted the same compile's user instructions on baseline (`-march=x86-64`) builds of `main`
(`91cfe9e0`) and of this change. Those builds take the scalar paths, so the selections' change doesn't show; the
other three are on both paths. Both wrote the same object.

| | `main` | this change | change |
| --- | ---: | ---: | ---: |
| the whole compile | 5,890.9 M | 5,846.6 M | -44.2 M (-0.75%) |
| glibc's `memset` | 188.9 M | 160.0 M | -28.9 M |
| `check_program`, inclusive | 170.5 M | 148.6 M | -21.9 M |
| `check_input_run`, inclusive | 1,697.0 M | 1,679.8 M | -17.1 M |
| `check_input_asm_labels`, inclusive | 29.1 M | 22.3 M | -6.8 M |
| `check_lower_prepare`, inclusive | 54.9 M | 49.7 M | -5.2 M |

`check_program_allocations` (6.1 M) no longer runs, and `check_control_leave` fell from 391 K to 7.5 K. These are
instruction counts, not time: the fills cost the 9700X mostly memory traffic and fresh pages, which only its own
comparison shows. Callgrind needs a finite address space to run the compiler (`ulimit -v 67108864`, docs/arena-memory.md).

The later bitsets (`planned`, `initialized` and `locals`, which #1580 removed again), counted the same way on baseline
builds of `main` (`92252c30`) and of that change, which wrote the same object:

| | `main` | the bitsets | change |
| --- | ---: | ---: | ---: |
| the whole compile | 5,868.9 M | 5,862.8 M | -6.1 M (-0.10%) |
| glibc's `memset` | 137.9 M | 131.1 M | -6.8 M |
| `check_program`, inclusive | 148.2 M | 145.1 M | -3.1 M |
| `check_lower_prepare`, inclusive | 51.0 M | 49.0 M | -2.0 M |

## The reader's growth since the common warnings (#1489)

On the frozen sqlite3.c, [#1489](https://github.com/buster14a/buster/issues/1489) found `check-input` at 59.2 ms on
the 9700X, against 45.0 ms before #1049 (`aca9e53a`). A `cycles:u` profile against that build put most of the
growth in three places. Each change keeps both paths' objects and diagnostics byte for byte.

- **The deprecated-use pass.** It runs in every unit that reads a `deprecated` or `unavailable` attribute, and glibc's
  headers have some (`getwd`, `readdir_r`). It walked every token twice, a dispatch over a dozen columns per token:
  25 M cycles of `-c sqlite3.c`. The vector path now visits only the tokens a step can act at (the identifiers whose
  key is marked, by gathers 16 at a time, the tag keywords and names, the bodies' and scopes' braces). It counts the
  bracket levels between those tokens from 64-token masks by popcount. The scalar path keeps both walks, and the two
  run the same step at each token they visit (the checker guide's "Deprecated and unavailable uses").
  `test-checker`'s deprecated walks compare the paths with each source shifted by 0 to 66 tokens.
- **#823's unused-declaration facts** (`check_input_binding_answers`). The pass read each binding event's facts
  (its body, parameter mark, function flags and file, through a binary search) before it tested whether the event
  was an ordinary object's or function's declaration. It now reads them for those declarations alone. The
  definitions' parameters are found from the bodies' openings (`check_sparse_next`) rather than from every token, and
  a use's incomplete array type is read before its initializer's end.
- **The attribute lookups.** Each GNU attribute item looked its name up again in the rules, clang's vocabulary, the
  facts, the subjects and the arguments' rows. Those are binary searches with a `memcmp` per step, and a `strlen` per
  step for the arguments. `check_input_attribute_gnu_rows` keeps a name's rows in one of 128 slots of the reader, by
  the name's `IdentId`. A C23 spelling's lookups, which its scope changes, run each time as before.

These are the medians of 11 rotated rounds on CPU 2, under the lock, with `perf stat -r 5` for the counters. The
[measurement record](https://github.com/buster14a/buster-performance/tree/15b45a96ca6ba84320fbc180f20d1ed3c80614e7/docs/measurements/check-input-1489-2026-11-06)
has the method, the receipts and the profiles.

| | before #1049 | main `6b3a7ebd` | #1489's change |
| --- | ---: | ---: | ---: |
| check-input | 44.99 ms | 59.24 ms | 50.42 ms |
| total | 105.93 ms | 138.99 ms | 130.00 ms |
| instructions:u | 1,155.7 M | 1,636.8 M | 1,393.1 M |
| cycles:u | 503.0 M | 674.7 M | 624.5 M |

The compiler's own preprocessed main.c checks in 1,885 ms rather than 1,958 ms, with `check-input` at 1,111 ms
rather than 1,192 ms.

About 5.5 ms of the growth remains in the reader. The largest parts are the expressions' coded walk (#1038), the
binding answers' other per-event loops, the declarations' steps, the intrinsics' setup (#893) and the controls.

## The compiler's own code and a profile (#880)

[#880](https://github.com/buster14a/buster/issues/880) found the 9700X's front end starved: op cache misses 2 to 2.6
times Zen 4's per instruction, in hot functions tens of kilobytes long. A profile-guided build of the compiler
(`-fprofile-instr-use`, trained on the frozen corpora) then took 2.4 to 3.6% off `-c`'s time, for the same objects, and
ordering its functions without a profile took nothing
([buster-performance#182](https://github.com/buster14a/buster-performance/pull/182)).
The owner chose to do both of these:

- **Bring the profile's gains to the source**, so that every build has them.
- **Keep the profile as a build of its own:** `BUSTER_PGO=1 ./build.sh` ([the build guide](commands/build.md)), which
  isn't the default and which no CI job builds.

**What the profile changes.** These are user-mode counts of sqlite3.c's `-fsyntax-only` at `3ce71ec3` on the 9700X,
where nearly all of the gain is (the backend's share of `-c` didn't move):

| | without | with the profile |
| --- | ---: | ---: |
| instructions | 948.5 M | 873.1 M |
| calls (`ex_ret_near_ret`) | 4.91 M | 1.96 M |
| taken branches (`ex_ret_brn_tkn`) | 77.4 M | 43.7 M |
| stores dispatched | 143.5 M | 122.0 M |

Sampled attribution of the 75 M instructions didn't agree with itself. Fixed-period `instructions:u` samples land after
stalls, and the savings are spread over hundreds of functions, a few million instructions each. Two exact methods split
it instead:

- **Profile subsets.** The checker's unit, which holds the whole compile path, was built with parts of the profile. The
  full profile gave -7.4% of the instructions. The input reader's functions (`check_input_*`, literals, bindings and
  constants) gave -3.7% alone, the graph's walk and actions -2.8%, the other `check_*` -1.6%, and the preprocessor,
  parser and types -0.1%.
- **The profile without its inlining.** Building with the inliner's thresholds for hot and cold calls set to the
  default (`-mllvm -hot-callsite-threshold=225`, `-inline-cold-callsite-threshold=225`,
  `-locally-hot-callsite-threshold=225`, `-inline-enable-cost-benefit-analysis=false`) gave -2.1% of the instructions,
  -1.6% of the cycles and -33% of the taken branches. That part is the profile's block layout; its inlining gave the
  rest, -5.3% of the instructions and -54% of the calls.

**Inlining.** Clang's inliner keeps a callee of more than one call out of line once its cost passes the threshold,
about 50 IR instructions, however often it runs. The profile raises the threshold at hot calls. Sampling returns with
their branch records (`perf record -e ex_ret_near_ret:u -j any_ret,u`, the record's `from` the callee and its `to` the
caller) named the hot calls, and 46 callees on the checker's paths are now always inlined, each with the caller it
saves. Among them are the graph's per-node `check_graph_pure`, `check_graph_scalar_node` (with `check_graph_action`,
`check_graph_store` and `check_graph_leaf`), `check_action_compute`, the reader's `check_input_add_node`, and the
declarators' and literals' helpers. `check_graph_scalar_node`'s body, `check_graph_node_action` since #1050 gave the
memoized transitions an out-of-line copy of it, calls `check_action_compute` itself, rather than through
`check_action_scalar`, whose 15 other callers keep that call out of line. Out of the 4.91 M calls, 2.36 M are left; the
profile left 1.96 M.

**Layout.** One test is 6% of the taken branches: `type_strip_aligned`'s, inlined everywhere, which clang laid out with
the rare aligned type on the path. It now says so (`__builtin_expect`). The rest of the profile's layout is a long tail
of tests that run once a token or a node and are rarely true, such as `check_input_resolve_range`'s events and
`check_input_expression_coded`'s operators, which [#1175](https://github.com/buster14a/buster/issues/1175) states (below). Marking
the diagnostics' reporters `cold` (clang treats a path to a cold call as unlikely) took nothing: they are tiny and were
inlined, and as calls they cost about 2 M instructions more.

**The opt-in build.** Trained on the compiler's own translation units, `BUSTER_PGO=1` gave more than #182's profile of
the frozen corpora. These are the builds' counts against the same source's plain build, the least of three runs each:

| | instructions | cycles | taken branches |
| --- | ---: | ---: | ---: |
| `-fsyntax-only sqlite3.c`, the corpora's profile | -7.95% | -3.29% | -43.6% |
| `-fsyntax-only sqlite3.c`, the compiler's own | -9.39% | -5.09% | -51.8% |
| `-c sqlite3.c`, the corpora's profile | -5.90% | -2.18% | -39.8% |
| `-c sqlite3.c`, the compiler's own | -7.15% | -2.79% | -46.1% |
| `-c` of 139 musl units, the corpora's profile | -5.61% | -3.95% | -34.5% |
| `-c` of 139 musl units, the compiler's own | -7.09% | -6.37% | -44.2% |

The build takes 2.0 minutes on the 9700X with `BUSTER_DEBUG_INFO=0`: the instrumented compiles and the training take 78
s before the usual 45 s of compiles.

**Times.** `time-runs --runs 11 --warmups 1 --copies 2` on the 9700X (CPU 2), with `BUSTER_HOST_ARCH=znver5` and
`BUSTER_DEBUG_INFO=0`, ran main (`3ce71ec3`), this change, and this change with `BUSTER_PGO=1`. Each build ran at four
placements of `main.c`'s code (`build.sh shifts`, 0 to 768 bytes): 88 runs per build and list. Every object was byte for
byte main's. The ranges are over placements and copies.

| `-c sqlite3.c` | wall | user cycles | instructions |
| --- | ---: | ---: | ---: |
| main | 116.47 ms | 546.1 M | 1,257.1 M |
| this change | -1.53% (-2.66% to -0.60%) | -1.73% | -3.64% |
| this change, `BUSTER_PGO=1` | -3.76% (-4.75% to -3.06%) | -4.32% | -7.25% |

| `-c` of 139 musl units | wall | user cycles | instructions |
| --- | ---: | ---: | ---: |
| main | 186.82 ms | 744.2 M | 2,239.5 M |
| this change | -1.15% (-1.80% to -0.69%) | -1.58% | -3.27% |
| this change, `BUSTER_PGO=1` | -5.15% (-5.93% to -4.51%) | -7.04% | -7.24% |

The record ([buster-performance#222](https://github.com/buster14a/buster-performance/pull/222),
[`pgo-port-880-2026-11-02`](https://github.com/buster14a/buster-performance/tree/ff44d0e6baf2231a30d454555abb4694ac06549b/docs/measurements/pgo-port-880-2026-11-02))
has the scripts, every variant's counts, the call and taken-branch samples, and the executables' hashes.

## The checker's rare tests and their layout (#1175)

[#1175](https://github.com/buster14a/buster/issues/1175) brings the rest of #880's profile layout to the source. Its
sites are tests that run once a visited token or a graph node and are rarely true. Clang's static layout put their
bodies on the path and jumped around them.

**How it was counted.** The counts are exact taken branches of the compiler's own layout, with no hardware counters. The
host was a Xeon VM without a PMU, and the 9700X wasn't reachable.

- The build was `znver5`, linked with `--emit-relocs`.
- `llvm-bolt -instrument -strict` (LLVM 23) counted every edge of the build's own code for sqlite3.c's
  `-fsyntax-only`.
- `llvm-bolt -dyno-stats` on the original executable turned the counts into taken branches of its layout.
- Each taken edge was attributed to its branch's innermost source line (`llvm-symbolizer`), as #880's LBR samples
  were.

BOLT can't follow `check_input_run`'s jump tables in the PIE code build.sh compiles: their base is hoisted out of the
block. A `-fno-pie` build of the same source lets it. That build counted the same taken branches as the PIE one in
every other function, to within 1.3%. The counts below are its. A taken branch here is a taken conditional, an
unconditional jump, a jump table's or a tail call's: BOLT's own dyno-stats total leaves the last two out. Calls, returns
and glibc's code aren't counted.

They agree with #880's 9700X samples: `check_input_resolve_range` 8.3 M (the samples: 8.4 M), `check_graph_run` 8.0 M
(8.6 M), `check_input_run` 6.2 M (6.5 M), `check_graph_region_postorder` 2.4 M (2.5 M). Clang's source coverage of the
same compile gave each test's bias before it was hinted.

**What clang keeps of a hint.** Three rules decided most of the changes:

- **`&&` chains.** A `__builtin_expect` around a chain of `&&` reaches only the chain's last test. The tests before it
  keep clang's static guess: a visit's mark reason, a null column, a lane flag. So each condition starts with its rare
  test, hinted. `BUSTER_CHECK_RESOLVE_RARE` is a visit mark's, which a scalar host, whose walk makes every test,
  expects true.
- **`||` chains.** A hint around a chain of `||` does reach every test: each short-circuit edge is unlikely.
- **Selects.** Clang makes a branch of a select whose arm is a load, or a field rewritten with itself. So the
  branch-free forms load first, and mask a field rather than select it.

**The sites,** as sqlite3.c's `-fsyntax-only` counted them. The before is main at `96b6cbcd`; the after is `a84d9823`,
this change before it merged main (below for main `8c9c5bf0`).

| site | taken before | after | change |
| --- | ---: | ---: | ---: |
| `check_input_resolve_range`, its own lines | 6,622,338 | 1,192,964 | -82.0% |
| its step to the next marked token (`check_input_resolve_next`, `check_input_scan_all`) | 836,622 | 20,123 | -97.6% |
| `type_changes_at`, a visit's (only the deferred bodies' tables record changes) | 259,625 | 0 | -100% |
| `check_input_expression_coded` | 1,325,161 | 878,547 | -33.7% |
| `check_input_expression_values` | 1,013,291 | 1,013,291 | none (below) |
| `check_input_system_tokens` | 710,067 | 88,763 | -87.5% |
| `check_graph_has_arguments` | 590,556 | 0 | -100% |
| `check_stream_value` | 746,528 | 116,238 | -84.4% |
| `check_stream_set_value` | 1,087,140 | 187,188 | -82.8% |
| `check_action_compute` | 2,519,303 | 1,701,609 | -32.5% |
| `check_action_integer_type` | 644,114 | 303,520 | -52.9% |

These sites changed:

- **`check_input_resolve_range`.** With what it inlines, it took 32 taken branches at each of the 259,625 visits, and
  now takes 7. Each rare test begins its condition, hinted: a reason a visit's mark rarely gives, a column only some
  units have, a rare role or kind. The label-name test and the function's end are computed without branches.
- **`check_input_scan_all`.** It clamps its count, as `c_files.c` does, rather than testing it.
- **`check_input_system_tokens`.** Its tokens read a sentinel level past the output's, so no token tests whether it
  has a source.
- **`check_graph_has_arguments`.** It tests one bit of a 64-bit mask instead of eight compares.
- **`check_input_expression_coded`.** At `a84d9823`, a hint put its visit of a token its class didn't act on (2%) out
  of the path. Main `8c9c5bf0` then moved the walk's counters and clock into the visit's branch, after which the hint
  cost 18,938 taken branches, so it is gone. The jump table's jump stays, one a token: no class is more than a fifth
  of the tokens (identifiers: 21%).
- **`check_input_expression_values`.** Its children have values 57% of the time, and 29% of those follow another's. It
  is unchanged: neither form tried helped.
  - Branch-free selects: clang made a branch of the conditional store again, which took the same 1,013,291 branches.
  - A sink slot past the nodes for the link: lanes share `value_next`, so they would race on the slot.

The graph walk's other per-node tests, from the same counts, are hinted too:

- `check_graph_node_action`'s cases before the action's computation: 1,258,314 to 165,347.
- `check_graph_add`'s and `check_input_add_node`'s floating-point environment: 328,725 to 5,168.
- `check_action_operand`'s decays and rare lvalues: 1,049,081 to 267,424.
- `check_action_convert_value`'s vectors and complex numbers: 416,880 to 138,960.
- `check_graph_record_nodes`'s counters: 696,945 to 400,726.
- `type_resolved_index`'s aligned type, as #880's `type_strip_aligned`: 241,121 to 119.
- `check_program`'s overload addresses, which a C unit has none of.

`check_control_children`'s label test is hinted too. Its taken branches barely fell (577,979 to 575,627), since the
loop's back edge took the jump the test had taken: a loop takes one taken branch an iteration, whichever its layout. But
its iterations ran 2.76 M fewer instructions in both modes below, a run of the head without the hint showed.

**The whole run,** first as the change was made, main `96b6cbcd` against `a84d9823`:

| sqlite3.c | | main | this change | change |
| --- | --- | ---: | ---: | ---: |
| `-fsyntax-only` | taken branches | 61.46 M | 46.50 M | -24.3% |
| | instructions | 888.5 M | 876.8 M | -1.32% |
| `-c --lanes 1` | taken branches | 80.01 M | 63.74 M | -20.3% |
| | instructions | 1,174.4 M | 1,162.2 M | -1.04% |

Then after it merged main `8c9c5bf0` (`d9d9d32f`, measured as `a6405c80`, which differs in a comment's words only):

| sqlite3.c | | main `8c9c5bf0` | this change | change |
| --- | --- | ---: | ---: | ---: |
| `-fsyntax-only` | taken branches | 59.99 M | 45.04 M | -24.9% |
| | instructions | 884.0 M | 871.5 M | -1.41% |
| `-c --lanes 1` | taken branches | 78.53 M | 62.28 M | -20.7% |
| | instructions | 1,169.6 M | 1,156.8 M | -1.10% |

In both, `-c`'s objects of sqlite3.c and main.c were byte for byte main's. Against `8c9c5bf0`, the sites' own lines
counted as above but for `check_input_expression_coded`'s. Its visit's test took 98 thousand more jumps, as the
layout around it changed, while the compiled function, with what it inlines, fell 18% (4.36 M to 3.57 M). The 9700X's cycles over placements (`time-runs
--copies`, #509) aren't measured yet, so whether a change pays in time is still open.

**The gap that remains.** By #880's counts, `ex_ret_brn_tkn` (calls and returns included) fell 33% with the profile's
layout alone, and 44% with the whole profile. From main's 67 M, this change's 15.0 M fewer taken branches bring it to
about 32% below #880's 77.4 M. That is near the layout's share, but about 8.6 M short of the whole profile's 43.7 M,
which is mostly the profile's inlining.

The largest remainders, at this change:

- **`check_graph_run`, 4.95 M.** The node loop's leaf, scalar-node and region dispatch (no case is rare). The operands'
  loop, two-fifths of whose operands are absent. `check_graph_constant_contexts`' loop's back edges.
- **`check_input_run`, 4.77 M.** The coded walk's dispatch: a back edge, a jump table's jump and the case's jump to the
  join, a token each. Then `check_input_expression_values`.
- **`check_input_specifiers` and `check_input_declarator_step`, 1.8 M each,** which the profile's inlining took.
- **`check_graph_region_postorder`, 1.71 M.**
- **`check_program`, 1.80 M.**
- **`check_action_convert_plain`, 1.34 M,** whose integer and pointer tests are even.
- **`check_control_drain`, 1.32 M.**
- **`pp_run_contexts`, 1.22 M.**

Each of these is a dispatch or a test without a bias. What's left needs a restructure, such as a vector scan of
`check_graph_constant_contexts`' marks, rather than a hint.

The record ([buster-performance#237](https://github.com/buster14a/buster-performance/pull/237),
[`layout-hints-1175-2026-11-03`](https://github.com/buster14a/buster-performance/tree/ebcaea833c1f42e4ba3dc72f5a6729dbc784d5f1/docs/measurements/layout-hints-1175-2026-11-03))
has the harness, BOLT's raw counts of main and of this change, the variants' patches, the coverage counts, the hint probe
and the receipts.

## Linking is not in the 194.8 ms

**`-c` doesn't link at all.** The 194.8 ms of #136's speedtest1 record, and the
189.6 ms here, are `compiler -c sqlite3.c -o sqlite3.o`: it ends when it has
written the relocatable object. Building and writing that object takes 0.57 ms
(0.3%); the direct path's link (the `executable` stage, since #778 `runtime`,
`executable` and `write`) is zero.

**The record's link is a separate step.** It linked both objects with `cc` (GCC's
driver and GNU ld) in 14.2 ms. That is 6.4% of the two-file build's 221.8 ms,
and no part of the 146.9 ms by which `-c` trails tcc's `-c`.

**The time is in the checker and the lowering.** Optimizing a linker, ours or
the system's, can't touch them.

The direct path (`compiler -o` of one unit), which links in the same process,
isn't measured here: its input is a different unit (speedtest1's with
`sqlite3.c`), and its link is work `-c` doesn't do. Its stages would show in
the `runtime`, `executable` and `write` rows, and its write's phases on the
report's `time-write` lines.

## Small-unit setup and exit (#652)

Main's terminal C and `check` commands, and its `-E` command, let process exit
tear down their mappings. Output files are closed before the command returns;
stdout is flushed by libc on ordinary process exit. The reusable
`run_check_command` and `run_preprocessor_command` routes still release all
owned mappings before returning, including error paths, so tests and other
callers can compile another unit. The driver suite runs each reusable route
twice and checks that it retains no additional mappings.

On the terminal routes the `release` stage includes no explicit arena teardown.
Whole-process measurements still include the kernel's teardown at exit; the
old release stage's time must not be reported as an equal whole-process saving.
The tables above retain their recorded source revisions and cleanup policy.

The ordinary arena's 64 KiB base-page prefix is retained. The
[frozen 9700X evidence](https://github.com/buster14a/buster-performance/tree/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-652/target)
compares cleanup-only `24e9c1f8` with `bc1b4861`: isolated A/A, A/B and reversed
B/A controls show about 13–14% lower empty syntax-check latency, 12% lower
preprocessing latency and 6.4–6.7% lower tiny object latency. SQLite is a
neutral control. Those timings apply to the recorded revisions, not a later
integration; its exact-head correctness and CI validation remain separate.

### The start's fixed work, part 3

What a compile still did at its start on the 9700X after #1237 and #1279
(main `483e5f70`: an empty file's `-fsyntax-only` took 0.59 ms, 373 page
faults and 60% of its cycles in the kernel), and what this part takes out:

- **The parser's tables** (`parse_tables_initialize`, 17 KB, 4.8% of the
  empty file's cycles) are baked with the backend's ("Baked tables", above),
  in the frontend unit, their home (`BUSTER_BAKED_FRONTEND`).
- **`RLIMIT_AS` once.** Without a pool every reservation read the limit, 30
  `getrlimit` calls; a compile's commands read it once (`arena_limits_fix`,
  `docs/arena-memory.md`).
- **The emission lanes off the stack.** `CheckProgramLanes`, 98 KB of every
  lane's arenas, was zeroed on the driver's stack for every command: 24
  pages, a page fault each. The driver's is in `.bss` now, and an emission's
  release reads only the lanes it used (`arenas_used`).
- **A compile outside `main_command`'s frame.** The driver unit's
  `main_command` inlines the tests' commands, 140 KB of locals, and a compile
  ran below it, touching whichever of its pages the inlining placed its
  spills in; it runs from `main_driver` now (`main_source_command`).
- **No libm.** Loading `libm.so.6` took 24 µs and 12 faults of a process's
  start, for six exact operations the compiler now has itself
  (`host_math.h`); its tests reach libm through `dlopen` (`test_libm.c`), or
  in a static link that file's table, and the link takes libm only as needed.
- **The local calendar at the first `__DATE__` or `__TIME__`.** Its first
  conversion read the time zone's file, 7 system calls of every unit's start.

Against main with main again as the floor, over four placements of the
checker unit's code (record `small-unit-start-652-2026-11-05` in
buster-performance):

| input | wall | user cycles | minor faults |
| --- | --- | --- | --- |
| empty `-fsyntax-only` | −8.77% | −17.21% | 390 → 353 |
| empty `-E -P` | −4.91% | −5.80% | 296 → 286 |
| `tiny.c`'s `-c` | −7.12% | −11.19% | 462 → 427 |
| `sqlite3.c`'s `-fsyntax-only` | −0.01% (floor +0.10%) | −0.00% | |

What remained, measured there: the dynamic loader, which the same objects
linked `-static` don't pay (8% to 12% less task-clock), and each arena a small
unit touches, about 1.7 µs at its start for a separate reservation (its
`mmap`, first commit and page tables) against 0.36 µs as one of an arena of
tables.

### A static link

The executable is now linked statically wherever nothing needs the loader
(`docs/commands/build.md`: every build but the sanitizers' PIEs, fuzzing and
`BUSTER_HOST_RUNTIME`'s). A static glibc executable's `dlsym(RTLD_DEFAULT,
...)` finds nothing, and a `dlopen`ed library brings its own C library, whose
state (`errno`, the environment, the heap and stdio's files) is its own: the
natives the interpreter's programs call (`test-ssa-source`, `interpret-ir
--bind`) and the libm oracles of the tests come from a table of the host
functions they name (`test_libm.c`), which links them in, and from nothing
else. The stack canary's check reads the compiler's objects, since libc.a
defines and calls `__stack_chk_fail` itself. The start doesn't register the
executable's 634 KB of `.eh_frame` with libgcc's unwinder, which `crtbeginT.o`
does where the unwinder is linked (glibc's `pthread_exit` needs it): libgcc 13
and later sort every FDE at that registration, which doubled the user
instructions of an empty file's `-fsyntax-only` (1.74 million against the
dynamic executable's 0.80 million on the 9700X) and made a cold start slower
than the dynamic one. The link wraps the registration into a function that does
nothing, and libgcc finds the frames through `_dl_find_object`, as a dynamic
executable's does.

### Strings in their objects' order

A static build's `auto` linker is lld, at `-O0` (`cmake/BusterSettings.cmake`).
lld and mold merge every object's identical strings and lay the pool out in
their hashes' order, so a compile's comparisons of its options and commands
against their names (about 930 `strcmp` calls in an empty file's
`-fsyntax-only`) read pages all over the pool: 55 of that compile's 319 faults
were in mold's string pool. At `-O0` lld keeps each object's strings where the
object has them, and the same compile faulted 28 pages of read-only data where
mold's link of the same objects faulted 67, and 280 in all. From a cold start
on the 9700X it took 13.8% less time than mold's link, `-E -P` 11.0%, a
one-line `-c` 7.3%, a `-c` of a file that includes `stdio.h` 6.2%, and
`sqlite3.c`'s `-fsyntax-only` the same. The strings that several objects repeat
take 1.8 MB more of the executable. A first argument that starts with `-` also
goes to the source commands at once, past the units' chains of command names.

## How it is measured

**The stage clock.** `--time-stages` (`c_check_clock.c`) makes the C driver
report, on standard error after the command, a row per stage.
- **Laps.** A lap charges the TSC ticks since the previous lap to the stage it
  names. So the stages never overlap, and they add up to the clock's span, from
  the driver's first instruction to its final stage mark. The TSC's rate is
  measured against `CLOCK_MONOTONIC` over the same span.
- **Marks.** The coarse laps read `getrusage` too, for each stage's system time,
  minor faults and growth of the peak RSS. The per-function passes (lowering,
  Tier 0, branch sizing, encoding, `-g`'s rows, unwind frames) take TSC laps
  alone, and the next mark charges their resource use to its stage.
- **System-time qualification.** The stage `system-ms` values are raw
  `getrusage(RUSAGE_SELF).ru_stime` deltas between resource marks, summed into
  the named row; the counter includes all process threads. They are
  **low-confidence** estimates, not precise exclusive stage kernel costs: the
  resource boundaries may differ from the TSC laps, and kernel accounting can
  attribute CPU time across short phase boundaries. The October 29 repeated
  SQLite capture had stable parse elapsed time but widely varying attributed
  system time ([issue #870](https://github.com/buster14a/buster/issues/870)).
  Do not rank stages by these deltas or subtract them from elapsed time to
  infer stage user time. The report retains the raw numbers and identifies
  their source, boundaries, status and low-confidence accuracy in
  `time-process` rows: `stage-system-status` is `available` or `unavailable`,
  `stage-system-accuracy` is `low-confidence` or `unavailable`, and every
  stage's system and fault columns are `-1` when the read failed. A failed
  `getrusage` is latched, so a substituted zero cannot distort a later delta.
  Elapsed timings remain exact when the split is unavailable; minor fault
  counts share the split's availability, because both come from the same read.
  The [accounting control](#the-accounting-control-870) validates the split
  against hardware user/kernel cycles rather than trusting `task-clock:k`.
- **Lanes.** The driver compiles the functions on lanes (#151), by default as
  many as their tokens pay for, up to one per core of the CPUs it may run on
  (#905). On one lane, the
  calling thread takes the per-function laps above. On several, a thread's laps
  wouldn't add up across threads, so the lanes' whole run is one stage, `lanes`,
  and the stages from `lower` to `unwind` keep only the calling thread's passes
  around it. The 9700X step pins every compile to CPU 2, so its functions take
  one lane. This document's figures were measured before the lanes merged, on
  the calling thread alone, which a pinned compile still is.
- **The session's start.** `lanes-start` (#905) runs from the options' end to
  the command's flow's start on lane 0: the session's start, which, the session
  being lazy, takes lane 1's creation alone, lane 1 creating the other lanes
  while lane 0 goes on; for a command run on a session already, its flow's
  dispatch. Two `time-process` lines give the session's lanes: `lanes-asked`,
  the lanes the command asked for (one per core of the CPUs it may run on, which
  lane 1 counts, or `--lanes N`, at most one per CPU; at most `--worker-budget
  N`, and one for a source without code to emit), and `lanes`, those its flow
  ran on, one after a failed start.
- **The preprocessor's phases** inside its stage come from its own `PPOptions.timing`.
- **The lowering's phases** inside its stage (#1048) are TSC ticks the emission
  adds to the clock (`check_clock_lower`): `lower.prepare`,
  `check_lower_prepare`'s; and on one lane each part's (`CheckLowerPart`'s
  ticks: with the option alone, three TSC reads a part and two a body)
  `lower.statements`, its bodies' statements, their controls and tasks' loops;
  `lower.bodies`, the rest of its bodies' walks (frames, parameters, locals,
  labels and the columns' copies); and `lower.validate`, its ABI rows (the
  calls' and the locations', the signatures' checks and each function's
  parameters and result), `abi_assign` and `tier0_validate`. The last row,
  `lower.rest`, is the rest of the stage: the parts' own bookkeeping and
  `check_lower_finish`'s merge. On several lanes, the parts run in the `lanes`
  stage and read no TSC, and only the preparation and the rest remain.
- **The expressions' walk** inside `check-input` is a line of its own after the
  total, `time-check expressions MILLISECONDS PERCENT`: the TSC's ticks across
  `check_input_expressions`, which the reader keeps (`CheckInput.expression_ticks`,
  #1038; two TSC reads a unit, with the option or without) and the driver adds
  up. It overlaps the stage, so it is no row of the stages' sum. The report's
  reader (`check_clock_read`) requires it once, within its stage. Its parts
  follow, each a `time-check expressions.PART MILLISECONDS PERCENT` line
  (`CheckInput.expression_part_ticks`, disjoint and within the walk):
  - `classes`, the classes' pass;
  - `prepare`, the coded walk's chunks' compaction and identifiers' leaves;
  - `actions`, the classes' own actions in the coded walk;
  - `visits`, the walk's own visits (`check_input_expression_visit`): in a
    coded unit those of the tokens the classes' actions leave, in a unit the
    coded walk leaves to the visit throughout all of its chunks' actions, and
    on the scalar path all of the walk past its classes.

  `prepare` and `actions` are the coded walk's: on the scalar path (`--scalar`)
  both are `0.000`, which is no measurement, and in a unit the coded walk leaves
  to the visit `actions` is, while `prepare` is its chunks' compaction. Then
  comes
  `time-check expressions.usage SYSTEM_MS MINOR_FAULTS`: the process's
  `getrusage(RUSAGE_SELF)` counters across the walk, low-confidence as the
  stages' are. They count every thread's, which in a one-lane compile (as the
  9700X step's) are the reader's own; in a command with several units on
  lanes, other threads' too. The faults are mostly the first touches of the
  graph's node columns, which `check_graph_initialize` leaves unwritten and the
  walk writes a node at a time; the reader's per-token columns and the output
  columns `check_input_output_none` clears are written before the walk, so its
  faults don't count them. Where a unit's read failed, the line is
  `-1.000 -1`, latched across the units as the stages' columns are, so a
  substituted zero cannot read as a measurement. `check_clock_read_walk` reads
  these lines, each once, the parts nonnegative and adding up to no more than
  the walk, and the usage numeric or wholly unavailable. `measure-compile`
  prints their medians under `check-input.expressions`
  (`check_clock_print_walk`), `-1` where a run's usage was unavailable.
- **The walk's clock is always on.** The walk's row and parts are read with or
  without the option: two TSC reads a unit for the walk, two for the classes'
  pass, three a chunk of 1,024 positions, two a visit in a coded unit, and two
  `getrusage` calls a unit. On sqlite 3.53.2's amalgamation with the 9700X
  step's `-D` options (710,061 tokens: 694 chunks and 7,800 visits) that is
  some 18,000 TSC reads and two system calls, well under a millisecond against
  a walk of about 30 ms on a 4-CPU VM; the frozen corpus's `sqlite3.c` has
  6,639 visits, about 15,000 reads. Reading them
  only under the option would take a flag through the reader for less than the
  run-to-run noise.
- **Off by default.** Without the option the driver reads no clock, and each lap
  is a test of a null pointer; the reader's walk keeps its own counts, above. With it, the report and the objects are checked
  by `test-checker-driver --executable`.

**The harness.** `measure-compile [--runs N] [--warmups N] [--reference PATH
[--reference-option OPTION]... | --variant OPTION]... [--directory DIR] [--keep] --
ARGUMENT...` (`compile_measure.c`) times `ARGUMENT... -o FILE` as whole processes
in a Williams design's order: this compiler without the option, with it, and up
to four references (#146): other compilers, each with its own options before the
arguments (`--reference clang --reference-option -O0`), or this compiler with an
option (`--variant`).
- **Measures.** Each process waits on a pipe until its counters are open; its
  wall time runs from its release to `wait4`'s return, and `wait4`'s rusage
  gives its user and system time, peak RSS and faults. Its user cycles and
  instructions come from `perf_event_open`, from its exec to its exit, as
  `time-runs`' do; where the kernel refuses them, the report says `uncounted`.
- **Per token (#146).** `--time-stages`' report gives the preprocessed tokens,
  `time-process tokens` (the preprocessor's output, its end of file among it),
  which every run must give alike. Each compiler's median wall time and user
  cycles are divided by them, and each stage's median by them twice: as TSC
  ticks at the report's `tsc-mhz`, and as core cycles at the `--time-stages`
  processes' average clock: the median of their cycles, user and kernel (a
  third counter), per wall millisecond, which a pinned process spends running
  but for its exec's and exit's waits. The core figure takes a stage as running
  at that average clock, which a pinned, single-lane compile on a steady boost
  clock nearly is; the TSC figure is exact but counts at the TSC's fixed rate,
  not the core's. (`rusage`'s user time is too coarse for the clock: user cycles
  per user millisecond came to 5.63 GHz, above the 9700X's 5.5 GHz boost.)
- **Checks.** Every process must exit with status 0, the two objects of every
  run must be the same bytes, and every report must be whole.
- **Coverage.** After the runs, with the medians, it prints the first
  `--time-stages` report's `lowering-nodes` line (every run's is the same), as
  `lowering coverage:`: the evaluated nodes and the shares #624's fragments
  and #1048's node templates took, on the host's path (no compile takes
  either since #1048, so both are 0).
- **Report.** It prints every run, the medians and ranges, what the option
  costs, the default against the reference, and each stage's median and range,
  ranked by elapsed time. The preprocessor's phases and the expressions' walk
  (`check-input.expressions`) come under their stages, outside the stages' sum.
  Its stage table's system-time, fault and peak columns are `-1` when
  unavailable; it never ranks by the low-confidence system split.

### The accounting control (#870)

`--time-stages` cannot certify `getrusage`'s user/kernel split on every host:
the source is a process-wide CPU-time counter, and the kernel may account it
in ticks or charge a boundary's work to a neighboring interval. The reporting
side therefore labels the values low-confidence. The control campaign measures
that label instead of asserting it:

- **Known work.** A small C control alternates a CPU-only xorshift chain, page
  faults on freshly `madvise(MADV_DONTNEED)`-ed anonymous pages, and
  `getpid` syscalls. It sweeps phase lengths from 250 µs to 50 ms and start
  offsets across a 10 ms grid, so quantization and boundary leakage show as
  both a length effect and a phase-of-tick effect.
- **Independent split.** At every phase boundary it reads hardware
  `cycles:u` and `cycles:k` through `perf_event_open`
  (`exclude_user`/`exclude_kernel`), converts them with that phase's own
  cycle rate, and compares them with the `getrusage` system delta and minor
  faults. `task-clock:k` is collected and reported but not trusted: it is a
  software task clock with a privilege suffix, and the control shows it
  tracking whole-phase time rather than kernel cycles. Its previous use as an
  independent kernel timer is rejected.
- **Overhead and configuration.** An empty phase measures the marks' own
  system time, and the control records the kernel version, command line,
  accounting options from `/proc/config.gz` when readable, online, isolated
  and `nohz_full` CPUs, affinity, governor, frequency, and
  `perf_event_paranoid`. Voluntary and involuntary context switches separate
  a preempted phase from accounting leakage.
- **Verdict.** The control compares the system share with the hardware kernel
  share and their absolute difference at each tested length. A disagreement
  outside the campaign's tolerance yields `low-confidence`; unavailable
  hardware counters yield `unavailable`. A consistent host is evidence that
  the split is usable *there and at those lengths*, not a claim that every
  stage boundary is exact.

The harness, raw TSV and the repeated frozen SQLite report live in
[the 2026-11-01 accounting record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/compile-clock-870-2026-11-01).
Its results and the locked-9700X configuration are summarized there and in
the record's `analysis/summary.md`; no performance gate consumes the split.

**The 9700X step.** "Attribute sqlite3.c's compile time to the compiler's
stages, against tcc, clang -O0 and gcc -O0" runs, pinned to CPU 2 under the
host's lock:
- `measure-compile --runs 11 --reference tcc --reference clang
  --reference-option -O0 --reference gcc --reference-option -O0` on the frozen
  corpus's `sqlite3.c` (clang's and gcc's rows since #146's decision of
  2026-11-11);
- `perf stat -r 11` of both compilers;
- `perf record` of 5 compiles, reported by DSO and by symbol.

It runs on the daily and dispatched runs, and on pull requests that change the
clock, the stages' marks or the harness (the `changes` job's `compile_time`).
It fails rather than report part of the attribution:
- without tcc, or with a tcc whose sha256 isn't the reference's below. An
  upgrade changes the reference, so it updates the hash here and in the step
  together, and starts a new series of ratios. clang and gcc are unpinned, by
  the owner's choice: the step prints their versions and hashes, and an Arch
  upgrade of either changes their rows without failing it;
- when perf can't run, a counter reads `<not supported>` or `<not counted>`, or
  the profile comes out empty.

**Inputs.**
- **The source.** SQLite 3.53.2's `sqlite3.c`, sha256
  `0a409f1633283fa31a9126b11fbfd64a1991c5d30defad07e5745d4667f5e23d`, from the
  frozen corpus `preprocessor-391e729af658`: the file #136's speedtest1 record
  compiled. It is 706,237 tokens after preprocessing.
- **The options.** The record's options: SQLite's own `test/speedtest.tcl`
  options without `-Os` and `-g`, and the host's default headers.

  ```sh
  options=(-c -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -DSQLITE_ENABLE_MEMSYS5 -DSQLITE_ENABLE_RTREE -I"$sqlite" "$sqlite/sqlite3.c")
  taskset -c 2 ./build/compiler measure-compile --runs 11 --reference /usr/bin/tcc -- "${options[@]}"
  taskset -c 2 ./build/compiler --time-stages "${options[@]}" -o sqlite3.o
  ```

- **The reference.** tcc 0.9.28rc 2026-01-17 (`/usr/bin/tcc`, sha256
  `49e2d1afd09cdab6593ca127b056ccc35fc5f3a2d0e08dae67f604db2751c082`).
- **The compiler.** Built by the job with `BUSTER_DEBUG_INFO=0 ./build.sh --config Release`
  (clang 23, `-O3 -march=native`).

**What it leaves out.**
- **Warm caches only.** Every run follows a warm-up, so the source, the headers
  and the compiler's executable are in the page cache; a cold run would add their
  reads, which needs root to arrange (`drop_caches`).
- **Run time is #136's, not this one's.** The generated code's run time (speedtest1's 6,866 M
  instructions against clang -O0's 3,922 M, and `switch` as compare chains) is
  #136's, not this document's.

**Default-path regression.** Without the option, the objects are the same
bytes as `main`'s. On a Xeon VM, rotated runs against `main`'s compiler as the
reference showed no slowdown:

| input | runs | this head | `main` |
| --- | ---: | ---: | ---: |
| `sqlite3.c` | 21 | 420.7 ms | 432.0 ms |
| a small file | 201 | 4.84 ms | 5.03 ms |

On the small file, the backend's tables (`backend-setup`) take more than half
of the compile.

### Avoiding backend startup when no function is emitted (#590)

For an empty or data-only translation unit, and one whose static functions are
all dropped by selection, `backend-setup` now contains only the stage-clock
boundary. Encoder, operand-semantics, Tier 0 and unwind table initialization is
conditional on a selected body. Data/symbol emission and normal diagnostics
still execute, and object bytes are unchanged. The first selected function in
a reusable caller initializes the tables as before. This removes table work
independently of whether the process's pages use huge pages or base pages;
small files with a selected function still pay the existing setup cost.

The [frozen qualification record](https://github.com/buster14a/buster-performance/tree/4df43f7d7a685133b6beade655d9e9a306947081/docs/measurements/unclaimed-perf-2026-10-28/issue-590/target)
compares `bc1b4861` with `8b815992`, including the host's default page policy,
an exec-inherited per-process THP-disable control and body-bearing units.
That control did not change the host to `madvise`. These timings predate the
later encoder changes; validation of the current source is recorded separately.

### The backend's tables when a function is emitted (#590)

A unit with a selected function still builds the backend's tables in every
process. On a 2.1 GHz Xeon VM (4 vCPUs, kernel 6.18, transparent huge pages
`madvise`, as Ubuntu, Debian and Fedora ship them), `hello.c`'s
`backend-setup` was 3.4 to 4.5 ms of a 10 to 14 ms compile. It took 1,132 base
pages' faults:
- 788 in the encoder's template table;
- 257 in Tier 0's tables;
- 41 in the operand semantics' tables;
- 38 in the unwind templates.

Without faults, the encoder's own build was about 0.9 ms. Of that, 0.41 ms was
the forms' templates, 0.40 ms was building their FS and GS templates again,
and the rest was a 1.6 MB copy for the marked forms. The 9700X hid most of
this: its kernel's `always` gives `.bss` huge pages without being asked.

What changed:
- **The encoder's FS and GS templates come from each form's own.** In 64-bit
  mode an override only inserts its byte, so `x86_template_with_segment` makes
  both from the form's template and the counts its build kept, and refuses
  where the build would. Every entry is compared with the builder's
  ([the encoder's design](x86-64-encoder.md), "Templates and the canonical
  slot"). Each entry's marked copy is written in the same per-entry pass
  that writes its halves the other way around.
- **`x86_encode`'s template table gets huge pages.** Before the build, the
  encoder asks for huge pages (`MADV_HUGEPAGE`) and commits the ranges it
  writes (`MADV_POPULATE_WRITE`, one call per range).
  - It asks only for the 2 MB pages the table fills at least 750 KB of
    (`lane.c`'s measured break-even). Its bytes fall in two or three, and that
    is usually two of them.
  - That is a fault per huge page where huge pages come, and base pages
    committed in one call per range for the rest, or where none come.
  - A 2 MB margin on either side keeps them in the table's own storage. The
    storage isn't aligned to a huge page, because wild 0.10, which links CI's
    builds, refuses alignments past 64 KB.
  - Only `main.c`'s units' copy, the one a compile builds, has the margins,
    4 MB of `.bss` in all. The benchmarks' units, which no compile runs, keep
    their copies as they were, rather than 67 MB of margins over 16 copies.
  - `x86_encoder_initialized` is in `.data`, as `lane.c`'s `LaneProcess` is.
    In `.bss` it followed the margin into a 2 MB region where a compile writes
    nothing else, and an `always` kernel cleared that region's huge page for
    the one byte. At the earlier heads that had it there (`8da9e479` and
    `f5179878`), on the 9700X, that cost 2 MB of peak RSS, and `hello.c`'s
    other stages took 0.05 to 0.06 ms more; the table below is from after the
    move.
  - The other modes' tables (32-bit addresses, APX, 32- and 16-bit modes),
    which no compile builds, keep their size. They get only the commit.
- **Tier 0 commits its nine tables' pages in one call per array**
  (`tier0_prepare_tables`). One structure of them, in a huge page of its own,
  ran 2.2 times as many instructions, because LLVM no longer knew the tables
  apart ([Tier 0](tier0.md)).
- **The semantics and unwind tables are left as they are.** Their 79 faults
  cost less than a huge page's clearing would.

Objects are the same bytes: the 220 programs of c-testsuite's subset,
`hello.c` and `sqlite3.c` on the 9700X (the record's `objects.txt`), and the
compiler's own `main.c` (39 MB) on the VM, all compiled with `-c` by both
builds. The encoder's loops are unchanged.
[The frozen record](https://github.com/buster14a/buster-performance/tree/5af90b2008b8440ff284b6e41ff1250479a29eb5/docs/measurements/backend-setup-590-2026-11-01)
has the raw outputs, commands and harness. In the tables, "disabled" is a
process's `PR_SET_THP_DISABLE`, which its children inherit; it stands in for a
kernel with `never`.

On the 9700X (THP `always`, under its lock, CPU 2), this change at `32e0c754`
against `main` at `b823d1c2`, both built by `build.sh` there, with tcc 0.9.28rc
as the reference:

| input | THP | `main` | this change | tcc |
| --- | --- | ---: | ---: | ---: |
| `hello.c`'s `backend-setup`, medians of 3 × 41 | `always` | 0.583 ms | 0.518 ms | |
| `hello.c`'s stages | `always` | 2.080 ms | 2.035 ms | |
| `hello.c`'s `backend-setup` | disabled | 0.831 ms | 0.627 ms | |
| `hello.c`'s stages | disabled | 2.153 ms | 1.944 ms | |
| c-testsuite's 220, best of 3 each, mean | `always` | 1.913 ms | 1.809 ms | 0.553 ms |
| c-testsuite's 220, best of 3 each, mean | disabled | 2.078 ms | 1.852 ms | 0.515 ms |
| `sqlite3.c`, `measure-compile --runs 11` | `always` | 135.25 ms | 135.39 ms | |

- **Peak RSS on the 9700X.** `backend-setup`'s peak added is `main`'s (6,564
  KiB), and its 2 MB windows of `.bss` are `main`'s six.
- **Small files there.** Its kernel already gave the tables huge pages, so the
  gain is the derived templates and the commits: a tenth of `backend-setup`,
  and about 2% of a small file's stages.

On a 2.1 GHz Xeon VM (THP `madvise`), `91cfe9e0` against `8da9e479`, both
linked by wild 0.10.0, with tcc 0.9.27 (Ubuntu's):

| input | THP | `91cfe9e0` | this change | tcc |
| --- | --- | ---: | ---: | ---: |
| `hello.c`, `measure-compile --runs 31`, two rounds | `madvise` | 13.8, 12.8 ms | 10.9, 10.1 ms | 3.2 to 3.5 ms |
| `hello.c`'s `backend-setup` | `madvise` | 3.87, 3.86 ms (1,129 faults) | 2.09, 2.17 ms (420) | |
| `hello.c`, two rounds | disabled | 13.6, 11.7 ms | 10.7, 10.7 ms | |
| `hello.c`'s `backend-setup` | disabled | 4.21, 3.21 ms | 2.36, 2.53 ms | |
| c-testsuite's 220, best of 3 each, mean | `madvise` | 8.84 ms | 8.03 ms | 2.04 ms |
| c-testsuite's 220, best of 3 each, mean | disabled | 9.07 ms | 7.70 ms | 2.11 ms |

- **Minor faults.** A kernel counts `MADV_POPULATE_WRITE`'s pages as minor
  faults: about 1,130 of them in the "disabled" rows, with none taken one at a
  time.
- **Peak RSS on the VM.** With huge pages it grows by 1.2 MB (31.0 to
  32.3 MB), as the kernel clears the table's huge pages whole.
- **Address space.** On the 9700X, `size build/compiler` gives `.bss` as
  191,387,924 bytes before and 195,641,236 after: the margins. That is address
  space, not memory or file size: the `.bss` segment has no file bytes.
- **Noise.** The VM is noisy: rounds vary by 1 ms, and a run in 31 can be
  several times the median.
- **Against tcc.** c-testsuite's programs take 3.3 to 4 times tcc's time,
  against 3.5 to 4.3 times before. What remains of `backend-setup` (2.1 to
  2.5 ms on the VM, 0.52 ms on the 9700X) is mostly the encoder's build and its
  pages' clearing, Tier 0's build, and semantics and unwind.
- **Since.** The tables are baked into the executable ("Baked tables",
  below), which took their build and clearing out of a compile and replaced
  this section's huge pages, margins and `x86_encoder_initialized` in `.data`.

### Baked tables (#590)

The build's post-link step has each executable it links write itself with the backend's tables
built (`bake-tables`, `src/buster/compiler/baked.c`): x86_encode's templates
and their marked copies, the x86 semantics' tables, Tier 1's selection state
tables (#141, `tier1_select_burs.c`, shared by main.c's units as the encoder's
are), the checker unit's copies of Tier 0's tables, the Zen 5 cost model's
tables and the unwind templates, and the frontend unit's copies of the
parser's tables (#652, `parse_tables_initialize`, 17 KB, which every compile
built before its parse) and the checker unit's asm matcher's index (#2532,
`check_asm_index_initialize`, 549 KB, which each unit with asm statements
made), with their initialized flags, all in a section of their own
(`buster_baked`, `baked.h`), 6.6 MB of the file (`test-baked-tables` prints
its size). A compile's process starts with them built, at their fixed
addresses, and reads the pages it touches from the page cache, shared with
every other process.

- **Why baked, not a snapshot.** A snapshot in the cache directory, as the C
  runtime has, would need the executable's identity, staleness checks, a
  rebuild on a first run and, to keep the encoder's constant address, a fixed
  mapping over `.bss`. Baked tables are part of the file that builds them: no
  identity to check, nothing to go stale, and the first run has them.
- **Checked.** The bake refuses, and fails the build, if the initializers wrote
  any byte of the image's writable memory outside the section (but the GOT,
  which the loader fills as functions are first called), or if its writable
  ranges or GOT sections outgrow the arrays it tracks them in. Its first run
  found Tier 0's window and vector tables and the Zen 5 cost model's tables,
  which a list made by hand had missed. `test-baked-tables` makes the same
  check in any executable, a TSan or MSan build's PIE too, and in a baked one
  that a fresh build gives the file's bytes and build ID; a missing section
  fails it instead of skipping it. Under ThreadSanitizer and MemorySanitizer the
  runtimes keep the metadata of the globals they instrument in the image's
  `.bss` and write it as instrumented accesses run, so those builds check the
  section's bytes alone and say that the outside check was skipped.
- **The build ID.** The linker's GNU build ID is over the pre-bake file, so
  `bake-tables` recomputes it over the baked bytes, with the note's descriptor
  zeroed. The ID stays stable for a given build and never names bytes other
  than the file's.
- **Where it isn't.** PIEs (the sanitizers' builds, whose tables would hold
  their load address if any had a pointer), executables linked without
  the build's post-link step and builds with `BUSTER_BAKE=0` build the tables when they run, as
  before. So do the benchmarks' units.

Objects are the same bytes, baked or built: `hello.c` and c-testsuite's 220
programs, each compiled with `-c` by both builds and compared. On a 2.1 GHz
Xeon VM with transparent huge pages `madvise`, against `main` at `7c779480`,
both linked by wild 0.10.0:

| input | `main` | baked | tcc |
| --- | ---: | ---: | ---: |
| `hello.c`, `measure-compile --runs 31`, three rounds | 9.90, 10.27, 11.70 ms | 8.39, 7.20, 8.83 ms | |
| `hello.c`'s `backend-setup`, and its faults and peak added | 2.63 ms, 535, 4.6 MB | 0.014 ms, 1, 0 | |
| `hello.c`'s minor faults, whole process | 1,182 | 660 | |
| `hello.c`'s peak RSS (`wait4`) | 30.0 MB | 24.8 MB | |
| c-testsuite's 220, best of 3 each, mean | 7.74 ms | 5.22 ms | 1.94 ms |
| `sqlite3.c`, `measure-compile --runs 11`, two rounds | 331.9, 304.9 ms | 308.2, 300.0 ms | |

- **Small files.** c-testsuite's programs take 2.7 times tcc's time, against
  4.0 times before.
- **`sqlite3.c`.** Unchanged within the VM's noise: base pages from the page
  cache cost its encoding no more than the huge pages did.
- **The file.** The executable grows by the section, 5.1 MB, which the page
  cache shares between processes.

### The issue's checks at `ec5f0f6b` (#590)

#590 is done when an empty file's `backend-setup` is near zero with transparent
huge pages `madvise` and `always`, with the same objects, and a small file's
compile has been measured against tcc. These are those checks at `main`
`ec5f0f6b`. Each machine compares two executables, both made by `build.sh` from
one link:
- the baked executable;
- the same link with `BUSTER_BAKE=0`, which builds the tables when it runs, as
  every compile did before #1078 ("built" below).

The inputs:
- `empty.c`, an empty file;
- `tiny.c`, `int main(void) { return 0; }`;
- `hello.c`, `<stdio.h>` and a `puts`;
- c-testsuite's 220-program subset.

Each small-file row is `measure-compile --runs 41 --reference tcc -- -c FILE`,
pinned to CPU 2, and gives its median. On the VM there were two rounds, with the
executables' order swapped, and each figure is the mean of the rounds' medians.
On the 9700X there was one round, and fewer inputs: runs there waited up to 15
minutes for the lock behind CI jobs and other sessions. The
c-testsuite rows give each program's best of 3 runs, averaged over the 220.

**Objects are the same bytes, baked or built,** on both machines: c-testsuite's
220 programs, `empty.c`, `tiny.c` and `hello.c`, each compiled with `-c` by
both executables and compared.

On the Ryzen 7 9700X (kernel 6.18, THP `always`, under its lock), built there
by `build.sh`, with tcc 0.9.28rc. "Disabled" is `PR_SET_THP_DISABLE` on the
process, as in the sections above:

| input | THP | `backend-setup`, baked | built | wall, baked | built | tcc |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `empty.c` | `always` | 0.004 ms | 0.004 ms | 0.448 ms | 0.481 ms | 0.360 ms |
| `empty.c` | disabled | 0.004 ms | 0.004 ms | 0.561 ms | 0.598 ms | 0.360 ms |
| `hello.c` | `always` | 0.003 ms | 0.862 ms (1,136 faults) | 1.364 ms | 2.516 ms | 0.776 ms |
| `hello.c` | disabled | 0.003 ms | 0.746 ms (1,136 faults) | 1.447 ms | 2.307 ms | 0.769 ms |
| c-testsuite's 220, mean | `always` | | | 0.964 ms | 1.883 ms | 0.543 ms |

On a 2.1 GHz Xeon VM (4 vCPUs, kernel 6.18, linked by lld), with tcc 0.9.27
(Ubuntu's). The kernel's setting was switched between `madvise` and `always`.
On a VM the compiler turns huge pages off for itself (`lane.c`'s
`arena_page_policy`), so these runs set `BUSTER_HUGE_PAGES=1`, which leaves
the kernel's setting in force:

| input | THP | `backend-setup`, baked | built | wall, baked | built | tcc |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `empty.c` | `madvise` | 0.008 ms | 0.009 ms | 1.99 ms | 2.05 ms | 1.65 ms |
| `empty.c` | `always` | 0.009 ms | 0.010 ms | 2.11 ms | 2.41 ms | 1.79 ms |
| `tiny.c` | `madvise` | 0.008 ms | 2.25 ms | 2.22 ms | 4.90 ms | 1.73 ms |
| `tiny.c` | `always` | 0.009 ms | 2.19 ms | 2.45 ms | 4.87 ms | 1.83 ms |
| `hello.c` | `madvise` | 0.009 ms | 2.25 ms | 5.46 ms | 8.01 ms | 2.83 ms |
| `hello.c` | `always` | 0.008 ms | 2.24 ms | 4.88 ms | 7.55 ms | 2.66 ms |
| c-testsuite's 220, mean | `madvise` | | | 3.06 ms | 5.42 ms | 1.66 ms |
| c-testsuite's 220, mean | `always` | | | 3.20 ms | 5.63 ms | 1.71 ms |

- **Empty file.** `backend-setup` is the stage clock's boundary alone, under
  every page policy: 0.004 ms on the 9700X, and 0.007 to 0.010 ms on the VM
  (with its own default, huge pages off, too). A unit without a selected body
  initializes none of the backend's tables (#689), and the baked executable
  has none to initialize.
- **With a function.** The baked executable's `backend-setup` is 0.003 ms on
  the 9700X and 0.008 to 0.009 ms on the VM, with one or two faults. Building
  the tables when the process runs costs about 1,135 faults on both machines:
  0.75 to 0.86 ms on the 9700X, and 2.2 ms on the VM under `madvise` and
  `always` alike.
- **Assembly inputs (#1571).** Two routes still rebuilt the x86 semantics'
  tables in a baked executable, writing their baked pages:
  - a compile with an assembly or other non-C input (`-c foo.s`, or C files
    with one), which sets the driver's `mixed_inputs`;
  - `compiler assemble`.

  They now test `x86_semantics_initialized` first, as the other routes do.
  Their objects are unchanged, and an executable that isn't baked builds the
  tables once, as before. Two rounds of `measure-compile --runs 41`, CPU 2,
  against the build before:
  - on the 9700X, at `ec5f0f6b`, where the two files are `main`'s:
    - `-c foo.s` takes 0.479 and 0.476 ms with 230 faults, against 0.546 ms
      with 269, 13% less;
    - a link of a C file and `foo.s` takes 1.181 and 1.193 ms with 529
      faults, against 1.268 and 1.283 ms with 568;
  - on the VM:
    - `-c foo.s` takes 2.40 and 2.57 ms with 675 faults, against 2.64 and
      2.89 ms with 716;
    - the link takes 4.18 and 4.39 ms with 1,202 faults, against 4.49 and
      4.63 ms with 1,243.
- **Under the VM's own default** (huge pages off), the baked executable's
  `backend-setup` is the same, 0.007 to 0.009 ms. c-testsuite's programs take
  3.21 and 3.34 ms there, against 5.55 and 5.74 ms built.
- **Against tcc.** On the 9700X, c-testsuite's programs take 1.78 times tcc's
  time, against 3.47 times built, and `hello.c` 1.76 times, against 3.24. On
  the VM, c-testsuite's take 1.84 to 1.87 times, against 3.3. Before #1027,
  the 9700X's ratio was 3.5 and the VM's 3.5 to 4.3 ("The backend's tables
  when a function is emitted", above).
- **What remains of a small file isn't the backend.** On the 9700X, `hello.c`'s
  baked stage span is 1.18 ms:
  - preprocessing `<stdio.h>`, 0.43 ms;
  - the checker's reader, 0.35 ms;
  - `setup`, 0.12 ms.

  Outside the span, the process takes another 0.31 ms. On the VM, `hello.c`'s
  baked stage span is 3.8 to 4.5 ms:
  - preprocessing `<stdio.h>`, 1.3 to 1.5 ms;
  - the checker's reader (`check-input`), 1.0 to 1.2 ms;
  - `setup`, 0.45 to 0.55 ms.

  On the VM, the process outside the span (exec, `main`'s dispatch and exit)
  takes 1.2 to 1.6 ms for any input, and tcc's whole compile of `empty.c`
  takes 1.65 to 1.79 ms. On the 9700X they take 0.24 to 0.39 ms and 0.36 ms.
  - `setup`'s faults come from first touching the executable's pages. From
    `check_driver_command`'s entry to `check_driver_flow`'s (`setup` and
    `lanes-start`), gdb and `/proc/PID/smaps` show about 1.4 MB of code,
    1.3 MB of read-only data and 0.4 MB of RELRO becoming resident.
  - Baking doesn't change `setup`: the built executable faults there as often.
