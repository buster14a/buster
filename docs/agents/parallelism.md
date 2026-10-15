# Parallelism and serial prewarm

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

- **Parallelism target: SPMD across threads, SIMD within each thread.**
  Implementation code is written multi-core-by-default in the lane model of
  Ryan Fleury's raddebugger (`lane_run`, `lane_index`/`lane_count`,
  `lane_range`, `lane_sync`, `lane_broadcast` in `<buster/lib/os.h>`): every
  lane of a gang runs the same code, narrows serial work with
  `if (lane_index() == 0)`, and splits parallel work by `lane_range` or an
  atomic take-index — not through job systems, task queues, or callbacks.
  Lane-style code must degrade to serial: `BUSTER_SINGLE_THREADED` builds and
  one-lane gangs run the identical path with no threads and no separate serial
  variant. Within a lane, prefer wide data parallelism in the style of Daniel
  Lemire's simdjson kernels — process data in blocks, classify with tables and
  bitmasks, iterate set bits — over per-element branching, and shape data the
  way Casey Muratori advocates: flat index-linked arrays in arenas transformed
  by batch passes, not pointer graphs walked element-at-a-time. Wojciech
  Muła's notes (`http://0x80.pl/notesen.html`; fetch over plain HTTP — the
  host's HTTPS certificate is broken) are the project-endorsed catalogue of
  these kernel shapes: before writing a byte- or word-granularity loop, check
  whether one of his notes already solves it branchless with masks, `vpshufb`
  or `vpermb` lookups, `vpternlogd`, or compaction. Start from "Modern
  perfect hashing for strings", "SIMD-ized check which bytes are in a set",
  "AVX512VBMI — remove spaces from text", "AVX512VBMI2 and packed varuint
  format", "Parsing decimal numbers" parts 1–2, "SIMD-friendly algorithms
  for substring searching", and "AVX-512 conflict detection". Parallel
  stages must stay deterministic — write results into slots indexed by work
  item, never by completion order — so the self-hosting fixed point stays
  byte-identical at any lane count. `lane_run` keeps a persistent worker gang
  on the calling thread context and reuses it across phases; do not add phase-
  local thread creation. Gang construction is transactional: new workers stay
  behind a startup gate until every barrier and thread exists, and a partial
  construction is cancelled, joined, and destroyed before owner state is
  published. Variable-duration work uses an atomic take-index,
  writes function- or item-local fragments into stable source-indexed slots,
  and merges them only after a lane barrier.
