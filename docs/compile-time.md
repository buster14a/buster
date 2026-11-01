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
   - Follow-up: [#529](https://github.com/buster14a/buster/issues/529).
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
     took 115 MB of it and 17% of the kernel's cycles, #570 46 MB more and
     [#572](https://github.com/buster14a/buster/issues/572) 121 MB more
     ("Memory first touched", below).
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
follows up on these.

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
  change, and #592's passes' too.
- **The graph's leaves.** Their 96-byte values take 14 MB for 144,720 leaves, of
  which the identifiers' carry little but a type and an object: columns and
  payload rows, as the stream's values have, would take about 8 MB less.
- **The byte columns.** Twelve zero-default output columns of a byte a token,
  8.3 MB, written at declarations, most of whose bits would fit one byte.

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
  tokens in the `defined` and `tentative` bytes, rather than reading four
  columns at every declaring token; the attributes' passes visit only the
  declaring tokens that have attributes.

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
  read as none for all but the body's own. Only its parameters' and locals' entries are ever written, so preparation
  clears just those and marks them in a bitset (`locals`, a bit an object), before any lane lowers a body. Its readers
  go through `check_lower_local_of`, which reads none for an unmarked object; the lanes only read the bits.
- **The asm labels** (`check_input_asm_labels`). The reader swept the `attributes` column three times for its
  attributed tokens: for any label, for the labelled objects, and for the register variables' labels (which moved
  here from `check_input_asm_finish`, in the same order).
  - **One list.** `check_input_asm_attributed_tokens` now lists those tokens once, testing 8 pointers at a time.
  - **Sparse states.** The objects' states and first labels are kept only for the labelled objects, which a bitset
    marks, rather than cleared for every token (9 bytes a token).
  - **The uses.** Before the pass over the uses reads an identifier's object and state, it tests whether the
    identifier's declaration is one of a labelled object's: a bitset over the tokens
    (`check_input_asm_labelled_tokens`), which a gather of 16 tokens' words reads (`check_input_asm_labelled_uses`).
  - **Twins.** Each kernel's scalar path tests every token.
  - The emission's own sweep of `attributes` and the column itself (#628) are unchanged.

`test-checker` checks the selections' kernel against a reading of random forests of up to 1,100 tokens, and the asm
labels' three kernels against a reading of random columns. Every column ends at a `PROT_NONE` page, and both paths
run. The existing program fixtures of variable-length arrays (jumps out of their blocks among them), asm statements and
statement expressions run through the columns, and every unit without them skips them. The bitsets are checked against
readings of their own too, each ending at a `PROT_NONE` page: `initialized` against the tokens' initializer nodes on
random columns; `planned` against a walk of random initializers' parents to their statements, whose lists must be their
plans in order and which may write no other entry of `plan_first` or `plan_next` (`check_program_plans`), with as many
plans as tokens, fewer, and none; and `locals`
against each program fixture's bodies' parameters and automatic declarations, and through its accessors on random
objects.

**On the 9700X.** The pull request's `compile_time` step, at its first head (`f0084098`,
[job 124825289835](https://github.com/buster14a/buster/actions/runs/41297067398/job/124825289835)), on the frozen
`sqlite3.c` with the step's options: `-c` took 130.239 ms wall (129.656 to 131.298), the median of 11 runs pinned to
CPU 2. Its stages' medians:

| stage | ms | range |
| --- | ---: | --- |
| `check-input` | 56.694 | 56.288 to 57.042 |
| `check-program` | 11.992 | 11.892 to 12.021 |
| `lower` | 14.114 | 13.918 to 14.355 |

`main`'s pushes that day were cancelled or stopped before the 9700X job, so there is no base from the same step and day.
A matched comparison of the stages, interleaved and over code placements (`time-runs`, #509), is still to be made.

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

The later bitsets (`planned`, `initialized` and `locals`), counted the same way on baseline builds of `main`
(`92252c30`) and of that change, which wrote the same object:

| | `main` | the bitsets | change |
| --- | ---: | ---: | ---: |
| the whole compile | 5,868.9 M | 5,862.8 M | -6.1 M (-0.10%) |
| glibc's `memset` | 137.9 M | 131.1 M | -6.8 M |
| `check_program`, inclusive | 148.2 M | 145.1 M | -3.1 M |
| `check_lower_prepare`, inclusive | 51.0 M | 49.0 M | -2.0 M |

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
- **The expressions' walk** inside `check-input` is a line of its own after the
  total, `time-check expressions MILLISECONDS PERCENT`: the TSC's ticks across
  `check_input_expressions`, which the reader keeps (`CheckInput.expression_ticks`,
  #1038; two TSC reads a unit, with the option or without) and the driver adds
  up. It overlaps the stage, so it is no row of the stages' sum. The report's
  reader (`check_clock_read`) requires it once, within its stage.
- **Off by default.** Without the option the driver reads no clock, and each lap
  is a test of a null pointer. With it, the report and the objects are checked
  by `test-checker-driver --executable`.

**The harness.** `measure-compile [--runs N] [--warmups N] [--reference PATH]
[--directory DIR] [--keep] -- ARGUMENT...` (`compile_measure.c`) times
`ARGUMENT... -o FILE` as whole processes in a rotating order: this compiler
without the option, with it, and the reference compiler.
- **Measures.** Each process's wall time runs from `fork` to `wait4`'s return,
  and `wait4`'s rusage gives its user and system time, peak RSS and faults.
- **Checks.** Every process must exit with status 0, the two objects of every
  run must be the same bytes, and every report must be whole.
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
stages, against tcc" runs, pinned to CPU 2 under the host's lock:
- `measure-compile --runs 11 --reference tcc` on the frozen corpus's
  `sqlite3.c`;
- `perf stat -r 11` of both compilers;
- `perf record` of 5 compiles, reported by DSO and by symbol.

It runs on the daily and dispatched runs, and on pull requests that change the
clock, the stages' marks or the harness (the `changes` job's `compile_time`).
It fails rather than report part of the attribution:
- without tcc, or with a tcc whose sha256 isn't the reference's below. An
  upgrade changes the reference, so it updates the hash here and in the step
  together, and starts a new series of ratios;
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
- **The compiler.** Built by the job with `BUSTER_DEBUG_INFO=0 ./build.sh`
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

build.sh has each executable it links write itself with the backend's tables
built (`bake-tables`, `src/buster/compiler/baked.c`): x86_encode's templates
and their marked copies, the x86 semantics' tables, and the checker unit's
copies of Tier 0's tables, the Zen 5 cost model's tables and the unwind
templates, with their initialized flags, all in a section of their own
(`buster_baked`, `baked.h`), 5.1 MB of the file. A compile's process starts
with them built, at their fixed addresses, and reads the pages it touches from
the page cache, shared with every other process.

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
  check in any executable, a sanitizer build's PIE too, and in a baked one that
  a fresh build gives the file's bytes and build ID; a missing section fails it
  instead of skipping it.
- **The build ID.** The linker's GNU build ID is over the pre-bake file, so
  `bake-tables` recomputes it over the baked bytes, with the note's descriptor
  zeroed. The ID stays stable for a given build and never names bytes other
  than the file's.
- **Where it isn't.** PIEs (the sanitizers' builds, whose tables would hold
  their load address if any had a pointer), executables linked without
  build.sh and builds with `BUSTER_BAKE=0` build the tables when they run, as
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
