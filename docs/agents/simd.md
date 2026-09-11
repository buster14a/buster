# Data-oriented design and SIMD

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Current compiler priority

Prioritize compiler throughput and producing an artifact as soon as possible.
Do not add optimization passes or spend compile time improving generated code
unless that work is explicitly requested. Prefer direct, predictable lowering
and code generation with minimal analysis overhead.

The long-term implementation direction is to make the compiler broadly
SIMD-friendly: prefer compact contiguous data, batchable lane work, masks for
tails and irregular active sets, branch-regular loops, and layouts that can
evolve from scalar to vector processing. Reducing branch and cache misses is
therefore an architectural goal as well as a present-day performance lever.
Keep the measurement discipline explicit: record SIMD-readiness and miss-rate
improvements, but do not describe a scalar throughput regression as a current
speedup merely because a proxy improved.

### Data-oriented compiler design

- Optimize the real workload, especially its common cases, rather than every
  theoretically possible case. Organize and measure the data before optimizing
  the code that transforms it.
- Where there is one, look for many — including along the time axis. Before
  designing a scalar one-off path, look for the population across instructions,
  values, blocks, functions, modules, repeated queries, or successive compiler
  phases, and preserve enough context to process that population together.
- Put frequently processed values together and separate different states and
  cold metadata. Maximize useful information per cache line; avoid large hot
  objects, padding, scattered flags, and pointer graphs that spend bandwidth
  carrying data the current operation does not use.
- Replace per-object, last-minute decisions with explicit classification
  followed by homogeneous batch processing. Prefer masks, compact indices,
  active counts, command buffers, state-specific arrays, and other forms that
  can evolve into SIMD lanes over repeatedly testing independent booleans.
- Do not reread or recompute facts already available. Hoist invariant reads,
  calls, and branches even when an optimizing compiler might discover the same
  transformation. Make constraints explicit so hot interfaces retain the
  context needed to avoid generic work.
- Precompute offline or during serial prewarm whenever practical. The cheapest
  runtime operation is one that no longer exists; keep cold malformed-input and
  fallback handling outside regular hot loops where the public contract allows
  it.
- Estimate before implementing: organize the cases, rank them by
  `probability * count`, calculate approximate memory and compute cost, then
  measure. Track useful information per cache line and passes per item, not
  merely time attributed to a function.
- Apply the process recursively. After improving one hot data transformation,
  inspect the next largest remaining source of waste. Preserve maintainability,
  debuggability, determinism, concurrency, and portability while doing so;
  good data organization should improve all of them.
- For buster, favor SoA or compact projections for hot compiler facts, explicit
  active-lane masks and counts instead of fixed-maximum scans, grouped machine
  instruction states where ordering permits, and prewarmed compact plans rather
  than repeated metadata interpretation. Treat AVX-512 as a consequence of a
  sound data layout, not the starting point: a compiler can optimize the final
  instruction sequence, but it cannot repair wasteful data movement or context
  that the program discarded.

### SIMD transformation rules

- Choose the data layout before choosing SIMD instructions. Prefer SoA for
  homogeneous transforms, use hybrid/tiled layouts only when measurements show
  a scalar/SIMD tradeoff, and locally project fixed external AoS input into the
  compact form a hot transform needs. Public interfaces do not have to mirror
  internal storage.
- For small divergent operations, compute candidate results and select with a
  mask. For larger or more expensive divergence, first partition or compact
  stable indices, then run a dedicated homogeneous kernel over each set. A hot
  data-dependent branch needs strong measured predictability; a predictable
  loop branch is not a problem merely because it is a branch.
- Treat compare masks as useful data. Prefer compare -> mask -> compact/select
  pipelines, advance compact outputs by mask population count, and use
  precomputed permutation controls when a direct compress instruction is not
  available. Preserve stable order whenever output order is observable.
- Do not add software prefetch to linear streams the hardware already handles.
  Consider it only for measured, sufficiently distant irregular pointer/index
  accesses, and validate every supported microarchitecture. Likewise, avoid
  habitual unrolling beyond the natural SIMD width: inspect generated code,
  register pressure, and spills first.
- Use non-temporal loads or stores only to prevent demonstrated cache pollution,
  after the ordinary kernel is correct. Their ordering and visibility require
  an explicit fence and ownership proof; they are never a cosmetic replacement
  for normal memory operations.
