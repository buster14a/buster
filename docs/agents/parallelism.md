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
  local thread creation. Variable-duration work uses an atomic take-index,
  writes function- or item-local fragments into stable source-indexed slots,
  and merges them only after a lane barrier.
- **A global built on first use is prewarmed, never raced.** Several hot
  tables are derived once and read forever after through plain loads — the C
  frontend's character classes, compact lexer tables, punctuator dispatch and
  declaration keywords, the codegen ABI target cache, the font provider's
  resolved paths, and the x86 metadata decode plus the three caches over it.
  That shape is sound only while no other thread can be reading, so every such
  build states `BUSTER_CHECK_SERIAL_INITIALIZATION()` (`<buster/lib/os.h>`,
  always compiled in, over `os_is_only_live_thread()`), and every owning
  module publishes a prewarm entry point that fills it on the calling thread:
  `c_prewarm`, `codegen_prewarm`, `font_provider_prewarm`,
  `buster_x86_metadata_prewarm`, and `compiler_prewarm` for the compile
  pipeline as a whole. **Call the prewarm
  before `lane_run`** — x86 metadata separately, since its prewarm costs a
  full table decode. That decode is on the compile path:
  `codegen_prewarm_for_target` runs it for every x86-64 module, and the
  encoder's fallback rows still query forms per row, so the cost is the
  per-invocation floor of every `ide cc`, not an optional extra. Its per-form
  caches — the normalized row, the parsed pattern, the operand views, the
  derived facts and each record's validity — fill on the first *serial* touch
  of each form, so a caller about to hand the tables to a gang must call
  `buster_x86_metadata_prewarm_all_forms()` first, which fills every one of
  them. A new lazily built global adds both the check and a line in its
  module's prewarm.
  Publish the flag *after* the state, never before: the x86 metadata decode
  needs two flags for this, one guarding re-entry from the reads its own
  layout checks make through the accessors and one, set last, that every
  accessor tests. Spelling the character-class
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

Do not parallelize functions by copying the TU design blindly: signature plans,
IR source cursors, module line suppression and inline-assembly symbol changes
are shared within a TU. Serial table prewarm does not remove these dependencies.
`compiler_parallel_prewarm()` is the complete native-C prewarm entry for a
caller launching an external gang; idle persistent workers still count as live.
