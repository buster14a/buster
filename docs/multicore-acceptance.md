# Multicore acceptance

This is the integration checklist for [#149](https://github.com/buster14a/buster/issues/149),
not a declaration that its remaining performance acceptance has passed.
The implementation audit started at `ce606a6ef1f61793dc955717d75e29de9165e04a`.
See [function lanes](function-lanes.md), [translation units](translation-units.md),
[determinism](determinism.md), and [Tier 1](tier1.md) for the implementation contracts.

## Actual lanes, not just requested lanes

The correctness manifest's CPU allocation is a scheduling budget, not an
affinity mask. A title's `[N CPUs]` tells the parallel runner how many CPUs its
command keeps busy, so that it doesn't start too many commands at once; the
driver itself clamps its lanes to `sched_getaffinity`. Both multi-file commands
spawn children that each run up to eight lanes while their manifest entries
declared two CPUs, so the runner was free to co-schedule other two-CPU commands
on the same cores. Both entries now declare eight CPUs. A smaller hosted runner
still supplies only its available CPUs; that is correctness coverage, not the
9700X's full eight-core qualification, and the harness reports the upper count
it actually tested.

`tests/driver/multicore.c` adds the mixed Tier 1/Tier 0 consumer of the existing
lane machinery. The shared correctness suite builds and runs it; it does not
create another pool, workflow, benchmark host or compiler build configuration.

The test generates an original program with 256 externally visible integer
functions, loops and conditional control flow, and a caller of every function.
Two constructs keep the fixture mixed: `fallback` reads an unnamed argument
through its incoming variadic frame, and calls `widen` through a volatile
function pointer, whose floating conversion Tier 1 also refuses. It compiles
that program as one source and as two sources, into objects and executables,
through the default and scalar host paths. Each path has its own one-lane
reference: Tier 1's scalar and vector allocators may legitimately choose
different code.

For every available lane count from one through eight, it requires:

- Complete object/executable byte equality against that path's reference, and
  identical aggregate Tier 1 reports. Single-source repeats perturb identifier
  IDs; multi-source repeats poison the reusable arenas.
- A successful compiler exit and a report with both Tier 1 and Tier 0
  functions. A regression that refuses the fixture or silently compiles
  everything at Tier 0 is not a passing test. If a later Tier 1 covers every
  function, the harness exits 77 with a message naming the fixture to
  re-anchor, rather than failing a correctness run.
- `--time-stages` receipts whose `lanes-asked` and actual command `lanes` both
  equal the tested count. These establish the command team, not utilization or
  speedup. The test reports its actual upper count, rather than calling a
  two-CPU hosted run an eight-lane qualification.
- Successful execution of every linked reference and candidate against a
  closed-form arithmetic result, independent of the generated loops' execution.
- A link of the `-c` cohorts' objects with the host `cc`, run against the same
  result, so that individually deterministic objects that don't form a working
  program still fail.

Every compile first removes its output paths. The harness checks each child's
own `waitpid` status, retries interrupted waits, bounds child runtime, rejects
empty or missing output, and retains the temporary input/output directory on
failure. It does not use an operand-free shell `wait` or let stale objects prove
success. Its stage-clock output is used only for the team receipt; no elapsed
time is treated as benchmark evidence.

The large source is padded past the units scheduler's size threshold, and its
many function bodies supply parallel work. The units driver budgets working
lanes by `units + source_bytes / 32768`; `body.c` is padded to six 32-KiB units
so the two-source cohorts admit eight lanes, while `single.c` needs no padding
because a single `--lanes` command already asks for every lane it may run on.
This avoids an accidentally tiny fixture whose useful-work rule never admits the
requested team. It does not claim that every admitted worker executes the same
amount of work.

The mixed-tier anchors are deliberate, and the harness does not depend on a
particular one staying refused. Tier 1's selection refuses variadic definitions
whole (`tier1_select.c`) and floating-point conversion (`uitofp` among them),
and [the Tier 1 guide](tier1.md) lists both among the constructs still refused.
If a later Tier 1 covers every function in the fixture, the harness prints
`SKIP (Tier 1 covered every function)`, says which file to re-anchor, and exits
77, which the shared runner reports as skipped. A regression that leaves Tier 1
with no compiled function still fails.

## What still constitutes parent-issue acceptance

A green correctness run is necessary, not sufficient, to close #149.
The published function and translation-unit guides contain useful historical
curves, but they describe different revisions and selected lane counts. Do not
splice them into a purported fresh 1-through-8 curve for one revision.

The final acceptance record must identify one compiler revision and frozen
inputs for the single-unit SQLite case, the many-unit synthetic case
(512 files of 200 functions) or the specified SQLite source-tree alternative,
and musl. It must retain exact commands, source identities, output comparisons,
actual lane/CPU receipts, the host's governor and wait policy, repeated samples
and their variability. Run performance qualification on the approved 9700X,
CPUs 0 through 7 with one hardware thread per core, under the shared host lock.
Store raw runs and measurement harness artifacts in `buster14a/buster-performance`,
not in this compiler repository.

Report the one-lane serial-stage share and its Amdahl bound separately from
observed whole-process speedup. An effective serial fraction fitted from an
N-lane speedup also includes scheduling and memory-system overhead; it is not a
measurement that the corresponding source stages are inherently serial.
Record the body-checking decision against current measurements rather than
assuming that a deferred-reader test means the production driver uses it.

The translation-unit guide also records two important limits that this change
does not erase: the rejected global prewalk experiment is not a measurement of
a concurrent process-wide shared cache, and the historical #777 two-process
comparison whose child statuses were not verified is not valid performance
acceptance. Their owning issues retain those decisions and evidence requirements.

## Provenance

`tests/driver/multicore.c`, its generated fixtures and this guide are original
project work under `Apache-2.0 WITH LLVM-exception`. The fixture and closed-form
oracle import no third-party implementation or corpus. Existing compiler and
lane-module licenses are unchanged. Human contribution certification remains
subject to `CONTRIBUTING.md`; generated authorship is not a human DCO sign-off.
