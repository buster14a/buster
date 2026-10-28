# Where sqlite3.c's compile time goes

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

`debug` (`-g`'s DWARF) and `executable` (the direct path's link) are zero in
`-c` without `-g`, and are left out. The stages' medians add up to 189.27 ms,
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
     took 115 MB of it and 17% of the kernel's cycles, and #570 46 MB more
     ("Memory first touched", below); [#572](https://github.com/buster14a/buster/issues/572)
     has the rest.
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

## Memory first touched (#528)

Three allocations touched far more memory than they used.
- [#528](https://github.com/buster14a/buster/issues/528)'s two changes took
  115 MB off the compile's peak RSS, and 17% of its kernel cycles.
- #570, the lowering's work for [#531](https://github.com/buster14a/buster/issues/531),
  takes 46 MB more with the third. #528's branch had made that change first,
  and left it to #570.

The objects are the same bytes.

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

### What's left

At about 512 MB, with the three, by arena (smaps at the compile's first large
release, on the laptop):
- **The checker's scratch, 184 MB.** Its high-water is mostly the reader's:
  some 55 token columns, many of them sparse but cleared whole, and the binding
  table. The parse's temporaries fit under it.
- **The driver's arena, 142 MB.** It holds:
  - the reader's token columns that the later stages read, 61 MB, many filled
    with `NONE` where few tokens have an entry;
  - the expression graph, with a 96-byte `CheckValue` leaf for each of its
    343,946 nodes (33 MB);
  - the binding environment that #113's deferred reader takes, which a
    source-order compile doesn't read.
- **The checker's output arena, 72 MB.** Most of it is the expressions'
  stream, with a 96-byte value per node; the declarations' and the program's
  outputs are the rest.
- **The front end:** the preprocessor's 47 MB, and the parser's 13 columns,
  each in an arena of its own: about 31 MB by their sizes.

These were measured on #520's head with the three changes, before main took
#568 (the reader, #529) and #517. `abi_initialize` cleared ABI rows for every type the table could hold, about
37 MB of the checker's scratch, inside what the reader touched: user time
rather than new pages, which #517 sizes by the types.
[#572](https://github.com/buster14a/buster/issues/572) follows up on the rest:
sizing the reader's columns by their records, as #528 proposed, and the graph's
and the stream's values. It shares the reader with
[#529](https://github.com/buster14a/buster/issues/529).

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
  it reads, two cache lines of each 96-byte record. The `leaves` column holds
  a record for every node, 33 MB on `sqlite3.c`, which only the leaves use.
  Its layout is the reader's ([#529](https://github.com/buster14a/buster/issues/529))
  to change.
- **The actions.** The rest of the graph run is the actions and their
  per-node structures, on either path.
- **The program pass.** Most of what remains of it is the bodies' builds and
  their reference walks, one statement at a time.

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
so their alias/retain refusal is preserved. These are representation/work reductions; fresh 9700X timing
and small/large corpus comparisons are required before assigning them a speedup.

Selection retains its direct object-indexed scratch maps and computes its root/inline policy where it is
consumed. A compact sorted-key variant moved policy construction into static-data preparation, but the complete
producer/consumer comparison did not demonstrate a benefit. Keeping that extra metadata and lookup work is
therefore deferred; the representation changes above do not depend on it.

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

## Linking is not in the 194.8 ms

**`-c` doesn't link at all.** The 194.8 ms of #136's speedtest1 record, and the
189.6 ms here, are `compiler -c sqlite3.c -o sqlite3.o`: it ends when it has
written the relocatable object. Building and writing that object takes 0.57 ms
(0.3%); the `executable` stage, the direct path's link, is zero.

**The record's link is a separate step.** It linked both objects with `cc` (GCC's
driver and GNU ld) in 14.2 ms. That is 6.4% of the two-file build's 221.8 ms,
and no part of the 146.9 ms by which `-c` trails tcc's `-c`.

**The time is in the checker and the lowering.** Optimizing a linker, ours or
the system's, can't touch them.

The direct path (`compiler -o` of one unit), which links in the same process,
isn't measured here: its input is a different unit (speedtest1's with
`sqlite3.c`), and its link is work `-c` doesn't do. Its stages would show in
the `executable` row.

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

The ordinary arena's 64 KiB base-page prefix is retained. A 1 MiB-prefix
experiment reduced some huge-page clearing but increased base-page faults and
whole-process time on both small units and SQLite in the local screening run.
It is excluded from the cleanup change; the issue's remaining setup directions
need separate target measurements.

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
- **Lanes.** The driver compiles the functions on lanes (#151), by default as
  many as their tokens pay for, up to one per CPU it may run on. On one lane, the
  calling thread takes the per-function laps above. On several, a thread's laps
  wouldn't add up across threads, so the lanes' whole run is one stage, `lanes`,
  and the stages from `lower` to `unwind` keep only the calling thread's passes
  around it. The 9700X step pins every compile to CPU 2, so its functions take
  one lane. This document's figures were measured before the lanes merged, on
  the calling thread alone, which a pinned compile still is.
- **The preprocessor's phases** inside its stage come from its own `PPOptions.timing`.
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
  ranked.

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