- Inspect the generated instructions for every explicit SIMD kernel. Count
  useful lanes, loads/stores, shuffles, dependencies, spills, and work per
  iteration. Wider code is a win only when the measured bottleneck is compute
  rather than memory bandwidth and the target hardware executes that width
  economically.

- **Microarchitecture tuning target: design for Zen 5's native 512-bit
  width; Zen 4 must break even, Zen 5 collects the upside.** The main
  development machine is a Ryzen 9 7940HS (Zen 4) and the CI x86-64 runners
  cover both generations (the Windows runner is a Zen 5 box), so
  single-thread throughput decisions — SIMD width, table sizes,
  branch-vs-cmov trades — are made for and measured on these cores.
  GNU-family builds compile with `-march=native`, so clang-built binaries on
  these hosts already have AVX-512 (VL/BW/DQ/VBMI/VBMI2) available. Write
  kernels at full 512-bit width: Zen 4 executes them double-pumped over its
  256-bit datapaths at AVX2-equivalent bytes per cycle with fewer
  instructions retired — no downclocking, no penalty, just no width upside —
  while Zen 5's native 512-bit units roughly double the same code's
  throughput. A kernel therefore has to justify itself at Zen 4's effective
  width and is validated there, but its shape (masks, compaction, permutes)
  is chosen for Zen 5. Watch the exceptions on Zen 4: the few instructions
  whose 512-bit form costs more than 2x the 256-bit form (check uops.info
  before leaning on exotic two-source permutes; `vpcompressb` is safe at
  ~9 cycles Zen 4 / ~5 cycles Zen 5), store-throughput-bound kernels (one
  256-bit store per cycle on Zen 4), and unaligned 64-byte loads — keep hot
  streamed buffers 64-byte aligned. Explicit intrinsics must stay behind
  feature/compiler guards with a scalar or SWAR fallback: MSVC builds carry
  no `-march`, aarch64 (macOS/Android/iOS) must keep building, and the
  self-hosted `ide cc` stages compile the same tree without vendor headers —
  performance is only ever quoted from clang-built binaries, so fallback
  paths need correctness, not speed.
- **Write 512-bit kernels in the vocabulary of `<buster/lib/simd.h>`, not in
  `<immintrin.h>`.** That header is a target-fixed list of AVX-512 operations
  — masked 512-bit loads and stores, byte comparisons producing a `Mask64`,
  `vpermt2b`, `vpcompressb` and its compacting store, byte-to-word widening,
  `vpternlogd`, lanewise arithmetic — with three implementations behind one
  spelling: `__builtin_buster_simd_*` for the self-hosted stages, host
  intrinsics for clang and gcc, and a scalar fallback for MSVC, AArch64 and
  pre-AVX-512 x86. `BUSTER_SIMD_512` says which one is in play; guard on it
  only where a *different algorithm* is worth writing, never merely to keep
  the tree building. It is deliberately not a portability layer — every
  operation names one instruction, and an abstraction wide enough to also
  describe NEON would lose exactly the operations that make these kernels
  fast. Adding to it means adding an `IrSimdOperation`, its arity in
  `ir_simd_operation_shape`, its validation, its EVEX lowering in
  `codegen_canonical_x64_simd_operation`, the builtin in `c_ir_simd_builtins`,
  its spelling in `c_symbol_predefined` (`c_source.c` — without it the
  self-hosted stages fail with "could not lower unbound identifier"),
  a fallback, and a case in `tests/basic_c_simd.c`; the shape table is the
  single source of truth that keeps a new operation from being half-taught to
  the pipeline. Two consequences are worth knowing before writing a kernel.
  **Everything in the header is a macro**, because `ide cc` lowers directly
  and runs no inliner — not even for `always_inline` — so a function wrapper
  would be a real call per SIMD operation in the self-hosted stages;
  arguments must therefore be free of side effects. A `Simd512` may still
  cross a call boundary by value where that is what the code wants: SystemV
  passes and returns it in a vector register and spills past the eighth, and
  a target whose vector registers are narrower than the value says so with
  `CODEGEN_ERROR_UNSUPPORTED_ABI` rather than encoding a register it does not
  have. And **masks are `u64`, not an opaque k-register handle**: the
  canonical backend allocates no registers, so a mask spills to its frame slot
  either way, the vector instructions pick it back up with a single `kmovq`,
  and mask shifts, Boolean combinations, `mask64_count` and `mask64_first_set`
  retain integer semantics across general-purpose/predicate register bridges.
  Canonical shape metadata specifies the internal predicate width and mask
  operand/result boundaries. The complete generic/exact opcode classification,
  feature refusal rules, and cross-target representation contract are in
  [vector semantics](../ir-vector-semantics.md).
