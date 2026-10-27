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
   - Follow-up: [#528](https://github.com/buster14a/buster/issues/528).
3. **The checker's graph and program passes**, 35.7 ms (18.9%):
   `check-expressions` 20.0 ms and `check-program` 15.7 ms, with no system
   time.
   - Upper bound: 35.7 ms.
   - Follow-up: [#530](https://github.com/buster14a/buster/issues/530), which
     took 8.4 ms off them: [below](#the-checkers-graph-and-program-passes-530).
4. **The lowering to the typed stream**, 25.7 ms (13.6%): 19 ms of user time,
   five times Tier 0's compile of the stream it produces (5.0 ms).
   - Upper bound: about 19 ms of user time; its 6.5 ms of system time is #528's.
   - Follow-up: [#531](https://github.com/buster14a/buster/issues/531).
     [#151](https://github.com/buster14a/buster/issues/151)'s per-function jobs
     are its multicore route.
5. **`x86_validate` on every Tier 0 record**, about 5 ms (2.7%).
   - `branch_sizing_validate` validates each function's records before sizing
     them, which makes the branch-sizing stage (5.6 ms) 15 times the encoder's
     (0.36 ms). `--verify` exists for that check.
   - Upper bound: about 5 ms.
   - Follow-up: [#532](https://github.com/buster14a/buster/issues/532).

The rest are small: preprocessing (9.8 ms) and parsing (9.6 ms) have their own
issues (#117, #126, #451); Tier 0 takes 5.0 ms; the object's build and write
take 0.6 ms.

## The checker's graph and program passes (#530)

**Measured, base against head on the 9700X.** Medians of 15 interleaved
`--time-stages` runs of each build, pinned to CPU 2. Every object is the same
bytes, by both paths. [The measurement record](https://github.com/buster14a/buster-performance/tree/2f36c27c24ffcea2317721c0337544d025f98fb8/docs/measurements/checker-passes-530-2026-10-27)
holds every run, the script and the builds' identities.

| | base | head | change |
| --- | ---: | ---: | ---: |
| `check-expressions` | 20.262 ms | 15.515 ms | -23.4% |
| `check-program` | 15.936 ms | 12.235 ms | -23.2% |
| the clock's span | 190.517 ms | 181.889 ms | -4.5% |
| user instructions (perf stat) | 2,227.3 M | 2,031.6 M | -8.8% |
| user cycles (perf stat) | 901.3 M | 854.3 M | -5.2% |

The two stages' ranges don't overlap (20.0 to 21.2 ms against 15.4 to 17.0,
and 15.6 to 16.3 against 12.1 to 12.4), and every other stage is within 0.2 ms
of base's median.

**The barrier wasn't the cost.** On the frozen `sqlite3.c`'s 343,555 graph nodes
(the record's laptop counts):
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
    declarations, the reached builtins, and the static objects and symbols.

On `sqlite3.c`'s 319,193 body nodes the graph run went from 55.2 to 40.2 ns a
node on the 9700X (303 to 223 cycles).

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

## How it is measured

**The stage clock.** `--time-stages` (`c_check_clock.c`) makes the C driver
report, on standard error after the command, a row per stage.
- **Laps.** A lap charges the TSC ticks since the previous lap to the stage it
  names. So the stages never overlap, and they add up to the clock's span, from
  the driver's first instruction to its last release. The TSC's rate is
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