- **A global built on first use is prewarmed, never raced — unless each
  entry is filled through a once-claim.** Several hot tables are derived once
  and read forever after through plain loads — the C frontend's character
  classes, compact lexer tables, punctuator dispatch, declaration keywords
  and symbol-table prototype (built on a process's second serial table, so a
  one-compile process never builds it), the codegen ABI target cache, the
  font provider's resolved paths, and the x86 metadata decode. That shape is
  sound only while no other thread can be reading, so every such build states
  `BUSTER_CHECK_SERIAL_INITIALIZATION()` (`<buster/lib/os.h>`, always
  compiled in, over `os_is_only_live_thread()`), and every owning module
  publishes a prewarm entry point that fills it on the calling thread:
  `c_prewarm`, `codegen_prewarm`, `font_provider_prewarm`,
  `buster_x86_metadata_prewarm`, and `compiler_prewarm` for the compile
  pipeline as a whole. **Call the prewarm before `lane_run`.** A new lazily
  built global adds both the check and a line in its module's prewarm.
  Publish the flag *after* the state, never before: the x86 metadata decode
  needs two flags for this, one guarding re-entry from the reads its own
  layout checks make through the accessors and one, set last, that every
  accessor tests.

  The exception is a table whose entries are filled one at a time on first
  use, where a compile reaches a few hundred of thousands of entries and a
  complete prewarm would cost more than the work (#53): the x86 per-form
  metadata caches (string-pool chunks, the iclass/iform name indexes,
  records, validity, NUL distances, normalized rows, parsed patterns, operand
  views, form facts, the physical-view memo, exact plans)
  and the machine encoder's closed expansion shapes, dense GPR tables,
  variable-memory cells and fixed-template rows. Their entries fill from
  whichever thread reaches them, serial or on a lane, under this contract:
  - **Per-entry publication.** Each entry has a state, read with an acquire
    load (`atomic_u8_load_acquire` and siblings in `os.h`, a plain move on
    x86-64) and published with a release store after the entry's storage. A
    reader uses an entry only once its state is final; the hot read path takes
    no lock.
  - **Once-claims.** A fill that must not run twice claims the entry with
    `once_claim_u8`/`once_claim_u16`: the compare-exchange winner fills only
    that entry and calls `once_publish_*` (or publishes the pending code to
    abandon the claim); every other caller waits for the final state. An entry
    whose whole value is one byte and a pure function of its key, such as a
    record's validity, is stored with a release store and needs no claim; so
    is a bit in a word-sized set (`atomic_u64_or_release_plain`) whose payload
    every writer stores identically.
  - **Entry-local, pure fills.** A fill writes only its own entry (or a
    lock-free leaf such as the NUL-distance bitmap), and stores a pure
    function of the generated tables and its key, so a lane-time fill stores
    what a serial one would. Shared counters are atomic adds. The only
    order-dependent values are exact-plan slot numbers (and their token
    integrity byte) and memo slot positions, and neither reaches an artifact.
  - **Level order.** A thread holding a claim may only claim strictly lower
    levels (`BusterX86OnceLevel`: template, machine table, shape, TLS/GOT
    envelope, plan, facts, pattern, row, operand view, record, memo), so every waiter waits on a
    holder that can only wait lower, and no cycle exists. `os.c` always
    checks the nesting and fails on a thread waiting on a claim it holds, so
    a new edge that breaks the order fails instead of hanging.
  - **The serial path is the same path.** A serial compile fills the same
    entries in the same order with the same values; the extra cost is one
    compare-exchange per first fill. `BUSTER_SINGLE_THREADED` compiles the
    atomics to plain accesses.

  A64 semantic numeric blocks follow the same once-publication contract in
  their existing decoded buffers. A cold lookup claims one disjoint block
  at leaf level 1, decodes only immutable generated bytes and publishes its
  state last. It claims no other entry. The serial whole-table prewarm uses
  the same block path, then publishes global readiness after all blocks;
  it never rewrites an already published block. Cold callers do not write
  that global flag. The semantic storage regression races cold fills on a
  temporary lane owner and joins its workers before later serial prewarm.

  Every shape pins the form key the selector picks, one
  `machine_x64_metadata_shape_pins` row per distinct shape in registration
  order, so its first lookup prepares that key without a selector search. A
  row whose folded signature is not its shape's refuses the shape cache, and
  `machine_x86_64_exact_prewarm_all()` re-checks each pin against the
  selector and refuses the cache on a disagreement (#1125).

  What stays serial is still the per-process floor of every x86-64 compile:
  `codegen_prewarm_for_target` runs `buster_x86_metadata_prewarm` (including
  the decode) and `machine_x86_64_exact_prewarm` once per process, before the
  first function is encoded. On the Ryzen 7 9700X it costs 1.74 ms of the
  compiler's own time and 6.6 M retired instructions above an AArch64 compile
  of the same one-function unit, more than half of that x86-64 compile, and it
  lies outside the A/A interval
  ([`2026-10-13T160944Z`](../performance-audits/2026-10-13T160944Z.md)). A
  batched or lane-parallel process pays it once; one process per translation
  unit pays it for every unit. `-fsyntax-only` and `-E` never reach the
  prewarm unless `-fmetrics-out` requests the driver's setup (#4135).

  The TLS and GOT relocation recipes fill one group at a time under a fill
  claim that returns to idle after each fill, and publish their prepared and
  valid masks with release stores. The whole-table fills over these caches —
  the decode, the coverage decode that shares the blob scratch, the
  forwarding and NOP recipes and the `*_all` walks — stay serial-checked.
  `buster_x86_metadata_prewarm_all_forms()` and
  `machine_x86_64_exact_prewarm_all()` still prepare every entry, for the
  audits and the test harness; a gang does not need them. Plain stores order
  the writes of a serial fill only; they are not a release barrier.
  State that failure paths may read from several lanes at once is different:
  `is_debugger_present` publishes one atomic tri-state after its probe, and
  `os_is_only_live_thread` reads the live-thread count through
  `atomic_u64_load`, which is a real atomic load on every build. Spelling the character-class
  tables as constant initializers over a predicate macro would remove four of
  these outright, and was measured at **+178.8 M stage-1 instructions
  (+3.5%)** for 112 k extra preprocessed tokens — one predicate expansion per
  byte value per table. A real generator writing the bytes into source would
  not cost that; the preprocessor doing it at every compile does.

## Native C link cohorts

The opt-in driver consumer is documented in [driver.md](driver.md#opt-in-native-translation-unit-lanes).
It reuses `lane_run`, not a new pool or dispatch system. A worker-sized cohort
has no queued second TU to claim: each participating lane owns one input
slot, and ordered publication releases that cohort before the next one. An
atomic take-index would add contention without changing ownership in this
bounded slice. Extending to more pending inputs must evaluate dynamic claims,
full-TU/fragment lifetimes, grain size and memory together; no load-balancing
speedup is established by this implementation.

Function lanes do not copy full translation units. The codegen coordinator
publishes canonical CFGs and module-assembly labels before the gang. Workers
share canonical rows and the immutable type projection, but own ABI pages,
call-signature plans, source cursors and bounded machine fragments. Encoding
runs on these private fragments; module line suppression, debug locations,
relocation resolution and code layout publish serially in emission order.
The one-lane caller enters the same preparation kernel with its original
program and per-function scratch, then encodes in place. An enclosing TU gang
clamps function workers to one. Serial prewarm alone would not remove any of
these ownership dependencies.
`compiler_parallel_prewarm()` is the complete native-C prewarm entry for a
caller launching an external gang; idle persistent workers still count as live.
Its x86 part is the serial compile's own `codegen_prewarm_for_target` plus
`buster_x86_metadata_prewarm_for_lanes()` (the serially built forwarding and
NOP recipes, which every compile reaches); the per-form caches, TLS/GOT
recipe groups and machine tables fill on the lanes.

The driver's own cohorts prepare only their target family. To prepare another
family later, the owner of a parked gang calls `lane_release()` outside any lane
region. The calling thread is then the only live thread again, the serial
prewarm runs, and the next `lane_run` recreates the workers. Releasing never
helps while another caller's threads are alive.

`buster_x86_metadata_exact_plan_prepare` claims the form's slot entry on an
unprepared key and builds the plan in local storage before it publishes the
slot. A prepared key validates and returns immutable process-owned state
without writing the cache, so repeated preparation is safe on active lanes and
after the gang parks.
The registered driver cache-lifetime fixture permutes work identities and
batch boundaries through the same lane kernel and checks fixed encoding bytes;
the unit-batch fixture also compiles valid input after each failed cohort.
These tests do not require linked images to retain their layout after changing
command-line input order. Within a fixed input/options/path cell, every byte
and stable diagnostic must remain identical; worker counts and timing counters
are execution telemetry, not artifact identity.

Arena reuse pools are per thread (`arena_pool_head` in `arena.c`): a destroyed
arena parks on the destroying thread and only that thread's `arena_create`
can take it back. A pooled arena that one lane fills and another thread
releases must therefore be created by the releasing thread. The TU cohort's
coordinator creates each slot's arena before `lane_run` and destroys it after
ordered publication, so at most one TU arena per slot circulates. Creating it
on the lane reserved a fresh arena on every worker lane in every cohort and
parked each one on the coordinator, up to `ARENA_POOL_LIMIT`; no worker ever
reused one.

Lane startup allocates worker records and handle storage before acquiring the
startup gate. Allocation failures may enter the fatal diagnostic path, so the
gate protects thread publication and cancellation without enclosing allocation.

## ThreadSanitizer gate

Byte-identical self-host output and ASan/UBSan do not see a publication bug
such as a plain read of a byte another lane accesses atomically; #53's lane
first fills found one only under a ThreadSanitizer build. The
[`generate --tsan`](build.md#build) tree and its `test_tsan` target are the
gate for this class: run them after changing a once-claim, a lane kernel, a
shared counter, or anything a `-fcompile-jobs` cohort reaches.

`test_tsan` runs `os_tests` (gang construction, barriers, broadcasts and
once-claim nesting) and `compiler_driver_tests`, whose fixtures spawn `cc`
children that link several translation units at several `-fcompile-jobs`
widths: `compiler_driver_test_lane_first_fills` reaches the cold x86 caches
from every lane at once, and `compiler_driver_test_unit_batches` covers
failed cohorts and ordered publication. Children inherit the fatal
`TSAN_OPTIONS`, so a race in one fails its fixture instead of only printing
a report. Test-only instrumentation that lanes reach must be thread-local or
atomic, like the work-ledger counters and `c_ir_label_find_probe_total`
(#3948). Do not add suppressions to make the gate pass.
