# Tier 1 allocates each function once (#2538)

Status: implemented, all four steps; "What was built and what it measured"
below has the code and its 9700X figures, in
[the implementation's record](https://github.com/buster14a/buster-performance/tree/5c7dcf7c68f18a9ec722368af6e53737546a081e/docs/measurements/tier1-one-allocation-2538-2026-11-12).
The design follows as it was written. Its measurements, and the prototypes that
back its gains, are in
[the design's record](https://github.com/buster14a/buster-performance/tree/94aadb4968b7523793291ee41fbafd2aa09a9c75/docs/measurements/tier1-compile-cost-138-2026-11-12),
whose patches are measurement code, not this design's implementation. It is the
compile-cost side of [#138](https://github.com/buster14a/buster/issues/138):
what Tier 1's pipeline costs a compile, by stage, and the change that the
stages' measured bounds rank first. The issue is
[#2538](https://github.com/buster14a/buster/issues/2538). The names of
existing code are main's at `c5f0efb2`, and every figure is that revision's on
the 9700X (CPU 2, one lane, under the lock) unless it says otherwise.

**In one paragraph.** `--tier1 -c` of the frozen `sqlite3.c` takes 426.1 ms
against the default compile's 124.0 ms, and 298.6 ms of it is Tier 1's pipeline:
66 stages, none above 12%. Register allocation is the largest, 84.6 ms, because
1,584 of the 2,221 functions that reach the CFG allocator are allocated twice:
once plainly, then again whole, to give the values that live across a call the
callee-saved registers. The second allocation is taken every time it is asked
for (3,172 times in three corpora), and it rebuilds what the first one built:
the same validation, liveness and intervals, while the first one's lookup
columns and edge moves are thrown away. The design is one allocator call for
both problems: everything up to the intervals once, the plain assignment only
as far as its verdict needs, the preferred assignment over the same intervals
with another column of registers, and lookup, slot sharing and edge moves once,
for the allocation that stands. The objects don't change by a byte. Prototypes
of the order measure `sqlite3.c` at 400.6 ms from 425.9 (−6.4% user cycles) with
the plain assignment kept whole, and at 393.0 ms (−8.3%) with its verdict taken
from a prefix of the events inside the preferred assignment's call; the objects
are byte-identical on `sqlite3.c`, Lua's 33 units and musl's 1,336. Today's
order stays as the twin and the oracle.

## What was built and what it measured

Four commits, one per step, steps 1, 2, 4 and 3 in that order:

1. `tier1_cfg_allocate` is `tier1_cfg_prepare`, `tier1_cfg_assign` and
   `tier1_cfg_finish` in a row, the preparation's state on the scratch arena in
   a `Tier1CfgPrepared` until `tier1_cfg_release`.
2. The CFG consumer allocates both problems in one call
   (`tier1_emit_cfg_one_call`): another problem's column over the prepared
   intervals (`tier1_cfg_column`, with `tier1_cfg_input_valid`'s two checks on a
   value's registers as a vector kernel and a scalar twin), and lookup columns,
   slot sharing and edge moves once, for the allocation that stands. Today's
   order is `tier1_emit_cfg_two_calls`: the path for fixed ranges, and the
   reference `--verify` runs after the one-call order on every function,
   comparing the allocations field by field (`tier1_cfg_differs`), the problems
   each left and the values each moved.
4. The preference counts the calls before each position
   (`tier1_emit_cfg_calls_before_vector`, a prefix sum with a running-count twin)
   and tests each interval with two loads; the reference order keeps the search.
3. `tier1_allocate_verdict` takes the plain problem's verdict inside the
   preferred assignment's call, from the events below the last bound
   definition's end, or all of them where the class has fewer registers than its
   most required uses at one position; `tier1_cfg_order_prepare` and
   `tier1_cfg_order_run` are the order, the plain problem assigned whole only
   where the preferred allocation isn't taken. The preference gives each value's
   set without its definition's registers, which `tier1_cfg_preferred` adds while
   the definition is bound.

`test-tier1-cfg` and `test-tier1-cfg-source` run every case as two problems,
in one call and in two, on both host paths, with definitions unbound, bound at
random and bound in clashing pairs (two values required at one position given
one register), which drives the constraint path. The constraint path is taken in
3 functions of `sqlite3.c`, 2 of Lua and none of musl, as the design counted.

On the 9700X (main `33f3f442`, each tree at four placements of the backend's
code, CPU 2, one lane, the record's method), the objects are main's, byte for
byte, on `sqlite3.c`, Lua's 33 units and musl's 1,336. User cycles against
main's, paired by placement, and wall time:

| corpus | main | step 2 | and step 4 | and step 3 |
| --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 427.0 ms | 400.9 ms, −6.31% | 398.9 ms, −6.83% | 391.7 ms, −8.63% |
| Lua, 33 units | 168.4 ms | 163.5 ms, −2.86% | 163.0 ms, −3.21% | 162.5 ms, −3.59% |
| musl, 1,336 units | 1,377.8 ms | 1,365.5 ms, −0.94% | 1,363.5 ms, −1.23% | 1,359.1 ms, −1.48% |

Step 1 alone is −0.18% on `sqlite3.c` (−0.14% to −0.21% over placements);
step 3 takes 1.93% more of `sqlite3.c`'s cycles than step 4. Step 4 takes
11.8 M cycles and 2.0 ms from the compile, of the callee-saved step's 20.0 M
and 3.5 ms above, which leaves about 1.5 ms: inferred from the whole compile,
not measured by stage. Peak resident memory falls on all three: `sqlite3.c`
299,048 to 292,744 KiB, Lua 102,692 to 90,764, musl 149,314 to 148,544, each the
median over a tree's four executables. A peak counts the executable's own file
pages, which differ by about a megabyte between two files of the same code; one
executable per tree read musl's 1.4 MB higher with step 2, which the four
placements' files didn't. The expected case was 393 ms on `sqlite3.c` and
162.2 ms on Lua; the optimistic, 389 ms.

## The problem

### What `--tier1` costs

Main `c5f0efb2`, Release, the default host; whole processes pinned to CPU 2
(one lane) or CPUs 0 to 7 (eight), each configuration writing its objects in a
directory of its own, in a Williams design's order. Medians of 7 runs (musl: 5)
with the range of the wall times; peaks from a pass without `perf`. The
eight-lane rows are from [a second record](https://github.com/buster14a/buster-performance/tree/c7e42f773b04c88f8030103e3bcf396b556e12fa/docs/measurements/tier1-eight-lanes-2555-2026-11-12),
which ran the same commit, scripts and inputs again once
[#2555](https://github.com/buster14a/buster/issues/2555)'s stray process was
gone. That process had looped on core 0 beside the first record's eight-lane
runs, which read 3% (`sqlite3.c`) to 36% (Lua) high. In the second record one
run of musl's eight-lane default row spent about 250 ms off the CPUs, hence its
range; that row's second round ranges from 204.3 to 208.4 ms.

| corpus | lanes | compile | wall ms | user cycles, M | user instructions, M | peak MB |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 1 | default | 124.0 (123.4 to 149.3) | 599.3 | 1,347.5 | 235.1 |
| | 1 | `--tier1` | 426.1 (425.5 to 426.9) | 2,257.1 | 5,278.7 | 295.8 |
| | 8 | default | 112.2 (111.8 to 113.4) | 636.0 | 1,348.4 | 279.7 |
| | 8 | `--tier1` | 166.7 (166.0 to 168.2) | 2,339.3 | 5,280.7 | 454.1 |
| Lua 5.4.8, 33 units | 1 | default | 91.7 (91.5 to 104.7) | 439.9 | 1,165.0 | 64.5 |
| | 1 | `--tier1` | 167.5 (167.4 to 180.8) | 827.7 | 2,014.2 | 103.1 |
| | 8 | default | 27.2 (26.8 to 27.6) | 666.7 | 1,201.2 | 217.9 |
| | 8 | `--tier1` | 40.1 (39.8 to 41.1) | 1,019.2 | 2,048.7 | 231.1 |
| musl, 1,336 units | 1 | default | 1,052.9 (1,052.3 to 1,080.4) | 4,747.0 | 12,756.1 | 142.8 |
| | 1 | `--tier1` | 1,352.4 (1,350.9 to 1,354.1) | 6,208.1 | 15,145.0 | 148.7 |
| | 8 | default | 206.2 (205.0 to 453.4) | 5,645.7 | 12,780.0 | 289.3 |
| | 8 | `--tier1` | 253.0 (251.4 to 257.8) | 7,112.5 | 15,166.9 | 293.5 |

Tier 1 compiles 2,668 of `sqlite3.c`'s 2,668 functions, 1,087 of Lua's 1,088
and 2,755 of musl's 2,756. On one lane `--tier1` is 3.44 times the default
compile on `sqlite3.c`, 1.83 on Lua and 1.28 on musl, where 1,336 units' front
ends dilute it. On eight lanes it adds 54.5 ms to `sqlite3.c`'s compile (166.7
against 112.2) where it adds 302.1 ms on one, since the pipeline runs per
function on the lanes; the cycles it adds are about the same.

### Where the pipeline's time goes

The pipeline is `check_tier1_compile`, once per function. A measurement patch
stamps the TSC at each stage's end inside it (66 stages) and at each of the
driver's; a second pass samples the hardware counters with times
(`perf record -T -k CLOCK_MONOTONIC_RAW`) and puts each sample in the stage and
the function its time falls in. Unlogged, the patched executable runs the
compile in 2,261.0 M user cycles against 2,258.9 M, and its objects are the
unpatched one's. `sqlite3.c`, medians of 5 logs for the times and means of 6
sampled runs for the counters; a stage's bound is the compile if the stage cost
nothing:

| stages | ms | of the compile | the compile without it | M cycles | IPC | slots waiting for the frontend | whose |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| **register allocation**: the first call | 48.4 | 11.3% | 378 ms | 272.7 | 2.24 | 45% | [#1399](https://github.com/buster14a/buster/issues/1399), [#1384](https://github.com/buster14a/buster/issues/1384) for parts |
| the second call | 32.7 | 7.7% | 393 ms | 184.3 | 2.74 | 39% | none: this design |
| the callee-saved step between them | 3.5 | 0.8% | 423 ms | 20.0 | 2.38 | 45% | none: this design |
| **emission**: the allocation problem | 18.2 | 4.3% | 408 ms | 104.0 | 2.29 | 47% | [#1379](https://github.com/buster14a/buster/issues/1379) for its checks, [#1356](https://github.com/buster14a/buster/issues/1356) |
| saves and frame | 7.1 | 1.7% | 419 ms | 40.2 | 2.60 | 45% | none |
| records | 35.0 | 8.2% | 391 ms | 198.9 | 2.46 | 43% | none |
| **SSA construction** | 37.6 | 8.8% | 389 ms | 208.4 | 2.25 | 48% | [#1353](https://github.com/buster14a/buster/issues/1353) for its tables' memory |
| **instruction selection** | 33.8 | 7.9% | 392 ms | 190.8 | 2.18 | 45% | none |
| **IR verification**, six checks | 30.6 | 7.2% | 395 ms | 173.5 | 2.28 | 50% | [#1379](https://github.com/buster14a/buster/issues/1379) |
| **optimizer**: analyses, SCCP, dead code, rewrite | 30.5 | 7.1% | 396 ms | 171.3 | 2.16 | 50% | [#139](https://github.com/buster14a/buster/issues/139), [#1729](https://github.com/buster14a/buster/issues/1729) |
| **lowering to the IR** | 14.1 | 3.3% | 412 ms | 80.0 | 2.14 | 52% | none |
| inlining, wide integers, vector pieces, truth, if-conversion | 4.2 | 1.0% | 422 ms | 23.0 | 2.00 | 51% | |
| straight-line functions' whole back end | 2.1 | 0.5% | 424 ms | 12.2 | 1.64 | 63% | |
| arenas, publication | 0.8 | 0.2% | 425 ms | 4.2 | 2.34 | 52% | |
| **the pipeline** | **298.6** | **70.0%** | 127 ms | 1,683.6 | 2.31 | 46% | |

Lua and musl give the same shares: allocation's two calls and the step between
are 25.3% of Lua's 70.0 ms pipeline and 21.9% of musl's 226.0 ms, records 12.0%
and 14.7%, the six IR checks 10.1% and 9.0%. The record has every stage for the
three corpora, the counters by stage, and each function's row.

Four things in the table shape the rest:

- **It is flat.** The largest single stage, `tier1_emit_cfg_records`, is 11.7%
  of the pipeline; the largest group, register allocation, 28.3%. No one stage
  is the 3.44 times.
- **It is linear.** 476.9 ns for each of the 625,409 lowered IR instructions:
  432 to 564 ns in every size class above 64 instructions, 995 below 16.
  `sqlite3VdbeExec` is 20.7 ms, 6.9%; the eight functions of 4,096 instructions
  and more are 16.3%. Allocation alone grows with size, from 4% of a function
  under 16 instructions to 39% of those eight.
- **Verification is a tenth, and more than the table's row.** Beside the six IR
  checks, the back end validates its own inputs again:
  `tier1_emit_cfg_problem` runs `tier1_liveness_validate_plan` (15.8 M cycles)
  and, through `ir_build_cfg`, `ir_check_rows` and `ir_cfg_validated` once more
  (16.3 M); each allocator call runs `tier1_cfg_input_valid` (4.9 and 3.1 ms)
  and each assignment `tier1_allocation_input_valid` (5.4 M); the records run
  `instruction_is_valid`, the template checks and `x86_zen5_id` (31.9 M).
  About 51 ms in all, of which the 30.6 ms are #1379's.
- **The second allocator call is warm, and still 32.7 ms.** It runs the code
  the first call just ran, on the same function: its assignment retires 2.87
  instructions a cycle against the first's 2.28, with 5.4 mispredicted branches
  per 1,000 instructions against 8.1.

### What the counters say, and what was tried for it

The pipeline is frontend-bound. Of its dispatch slots (8 a cycle on Zen 5), 26%
retire an op, 17% are bad speculation, 46% wait for the frontend and 11% for the
backend; it mispredicts 6.9 branches per 1,000 instructions, 43 for each lowered
IR instruction, and misses the op cache 181 M times a compile. By function size
the frontend's share falls from 62.7% (under 32 instructions) to 33.2% (4,096
and more), op-cache misses from 257 to 34 per 1,000 cycles, and IPC rises from
1.75 to 2.93. The samples fall on at least 318 KiB of distinct code lines in
146 functions, run once per function.

Two prototypes tested the obvious readings of that, and neither is this
design's lever:

- **Stages over batches of functions** (the pipeline as phases over an item
  struct, each phase run for a batch of functions before the next; objects
  byte-identical at every batch size). `sqlite3.c`: 2,261.2 M user cycles as it
  is, 2,197.6 M at 64 functions a batch (−2.8%, 416.1 ms, peak 294.6 to
  302.5 MB), 2,180.7 M with batches closed at 16,384 typed entries (−3.5%,
  311.2 MB), and 2,219.1 M with the whole unit as one batch (703.4 MB, slower
  in wall time). The frontend's share of the pipeline's slots goes from 46% to
  43%. Lua and musl gain nothing (+0.3% and +0.6% cycles). So the starvation is
  in the stages' own short, branchy loops, not in cycling through the stages.
- **A profile-guided build** (`BUSTER_PGO=1`,
  [#880](https://github.com/buster14a/buster/issues/880)'s, trained on these
  same `--tier1` and default compiles, so the most a profile gives):
  `sqlite3.c` by `--tier1` 426.4 to 403.5 ms, −5.5% user cycles, and its
  default compile 124.3 to 117.8 ms; Lua by `--tier1` 167.0 to 154.6 ms; musl
  1,351.6 to 1,187.9 ms, where the default compile gains as much (1,053.3 to
  916.3 ms), so it is the front end's. On `sqlite3.c` that is about 16 ms of
  the pipeline's 298.6. It is a build decision that exists already, and it
  composes with this design.

### The allocator runs twice

`tier1_emit_cfg_function` allocates a function's values over its CFG in this
order:

1. `tier1_cfg_allocate` on the **plain problem**: every value may take any
   register of its class, the general class's being the eight caller-saved
   ones; a parameter is bound to its incoming register over its definition's
   interval. If the assignment meets a constraint
   (`TIER1_CFG_CONSTRAINT`: a required use finds every register it may take
   held by intervals required at the same position), every parameter is freed
   and the whole call runs again.
2. `tier1_emit_cfg_callee` reads the allocation's intervals and decides, per
   value, whether it **crosses** a call: a phi's web as one. If any does, it
   builds the **preferred problem** (crossing values in the callee-saved
   registers alone, every other general value in the caller-saved ones, a
   parameter's definition interval still bound) and calls `tier1_cfg_allocate`
   again, whole.
3. If that second allocation succeeds without a split, it replaces the first.
   Otherwise the first stands.

`tier1_cfg_allocate_impl` has eight parts. What each costs in the two calls
(`sqlite3.c`, ms), and what it depends on:

| part | first call | second call | reads the registers a value may take? |
| --- | ---: | ---: | --- |
| validation (`tier1_cfg_input_valid`) | 4.88 | 3.11 | two compares per value |
| homes, dense values, phi hints | 2.12 | 1.05 | no |
| liveness | 1.98 | 1.16 | no |
| block lists | 2.88 | 1.72 | no |
| intervals | 8.66 | 6.35 | only around fixed ranges, and there are none |
| assignment (`tier1_allocate`) | 15.16 | 9.94 | yes |
| lookup columns, slot sharing | 1.95 | 1.29 | no: the assignment's result |
| edge moves | 10.79 | 8.08 | no: the assignment's result |
| all | 48.43 | 32.70 | |

What the measurements say of the second call, on `sqlite3.c`, Lua and musl:

- **It is asked for in 1,584, 725 and 863 functions and taken in every one**:
  3,172 of 3,172. No preferred allocation was refused, split or short of
  memory.
- **Its intervals are the first call's in every one** (starts, ends and values
  compared). The CFG consumer passes no fixed range (0 in the three corpora),
  and without one an interval's extent doesn't depend on the registers.
- **So 13.4 ms of it rebuild what the first call had**, and 12.7 ms of the
  first call (its lookup columns and edge moves) are for an allocation that is
  then replaced: 11.9 ms of them in the functions with a second call.
- **The plain assignment is needed for one bit.** Where a second call is
  taken, nothing of the first assignment survives but whether it met a
  constraint: 3 functions of `sqlite3.c`, 2 of Lua, none of musl. In
  `sqlite3.c` 637 functions make one allocator call, 1,581 two and those 3
  three.

No open issue covers this. #1399 is the edge moves' searches (its #1509
merged) and [#1384](https://github.com/buster14a/buster/issues/1384) the
liveness matrices' memory; both are inside one call.

## The design

### One call, two assignments

The allocator's eight parts become three functions, and `tier1_cfg_allocate`
stays as the three in a row for its other callers (the tests, the benchmarks,
the certificate's):

- **`tier1_cfg_prepare`**: validation, homes and dense values, hints, liveness,
  block lists and the intervals. It leaves its working state (the liveness
  rows, the dense map, the blocks' entry counts) on the scratch arena and the
  interval columns in the output arena, as today.
- **`tier1_cfg_assign`**: one assignment over the prepared intervals, for a
  problem given as a register set per class and an allowed set per value. The
  first problem's column is the one the interval builder wrote. For another
  problem it writes a new column (below), checks the two conditions
  `tier1_cfg_input_valid` puts on a value's registers, and calls
  `tier1_allocate`.
- **`tier1_cfg_finish`**: lookup columns, slot sharing and edge moves for one
  assignment's result, then the scratch arena back to where `prepare` found it.

`tier1_emit_cfg_function` then runs, for a function that has callee-saved
registers to give:

```text
prepare(plain problem)                          validation ... intervals, once
preference(intervals)                           which values cross a call; none: plain
verdict = plain assignment                      whole (step 2), or over its prefix (step 3)
if verdict is a constraint: free the parameters; verdict again
preferred = assign(preferred column)            over the same intervals
if preferred is OK:   finish(preferred)         the allocation that stands
else:                 plain assignment if it only has a verdict; finish(plain)
```

A function with nothing to prefer runs `prepare`, the plain assignment and
`finish`: today's one call. A function whose problem has a fixed range takes
today's order, two whole calls: there the intervals depend on the registers
(`tier1_cfg_allocate_impl` cuts a value's range around a fixed range that
blocks every register it may take), so they can't be shared. No function in
the corpora has one; the path is the rare case's exact one.

Why the result is the same allocation, part by part:

- **Prepare.** Validation's structural checks, the homes, the hints, liveness
  and the lists read the CFG, the values' definitions and uses, and the phis:
  not the registers. The intervals read `allowed_registers` in one place, the
  test against a fixed range, and the design doesn't share them when there is
  one.
- **The preferred column.** `tier1_cfg_piece` gives an interval the
  definition's register where it holds a bound definition, else the value's
  allowed set. The new column is that rule over the preferred sets: per value,
  its run of intervals takes the value's set, and the first takes the
  definition's register where one is bound. The two per-value checks
  (`allowed ⊆ registers[class]`, `definition ⊆ allowed`) are
  `tier1_cfg_input_valid`'s lines for the preferred problem; a failure is
  `TIER1_CFG_INVALID`, as today.
- **The preferred assignment** gets the same `Tier1AllocationInput` as today's
  second call: the same columns but `allowed_registers` and `registers[0]`.
- **Finish.** `tier1_cfg_install_lookup`, `tier1_cfg_share_slots`, the splits
  and the edge moves read the input's CFG and phis, the intervals, the
  liveness rows and the assignment. None reads the registers a value may take.
- **The preference step** reads the intervals, which are now `prepare`'s, and
  the same plan. It moves before the plain assignment; nothing it reads is
  written between.

What can differ is an outcome that is no allocation: a plain allocation that
today runs out of arena in its own lookup or edge moves, in a function whose
preferred allocation would have fit, fails today and compiles after. The new
order uses less of both arenas (one set of intervals, one of lookup columns
and moves).

### The plain assignment's verdict

Step 2 below runs the plain assignment whole, as today, for its verdict. It is
15.2 ms of `sqlite3.c`'s compile, and in the 1,584 functions with a preferred
allocation only the verdict is used. Two exact ways to pay less for it were
prototyped.

**From a prefix.** In the plain problem the only interval that may not take
every register of its class is a parameter's definition interval, which starts
at position 0. `tier1_allocate_scan_uses` fails in one place: a required start
or use of interval *i* at position *p* finds no free register in *i*'s set and
every register of the set held by an interval required at *p*. An interval
that may take the whole class can't fail while the class has at least as many
registers as the most required uses its values have at one position (*i* is
one of them, so at most that many less one hold registers it can't take away).
So a constraint can only be met at an event of a bound definition interval,
which is before the last such interval's end, and the scan's state at an event
depends only on the events before it. The verdict is therefore the scan's over
the events before that end, per class: a class the common path would color
(`tier1_allocation_color`: every interval free, pressure within the
registers) needs none.

In the three corpora the most required uses at one position are 7, 6 and 7
against the general class's 8 registers; where a class has fewer registers
than its most, the whole assignment runs. The prefix is short: the positions
before the last bound definition's end hold 11.6% of the plain assignment's
time in `sqlite3.c`'s functions with a preferred allocation (25.8% in Lua's,
11.8% in musl's; `sqlite3VdbeExec`: 272 of 36,756 positions).

The scan is half of an assignment's cost. The other half is
`tier1_allocate_body`'s own preparation: `tier1_allocation_input_valid`, the
counting sort of the events by position, a pass per class for its pressure. A
verdict taken by a call of its own repeats that (the plain assignment goes
from 15.0 to 8.5 ms, not to 1). Both problems have the same intervals, so the
same events: the verdict belongs inside the preferred assignment's call, which
validates and sorts once, scans each class's prefix with the plain column,
puts the scan's state back (the registers, the pieces, the hints' last
registers) and assigns the preferred problem. The most required uses at a
position come from the same pass that counts the events.

**From a bound on the pressure** (the prototype's "quiet"): no assignment at
all where, besides, the class's pressure before that end never exceeds its
registers, since then nothing is evicted and no bound interval loses its
register. It holds in 1,374 of the 1,584 functions, but they are the small
ones: 38% of the plain assignment's time. The prefix covers every function and
needs the weaker argument, so the design takes the prefix and leaves this out
(measured: −6.7% on `sqlite3.c` where the prefix by its own call gives −7.3%).

If the preferred assignment is refused after a prefix verdict, the plain
assignment runs whole then. That is today's result by a longer path, for a
case the corpora never took.

### The preference from prefix counts

`tier1_emit_cfg_callee` finds, for each of a value's intervals, the first call
at or past the interval's start by halving the function's sorted call
positions, and again for the asm bodies: 20.0 M cycles and 356,000 mispredicted
branches for `sqlite3.c`'s 244,380 intervals, 82 cycles and 1.5 mispredictions
an interval. Its test is

```text
crosses(i) = first call at or past start(i) is before end(i), and isn't the value's own definition
```

With `calls_before[q]`, the count of calls at positions below `q` (one prefix
sum over the function's positions), that is

```text
crosses(i) = calls_before[end(i)] != calls_before[start(i)]
             and not (the value is a call's result and start(i) is that call's position)
```

two independent loads and a compare for each interval, no search and no
branch. The two forms agree because a call's own result is the one value whose
interval can start at a call's position; for it, the first call at or past the
start is its own, and today's test then stops, whatever the interval covers
after. The design keeps that exactly (see "What it found on the way"). The asm
bodies' registers stay as they are, a search per interval in the functions
that have an asm body.

### Memory

One set of interval columns instead of two, one of lookup columns and moves,
and the plain assignment's fragments only where it runs whole. The preferred
column is 4 bytes an interval and `calls_before` 4 bytes a position, both
freed with the function. Peak resident memory should not rise; step 2's gate
measures it.

### The twin, the oracle and the checks

- **Today's order stays in the tree as the reference**: two whole
  `tier1_cfg_allocate` calls with the preference between them, as
  `tier1_emit_cfg_function` has them now. **`--verify` runs it after the
  one-call order** on every function and compares the two
  `Tier1CfgAllocation`s field by field (intervals, fragments, registers,
  spills, lookup columns, edge placements, moves) and the problem as each left
  it, naming the function on a difference. With a prefix verdict it also runs
  the plain assignment whole and compares the verdicts.
- **Both host paths take the one-call order**: what it saves isn't a vector
  path's. `--scalar` runs its kernels' scalar forms (below) and the
  allocator's scalar twin as now, so every existing comparison of the default
  path with `--scalar`, objects byte for byte, checks them.
- **The certificate** (`tier1_certificate_cfg_check`, #939) still checks the
  final allocation against the final problem, and `tier1_cfg_verify` the
  blocks and edges: neither knows which order produced them.
- **`test-tier1-cfg`** gains the two-problem case: for its fixtures and its
  random SSA over random CFGs, a random partition of the general values into
  two register sets, allocated by one call and by two, on both paths, results
  equal; with mutants that bind a definition to a register another required
  value needs, so that the constraint path and the prefix verdict are taken.
  **`test-tier1-cfg-source`** does the same for every function of real C.
- **The corpora**: `sqlite3.c`, Lua, musl, c-testsuite and the compiler's own
  units by `--tier1 -c`, objects byte for byte against the revision before,
  and the five functions whose plain assignment meets a constraint
  (`sqlite3GenerateRowDelete`, `sqlite3UniqueConstraint`, `windowIfNewPeer`,
  `finishbinexpval`, `newlabelentry`) by name.
- **The crossing test's twin** is today's search, kept as the scalar form and
  compared under `--verify`: the same preferred set for every value.

### Data-parallel first: what is vector and what is ordered

Nothing here is a new sequential stage; the design removes work and reorders
it. Its new kernels, each with the scalar form it is checked against:

- **The preferred problem's checks**, 16 values a step: the allowed sets and
  the definition registers loaded, the class's register set by an in-register
  lookup of a three-entry table, two `andnot`s and a test into a mask. Twin:
  `tier1_cfg_input_valid`'s two compares per value.
- **`calls_before`**: a 0/1 byte per position (the calls' positions are
  already a sorted list) and a prefix sum, 16 positions a step, with the
  carry in a register. Twin: the running count.
- **The crossing test**: branch-free and independent per interval, two loads
  from `calls_before`. It stays scalar loads on purpose: sixteen intervals'
  loads as two `vpgatherdd` are about 25 cycles on Zen 5, 1.6 an interval,
  against scalar loads that don't depend on each other and issue three a
  cycle. What it replaces is the cost: a dependent, mispredicting search of 82
  cycles an interval.
- **The preferred column**: a value's run of intervals filled with its set,
  the definition's interval overwritten where bound. A fill by runs, not a
  gather by `values[i]`, for the same reason; the intervals are in value
  order. 244,380 stores for `sqlite3.c`.

What stays ordered, and why:

- **The assignment's scan** (`tier1_allocate_scan_uses`) takes one event at a
  time because each choice reads the registers' state after the last one: the
  free set, the claims of the intervals starting at the same position, and an
  eviction by farthest next use. 99.6% of `sqlite3.c`'s intervals take it
  (243,410 against 970 on the coloring path): a parameter's bound interval
  makes its class irregular, and nearly every function has one. It costs 348
  cycles an interval in the plain assignment (85.0 M cycles, 6.4 mispredicted
  branches an interval) and 241 in the second, which has 230,411 of them. The
  design doesn't make the scan data-parallel; it makes the ordered part
  smaller: one whole scan a function and a prefix, where there were two whole
  scans. A vector form of the choice itself is the allocator's own redesign,
  not this one.
- **The phi webs** (`tier1_emit_cfg_callee`'s union-find): `sqlite3.c` has
  9,616 phis in 2,221 functions, 4.3 a function, inside a step that is 3.5 ms
  with the searches. Pointer jumping over the parents would gather for webs
  shorter than a vector. It stays, and step 4's gate measures what is left of
  the step.
- **The events' counting sort** in `tier1_allocate_body` (a histogram over
  positions, a prefix sum, a scatter) is as it is, and runs once for both
  problems from step 3 on.

### What the prototypes measured

Four orders were prototyped on the measurement patch, switched by an
environment variable in one executable, with every configuration's objects
compared with the unpatched compiler's. `--tier1 -c`, one lane, CPU 2, whole
processes in a Williams design's order, medians of 9 runs (musl: 5), the
unpatched executable in the same design:

| order | `sqlite3.c`, ms | user cycles | Lua, ms | user cycles | musl, ms | user cycles |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| src | 425.9 | 2,258.1 M | 167.5 | 829.3 M | 1,350.1 | 6,208.2 M |
| two | 428.4 | +0.44% | 168.6 | +0.55% | 1,359.0 | +0.83% |
| shared | 400.6 | −6.39% | 162.9 | −2.78% | 1,344.4 | −0.38% |
| prefix | 397.5 | −7.11% | 163.1 | −2.64% | 1,344.2 | −0.41% |
| inline | 393.0 | −8.25% | 162.2 | −3.21% | 1,341.4 | −0.62% |
| peak MB, src to inline | 295.9 to 288.4 | | 104.3 to 91.0 | | 149.9 to 147.2 | |

Every order's objects are the unpatched compiler's, byte for byte: 0 of 1, of
33 and of 1,336 differ. In the one-call orders 2,218 of `sqlite3.c`'s
functions make one allocator call and the 3 with a constraint two.

- **src** is the unpatched executable, the reference.
- **two** is the patched executable in today's order. It also computes, in
  every function, a statistic today's compiler doesn't (whether the plain
  assignment provably meets no constraint, about 1.9 ms on `sqlite3.c`), which
  is why it isn't the reference.
- **shared** is step 2: one allocator call, the plain assignment whole.
- **prefix** is the verdict from the events before the last bound definition's
  end, by a `tier1_allocate` call of its own.
- **inline** is step 3: that verdict inside the preferred assignment's call.
  It still computes the most required uses with a pass of its own in the
  callee-saved step (1.8 ms on `sqlite3.c`), which the design folds into the
  events' count.

An earlier executable measured the pressure bound ("quiet") in the same way:
−6.7% on `sqlite3.c`, −2.8% on Lua, −0.8% on musl, byte-identical. And the
first prototype put the preferred allocation first and ran the plain one only
on a refusal, without sharing anything: −5.7% on `sqlite3.c`, and the five
functions whose plain assignment meets a constraint came out larger
(`sqlite3GenerateRowDelete` 1,382 to 1,742 bytes). The verdict is not
optional.

The allocator by what it does, `sqlite3.c`, ms (medians of 3 lap logs of each
order; in the one-call orders lookup and edge moves are the allocation's that
stands):

| | two | shared | prefix | inline |
| --- | ---: | ---: | ---: | ---: |
| validation to intervals, the first call | 20.7 | 20.8 | 20.9 | 20.8 |
| validation to intervals, the second call | 13.6 | 0.0 | 0.0 | 0.0 |
| the plain assignment, or its verdict | 14.9 | 15.0 | 8.5 | 1.1 |
| the preferred assignment | 10.0 | 9.6 | 10.6 | 14.2 |
| lookup columns and slot sharing | 3.1 | 1.8 | 1.8 | 1.8 |
| edge moves | 19.3 | 9.8 | 9.9 | 9.8 |
| the callee-saved step | 5.5 | 3.6 | 5.4 | 5.4 |
| **the allocator** | 86.9 | 60.7 | 57.1 | 53.3 |
| **the pipeline** | 300.5 | 272.5 | 268.9 | 265.3 |

In the inline order the plain assignment's row is the 637 functions with
nothing to prefer, and the preferred assignment's row holds the verdicts'
prefix scans.

What runs after a removed part gets colder: with the plain lookup and edge
moves gone, the preferred allocation's edge moves take 9.8 ms where they took
8.1 after the plain ones, and after a verdict-only call the preferred
assignment takes 10.6 ms instead of 9.6. The model below uses the measured
orders, not sums of the parts.

## The steps and their gates

Each step is one pull request with its 9700X numbers, main against head, in
its body.

1. **Split the allocator** into `tier1_cfg_prepare`, `tier1_cfg_assign` and
   `tier1_cfg_finish`, with `tier1_cfg_allocate` and its scalar twin as the
   three in a row. No caller changes.
   *Gate:* objects byte-identical on the corpora; `test-tier1-cfg`,
   `test-tier1-cfg-source`, `test-tier1-allocation` and `test-tier1-pipeline`
   pass on both paths; `--tier1 -c sqlite3.c` within ±0.3% user cycles of
   main over several placements of the backend's code
   (`time-runs --build --copies`), since the split moves a 53 KiB function's
   code.
2. **One call for both problems**, today's order kept as the reference under
   `--verify`, and the two-problem cases in the tests.
   *Gate:* objects byte-identical (the corpora, c-testsuite, the compiler's
   own units); `--verify` clean on the corpora; user cycles of
   `--tier1 -c` down at least 5% on `sqlite3.c` and 2% on Lua, and not up on
   musl; peak resident memory not above main's.
3. **The verdict from the prefix, inside the preferred assignment's call**:
   the intervals validated and their events sorted once.
   *Gate:* byte-identical again, with the five constraint functions checked by
   name; `--verify`'s whole plain assignment agrees with every prefix verdict
   on the corpora; at least 1% more of `sqlite3.c`'s user cycles.
4. **The crossing test from prefix counts.**
   *Gate:* the same preferred set for every value on the corpora (the twin
   under `--verify`); the callee-saved step at most 1.5 ms on `sqlite3.c`
   from 3.5.

Steps 3 and 4 are independent of each other; both need 2.

## Expected gains

`--tier1 -c`, one lane, as wall time from main's 426.1 ms (`sqlite3.c`),
167.5 ms (Lua) and 1,352.4 ms (musl), with the change in user cycles. The
expected case is what the prototypes measured against the unpatched
executable. The conservative one is step 2 alone at four fifths of its
measured gain, for what another layout of the split allocator may cost. The
optimistic one adds step 4 at its gate and takes out the pass the prototype
runs for the most required uses, both at their measured sizes.

| case | what it assumes | `sqlite3.c` | Lua | musl |
| --- | --- | ---: | ---: | ---: |
| conservative | step 2 at 80% of the prototype | 406 ms, −5.1% | 163.8 ms, −2.2% | 1,348 ms, −0.3% |
| expected | steps 2 and 3 as prototyped | 393 ms, −8.3% | 162.2 ms, −3.2% | 1,344 ms, −0.6% |
| optimistic | and step 4, and the prototype's own pass gone | 389 ms, −9.2% | 161.4 ms, −3.7% | 1,341 ms, −0.8% |

musl's figures are low by construction: 1,336 units' front ends are 83% of
its compile, and the patched executable is 0.8% slower than the unpatched one
there before any order is switched on, which every row carries. Against the
patched executable's own order the inline one is −1.4% on musl.

Eight lanes should gain the same cycles and less wall time: there the
pipeline adds 54.5 ms to `sqlite3.c`'s compile, so the expected case's 11% of
its work is about 6 ms of 166.7. It isn't measured: the prototype keeps its
state in globals and runs on one lane.

Generated code does not change in any case.

## What would refute it

- **One function, anywhere, whose allocation differs** between the orders
  under `--verify` or in the tests' random problems: the sharing argument is
  then wrong somewhere, and the step that introduced it goes back.
- **A whole plain assignment that meets a constraint its prefix didn't.** The
  prefix argument is then wrong, and step 3 falls back to the whole assignment
  (step 2's gain stays).
- **Step 2 measuring under half the prototype's gain** on the 9700X over
  placements. The prototype's order is the design's, so that would mean the
  split's own cost, and the answer is to keep the allocator one function with
  the second assignment inside it, as the prototype has it.
- **Preferred allocations that are often refused** in some workload. The
  design then pays for a plain assignment after a prefix it could have run
  once. `--tier1-report` should count asked, taken and refused; three corpora
  say 3,172, 3,172 and 0.
- **The CFG consumer starting to pass fixed ranges.** Those functions take
  today's order, and the gain shrinks by their share.

## Risks and open questions

- **Code-quality work on the same step.** #138's quality side and
  [#1116](https://github.com/buster14a/buster/issues/1116) own what the
  callee-saved step decides. This design changes when it runs and what it
  costs, not what it decides; a change to its rule lands in one place either
  way, but the two should be sequenced.
- **Layout.** The pipeline is frontend-bound, and `tier1_cfg_allocate_impl` is
  53 KiB of code. Step 1's gate is there for that.
- **The scratch arena's lifetime** spans the two assignments and the
  preference step, where today each call restores it. Capacity is the same or
  lower; the failure paths need the same care as today's.
- **A constraint's second verdict.** After the parameters are freed nothing is
  bound, so the plain problem can't meet a constraint again while the class
  has the registers; where it hasn't, today's whole call decides.
- **Other callers.** The straight-line consumer takes no callee-saved register
  and is not touched. If it gets the preference (#1116 lists it), it gets it
  through `assign`.

## What it found on the way

Each is somebody else's lever or a separate fix; #138's result comment and
the record carry the numbers.

- **A call's own result that crosses a later call in the same interval is not
  counted as crossing** (`tier1_emit_cfg_callee` tests the first call at or
  past the interval's start alone). `long x = f(); long y = g();` followed by
  a loop keeps `x` in `rax` and stores it around `g()`, where
  `long x = f() + 5;` gets `rbx`. A code-quality finding for #1116; this
  design reproduces the test as it is.
- **The back end's validations** above (about 21 ms beside #1379's 30.6), and
  `ir_build_cfg`'s second `ir_check_rows` inside the allocation problem.
- **`--tier1`'s `check-program` is 7.8 ms slower** than the default's (31.6
  against 23.8 ms): [#1355](https://github.com/buster14a/buster/issues/1355)'s
  lifetimes.
- **An object written over a longer one waits 20 ms** for the file system's
  truncation on the benchmark host (`object_write_file` truncates only when
  the file was longer): two configurations timed in one directory read
  `--tier1` 447 ms instead of 426.

## What it leaves

Ranked by what was measured, on `sqlite3.c` by `--tier1 -c`:

| lever | measured | whose |
| --- | --- | --- |
| this design | −6.4% of the compile's user cycles with one call, −8.3% with the verdict inside it; the objects unchanged | #2538 |
| a profile of `--tier1`'s compiles | −5.5%, trained on the measured inputs | a build decision; #880's mode |
| the six IR checks | 30.6 ms if all were free, 7.2% | #1379 |
| stages over batches | −2.8 to −3.5%, more memory | none; not proposed |
| the back end's own validations | about 21 ms if free | none; with #1379's rule |
| records, selection, SSA construction, lowering | 35.0, 33.8, 37.6 and 14.1 ms of work, no measured waste | none |

After this design the allocator is still the largest stage, at about 50 ms
of `sqlite3.c`'s compile: the intervals, one assignment and one set of edge
moves. Its next lever is the scan itself.

## Related issues

- [#138](https://github.com/buster14a/buster/issues/138): Tier 1; this is its
  compile-cost side.
- [#1379](https://github.com/buster14a/buster/issues/1379): the IR verified
  about ten times.
- [#1399](https://github.com/buster14a/buster/issues/1399),
  [#1384](https://github.com/buster14a/buster/issues/1384),
  [#1356](https://github.com/buster14a/buster/issues/1356),
  [#1353](https://github.com/buster14a/buster/issues/1353),
  [#1729](https://github.com/buster14a/buster/issues/1729): the pieces
  inside single stages.
- [#1116](https://github.com/buster14a/buster/issues/1116): what the
  callee-saved step decides.
- [#880](https://github.com/buster14a/buster/issues/880): the frontend's
  share, and the profile-guided build.
- [#672](https://github.com/buster14a/buster/issues/672) and
  [#2524](https://github.com/buster14a/buster/issues/2524): the default
  compile's one-core path, whose method the stage counters here follow.