- **SIMD C lexing method: the Validark lineage.** `c_lex_compact` in
  `frontend/c/c_source.c` draws on Niles Salter's (Validark's) Accelerated Zig
  Parser — local checkout `~/dev/Accelerated-Zig-Parser`, upstream
  `github.com/Validark/Accelerated-Zig-Parser` — plus validark.dev (start with
  `posts/deus-lex-machina` and
  `posts/eine-kleine-vectorized-classification`) and the three Utah Zig talks
  (YouTube `oN8LDpWuPWw`, `FDiUKafPs0U`, `NM1FNB5nagk`). When accelerating the
  C lexer, reach for these techniques first and keep new code compatible with
  their shapes. The core vocabulary: (1) per-64-byte-chunk classification —
  every class (whitespace, newline/CR, alpha/digit/underscore, quotes,
  backslash, slash, operator chars) becomes one comparison into a `k`-mask
  bitstring, all classes over the same chunk in lockstep sharing one load; (2)
  token extents by mask arithmetic — starts `x & ~(x << 1)`, ends
  `x & ~(x >> 1)`, then either cursor queries (shift by cursor, count trailing
  ones — one tzcnt replaces an unpredictable byte loop) or full vector
  compaction: `vpcompressb` an iota vector through the starts and ends masks,
  subtract, and every token length in the chunk materializes at once; kinds
  come from masked broadcasts into a kinds vector compressed by the same
  starts mask, interleaved with lengths on store; (3) charset membership via
  nibble-decomposed `vpshufb`/`vpermb` tables —
  `table[c & 0xF] & (1 << (c >> 4))` per lane, the upper-nibble powers-of-two
  vector shared across charsets, `vptestmb` folding the AND and test on
  AVX-512; (4) multi-character operators as a bit-channel `vpshufb` NFA: three
  per-position tables whose looked-up bytes AND together so each of the 8
  bit-channels legalizes one family of 2-/3-char sequences, plus a short
  effectively-branchless reconciliation of overlaps; (5) keyword and builtin
  recognition by perfect hash, never a memcmp ladder —
  `((len << 14) ^ first_two_bytes) * last_two_bytes >> 8` to 7 bits, Bagwell
  array-mapped compression (two u64 bitmaps + popcount rank) into a dense table
  of length-padded entries validated by a single wide compare, with
  compile/startup-time collision checks so editing the keyword set stays safe;
  (6) sentinels instead of bounds checks — a leading newline, trailing
  quote/NUL sentinels, and chunk-aligned overallocation let the hot loops drop
  every length test; (7) upper-bound allocation plus post-scan shrink instead
  of grow-and-check (the `2026-08-08k` tokenizer change is this trick alone,
  `-21.6%` bench instructions); (8) length-based token streams — kind + length
  with a 0-length escape to a wide length, no per-token start offsets or
  eagerly materialized line/column, offsets rebuilt by a running cursor and
  line numbers recovered by popcount over retained newline bitmasks.
  Escape-run parity uses the simdjson backslash algorithm (simdjson PR #2042
  has the current best form). All of it sits behind the guard rules of the
  tuning target above; scalar fallbacks keep the exact current semantics.
  `c_lex_compact` walks source in **item-aligned 64-byte windows** so no lexer
  state crosses a window: the item touching a window's last byte is deferred
  and rescanned by the next one, and shapes the masks do not model escape to
  the scalar single-item scanner `c_lex_scan_one`. C skips whitespace and
  comments instead of tokenizing them, so token ends need an explicit boundary
  mask rather than `starts >> 1`; lookahead loads are masked by the **file**
  bounds rather than the window's because a delimiter near the window end can
  be spelled from bytes the next window owns. Every SIMD lexer change must keep
  the differential gate asserting byte-identical agreement with the scalar
  reference over construct cases slid across the window boundary, items longer
  than a window, the real corpus at every window phase, and fuzz blobs over the
  full alphabet.
