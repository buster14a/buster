# Performance audit notes

Performance audit history for this repository, newest first. Every entry is a
record of what was measured, what was fixed, and what the numbers were at the
time; the methodology for taking new measurements — which benchmark to trust,
how to profile the sanitized and Release trees, how to symbolize — stays in
the "Benchmarking and diagnostics" section of `AGENTS.md`.

**Read the newest entry before starting performance work.** It carries the
reference points the next audit is measured against, the finds that were
deliberately left untaken, and the mistakes an earlier audit already paid for.
When an audit lands, add a new dated entry at the top and leave the older ones
as written — they are a record, not documentation to keep current.

`2026-08-10f` (Linux x86_64, Zen 4 7940HS; not a throughput audit — an ABI
feature costed against the gate. **Stage 1 `5.3442 G` to `5.3516 G`
instructions (`+7.44 M`, `+0.14%`)** against the parent commit `e8586c1`, of
which `~1.45 M` is the unit being compiled growing `18.396.357` to
`18.401.349` bytes; instructions per byte `290.503` to `290.828` (`+0.11%`),
per preprocessed token `3773.411` to `3777.595` (`+0.11%`), is the work
itself. Fixed point deterministic, `test_all` green in Release and in Debug.)

- **What moved.** x86-64 SystemV and Darwin can now pass and return a vector
  wider than any register the target owns. `ir_classify_abi_value` classifies
  a vector by the psABI rule alone — a 512-bit vector is one
  `IR_ABI_CLASS_VECTOR` part whatever the machine — so the canonical backend
  is where the target gets a say. Two helpers say it once:
  `codegen_canonical_x64_vector_part_registers` reports how many consecutive
  registers a part occupies and how much each carries, and
  `codegen_canonical_x64_abi_value_in_registers` answers whether the value
  fits the registers its classification named. A return and a call result
  split across as many registers as it takes — `xmm0`–`xmm3` for a 64-byte
  vector without AVX, `ymm0`/`ymm1` with it; an argument that does not fit one
  register is passed in memory, because it competes for a shared pool a return
  does not. Both are what clang emits for the same declaration, verified by
  linking the two compilers' halves of one program at all three widths, both
  directions, at 32 stack offsets each and under `gdb`.
- **What it cost.** Three predicate calls per argument — the call layout, the
  callee's own argument, and each argument before it in the callee's
  register-accounting walk, which was already quadratic in the parameter
  count. The cost is the calls, not their work: making the answer for a
  scalar part a single `size > 16` compare instead of a CPU-feature-set walk
  moved nothing measurable, and the figure only came down (`-1.8 M`, measured
  before this was rebased) when the two 48- and 56-byte structs stopped being
  passed by value. That is the floor for this shape. **If this tenth of a
  percent is wanted back**, the shape is to fold the predicate into the
  classification the backend already reads, so the answer is computed once per
  type per convention where `IrTypeAbi` caches it rather than once per
  argument occurrence — which needs that cache keyed by target and not only by
  convention.
- **What this depended on.** The stack-passed half of it only works because
  `2026-08-10a` aligned the System V outgoing argument area first. This work
  found that gap and left it — a stack-passed 512-bit vector read back with
  `vmovaps zmm` agreed or faulted by where the stack happened to be — and
  `2026-08-10a` took it, which is why `vector_ninth`'s spilled ninth argument
  is deterministic here and was a lottery when it was written. The two land
  in either order; only together do they make the fixture's nine-vector shape
  mean anything against another compiler's object.

`2026-08-10e` (Linux x86_64, Zen 4 7940HS; the retry `2026-08-10d` left
untaken, built and measured. **Stage 1 `5.3955 G` to `5.3443 G` instructions
(`-0.95%`), which is the whole of `10d`'s walk and a quarter percent more —
the result sits below `10d`'s own parent, `5.3537 G`, and not merely below
`10d`.** Fixed point deterministic, `test_all` green in Release and in
sanitized Debug, and the objects both wide-argument fixtures produce are
byte-identical to the ones the walk's reserve produced for all sixteen
target/fixture pairs the driver test builds.)

- **The measurements.** Four `test_self_host --config Release` runs on one
  tree, one machine, one session, quoted per token as well because the source
  moves slightly between variants. The walk, which is `10d` as merged,
  `5.395535 G` / `3809.236` instructions per token — that reproduces `10d`'s
  own gate figure exactly. The flat reserve with no retry, which is this change
  with the doubling removed and both wide fixtures failing, `5.359087 G` /
  `3784.383`. This change, `5.344287 G` / `3773.478`, with a second run at
  `5.344313 G` for the reproducibility band. So the walk costs `0.68%` on this
  tree and this change gives back `0.95%`.
- **What was built.** `codegen_generate_canonical_module` is now a wrapper: it
  checks the target, prepares the program ABI and validates the IR once, then
  snapshots the arena and calls an attempt function that generates the whole
  module into a code buffer reserved at a scale times the flat per-instruction
  estimate. When the attempt reports that the *code buffer specifically* ran
  out, the wrapper rewinds the arena and generates again with twice the room.
  The signal is one `bool*` on `CodegenBuffer`, written only where
  `codegen_emit_u8` refuses a byte, so the capacity failures a bigger buffer
  cannot fix — an out-of-range frame displacement, a frame past `UINT32_MAX`, a
  reserve already at the limit of a `u32` offset — are reported as they stand
  instead of being recompiled up to twenty-eight times. The reserve's own
  `UINT32_MAX` ceiling is what ends the doubling; the `10d` and `2026-08-10`
  `.p2align` padding guards are what keep an exhausted buffer terminating
  rather than hanging. The `.p2align` and over-aligned-argument reserves stay:
  both are computed in loops that already run, and neither is a walk.
- **The retry is free because it almost never fires.** Not once on the
  self-host unity translation unit, which is the gate; twice (scale 4) for
  `basic_c_wide_argument.c` on Win64 and once (scale 2) for the same fixture on
  x86-64 SystemV. A module that needs it pays one extra generation of itself
  and nothing else pays anything.
- **The flag cost `+0.21%` written inline, and the fix was to move the refusal
  out of line.** Reporting it inside `codegen_emit_u8` — a load, a branch and a
  store, on a path never taken — measured `5.370319 G` / `3791.940`, `+0.21%`
  over the flat reserve with no retry at all and `+0.49%` over what shipped.
  `codegen_emit_u8` is inlined into every emitter in the backend and the unity
  build emits nineteen megabytes of code through it, so growing its body
  changes inlining decisions all over the emitters; the cost is per emitted
  byte, not per refused one. Reporting from a
  `BUSTER_COLD BUSTER_PRESERVE_MOST` helper, and moving the `buffer->error`
  store into it as well, made the hot body *smaller* than it was before this
  work and landed `0.28%` under the no-retry reference — which is where the
  extra quarter percent in the headline comes from. This is the trap the
  nested-ternary label fix paid in the C frontend's hot per-token predicate,
  which is where `BUSTER_PRESERVE_MOST` came from; assume it applies to any
  edit inside `codegen_emit_u8`, however cold the new code looks, and A/B it
  on the same corpus rather than reasoning about which branch runs.

`2026-08-10d` (Linux x86_64, Zen 4 7940HS; not a throughput audit — a
correctness fix costed against the gate, recorded here so the next audit does
not read the step as a regression of its own. **Gate: stage 1 `5.3537 G` to
`5.3955 G` instructions (`+0.78%`)** against the parent commit `2997c5a`, not
against `2026-08-10c`'s tree; fixed point deterministic, `test_all` green in
Release and in sanitized Debug. **The retry it leaves untaken is built in
`2026-08-10e`.**)

- **What moved.** The module code buffer was reserved at a flat 48 bytes per
  IR instruction on x86-64 (128 on aarch64), but an instruction that moves an
  aggregate encodes a load and a store per eightbyte, so its size grows with
  the type and not with the instruction count. Two 64-byte vectors by value
  was already more code than a whole small module was given, and the buffer
  ran out. `codegen_generate_canonical_module` now adds a reserve for the
  values wider than an eightbyte that each instruction can move — its own and
  every operand it reads — which costs one walk of the instruction and operand
  arrays per function that holds such a value.
- **What it cost and why it was paid anyway.** The walk is the whole `+0.78%`:
  a `perf` line profile of one stage-1 compile splits it about evenly between
  filling the per-value reserve alongside the frame-slot loop that already
  reads every value's type, and the instruction and operand walk itself. Two
  cheaper shapes were measured and rejected — recomputing each operand's type
  in the walk instead of tabling it per value is worse, and splitting the table
  fill into a second value pass taken only by functions that hold a wide value
  is also worse, because most C functions hold one. Those two were measured
  before this work was rebased, against the pre-`2026-08-09h` main, where the
  shipped shape cost `+0.96%` (`5.1329 G` to `5.1822 G`) against the other
  two's `+1.59%` and `+1.27%`; the ordering is what the numbers are for, not
  the absolute values. The alternative that costs nothing in the
  common case is to keep the cheap estimate and generate the module again with
  more room when it runs out; it was left untaken because
  `CODEGEN_ERROR_CAPACITY` is also raised for reasons a bigger buffer cannot
  fix (an out-of-range frame displacement, a frame past `UINT32_MAX`), so a
  retry needs a signal that the code buffer specifically overflowed, and there
  is no return path in that function that carries one today. **If this
  percent is wanted back, that is the shape to build**: one flag on
  `CodegenBuffer` set only where `codegen_emit_u8` refuses a byte, surfaced on
  the result, and the estimate reverts to the flat reserve.
`2026-08-10c` (Linux x86_64, Zen 4 7940HS; the structural buy-back the
`2026-08-09g` entry recorded as its next step — IR source ranges stop
carrying line and column, and the four consumers that print one recover it
from the offset the range already holds. Developed against the pre-`h` main.
**The throughput figures below were measured on `05c8973`**, three points
taken back to back against the `2026-08-09j` reference points there: stage
1 `5.2514 G` instructions / `209.8 k` minor faults / `131.5 M` L1d load
misses, `instructions_per_token 3716.662`. The branch was then rebased onto
`bd754be`, where the `2026-08-10b` `c_lex` compaction moved the baseline to
`instructions_per_token 3648.608` — the deltas are the ones to read, and CI
publishes the absolutes on the current base. Correctness and the fixed
point were re-verified on the rebased tree. **Two changes, and they pull
opposite ways. The deferral is `-45.1 M` on its own (stage 1 `5.2064 G`, `-0.86%`;
`-0.94%` on the fixed-workload cross-check), `201.1 k` minor faults
(`-4.2%`), `125.9 M` L1d (`-4.3%`), emission byte-identical. Sizing
`instruction_canonical_sources` with the instructions then puts the C line
table back — `3,457` `.debug_line` rows to `529,928` — for `+235 M`, of
which `+152 M` is the position resolutions a real line table has to run.
Net on the gate: stage 1 `5.4409 G`, `+3.61%` over the baseline and
`+3.50%` per token, in exchange for debug information that was silently
absent. A `-g0` compile pays `+4.6 M` of that.**)

- **What was built.** `IrSourceRange` is `{IrSourceId source; u32 offset;
  u32 length}` — 12 bytes, down from 32. Line and column resolve through
  `ir_source_position`, over one of two frontend-neutral structures the IR
  model now owns: an `IrSourceMap` (sorted `IrSourceRegion`s over the
  frontend's byte space, per-line `IrSourceCheckpoint`s in TEXT regions and
  one stamped position per macro expansion in STAMP regions), or
  `IrSource.text` scanned with a resume cursor for frontends whose ranges
  index the parsed bytes. The C frontend's `CSourceMapEntry`/
  `CSourceMapRecovery` became that map — it is handed to the program as
  data, so the preprocessor's own `c_preprocess_token_location*` and every
  IR consumer are one lookup over one structure, and no callback crosses
  the frontend boundary. `CSourceLocation` keeps the file byte offset and
  gains `map_offset`, the spelling-space offset it was recovered from,
  which is what a range stores. Checkpoints dropped their unused `file`
  and their `u64` offset: 24 bytes to 12, and the lexer writes one per
  line of every include.
- **The prize, measured before building anything.** Returning a constant
  from `c_ir_token_source_range` was `-76.2 M`; keeping the region lookup
  but skipping the per-line checkpoint search inside the shared recovery
  was `-57.9 M`. The gap between them is the region lookup, and the `57.9`
  is *not* all deferrable: it also covers the cursorless
  `c_preprocess_token_location` that parsing calls for diagnostics, which
  has to produce a line no matter when it runs.
- **Probe counters first, and they moved the design twice.** Stage 1 runs
  `1,135,745` recovery queries from lowering (34% memo hits, 26% reaching a
  checkpoint binary search) and `955,269` canonical-source reads in
  codegen. The first working deferral measured **`+94 M`**, worse than the
  eager design, and two finds turned it around:
  - **The line table asked for a position per instruction.** Deferral moves
    the query, it does not delete it. Rejecting the repeat on the range's
    own offset before resolving — consecutive instructions overwhelmingly
    come from one token, so `955,269` reads hold only a few hundred
    distinct offsets — was worth **`-99 M`**.
  - **The region search read `start` fields 64 bytes apart.** Splitting
    `{start, source}` out of the 64-byte region into a dense
    `IrSourceRegionKey` array (eight per cache line, with a `UINT32_MAX`
    sentinel so a containment check needs no bounds test) took the
    remaining lowering-side lookup from `55 M` to `14 M`: **`-41 M`**. The
    hot query has its own entry point, `ir_source_map_source`, because
    naming the file is the region search *without* the checkpoint search
    that a full position runs after it.
- **Measured negative, do not retry as written:** checking the cursor's
  text-region slot before its stamp slot (`+1.0 M` — the stamp slot is the
  narrower of the two, so a hit there is decisive and belongs first).
- **The line table the gate was not emitting, now emitted.**
  `function->instruction_canonical_sources` was null for essentially every
  canonical function — `948,328` of the `955,269` reads codegen makes —
  because `c_lower_to_ir` preallocates `function->instructions` at a measured
  capacity and `ir_function_add_instruction` stores a canonical source only
  into an array that already exists. `939,423` of the `1,004,390` ranges the
  lowering built were computed and dropped, and a stage-1 executable carried
  `3,457` `.debug_line` rows: one per function, plus the handful from the
  functions that outgrew their preallocation. Sizing the array with the
  instructions fixes it — **`529,928` rows**, with columns that track
  sub-expressions (verified against a hand-checked fixture: a three-statement
  function now maps its operand loads, its operator and its store to the
  right columns of the right lines).
- **What that costs, decomposed.** Stage 1 goes `5.2064 G` to `5.4409 G`
  (`+4.51%`, `+4.49%` per token), and the split is not where it looks: the array and its store
  per instruction are `+4.6 M`, the `698,278` position resolutions are
  `+152 M`, and recording `520 k` rows plus encoding them into
  `.debug_line` is `+83 M`. Resolution dominates because a line table walks
  lines: about half the queries land on a different checkpoint than the last
  and pay a search. **This is the price of the debug information, not of the
  deferral** — a `-g0` compile pays only the `+4.6 M`, and the same unit
  compiles in `5.005 G` with `-g0` against `5.441 G` with `-g`, so debug
  information is now 8% of what a self-host stage costs and the gate has
  been measuring it all along without emitting it. (The three-way split was
  taken on the pre-rebase base, where the total was the same `+231 M`.)
- **Bracketing the checkpoint search by page** — the same shape
  `IrSourceMap.pages` already gives the region search, one entry per 128
  bytes of a region's text, built in the lexer's one linear pass over the
  finished checkpoints — was `-8.2 M`. `256` bytes measured `-6.4 M` and
  `1 KB` nothing, so the bracket has to be finer than a source line's
  scale to bite.
- **Measured negative on the resolution path, do not retry as written:**
  stepping the checkpoint cursor forward before falling back to the search
  (`+44 M` at a limit of 16, `+26 M` at 4). A probe said 89% of the searches
  were forward moves, which reads like a walk — but the distance is a
  statement's worth of lines, so the steps ran out and the search happened
  anyway: `4.0 M` steps bought `88 k` skipped searches. Holding the
  checkpoint cursor per region instead of per querying cursor, so a
  file/macro interleave stops discarding the file's position, was `+3.8 M`
  — the store into the 64-byte region row costs more than the resets save.
- **Verification, on the rebased tree.** Fixed point holds (`SELF_HOST
  deterministic bytes=29452352`) with `530,535` `.debug_line` rows, and the
  hand-checked fixtures still map operand loads, operator and store to the
  right columns and macro-expanded code to its invocation line — the
  `2026-08-10b` `c_lex` rewrite lands on the same checkpoints. The deferral
  on its own was emission-byte-identical: a frozen snapshot of the
  pre-change tree compiled with the pre-change and post-change compilers
  produced identical executables, unity translation unit included — the line
  table is the only thing that changed after it. `ide bench` is untouched;
  the buster tokenizer is not on this path. 24317 Release unit tests, 23420
  sanitized unit tests and 30 module tests pass. Cycles are not quoted: the
  measurements ran with other load on the box, and instructions and minor
  faults are the counters that survive that.
- **The next lever on the resolution, unmeasured and not taken.** Codegen
  resolves in emission order, which defeats the cursor; resolving a
  function's rows in offset order would answer all of them from one
  monotone walk over the checkpoints and no searches at all. That means
  recording `(code_offset, source, offset)` unresolved, ordering by offset
  — nearly sorted already, so an insertion pass — then resolving and
  compacting in code order. Rough estimate `~40 M` against the `152 M`
  standing today. It restructures how `codegen_generate_canonical_module`
  records lines and moves the row dedupe after resolution, so it belongs in
  its own session with its own fixture check.
- Reference points for the next audit, all clang-built on this host:
  stage 1 `5.4409 G` instructions / `212.2 k` minor faults / `128.8 M` L1d
  load misses, `instructions_per_token 3846.918` — all on `05c8973`, before
  the `c_lex` compaction landed under this branch — 24317 Release unit
  tests and 30 module tests, `CToken` 16 bytes, `IrSourceRange` **12
  bytes**,
  `530,535` `.debug_line` rows in a stage-1 executable, byte-identical
  fixed point (`SELF_HOST deterministic bytes=29452352`) on `bd754be`.
  Stage 2 `73.052 G`, validation only. The `5.2064 G` the deferral reached before
  the line table went in is the number to compare against if that trade is
  ever revisited.
`2026-08-10b` (Linux x86_64, Zen 4 7940HS; the structural buy-back
`2026-08-09g` recorded as the next step — the `2026-08-09c` Deus-Lex
compaction architecture ported from the buster tokenizer to `c_lex`, which
still held `~300 M` and now had a 16-byte row to store into. Every number
clang-built on a quiet machine, counter medians of 5 runs, with a
fixed-workload cross-check separating compiler efficiency from the tree's own
growth. **Stage 1 `5.1968 G` to `5.0945 G` instructions (`-102.3 M`,
`-1.97%`), branches `992.10 M` to `956.86 M` (`-3.55%`), both from a
fixed-workload cross-check on the identical tree. Fixed point
deterministic.** Landed with a prerequisite codegen fix described below,
without which the emitter measured `+221 M` instead.)

- **What was built.** `c_lex` now dispatches to a compaction emitter
  (`c_lex_compact`, guarded `__AVX512VBMI__ && __AVX512VBMI2__` on top of
  AVX-512, so MSVC, aarch64, non-AVX-512 hosts and the self-hosted stages keep
  the scalar loop) that walks the translated source in **item-aligned 64-byte
  windows**, the same architecture the buster tokenizer runs: one masked load
  classifies every byte class in lockstep; quote/comment/escape spans resolve
  by mask arithmetic with escape parity from the simdjson backslash algorithm;
  multi-character punctuators legalize through a bit-channel `vpermi2b` NFA;
  preprocessing-number extents are one `tzcnt` over a continuation mask; and
  emission is the `vpcompressb` iota compaction — starts and ends compress
  through the token-boundary masks, one byte subtract yields every length, the
  kind and punctuator vectors compress by the same starts mask, and widening
  interleaved stores write the 16-byte `CToken` rows eight at a time
  (`vpermi2q` twice per eight rows).
- **Three places where C forced a different shape than the buster tokenizer,
  and they are the whole design.** (1) **C skips whitespace and comments
  instead of tokenizing them**, so the reference's `ends = starts >> 1` is
  wrong — `a  b` would give `a` length 3. Ends come instead from
  `((boundary >> 1) | bit(bound-1)) & token_span`, where `boundary` marks
  every position that begins *anything* (whitespace per byte, so a window of
  pure whitespace still advances 63 bytes instead of one) and `token_span` is
  the complement of the bytes no token owns. (2) **A preprocessing number's
  continuation set is context-sensitive** — `+`/`-` continue only after
  `e`/`E`/`p`/`P` — but `(plus|minus) & (exponent_letter << 1)` models that
  exactly, so C numbers need no scalar scanner at all, where the buster
  emitter had to keep one. (3) **Four item kinds can swallow another item's
  start** (numbers, comments, literals, and `...`, whose last dot would
  otherwise seed the number in `...5`), so one left-to-right cursor loop
  resolves all four and everything else — identifiers, punctuators,
  whitespace, newlines — falls out of pure mask arithmetic. That loop runs
  about two or three times per window on real C, not once per token.
- **Two exactness details worth keeping.** Lookahead loads are masked by the
  **file** bounds, not the window's: a punctuator or comment delimiter near
  the window end is spelled from bytes the next window owns, and reading them
  as zero would silently mis-spell `%:%:` at offset 61 into two `%:`. And the
  literal-prefix rule (`u8`/`u`/`U`/`L`) is decided from the word run in front
  of the quote measured against the cursor, which is what keeps `L'a'` a
  character literal, `x'a'` an identifier plus a character literal, and
  `1'000` a single preprocessing number — the three readings of the same
  adjacency.
- **The differential gate.** `c_lex_reference` stays exported and
  `c_test_frontend_lex_differential` asserts the two paths agree on token
  rows (`memcmp`), the whole `CSourceMetrics` struct, and every diagnostic's
  kind, message and location: 53 construct cases each slid across the window
  boundary by 0-66 pad bytes, ten single-item shapes at ten lengths spanning
  whole windows, twelve real C files including `c.c` and `ide.c` whole plus
  their heads at all 67 window phases, and 48 deterministic LCG fuzz blobs
  over the lexer's full alphabet. The measurement struct is in the comparison
  deliberately — it is what the window pipeline reconstructs from masks
  rather than from the branches it replaced, and it is where the first bug
  would have hidden. Separately, `c_test_lex_punctuator_nfa_mismatches`
  reconciles the hand-assigned NFA channels against `c_punctuator_length`
  exhaustively over every byte sequence the emitter can classify, because the
  spelling tables are derived from `c_punctuator_spellings` while the channels
  are not.
- **The one bug the gate caught, and it was the interesting one:** `...5`.
  The dot-plus-digit number seed fired on the ellipsis's third dot, because
  spans were resolved before punctuators. Adding `...` to the cursor loop's
  candidates fixes it, and the reason it is the *only* such case is worth
  recording — a number seed is a digit or a dot, no punctuator spelling
  contains a digit, and `...` is the only spelling with a dot anywhere but
  first.
- **Measured negative, do not retry:** guarding the first store pass on a
  non-empty window (`+2.0 M` on the identical workload). A window with no
  token at all needs 64 bytes of whitespace or comment, which escapes to the
  scalar scanner long before it reaches the emitter, so the branch is paid
  every window and the two stores it saves are almost never there.
- **Measured and kept:** the per-window
  `BUSTER_CHECK(count == popcount(end_mask))` that pairs the compressed
  starts against the compressed ends costs `230 k` instructions, `0.005%` of
  stage 1. `BUSTER_CHECK` is live in Release, so it was priced rather than
  assumed free; a divergence there would mis-pair every start with the wrong
  end and emit plausible garbage silently, which is the `2026-08-09c`
  Windows Token-ABI failure mode, and 0.005% is the right price for catching
  it on any host.
- **The prerequisite: `optnone` had been leaking onto every AVX-512
  intrinsic in the tree.** `ide.c` wraps the unity build's
  `#include <buster/tests/test.c>` in
  `#pragma clang attribute push (__attribute__((optnone)), apply_to=function)`
  to keep test bodies out of the optimizer. `apply_to=function` stamps the
  attribute on every function *declared* while it is pushed, headers
  included — and `2026-08-09i`'s new `simd_test.c` reached
  `<buster/lib/simd.h>`, and through it the compiler's `<immintrin.h>`,
  inside that region for the first time. Clang's intrinsics are
  `static inline __always_inline__` wrappers around one instruction each, and
  an `optnone` function cannot be inlined however it is attributed, so every
  SIMD kernel in the clang-built unity Release binary began paying a real
  call per operation. Cost, measured: `ide bench` `343.2 M` to `446.7 M`
  (`+30%`) with `BENCH_PARSE` median `~47 us` to `92.3 us` — a regression
  that reached main unnoticed because `2026-08-09i` quoted the self-hosted
  stage-2 benchmark, which improved for its own reasons, rather than the
  clang-built number `feedback-perf-clang-binaries-only` requires. This
  emitter then lost `+221 M` on top. Including `<buster/lib/simd.h>` ahead of
  the push restores all of it: 33 out-of-line `_mm512_*` symbols to 0,
  `ide bench` back to `343.19 M`.
- **Counters, both compilers on the identical tree with that fix in, medians
  of 3:** instructions `5196.8 M` to `5094.5 M` (`-1.97%`), branches
  `992.10 M` to `956.86 M` (`-3.55%`). The instruction counter reproduced to
  `~150 k` across runs, so the `-102.3 M` is ~700x the noise. Pre-rebase the
  same change measured `-101.6 M` and `-34.7 M` branches against a different
  base, so the win is stable across two independent baselines. **Cycles do
  not move measurably at this workload's IPC and that is not evidence
  against the change** — the same note stands in `2026-08-09b` and
  `2026-08-09f`.
- **Unchanged as required:** `ide bench` `343.19 M`, back to the
  `2026-08-09c` reference once the `optnone` leak above is closed — the
  buster tokenizer itself is untouched by this change. Stage 2 `70.68 G`
  and its benchmark `6.89 G` (validation only): the self-hosted stages still
  compile the scalar lexer, so their share of this change is the cost of
  extracting `c_lex_scan_one` out of the scalar loop, the same outlining tax
  the `2026-08-09c` entry priced at `+3.9 M` on its side. Measured directly
  on the gate, that extraction is `+20.2 M` with the emitter compiled out.
- **Validated:** fixed point deterministic (`SELF_HOST deterministic
  bytes=26954520`), all 24139 Release and all sanitized (ASan+UBSan) tests
  pass, and the unity translation unit compiles clean under
  `-mno-avx512vbmi2`, `-mno-avx512vbmi` and `-mno-avx512f` — all three ways
  the emitter can be compiled out.
- **Leads left standing for the next session:** the scalar escape still owns
  every comment and literal longer than a window, which on this
  comment-heavy tree is the emitter's largest remaining slice — a
  64-byte-at-a-time forward scan for `*/` and `\n` inside `c_lex_scan_one`
  would pay on both paths; the punctuator array is the one memory round-trip
  left in the window (five masked `vpermi2b` blends would retire it, and the
  trade was not measured); `2026-08-09g`'s recorded next step of dropping
  per-IR-instruction line/column recovery is untouched; and the
  `2026-08-08g` Token 4 B to 2 B and vectorized keyword hash items remain
  open.
- Reference points for the next audit, all clang-built on this host: stage 1
  **`5.0947 G`** instructions / `~956.9 M` branches, `ide bench`
  **`343.19 M`**, Release `ide test` 24139 tests in 30 modules, `CToken` 16
  bytes, byte-identical fixed point (`SELF_HOST deterministic
  bytes=26954520`). Stage 2 `70.68 G` and its benchmark `6.89 G`, validation
  only. **Check `nm build/Release/ide | grep -c ' t _mm512_'` is 0 before
  trusting any SIMD measurement on this tree** — it is the cheap probe for
  the `optnone` leak above, and it reads 0 when the intrinsics are inlined.

`2026-08-10a` (Linux x86_64, Zen 4 7940HS; not a throughput audit — the
System V outgoing-argument alignment fix costed against the gate, measured
against its own parent on the same tree so the next audit does not read the
step as a regression of its own. Stage 1 `5.1954 G` to `5.2055 G`
instructions (`+0.19%`) while the unit grows `1.398.030` to `1.398.884`
preprocessed tokens, so instructions per token goes `3716.226` to `3721.160`
(`+0.13%`). Rebased onto `05c8973` the gate reads `5.2608 G` over `1.413.856`
tokens — `3720.910` per token, the same ratio — and **that is the reference
point for the next audit**; the pair above is this change against its own
parent and is not comparable to it. Fixed point deterministic, `test_all`
green in Release and in sanitized Debug.)

- **What moved.** A stack-passed argument was placed immediately after the one
  before it, so an argument wanting more than eight bytes of alignment landed
  wherever the pushes left it. `codegen_canonical_x64_call_layout` now records
  each stack argument at an offset rounded up to its own alignment and the
  area's own alignment alongside it; the `IR_OPCODE_ARGUMENT` prologue walks
  the incoming area by the same rule, and the `va_arg` overflow cursor rounds
  before reading. A call whose area wants more than sixteen bytes cannot be
  built by pushing at all — it saves the stack pointer to a frame slot, lowers
  and rounds it down, fills the area with `mov`, and restores from the slot.
- **What it cost and why it was paid anyway.** The gate moves for three
  additions, all of them per value or per argument rather than per
  instruction: the frame loop's flag for the save slot, the code-buffer
  reserve for an area filled an eightbyte at a time, and the rounding in the
  layout and prologue walks. **Two shapes were measured.** Computing the prior
  parameter's alignment once at the top of the prologue's walk instead of
  inside the branches that place a stack argument is *not* where the cost is:
  making it lazy measured `5.2078 G` against `5.2073 G`, inside the `54 K`
  spread two runs of identical code show. Reading the flag and the reserve off
  the `slot_alignment` the two value loops already compute, instead of loading
  `layout.alignment` again, is worth `2.3 M` and is what is in the tree; it
  over-approximates — an over-aligned local that is never an argument also
  reserves the eight bytes — which costs a frame slot and no work.
- **The reserve is the part to revisit.** `aligned_argument_capacity` pays
  `15` bytes per eightbyte plus `32` for every value wider than sixteen bytes,
  because the flat 48-bytes-per-instruction module reserve does not carry a
  call that copies a 64-byte argument in eightbytes. It is charged per value
  and not per call that actually passes one, since finding those needs the
  call-layout pre-pass only Windows runs today. **If this is wanted back**, the
  shape is a flag on `CodegenBuffer` set only where `codegen_emit_u8` refuses a
  byte and surfaced on the result, so a short estimate becomes a retry with
  more room rather than a `CODEGEN_ERROR_CAPACITY` that cannot be told apart
  from the ones a bigger buffer will never fix — an out-of-range frame
  displacement, a frame past `UINT32_MAX`. The flat per-instruction reserve is
  short for the same reason anywhere an instruction moves an aggregate, so
  that flag would pay for more than this entry.

`2026-08-09j` (Linux x86_64, Zen 4 7940HS; incremental IDE workspace
analysis. **Normal body edits now parse, index, analyze, and invalidate only
the changed document; exported-interface or import changes reanalyze only the
old/new reverse dependency cone.**)

- **What was built.** IDE documents now publish immutable, reference-counted
  syntax and module snapshots. Unchanged source/token/parser storage is shared
  across revisions. A normalized import graph carries forward and reverse
  edges plus deterministic dependency order, and exact tagged interface
  fingerprints distinguish body-only edits from import/type/data/code-signature
  changes. Publication is transactional: allocation or diagnostic-limit
  failure leaves the committed workspace generation and pointers untouched.
  Generic and cyclic workspaces retain a conservative full-semantic fallback,
  but still parse only the changed source. Recovered imports from syntax-invalid
  documents remain visible to the UI without participating in the semantic
  dependency graph. Incremental analysis owners retain a dense
  `affected_count` snapshot array; workspace-wide pointer/visibility scratch
  stays in staging storage, and each syntax/analysis owner initially commits
  one native OS page.
  Rebase integration with main's allocator and lazy-global hardening widens
  document-count arithmetic before allocation, uses a non-wrapping reverse
  prefix walk, and prewarms tokenizer tables at the model's serial
  initialization boundary.
- **Measured work sets.** A body edit in a four-document workspace is exactly
  `parsed=1 indexed=1 analyzed=1 invalidated=1`; an interface edit of the leaf
  of a three-document chain is `1/3/3/3`, while an independent fourth document
  keeps both its work flags and source-storage pointer. Import retargeting is
  restricted to the importer and its dependent. Exact same-text updates are
  true no-ops, and rollback, full-rebuild equivalence, generic fallback, cycle
  fallback, normalized/canonical import paths, and invalid-module recovered
  imports all have direct regressions. Two edits of independent documents each
  prove that exactly one semantic snapshot is allocated, rather than one slot
  per workspace document.
- **Compiler-throughput cost.** Against current main `4530f074`, stage 1 moved
  from `5,151,608,556` to `5,204,253,939` instructions (`+1.022%`) while
  preprocessed tokens moved from `1,383,454` to `1,398,096` (`+1.058%`);
  instructions per token therefore moved from `3723.730` to `3722.387`
  (`-0.036%`, effectively neutral). The deterministic executable grew from
  `26,787,912` to `26,990,352` bytes (`+0.756%`). Stage 2 moved from
  `69,625,494,331` to `70,352,079,541` instructions (`+1.044%`). Five
  alternating stage-2 benchmark pairs were neutral: minimum I/O was
  `1.951 ms` on main versus `1.954 ms` here (`+0.1%`), and minimum parse was
  `1.730 ms` versus `1.724 ms` (`-0.4%`).
- **Gates.** The clean rebased branch reached a byte-identical self-host fixed
  point at `26,990,352` bytes with identical `1,398,096`-token streams. Release
  and Debug passed `19,696/19,696` assertions, ASan+UBSan Debug passed
  `18,828/18,828`, and all three completed all 29 module tests. Tests-disabled
  self-host compilation and independent rebase review passed.
- **Known capacity limit.** Every live syntax or analysis owner still reserves
  128 MiB of uncommitted virtual address space because repository arenas cannot
  grow and shrinking the reservation would impose a new hard per-document
  allocation cap. Native-page initial commit avoids the physical multiplier,
  but a worst-case 4,096-document workspace in which every document has been
  touched can reserve roughly 1 TiB of VA. Slab compaction or a proven dynamic
  upper bound remains follow-up work for constrained mobile address spaces.

`2026-08-09i` (Linux x86_64, Zen 4 7940HS; the 512-bit SIMD builtin
vocabulary and the tokenizer port onto it, measured against `2026-08-09g` on
the same tree. **The self-hosted compiler now runs the AVX-512 tokenizer:
stage-2 benchmark instructions `8.3788 G` to `6.7761 G` (`-19.1%`) and
`BENCH_PARSE` median `1.999.827 ns` to `1.609.780 ns` (`-19.5%`). The stage-1
gate moves `5.1203 G` to `5.1617 G` (`+0.81%`) while the unit being compiled
grows `1.378.839` to `1.391.850` preprocessed tokens (`+0.94%`), so
instructions per token goes `3713.476` to `3708.488` (`-0.13%`) — the
compiler did not get slower per unit of work, there is simply more source in
the tree. Stages byte-identical.**)

- **What was built.** `<buster/lib/simd.h>` — fifteen target-fixed AVX-512
  operations with three implementations behind one spelling (self-hosted
  `__builtin_buster_simd_*`, host intrinsics, scalar fallback), plus
  `IR_OPCODE_SIMD` and its EVEX lowering in the canonical backend, and
  `__builtin_popcount`/`popcountll`. `parser.c`'s tokenizer moved off
  `<immintrin.h>` onto it, which is what unblocked the self-hosted stages:
  the compaction emitter was previously disabled under `__BUSTER__`.
- **The clang-built tokenizer did not change.** `tokenize` disassembles to
  the same 827 instructions with the same opcode histogram before and after
  the port, and HEAD-plus-port-only benches at `52.7`–`55.6 µs` median
  against HEAD's `53.9`–`54.1 µs` — inside the noise band. The vocabulary is
  a rename of the intrinsics on that path, not a re-implementation.
- **The `+0.81%` on stage 1 is tree growth, not throughput.** Read it with
  the token denominator, as the `SELF_HOST throughput` line is there for; a
  change that only adds source moves the absolute counter and leaves the
  ratio alone, which is exactly what happened here.
- **The 512-bit vector ABI is now wired**, so a vector can cross a call
  boundary by value. SystemV passes and returns it in a vector register and
  spills to the stack past the eighth, verified against clang for a plain
  identity, for a ninth argument that has to go on the stack, and interleaved
  with integers so both register files advance together
  (`vector_identity`/`vector_ninth`/`vector_mixed` in `tests/basic_c_simd.c`,
  which the driver runs in both the vector and the fallback build). Three
  things were wrong and are worth naming because each produced *plausible*
  code: the callee prologue let the "aggregates over two eightbytes are
  MEMORY" size rule override a classification the IR ABI had already made, so
  the parameter was read off the stack that the caller had put in `zmm0`; the
  stack copy on both sides used the ABI's *register* part count, which is 1
  for a vector, so eight eightbytes of argument were passed as one; and
  `codegen_canonical_x64_float_memory` only knew sizes 4, 8 and 16. That last
  one is now the single authority on which part sizes ride in a vector
  register — the four call sites report `CODEGEN_ERROR_UNSUPPORTED_ABI` on a
  false return instead of repeating the test — and it also refuses a part
  wider than `target_vector_register_size`, so a baseline or AVX2 target still
  gets a clean diagnostic rather than a `zmm` move it cannot execute. A
  32-byte vector now travels in `ymm` where it used to go on the stack, which
  is the psABI answer and a behaviour change for anything that passed one.
  AArch64 gained the indirect path for free. Win64 remains broken for two or
  more wide vector arguments — pre-existing, reproduces on `main`, filed
  separately.
- **Forwarding a SIMD result between adjacent instructions was implemented,
  measured at zero, and removed.** The strict-adjacency rule the `rax` reload
  peephole uses cannot fire here: the C frontend materializes every operand
  expression, so even a fully nested `simd512_store(out,
  simd512_compress_byte(simd512_equal_byte(a, b), a))` has pointer
  loads and a `vzeroupper` between each pair of SIMD instructions. Measured
  `simd_forwarded_operands=0` on a self-compile of the unity `ide.c` and on
  that nested expression; two hits on the synthetic fixture. What *would* pay
  is a mask-only rule that survives intervening scalar code — `k1` is written
  by nothing in the backend except the SIMD lowering, so a mask stays live
  across the `mov`/`vzeroupper` traffic between uses, and every masked
  operation would save its reload `kmovq`. It would have to invalidate on
  three things: a block start (a branch could arrive at a register it did not
  fill), any emission that can reach a `call` (k registers are volatile in
  both conventions), and the next SIMD instruction that writes `k1`. That was
  judged not worth the wrong-code risk for a gain that only lands in
  buster-compiled binaries, which are validation and not where performance is
  quoted from. What was kept from the attempt is the register discipline it
  needed: `vpcompressb` now leaves its result in the first vector register
  like every other operation, instead of the second.

`2026-08-09h` (Linux x86_64, Zen 4 7940HS; not a throughput audit — a
robustness change costed against the gate. An external audit reported that the
arena and emitter bounds checks are bypassable by integer wraparound
(`if (count + size > capacity)` passes when the sum wraps) and proposed
centralized `u64_add_checked`/`u64_mul_checked`/`u64_align_forward_checked`/
`arena_allocate_array_checked` helpers. The holes are real; the prescription
was measured at `+19.5 M` and shipped at `+3.5 M` instead. **Gate: stage 1
`5.1203 G` to `5.1373 G` (`+0.33%`); a fixed-workload cross-check attributes
`+3.5 M` to compiler efficiency (`+0.068%`) and the rest to the tree's own
growth (`+1.997 k` preprocessed tokens). `ide bench` neutral on an interleaved
A/B. Fixed point byte-identical, 19560 Release and all sanitized tests pass.**
Baselines here are the current main (`41814ea`), not `2026-08-09g`'s tree,
which is why stage 1 starts at `5.1203 G` rather than `5.0824 G`.)

- **The exchange rate this entry exists to record.** `arena_allocate_bytes`
  runs **`~7.3 M` times per stage-1 compile**, so one instruction retired on
  its fast path costs `~7 M` instructions, or `0.14%`. That number was read
  off the disassembly and then confirmed against the counter three times: the
  two-compare form measured `+19.5 M` for `+2` instructions plus a branch,
  the single-compare form `+7.3 M` for `+1`, and the shipped form `+3.5 M`
  for a `+1` that is a 10-byte `movabs`. Price any future check on this path
  against that rate before writing it.
- **What was built.** `arena_array_size` in `arena.h` guards every
  `sizeof(T) * count` (1187 sites) with `count > ARENA_MAX_RESERVATION /
  element_size`; `arena_allocate_bytes` bounds `size` against
  `ARENA_MAX_RESERVATION` so the sum-form capacity test is exact rather than
  bypassable, and the granularity round-up moved into the commit branch,
  which pays for the bound; `parser_bump_allocate` takes the same bound;
  `pdb.c`, `codeview.c` and `object.c`'s patch-back helpers move to the
  remaining-space form `dwarf.c` and `object_buffer_write` already used; and
  `ir.c`'s `ast.count * 3 + 1` is widened, because `count` is a `u32` and the
  product wrapped in 32 bits before any allocator could object to it.
- **Bound the operands, not the results.** A result check (`did a + b
  wrap?`) needs both operands live and never folds. An operand bound (`is
  size sane?`) folds to nothing wherever the operand is a constant —
  `parser_bump_allocate` gained one and `state_push` came out at the same 17
  instructions as baseline. This works because `os_fail_raw` is
  `BUSTER_NORETURN BUSTER_COLD`, so a bound established early lets the
  optimizer delete every later check it implies; verified in isolation, a
  redundant multiply guard behind an earlier range check disappears
  entirely.
- **`__builtin_mul_overflow` is unavailable and unnecessary.** The C
  frontend implements no `__builtin_*_overflow`
  (`c_conditional_builtin_supported`), so a builtin-based helper breaks the
  fixed point. It buys nothing anyway: the portable `count > UINT64_MAX /
  sizeof(T)` emits byte-identical code for power-of-two element sizes
  (`shr $60`/`jne`) and strictly better code for others (`cmp` against an
  immediate plus `lea`, against the builtin's `mulq`/`jo`). The guard folds
  away entirely when the count is a `u32` or a literal, which is why 1187
  guarded multiplies cost `+1.08 M` and not `+15 M`.
- **The gate charges for source size, not only for speed.** The first
  spelling, `arena_array_size(sizeof(T), (u64)(count))`, cost `+24.7 M` on
  the gate while costing only `+1.08 M` of real work — the other `+23.6 M`
  was the compiler compiling `+6.477 k` extra preprocessed tokens from a
  macro that expands at 1187 sites. Dropping the `(u64)` cast recovered
  `~4.7 k` tokens. Hardening spelled into a widely-expanded macro must be
  token-lean; nothing outside a self-hosting compiler would show this.
- **Measured negative or rejected, do not retry as written:** (1) the
  audit's remaining-space form as literally proposed, `aligned_offset <=
  reserved_size && size <= reserved_size - aligned_offset`, `+19.5 M`; (2)
  the same with the precondition hoisted to a hot `alignment <=
  ARENA_MAX_ALIGNMENT` check, `+7.3 M` — the constant alignment cannot fold
  in an out-of-line callee, which is the whole cost; (3) a saturating
  product instead of a trapping one, rejected on codegen inspection at 4
  instructions against the trapping form's 2, with no branch to win back
  since the trapping branch is never taken; (4) `u64_align_forward_checked`
  as a per-allocation check — alignment is a `BUSTER_ALIGN_OF` or a literal
  at every site but `disk_builder`'s, so it is not input-reachable and
  checking it is exactly find (2).
- **Traps paid for.** (1) The fastest variant measured, `-2.60 M`,
  establishes `aligned_offset <= reserved_size` by rounding reservations up
  to 4 KB at creation — and silently enlarges the 256-byte arenas
  `rendering_test.c` reserves on purpose to test exact boundary behaviour,
  which surfaced as an `os_commit` failure at the pre-existing `position <=
  os_position` assertion rather than anywhere near the change. Reservation
  granularity is not a property of this codebase's arenas; do not make it
  one to buy `0.05%`. (2) A `static inline` helper in a widely-included
  header needs `BUSTER_UNUSED_DECL`: the Release unity build passes
  `-Wno-unused-function` and the split Debug build does not, so it compiles
  everywhere except the configuration CI runs sanitized. (3) `ide bench`
  varies `~4%` between separately linked binaries from code layout alone,
  and a single-binary reading of it produced a phantom `+5%` regression
  early in this session; interleave the two binaries' runs and compare
  minima. Stage-1 instructions reproduce to `0.005%` and are the instrument
  to trust.
- **Recorded next step, verified rather than proposed.** No checked-allocator
  API can catch a count that wrapped before the call — `ast.count * 3 + 1` is
  the proof. Adding `unsigned-integer-overflow` to the sanitizer set in
  `CMakeLists.txt` was trialled end to end: **46 reports across the whole
  sanitized suite, every one deliberate** (`hash.c` mixing, the SWAR
  byte-scan at `c.c:325`, `u128` negation in `string.c`, djb2 in
  `ui_core.c`, `s64`-to-`u64` time diffs), and zero test failures. Annotating
  those `~10` functions with `no_sanitize("unsigned-integer-overflow")` makes
  the row run clean and catches this class permanently, in a configuration
  that pays no Release cost. The CMake change was reverted rather than
  shipped, because it reds the sanitized row until the annotations land.
- Reference points for the next audit, all clang-built on this host:
  stage 1 `5.1373 G` instructions, stage 2 `69.26 G` (validation only),
  `BENCH_PARSE` `~48.8 us` minimum, byte-identical fixed point
  (`SELF_HOST deterministic bytes=26743800`), 19560 Release unit tests and
  29 module tests.


`2026-08-09g` (Linux x86_64, Zen 4 7940HS; proposal 3 of `2026-08-08k` taken
as its own session — `CToken` 48 to 16 bytes with offset-based spellings and
on-demand location recovery. Developed against the pre-`2026-08-09c` main and
rebased onto the post-`2026-08-09f` main; every number below is clang-built
on the rebased tree against the `2026-08-09f` reference points, counter
medians on a quiet machine. **Mixed on the gate: stage 1 `4.9751 G` to
`5.0824 G` instructions (`+2.2%`; the fixed-workload cross-check attributes
`+80 M` to compiler efficiency and `+27 M` to the tree's own growth) against
minor faults `242.9 k` to `203.4 k` (`-16.3%`), L1d load misses `146.7 M` to
`~123.9 M` (`-15.5%`), cycles `2529 M` to `2513 M` medians (`-0.6%`, inside
the noise band, directionally down, the sys share of wall drops with the
faults). Opened as a PR with the tradeoff stated rather than parked: the
16-byte token is the recorded prerequisite for extending the Deus-Lex
compaction emitter to `c_lex`, and the fault/L1d wins are real today.**)

- **What was built.** `CToken` is now `{u32 offset; u32 length; u32 symbol;
  u16 pack_alignment; u8 kind; u8 punctuator}` — 16 bytes exactly. The
  spelling pointer is gone: every spelling is `spelling_base + offset`, one
  add on the hot paths, where the base is a single contiguous spelling
  space per preprocess run (its own 1 GB commit-on-demand `pool_reuse`
  arena carried behind `CPreprocessResult.recovery`) holding a fixed
  prelude of well-known spellings, every include's translated source, and
  every synthesized spelling. The eagerly materialized 24-byte
  `CSourceLocation` is gone from the token and from `c_lex` entirely (the
  lexer no longer computes locations at all — the checkpoint tables are
  retained instead of consumed); line/column/file recover on demand
  through a sorted source map: FILE regions reuse the retained checkpoints
  with the region's `#line` delta, and macro-expansion output copies its
  spellings contiguously so one EXPANSION entry per invocation carries the
  stamped invocation location — diagnostics are bit-for-bit what the eager
  design produced. `token.location.symbol` readers moved to `token.symbol`;
  the symbol still travels with the spelling (C23 respell now runs at the
  end of preprocessing and re-interns through prelude copies). Lookup is a
  1 KB page-bracket index plus a two-slot cursor (file/expansion) with a
  last-offset memo; parse/lowering-synthesized tokens (static-assert
  wrappers, folded sizeof bounds, cleanup-call names) rebase through local
  prelude-seeded spaces or point at space-resident names directly. The
  expansion machinery carries a transient `{CToken, location, foreign}`
  triple internally (48 bytes, the size the stored token used to be), so
  stamping semantics are unchanged while lexed arrays, staging copies, and
  macro definition rows all move 16-byte rows.
- **Measured, change by change where it mattered (numbers from the
  pre-rebase tree whose baseline was `4.998 G`):** the first working build
  sat at `+333 M` on the gate; a fixed-workload cross-check split that
  into `+302 M` compiler efficiency and `+31 M` tree growth. Cursor
  two-slotting (file tokens sit low in the space, their macro-expanded
  neighbors at the tail — one slot thrashed on every interleave) bought
  `-46 M`; lazy `__LINE__` breadcrumbs (two stores per line instead of a
  per-line recovery) plus bulk per-line expansion copies `-40 M`;
  converting `c_ir_named_label_at` from by-value `CPreprocessResult` to a
  pointer `-35 M` on its own; the page-bracket index removed the
  parse-side binary searches from the profile. The rebase onto the
  `2026-08-09c..f` main then halved the residual: the type-layout cache
  and resumable constant-evaluation frames deleted re-runs that were
  paying recovery repeatedly, leaving `+80 M` efficiency on the final
  tree.
- **Final numbers on the rebased tree:** stage 1 `5.0824 G` instructions /
  `203.4 k` minor faults / `~123.9 M` L1d load misses / `2513 M` median
  cycles; `ide bench` `343.2 M` (byte-for-byte neutral against the
  Deus-Lex baseline — the buster tokenizer is untouched); Release
  `ide test` `6.854 G` (19488 tests; not comparable to earlier references,
  the suite grew); stage 2 `72.1 G` (validation only); fixed point
  deterministic (`SELF_HOST deterministic bytes=26108920`); all 19488
  Release and 18620 sanitized tests pass.
- **Measured negative or neutral inside this session, do not retry as
  written:** an `IrSourceRange` memo keyed on (offset, length) in
  `c_ir_token_source_range` (`+8 M` — the 40-byte copy per query costs
  more than the recovery it skips); consolidating the five new result
  fields behind one pointer (instruction-neutral on its own — the win was
  already taken by the pointer conversion above, but it keeps the
  by-value tax off every future copy); once-per-line instead of per-token
  recovery in directive-line wrapping (neutral — the frame cursor had
  already amortized it).
- **Traps paid for.** (1) Registering file ids at include time instead of
  first-staged-token time broke the fixed point by 40 bytes of
  `.debug_line`: the CMake-built stage resolves `<stdint.h>` into the
  clang resource directory and the self-hosted stages (no resource dir)
  do not, so a fully-guarded header that stages no tokens must never
  reach the file table — file ids stay lazily assigned exactly as the
  eager design had them, and the include-resolution asymmetry between
  hosts is the reason why. `llvm-dwarfdump --debug-line` on the two
  stages localizes this class of break instantly. (2) `CPreprocessResult`
  travels by value through the whole parse/lowering surface, so every
  field added to it taxes thousands of call sites — recovery state lives
  behind one pointer, and the hottest by-value taker was worth converting
  outright. (3) The spelling space cannot be carved from the caller's
  arena: repeated preprocesses on one test arena each took a proportional
  slice and exhausted it — hence the dedicated arena on the result.
- **Where the remaining `+80 M` lives:** per-instruction line/column
  recovery in lowering (`c_preprocess_token_location_cursor` — every
  `c_ir_append_instruction` wants a full location the old design read for
  free from the token), spelling-accessor adds smeared across every
  consumer, and the expansion-copy pass. The structural buy-back is the
  recorded next step: stop materializing line/column per IR instruction
  (carry file+offset in `IrSourceRange`, resolve lines at the
  DWARF/diagnostic boundary) and extend the `2026-08-09c` compaction
  emitter to `c_lex`, which still holds `~300 M` and now has a 16-byte
  row to store into.
- Reference points for the next audit, all clang-built on this host:
  stage 1 `5.0824 G` instructions / `203.4 k` minor faults / `~123.9 M`
  L1d load misses, `ide bench` `343.2 M`, Release `ide test` `6.854 G`
  (19488 tests), `CToken` **16 bytes**, byte-identical fixed point
  (`SELF_HOST deterministic bytes=26108920`). Stage 2 `72.1 G`,
  validation only.

`2026-08-09f` (Linux x86_64, Zen 4 7940HS; the `c_parse_scope_add_entity`
byte-FNV lead the `2026-08-09d` entry left on the table — every number
clang-built, counter medians of 5 runs on a quiet machine, exact call
volumes from temporary probe counters. The proposed shape measured
negative and was not taken; a stronger shape measured positive and was.
Baseline re-measured on the 2026-08-09 main with the `2026-08-09d` intern
change and the sizeof operand-type resolver in: stage 1 `4.9774 G`
instructions / `2537.9 M` cycles / `146.8 M` L1d / `242.9 k` minor faults
— the `+15.9 M` over the `2026-08-09d` reference is the strict sizeof
resolver's own cost, priced before this session started.)

- **The proposed fix (per-id stored FNV on `CSymbolTable`, reused at
  entity adds) measured negative: gate `+1.42 M`, fixed-workload
  cross-check `+1.07 M`. Probe counters killed its premise.** Stage 1
  runs 46,244 entity adds hashing 614,480 name bytes, but only 29,604
  unique names totalling 584,646 bytes — added-entity names average 13.3
  bytes while unique interned names average 19.7, so storing the hash at
  insert re-hashes 95% of the byte volume the adds were paying and the
  reuse ratio is ~1.6x, not the ~3.4x the estimate assumed. The stored
  row then loses on its own overhead: a dependent random `hashes[symbol]`
  load per add (+~0.5 M L1d misses), the growth memcpys, and +90 minor
  faults for the array pages. Not merged.
- **What was taken instead: the name/typedef buckets key on the interned
  id itself, exactly the `c_parse_entity_lookup_hash` idiom.**
  `c_parse_name_hash(symbol, name)` returns `symbol * 0x9E3779B97F4A7C15`
  when the parse has a symbol table and the byte-FNV only in the
  table-less hand-built-test path. All four FNV sites convert: entity adds
  (46,245 calls / 614,503 bytes), `c_parse_lookup_typedef_name` (13,466 /
  60,949), `c_parse_first_constant_entity` (1,209 / 26,517), and the
  declaration-finalize candidate probe (9,994 / 263,111). The three probe
  sites intern first — every name reaching them comes from a token the
  preprocessor already interned, so post-`2026-08-09d` that is a pure
  identity-path hit, cheaper than the FNV it replaces. Chain membership
  changes bucket, never order: same-named entities keep their
  newest-first chain order and every probe still confirms by
  `string_equal`/symbol, so results are identical in both keying modes.
- **Stage 1 `4.9774 G` to `4.9751 G` instructions (`-2.25 M`, `-0.045%`);
  fixed-workload cross-check attributes `-1.98 M` to compiler efficiency.
  Byte-identical output verified directly: the old and new compilers
  produce identical objects on the same tree.** Cycles `2537.9 M` to
  `2529.4 M` medians (`-0.34%`, inside the noise band, directionally
  consistent), L1d neutral (`146.8 M` to `146.7 M`), minor faults neutral
  (`243.0 k`). Stage 2 `70.32 G` to `70.28 G` (validation only). Fixed
  point deterministic (`bytes=26111904`). All 16487 Release and 15619
  sanitized tests pass; `ide bench` unchanged (`409.8 M`,
  pre-`2026-08-09c`-Deus-Lex tokenizer).
- **Method note: the `-2.25 M` is 1000x the gate's measurement noise but
  22x smaller than run-to-run cycle noise.** The stage-1 instruction
  counter reproduces to ~1 k on this host (five runs spanned 958
  instructions); nothing this size is visible in wall time or cycles, and
  only the fixed-workload cross-check separates the `-1.98 M` efficiency
  gain from the `-0.27 M` the modified tree's own compile-cost delta
  contributes to the gate.
- **Leads left standing:** the 08l-scoped `c_ir_query_execute` and
  `c_parse_type_layout` leads were taken by the concurrent `2026-08-09e`
  session (PR 219, merged mid-session); the Token 4 B to 2 B, vectorized
  keyword hash, and CToken 48 to 16 items from `2026-08-08g` remain open.
- Reference points for the next audit, re-measured on this branch rebased
  onto the post-`2026-08-09e` main (the layout-cache and resumable-frames
  landings in), all clang-built on this host: stage 1 **`4.8793 G`**
  instructions / `~2453.6 M` cycles / `~234.5 k` minor faults /
  `~142.7 M` L1d load misses — within `~0.3 M` of additive with the
  `2026-08-09e` `4.8818 G` merged reference; stage 2 `64.13 G`
  (validation only), fixed point `SELF_HOST deterministic
  bytes=26124760`, `ide bench` `409.8 M` (pre-Deus-Lex; PR 217 lands its
  own bench reference).

`2026-08-09e` (Linux x86_64, Zen 4 7940HS; the two remaining `2026-08-08l`
stage-1 leads — the `c_parse_type_layout` persistent cache and the
`c_ir_query_execute` resumable frames — taken as one session, one measured
change per commit; every number clang-built, instruction totals from
counting `perf stat` / `STEP_INSTRUCTIONS`, never sampled. Ran concurrently
with the `2026-08-09c` Deus-Lex bench session (PR 217) and the
`2026-08-09d` `c_symbol_intern` session (PR 218) from the same pre-217/218
main, so the per-change numbers in the body are against that shared
baseline; the branch was then rebased onto the post-`2026-08-09d` main
(intern path plus the case-label/sizeof-operand fixes, no conflicts in the
code) and the reference points at the end are re-measured on that merged
tree — they are the ones the next stage-1 audit starts from.)
Stage 1 self-host `4.9958 G` to `4.9016 G` instructions (`-1.9%`), minor
faults `241.8 k` to `233.2 k`, L1d load misses `~146.2 M` to `~140.7 M`;
fixed point deterministic after each change, 16371 Release and 15519
sanitized tests pass.

- **`c_parse_type_layout` now answers from a persistent layout cache:
  stage 1 `4.9958 G` to `4.9093 G` (`-86.5 M`, `-1.7%`), faults `-8.6 k`,
  L1d `-5.5 M` — the symbol is gone from the stage-1 profile.** Two layers.
  Builtin scalar kinds and incomplete enums answer O(1) from the requested
  record alone, replicating exactly what the old seed pass plus its early
  exit produced. Everything else consults a machine-owned cache indexed by
  type id that stores only layouts that can no longer change, which is how
  the 08l completion trap is handled: the solver now propagates a
  per-query provisional bit — the guessed 4/4 layout of an enum whose
  underlying type has not landed, any array bound folded from an
  initializer-inferred count (`c_parse_validate_constexpr_initializer` can
  re-attach those without checking for an existing one), and every layout
  computed from either — and provisional results are never persisted, so
  completion never has anything to invalidate: an incomplete aggregate was
  never resolvable and an enum's pre-landing layout was never stored.
  In-place record edits are handled at one chokepoint — every mutation of
  a pre-existing type record passes through `c_type_parse_record_mutation`
  (the completion sites write to records that were uncached-incomplete,
  and redefinition of a complete aggregate is rejected) — which drops that
  id's entry in O(1), covering speculative tag completions and their
  rollbacks. Cache reads and writes are both gated to the parse's own
  token stream and writes additionally to an idle machine (no frames, no
  undoable mutations): the synthetic streams `c_parse_integer_constant_range`
  builds read different `pack_alignment` and bound tokens for the same
  type ids and keep their exact old per-call behavior. The full solve now
  also seeds resolved entries from the cache and harvests every
  non-provisional resolution it makes, so a miss is paid roughly once per
  new batch of types rather than once per `_Static_assert`. Fixed while
  there: the solve's scratch arrays are indexed by the type count captured
  at entry — `alignof`-operand parses can grow the table mid-solve, and
  the old live-count guards could index past the scratch allocations.
- **Constant-evaluation query frames now suspend and resume instead of
  restarting: stage 1 `-7.6 M` (`-0.16%`) on a fixed-workload cross-check
  (both binaries compiling the identical tree), Release `ide test`
  instruction-neutral, stage 2 `64.84 G` to `64.76 G` (validation only).**
  The 08l scoping named restart re-execution as the cost; for
  `C_IR_QUERY_FRAME_CONSTANT` the shunting-yard evaluator already syncs
  its value/operator stack heights into the machine before every
  sub-query, so suspension records just the token index and stack heights
  in a resume slot parallel to the frame stack, keeps the stack regions
  reserved for the sub-query to build above, and the re-run resumes at the
  suspended token where the completed sub-query now hits the completed
  list. Slots are cleared on frame push and consumed by the attempt that
  reads them, so genuine failures and capacity unwinds behave exactly as
  before, and a missed suspension site would only fall back to the old
  restart. The other frame kinds still restart from scratch — their
  attempt bodies are recursive-descent scanners with no machine-owned
  state to resume from — and that is where the residual cost lives: the
  Release-test `c_ir_query_execute` share 08l measured (`11.55%`) turns
  out to be first-run attempt execution and non-constant kinds, not
  constant restarts, so this cut is worth `-0.16%` on stage 1 and nothing
  on the test suite. Capacity note for the next reader: suspended
  ancestors keep their value/operator regions reserved, so reservations
  now stack; within one declaration the sum is bounded by the
  disjointness of consumed token ranges (ancestor stacks plus the current
  range plus 8 stays under the max-declaration-sized capacity), while a
  chain that crosses declarations through a type-name/array-bound hop
  could in principle stack two declarations' worth — not observed in the
  corpus, capacity left unchanged.
- Post-change stage-1 cycle shape (change-1 binary, 3k-sample `perf
  record`): `codegen_generate_canonical_module` 8.0%,
  `c_ir_lower_frame_fallback` 7.5%, `c_lex` 5.1%,
  `c_ir_lower_expression_core_step` 3.7%, `c_ir_lower_body_advance` 3.3%,
  `c_preprocess` 3.3%, `c_symbol_intern` 3.2% (taken concurrently by the
  `2026-08-09d` session, PR 218), `ir_validate_canonical_module` 2.2%,
  `c_ir_query_execute` 1.3% residual, `c_parse_type_layout` below the
  sampling floor. Both 08l-scoped leads are now closed.
- Reference points for the next audit, re-measured on the merged tree
  (this branch rebased onto the post-`2026-08-09d` main), all clang-built
  on this host: stage 1 **`4.8818 G`** instructions / `~234.5 k` minor
  faults / `~141.8 M` L1d load misses — the `2026-08-09d` intern win and
  this session's `-94 M` compose to within ~15 M of additive across the
  interleaved source growth; Release `ide test` `6.721 G` / `~81.7 k`
  faults pre-rebase (16371 tests; neutral across both of this session's
  changes — the `6.822 G` in 08l predates the 08k/09a landings);
  `ide bench` untouched (buster tokenizer/parser paths unchanged; the
  `2026-08-09c` Deus-Lex merge will land its own bench reference); fixed
  point `SELF_HOST deterministic bytes=26124872` on the merged tree.
  Stage 2 `64.17 G`, validation only.

`2026-08-09d` (Linux x86_64, Zen 4 7940HS; the `c_symbol_intern` cheaper
identity path the `2026-08-09a` shape flagged as a lead — every number
clang-built, counter medians of 5 runs on a quiet machine, exact call
volumes from temporary probe counters per the `2026-08-09b` rule that
per-symbol sampling diffs are ±50 M noise. One measured change, taken.
Ran concurrently with the bench-side `2026-08-09c` Deus-Lex emitter
session; this entry's `ide bench` reference predates that merge (the
Deus-Lex change is tokenizer-side and does not touch the cc path), and
its stage-1 numbers are re-measured on the 2026-08-09 main with the two
case-label parse fixes in.)

- **Where the intern cycles actually went (profiled first).** At 3.19% of
  stage-1 cycles plus a hidden bcmp slice (frame-pointer leaf attribution
  charges the libc call to the intern pass's caller — ~0.85% more),
  `c_symbol_intern` ran, per lookup: a byte-FNV over the whole name, a
  random u32 `slots[]` probe (28.7% of in-function cycles stalled on that
  load), a dependent `names[id]` String8 load (another 17.6%), and a libc
  `bcmp` PLT call to confirm the hit. Exact counters: **773 k lookups per
  stage-1 unity compile, 96.2% hits on 29.6 k unique names, avg name 9.9
  bytes** (56% under 8 bytes, 28% in 8–16, 16% over), 1.18 occupied-probe
  visits per lookup — ~116 instructions per call against the ~90 M total.
- **The change: the probe entry carries the name's identity instead of an
  id.** A 24-byte `CSymbolSlot` holds the name's first 8 bytes, last 8
  bytes, and `(length << 32) | id`. For names ≤ 16 bytes the two overlapped
  words cover every byte, so matching (low, high, length) *is* byte
  equality — no FNV, no `names[]` deref, no bcmp; 84% of stage-1 lookups
  end there. Longer names use the triple as a 17-byte filter and verify
  only the middle bytes inline in overlapped 8-byte words (no libc call
  anywhere on the lookup path). Growth re-places entries from their stored
  key words. Ids stay assigned in first-encounter order, so the old and new
  compilers emit **byte-identical objects on the same tree** (verified
  directly), and the buster-built stages run the identical scalar code — no
  intrinsics, no dual paths, no source-padding assumptions (`len < 8`
  builds the word bytewise; every wide load stays inside the name).
- **Stage 1 `4.9997 G` to `4.9615 G` instructions (`-38.3 M`, `-0.77%`),
  cycles `2595.8 M` to `2564.5 M` medians (`-1.2%`), L1d neutral
  (`146.5 M` to `146.2 M`), minor faults `+0.7 k` (241.7 k to 242.3 k, the
  wider slot rows).** Stage 2 `70.12 G` (validation only; `70.91 G` at the
  `2026-08-09a` reference), `ide bench` unchanged (`409.77 M`,
  pre-Deus-Lex tokenizer), fixed point deterministic (`bytes=26054048`),
  all 16371 Release and 15519 sanitized tests pass.
  Post-change `c_symbol_intern` is 2.15% of stage-1 cycles and the bcmp
  slice is gone; what remains is dominated by the single random probe load,
  which is intrinsic at this table size.
- **Trap paid for: multiply-only slot hashes cluster catastrophically on
  shared-prefix names.** The first cut mixed `(low ^ len) * K1 ^ high * K2`
  and masked bits [32,48) — differences in the *top* bytes of a key word
  only propagate upward through a multiply, so identifiers differing only
  in trailing characters (this codebase's dominant shape) collided en
  masse: probe steps went 167 k to 1.01 M, 6x. The landed hash folds the
  first product's high word down (`h ^= h >> 32`) and remultiplies before
  taking the high half; probe steps settled at 197 k (0.25 visits/lookup vs
  0.22 for full-byte FNV — an acceptable trade for the cheaper probe).
  Related negatives already on file stay valid: SWAR *hashing* of short
  identifiers and SWAR probes in dispatch loops measured negative in
  `2026-08-08k`/`i`; this change is not a hash-input widening, it removes
  the hash-then-recompare structure for short names entirely.
- **Small lead left on the table:** `c_parse_scope_add_entity` still runs a
  full `c_macro_name_hash` byte-FNV per added entity for the name/typedef
  buckets (the intern no longer computes FNV to reuse); est. single-digit
  M instructions, take it only with measurement. The 08l-scoped
  `c_ir_query_execute` and `c_parse_type_layout` leads still stand.
- Reference points for the next audit, all clang-built on this host:
  stage 1 **`4.9615 G`** instructions / `~242.3 k` minor faults /
  `~146.2 M` L1d load misses, `ide bench` `409.8 M` (before the
  `2026-08-09c` Deus-Lex merge lands its own bench reference),
  `IrInstruction` 112 bytes, byte-identical fixed point (`SELF_HOST
  deterministic bytes=26054048`). Stage 2 `70.12 G`, validation only.

`2026-08-09c` (Linux x86_64, Zen 4 7940HS; the full Deus-Lex-Machina
compaction emitter for the buster tokenizer that `2026-08-08k` recorded as
the endgame and `2026-08-09a` confirmed only pays complete — taken as its
own session per that scoping; every number clang-built on a quiet machine.)

- **What was built.** `tokenize()` now dispatches to a compaction emitter
  (`tokenize_compact`, guarded `__AVX512VBMI__ && __AVX512VBMI2__` on top of
  the existing AVX-512 guard, so `!__BUSTER__`/MSVC/aarch64 and the
  self-hosted stages keep the scalar loop) that walks the file in
  **token-aligned 64-byte windows**: per window one masked load classifies
  every byte class in lockstep; escape parity runs the simdjson
  backslash-parity algorithm; string/comment spans resolve with the
  reference implementation's forward-seeking cursor arithmetic (all lines of
  a window in parallel, borrow-subtraction seeks); multi-character operators
  legalize through the bit-channel `vpshufb` NFA — three per-position
  128-entry `vpermi2b` tables AND-ed, one channel per 2-/3-char family, with
  a left-greedy ctz loop reconciling overlaps; number extents and kinds come
  from mask-derived candidate starts fed through the scalar number scanner
  (extracted, shared verbatim); keywords keep the `2026-08-08k` perfect
  hash, patched per identifier start. Emission is the `vpcompressb` iota
  compaction: start and end positions compress through the token-boundary
  masks, one byte subtract yields every length in the window, the kinds
  vector compresses by the same starts mask, and widening interleaved
  stores write the existing 4-byte `Token` rows 16 at a time.
- **The enabling simplification: windows are token-aligned, not
  chunk-aligned.** Every window begins at a token boundary (the token
  touching a window's last byte is deferred and rescanned by the next
  window), so *no tokenizer state crosses windows at all* — no escape
  carry, no in-string carry, no operator-split carry, none of the carry
  enum the reference implementation maintains. The overlap tax is a few
  re-classified bytes per window; the correctness payoff is that the rare
  shapes the masks do not model (character literals, stray backslashes,
  non-ASCII bytes, `and?`/`or?` candidates, unterminated strings holding a
  recovery semicolon, tokens longer than a window) simply **escape to
  `tokenizer_scan_one_token`** — the scalar loop's own switch, extracted —
  so both paths agree on every hard case by construction. Corpus trigger
  rates: apostrophes in 2 of 193 chunks, `?` twice, non-ASCII zero.
- **Numbers (bench = the primary metric; the tokenizer is not on the
  stage-1 cc path):** `ide bench` **`409.8 M` to `343.2 M` instructions
  (`-16.2%`; `345.0 M` as first landed, then `-1.8 M` more from the Token
  layout fix below)**, `BENCH_PARSE` median `57.1 k` to `~47 k` ns
  (`-17.7%` same-session pairing; the baseline's day spread was `55-57 k`),
  min `55.1 k` to `44.5 k`; `BENCH_IO` median `~226 k` to `~218 k` ns;
  branches `68.10 M` to `59.18 M` (`-13.1%`), branch misses `~750 k` to
  `~440 k` (`-41%`); L1d load misses `2.62 M` to `3.00 M` (`+0.4 M`, the
  kinds-array round-trip and the 640 bytes of tables). Stage 1 `4.9984 G`
  — neutral as required. Fixed point deterministic. All 19372 Release unit
  tests and all 29 sanitized modules pass.
- **Trap paid for on the Windows runner (the fix landed as its own commit
  in this series): a bitfield row is not one layout across ABIs.** `Token`
  was `u32 length : 24;` followed by a plain `u8 id` — 4 bytes under the
  Itanium ABI, but **8 bytes under the MSVC bitfield rules** (clang and
  GCC on Windows both follow them: a non-bitfield member is placed after
  the bitfield's full allocation unit), so the emitter's packed u32 stores
  wrote garbage rows on `x86_64-windows-znver5` (CI task 16409:
  `ide_document_tests` lost every import; only that runner compiles AND
  executes the compact path — the shared Linux runner, the Android
  emulator job, and macOS all run the scalar fallback or skip). Local
  triage that did NOT reproduce it: znver5 tuning, GCC, clang 21.1.8, and
  the runner's exact clang 22.1.0 — all on Linux; a freestanding
  differential harness cross-compiled `--target=x86_64-pc-windows-msvc`
  and run under Wine reproduced it exactly (pre-fix struct fails on the
  first `import` source, fixed struct clean). The fix makes both fields
  share one u32 allocation unit (`u32 id : 8; u32 length : 24;`) with
  `BUSTER_CT_CHECK(sizeof(Token) == 4)` so any ABI drift fails the build;
  putting **id in the low byte** turned the parser's per-token kind checks
  into single byte loads and measured `-1.8 M` under the original
  length-low layout (the interim length-low/id-high bitfield variant had
  measured `+10.6 M` — do not put the kind byte behind a 24-bit field).
- **The differential gate that made this safe:** `tokenize_scalar` stays
  exported and `parser_tokenizer_tests` now asserts the two streams are
  byte-identical (`memcmp` over the token rows plus both counters) for 43
  construct cases each slid across the window boundary by 0-66 pad bytes,
  seven long-token shapes at 8 lengths spanning whole windows, the full
  61-file parser corpus, and 9 deterministic LCG fuzz blobs over the
  tokenizer's alphabet (quotes, backslashes, apostrophes, comment slashes,
  newlines, high bytes) — ~3000 differential assertions, ASan/UBSan-clean.
- **Trap paid for:** extracting the per-token switch out of the scalar loop
  costs `+3.9 M` bench instructions on its own (clang stops inlining it into
  the loop), and forcing `BUSTER_INLINE` on the extracted function measured
  *worse* (`424.8 M`, `+15 M`) — do not force-inline the big switch; the
  compact dispatch makes the outlined cost moot since bench no longer runs
  that loop.
- **Post-change bench shape** (whole process, `perf record -F 20000`):
  `parser_parse` 47.3%, `tokenize` 11.1% (23.2% at the end of `2026-08-08k`),
  `tokenizer_identifier_kind` 3.5%, `arena_allocate_bytes` 2.2%,
  `tokenizer_scan_number` 1.4%. The next tokenizer increments recorded for
  a future session, in expected-value order: the `Token` 4 B to 2 B
  kind+length shrink with the 0-length wide-length escape (the emitter
  already produces kind and length side by side; the parser side is the
  work), a vectorized keyword hash over compressed first/last byte pairs
  (the reference implementation's form) to fold the per-identifier 3.5%,
  and number-mask extents to retire the scalar number scanner's 1.4%. The
  parser itself (47.3%) is now decisively the bench lever — the
  `2026-08-08k` node-allocation-batching lead stands.
- Reference points for the next audit, all clang-built on this host and
  re-measured on the tree merged with `2026-08-09d`: stage 1 `4.9792 G`
  instructions (`+1.75 M` over merged main's `4.9774 G` measured
  back-to-back on this host — source-volume cost, the same class as the
  `2026-08-08k` `+2.8 M`; the `09d` entry's `4.9615 G` was a different
  tree state), `ide bench` **`343.2 M`** instructions per run,
  `BENCH_PARSE` median `~47 k ns` on a quiet machine, `BENCH_IO` median
  `~218 k ns`, `Token` 4 bytes on every ABI, `IrInstruction` 112 bytes,
  byte-identical fixed point (`SELF_HOST deterministic bytes=26111672`).
  Stage 2 `70.4 G`, validation only.

`2026-08-09b` (Linux x86_64, Zen 4 7940HS; the operand/target/immediate
pools + build/consume SoA row split that `2026-08-09a` scoped as the top
lever, taken as its own session — every number clang-built on a quiet
machine, cycles as medians of 5 runs. **Measured negative on the stage-1
instruction gate in all three variants tried; not merged.** The complete,
fully green implementation is preserved on branch
`claude/gallant-lederberg-4b8b2f` for reference or revival.)

- **What was built.** `IrInstruction` shrank from the 112-byte stored row to
  a transient build descriptor; storage became a 56-byte consumer row
  (operand/target/immediate pointers + u16 counts, `canonical_type`,
  `symbol`, `next`, `result`, one opcode-keyed u16 `operation` slot
  replacing the four conversion/unary/binary/atomic enums, u8 memory
  orders, packed flags, `id` dropped in favor of the array index) plus a
  24-byte `IrInstructionBuild` parallel array carrying the build-time-only
  fields (`type`, `entity`, `instantiation`, `local`, `canonical_local`).
  All ~900 access sites across ir.c, c.c, codegen.c, interpreter.c,
  driver.c and the four test files were converted through accessors with
  the delete-the-field enumeration method; opcode-keyed operation
  accessors keep the old COUNT-sentinel semantics exactly.
- **Three variants, three misses (stage 1 baseline `4.9958 G`
  instructions / `2516.6 M` cycles / `241.6 k` minor faults / `146.3 M`
  L1d load misses):**
  - per-function pools + u32 offsets in a 40-byte row (the shape 09a
    proposed): `5.19-5.21 G` (`+4.0-4.4%`), the extra load+lea on every
    pooled-array access dominates;
  - pools + pointers-into-pool in a 56-byte row (growth rebases rows):
    `5.148 G` (`+3.0%`), cycles dead neutral;
  - no pools, 56-byte pointer row + cold split only (the landed-on-branch
    form): **`5.099 G` (`+2.1%`), cycles `2502.7 M` (`-0.55%`), minor
    faults `236.6 k` (`-2.1%`), L1d `138.9 M` (`-5.0%`)**; stage 2
    `72.31 G` (`+2.0%`, validation only), `ide bench` unchanged
    (tokenizer/parser untouched), fixed point deterministic, all 16371
    Release and 15519 sanitized tests pass.
- **Why it loses on the gate.** A fixed-workload cross-check (new binary
  compiling the old tree) attributes ~`+186 M` of the pooled variant to
  compiler efficiency and only ~`+15 M` to the tree's own source growth.
  Stage 1 appends 991,693 instructions (1.41 M operand/target/immediate
  elements; the capacity-seeded pools never grew once), so the append path
  itself can only account for ~40 M — the rest is smeared across every
  consumer in per-access indirection (offset variants) or per-append
  packing/cold-array work, and no single profile symbol carries it. The
  cache story is real but too small to pay: `-5-7%` L1d misses buys back
  only ~0.5% cycles at this workload's IPC (~2.0), and the wall-clock win
  is inside the run-to-run noise the audit protocol refuses to gate on.
- **Traps paid for, in method order:** a variable-size `memcpy` per
  appended instruction is a libc call — element loops on tiny counts are
  `-31 M` by themselves; instruction-event `perf record -c` per-symbol
  diffs swing `±50 M` per symbol from layout/attribution churn between
  binaries (`c_ir_unsupported_gnu_construct` showed `+59 M` in one diff
  and `-55 M` in the next; only totals and probe counters were
  trustworthy); and `./build.sh test_all` behind a `tail` pipe reports
  success while ninja failed — the AGENTS.md "never behind a pipe" rule
  applies to the runner's own summary lines too.
- **What survives for the next session:** the accessor surface and the
  descriptor/row separation on the branch are exactly the seam a future
  SoA pass needs if a consumer ever becomes bandwidth-bound (a batch
  SIMD validator or DWARF walk would flip this trade); the u16-count and
  packed-operation encodings were verified safe against the whole corpus;
  and the malformed-IR test shapes now have pointer-form equivalents.
  Reference points stay those of `2026-08-09a`: stage 1 `4.998 G` /
  `~242 k` faults / `~145.7 M` L1d, `ide bench` `409.8 M`, `IrInstruction`
  112 bytes, stage 2 `70.9 G` validation only.

`2026-08-09a` (Linux x86_64, Zen 4 7940HS; the first 2026-08-08k follow-up
batch, on the merged tree — every number clang-built, measured
change-by-change).

- **The canonical source range left the IrInstruction row: 144 to 112 bytes
  (216 to 112 across the two audits, `-48%`).** The 32-byte `IrSourceRange`
  now lives in a dense per-function parallel array
  (`ir_instruction_canonical_source`): `c_ir_append_instruction` takes the
  range beside the row (the 59 initialize/append pairs hoist their range
  expressions into named locals), `ir_function_add_instruction` stores it
  for hand-built functions, and the buster canonical mapping pass writes
  the array where it wrote the field. **Stage 1 `5.1143 G` to `4.9978 G`
  instructions (`-2.3%`), minor faults `254.9 k` to `241.7 k`, L1d load
  misses `145.7 M`**; stage 2 `72.2 G` to `70.9 G` and the self-hosted
  artifact `28.2` to `26.0 MB` (validation only); `ide bench` neutral.
- **Measured negative first, do not retry as written:** narrowing
  `IrSourceRange` offset/length to u32 (32 to 20 or 24 bytes in place) cost
  a stable `+24 M` stage-1 instructions for `-2.2 k` faults — unexplained
  by copy sizes, reverted in favor of the eviction, which removes 4x the
  bytes without touching the field types.
- **The simple-chunk compaction hybrid for `tokenize` is dead on arrival:**
  only 5.1% of corpus chunks contain nothing but identifier characters,
  spaces, tabs, newlines, and fixed single-character punctuators, so a
  compaction emitter that falls back on any operator, digit-start, string,
  or comment would cover almost nothing — the same conclusion the C-side
  survey reached. The `vpcompressb` emitter only pays as the full
  Deus-Lex-Machina design (bit-channel operator NFA, number and
  string/comment masks) and is a dedicated project, not an increment.
- **Operand/target/immediate pools are scoped, not taken: 893 access sites**
  (many writers) — the conversion wants its own session, taken together
  with the build/consume SoA split so the row is redesigned once.
- Post-change stage-1 shape for that session:
  `codegen_generate_canonical_module` 7.9%, `c_ir_lower_frame_fallback`
  6.7%, `c_lex` 4.9%, `c_preprocess` 3.6%, `c_symbol_intern` 3.0% (the
  intern pass is now visible — a cheaper identity path is a lead),
  `ir_validate_canonical_module` 2.4%, `c_parse_type_layout` 2.2%,
  `c_ir_query_execute` 1.7% (the two 08l-scoped leads still stand).
- Reference points for the next audit, all clang-built on this host:
  stage 1 **`4.998 G`** instructions / `~242 k` minor faults / `~145.7 M`
  L1d load misses, Release `ide test` (unchanged suite) with
  `IrInstruction` **112 bytes**, `ide bench` `409.8 M`, byte-identical
  fixed point (`SELF_HOST deterministic bytes=26045208`). Stage 2
  `70.9 G`, validation only.

`2026-08-08k` (Linux x86_64, Zen 4 7940HS; a vectorization/branch-removal/
data-flattening survey — cycles, branch-miss, and L1d-miss `perf record`
profiles of the clang-built Release stage-1 `ide cc` and `ide bench`, plus
`perf stat` counter baselines; every number clang-built. Ran concurrently
with the `2026-08-08j` IrValue side-table branch, so its four claimed leads —
arena reuse-pool routing, blob chunk table, `c_ir_query_execute`,
`c_parse_type_layout` cache — were deliberately not touched here; this
worktree's baseline is the `2026-08-08i` tree. Landed after
`2026-08-08l`, so this entry sits above it; the reference points at the end
of this entry are re-measured on the merged tree and are the ones the next
audit starts from. AGENTS.md now records the
microarchitecture tuning target — design for Zen 5's native 512-bit width,
Zen 4 must break even at its double-pumped AVX2-equivalent throughput,
`-march=native` already on GNU-family builds, intrinsics keep guarded
scalar fallbacks because the self-hosted stages compile without vendor
headers — plus the Validark-lineage SIMD lexing/parsing method vocabulary
this entry's proposal 4 builds on.) Two changes taken and measured, the rest
recorded as ranked structural proposals below.

- **The buster tokenizer paid one `arena_allocate` call per 4-byte token.**
  `arena_allocate_bytes` held 17.6% of `ide bench` cycles and
  `tokenizer_emit_token` another 9.6% — pure per-token call overhead around a
  15-instruction bump allocator. Every scan step consumes at least one source
  byte, so `file_length + 1` bounds the token count including EOF: the array
  is now reserved once, tokens are stored through a cursor, and the unused
  tail is handed back via `arena_set_position`. **`ide bench` 713.0 M to
  559.1 M instructions (`-21.6%`), `BENCH_PARSE` median 95122 to 81987 ns
  (`-13.8%`), `BENCH_IO` median 274195 to 261601 ns**; post-change
  `arena_allocate_bytes` fell to 4.1% and the emit helper inlined away.
  Stage 1 instruction-neutral (5.4728 G vs 5.4721 G — the tokenizer is not on
  the `cc` path), byte-identical fixed point (`SELF_HOST deterministic
  bytes=28704320`), all 16356 Release tests and all 29 sanitized Debug
  modules pass.
- **Buster keyword recognition became a perfect hash** (taken after the
  rebase onto `2026-08-08l`, measured against the merged tree): the
  tokenizer ran a 23-entry `string_equal` ladder per identifier; now
  `((len << 9) ^ first_two_bytes) * last_two_bytes >> 8` masked to 128
  slots — a variant brute-force-verified collision-free for this keyword
  set — selects a single candidate verified by one compare, per proposal
  2/4's PHF item. Every keyword is ≥2 bytes so the pair loads stay inside
  the identifier (no sentinels needed); the table builds on first use with
  the lookup's own loads and hard-checks perfection, so keyword edits fail
  loudly. **`ide bench` 559.4 M to 438.1 M instructions (`-21.7%`),
  `BENCH_PARSE` median 78.5 to ~60 k ns (`-23%`), branches 107.7 M to
  76.6 M (`-29%`)**; all 16360 Release tests and 29 sanitized modules pass,
  fixed point deterministic, stage 1 `+2.8 M` (the added table source
  compiling in the unity TU). Post-change bench shape: `parser_parse`
  41.4%, `tokenize` 23.2%, `arena_allocate_bytes` 11.7% — the next bench
  levers are parser-side node-allocation batching and the proposal-4
  compaction tokenizer.
- **The parser's per-push/per-node allocations got an inline bump fast
  path** (`parser_bump_allocate`): `state_pop` rewinds the arena position,
  so pushes re-bumped over committed bytes yet paid the outlined
  `arena_allocate_bytes` call each time. **`ide bench` 438.1 M to 417.0 M
  (`-4.8%`)**.
- **The buster tokenizer's run scans became chunk-classified** (proposal 4's
  tokenizer-1 stage): each 64-byte chunk classifies once into five per-class
  bitmasks (`vpcmpb`-family into `k`-masks, masked tail loads so no caller
  padding), and every run scan is shift + count-trailing-ones. **417.0 M to
  409.8 M (`-1.7%`), branch misses `-17%`**; the `vpcompressb` compaction
  emitter remains the recorded endgame.
- **Proposal 2 landed: identifiers intern once during preprocessing** and
  macro lookup, the builtin ladder, entity scope lookup, and keyword
  classification all compare u32 symbol ids. **Stage 1 `5.1406 G` to
  `5.1070 G` (`-0.65%`), branches `-19 M`.** The intermediate states were
  measured and matter for the next reader: the intern pass *alone* was
  `+85 M` (it duplicates the macro-lookup hash until consumers convert), an
  8-byte-SWAR intern hash measured *worse* than byte FNV on short
  identifiers, and only converting the entity buckets and keyword-class
  bytes flipped the total negative. Two correctness rules are now encoded
  in c.c: **the symbol must travel with the spelling** (location
  transplants preserve it, constructions with foreign locations zero it via
  `c_location_without_symbol`, the C23 respell pass re-interns), and the
  classification arrays live **arena-resident on the table** — as file
  globals they were clobbered by a stray stage-1 write (create-time
  `kinds[8]==7` read as `1` later, buster-built stage only; follow-up task
  filed for the underlying codegen bug).
- **Proposal 1 landed in its rare-payload slice: `IrInstruction` 216 to
  144 bytes (`-33%`).** The inline-assembly name arrays and the
  string/float `literal` moved to a sorted per-function
  `IrInstructionExtra` side table and the buster `ParserSourceRange` to a
  dense parallel array (`ir_instruction_source`). **Stage-1 minor faults
  `272.0 k` to `254.9 k` (`-6.3%`), L1d load misses `150.9 M`**, at
  `+7.7 M` instructions of accessor cost; stage 2 `76.0 G` to `72.2 G`
  (`-5%`, validation only). Still open from proposal 1:
  operand/target/immediate pools as u32 offsets, `IrSourceRange` 32 to 16
  bytes, and the build/consume SoA split.
- **Counter shape of stage 1, for targeting (clang Release, this host):**
  5.472 G instructions / 3.108 G cycles (IPC 1.76), 1.035 G branches with
  18.5 M misses (1.79% — ≈8% of cycles at Zen 4's ~13-cycle redirect), and
  **168.6 M L1d-load misses**; 0.49 s of the 1.16 s wall is sys (the known
  fault plumbing). Memory touch, not branch misses and not scalar compute, is
  the stage-1 ceiling — data flattening ranks above SIMD kernels below.
  `ide bench` by contrast runs IPC 3.45 at 0.83% miss rate: compute-shaped,
  so its levers are call/branch removal in `tokenize`/`parser_parse`.
- **Proposal 1 — flatten `IrInstruction` (216 B/row today, measured via
  `ptype /o`): the single biggest open structural lever.** Six raw pointers
  (`operands`/`targets`/`immediates`/`label_names`/`operand_names`/
  `clobbers`), a 16-B `String8 literal`, a 16-B `ParserSourceRange`, a 32-B
  `IrSourceRange`, and ~17 id/enum words per instruction. Every consumer
  walks these rows linearly: `codegen_generate_canonical_module` (6.7% of
  stage-1 cycles, 7.5% of L1d misses), `ir_validate_canonical_module`
  (2.2%/3.4%), `dwarf_build_model` (1.1%/3.8%), and the lowering side that
  writes them (`c_ir_lower_*` ≥15% combined). Move operand/target/immediate
  arrays to u32 offsets into per-function flat pools (counts to u8/u16);
  move the inline-assembly-only fields (`label_names`, `operand_names`,
  `clobbers`, `literal`) into a sparse per-function side table keyed by
  instruction id — the exact pattern the `2026-08-08j` IrValue table just
  validated at `-3.3%` stage 1 for a smaller row; shrink `IrSourceRange` to
  u32 offset/length (`c_translate_source` already rejects sources over
  UINT32_MAX); and consider splitting build-time-only fields (`entity`,
  `instantiation`, `local`, analysis-side ids) from the consume-time set so
  codegen/validate/dwarf walk a ~48–64 B row. Expected larger than the
  IrValue win; also cuts the per-function `memset`/fault tax
  (`__memset_avx512` family holds ~14% of stage-1 libc L1d misses).
  `IrIncoming`/`IrBlockParameter`/`IrPredecessor` linked lists flatten to
  index-linked arrays in the same pass.
- **Proposal 2 — intern identifiers at lex time; make names u32 symbol ids.**
  Every identifier on every logical line runs `c_macro_find` (byte-at-a-time
  FNV then chain walk with `string_equal`; 1.4% cycles, 2.1% of branch
  misses), `c_parse_lookup_entity` re-probes per scope with string keys
  (2.3% cycles), `c_declaration_keyword` re-hashes spellings (1.1% cycles,
  2.3% of misses), and `c_ir_prepare_calls_discover` runs a ~25-probe
  `string_equal` builtin ladder per identifier token inside
  `c_ir_lower_frame_fallback` (9.1% cycles, 8.3% of misses — the #2
  mispredictor). Hash each identifier once in `c_lex` while its bytes are in
  L1, intern into a flat open-addressing table, store the u32 id in the
  token. Macro lookup becomes an id-indexed array load; keyword and builtin
  tests become integer range compares (intern keywords and builtin names
  first, in enum order); scope lookups key on u32. The obvious stopgap —
  a one-byte `spelling.pointer[0] == '_'` prefilter in front of the builtin
  ladder — was tried on the merged tree and **measured negative** (stage 1
  `5.1378 G` to `5.1436 G`, `+5.8 M`): `string_equal` already
  short-circuits on length, so the ladder costs ~25 register compares for
  an ordinary identifier while the gate adds a dependent byte load per
  identifier token. Do not retry the prefilter; only the id-compare
  restructuring pays here.
- **Proposal 3 — shrink `CToken` 48 B to 16** (u32 source offset replacing
  the `spelling` pointer, u32 length, u8 kind, u16 punctuator, plus the
  proposal-2 symbol id): the lexer allocates `48 B x (source bytes + 1)` of
  token rows per include file — the `c_lex include arrays ~26 ms` fault item
  from `2026-08-08i` — and the preprocessor's staging arrays, expansion
  copies, and range materialization move 48-B rows (`__memmove_avx512` ~1%
  of stage-1 cycles, libc 13.9% of L1d misses overall). The eagerly
  materialized 24-B `CSourceLocation` per token is recomputable on demand
  from the existing checkpoint tables (same eager-metadata shape the IrValue
  table removed). Pervasive `spelling` consumers make this the widest
  refactor of the three; sequence it with proposal 2.
- **Proposal 4 — compaction lexing for `c_lex` and the buster `tokenize`
  (Deus-Lex-Machina design), not more in-loop SWAR.** `c_lex` is 4.2% of
  stage-1 cycles but **9.4% of branch misses — the #1 mispredictor**; on
  the bench side `tokenize` holds 31% of cycles post-fix. `2026-08-08i`
  already measured that SWAR probes *inside* the per-token loop go negative
  (comment scan `+12 M`, whitespace run `+6 M`) — the structural version is
  Validark's (see the AGENTS.md method bullet, added alongside this
  amendment): classify each 64-byte chunk into per-class `k`-mask
  bitstrings in lockstep, derive start/end transition masks, then
  `vpcompressb` an iota vector through them and subtract — every token
  extent in the chunk materializes at once, kinds by masked broadcast
  compressed with the same starts mask, no per-byte dispatch at all.
  `vpcompressb` runs ~9 cycles on Zen 4 and ~5 on Zen 5 (Salter's own
  numbers), so the design already pays on this host and roughly doubles on
  the Zen 5 runner; guard behind `__AVX512VBMI2__`-family checks with the
  current scalar loops as self-host/MSVC fallback. The reference
  implementation reached 2.75x mainline-Zig tokenizer throughput with 2.47x
  less token memory. Multi-char punctuators use the bit-channel `vpshufb`
  NFA; keyword, builtin, and bench keyword-ladder recognition
  (`__memcmp_evex` 3.2% of bench cycles) use the
  `((len << 14) ^ first2) * last2 >> 8` perfect hash + one padded wide
  compare instead of ladders. The same pass emits the spelling/delimiter
  position streams `c_parse_position_index_build` rebuilds today (1.0%
  cycles, 2.5% of misses) and subsumes `c_translate_source`'s SWAR probe.
  Sentinel padding (leading newline, trailing quote/NUL, 64-byte-aligned
  overallocation) deletes the hot-loop bounds checks on both lexers
  independently of the SIMD work — cheap to take early. Sequencing note:
  proposals 3 and 4 are one coherent `c_lex` rewrite, not two — the
  compaction emitter should write the 16-B tokens directly (and on the
  buster side can shrink `Token` 4 B -> 2 B kind+length with a 0-length
  wide escape); locations drop out in favor of retained newline bitmasks
  popcounted on demand, which replaces the eager 24-B `CSourceLocation`
  more cheaply than checkpoint re-walks. The C parse side still needs
  random token access (position/delimiter indexes), so `CToken` keeps a
  u32 source offset — the buster side alone can go lengths-only.
  **Zen 5 retarget note:** the stage-1 ranking is unchanged by targeting
  Zen 5's native width — flattening still leads because stage 1 is
  L1d/fault-bound and wider vectors do not touch that — but this proposal's
  ceiling doubles on Zen 5 hardware, and the compaction lexer is itself a
  memory-shrink change (2–16 B tokens, no eager locations), so it attacks
  both limits at once.
- **Proposal 5 — WITHDRAWN before landing: the `2026-08-08l` audit fused the
  three canonical-codegen scans and measured it NEGATIVE** (+6 M
  instructions, +3.5% minimum cycles — clang optimizes the three tight
  single-purpose loops better than one fused branchy loop; the surviving
  piece was the Win64 call-layout cache, taken there). Do not retry the
  fusion as written; if proposal 1's flattening lands, the scans get cheap
  by shrinking the rows they walk instead.
- Left alone deliberately: threaded/lane-parallel codegen (excluded from
  this survey's mandate), and the four `2026-08-08j`-branch leads listed at
  the top. Traps re-confirmed for the next reader: quote performance only
  from clang-built binaries; run `test_self_host` unpiped after frontend
  changes; SWAR-in-the-token-loop is a measured dead end — classification
  must move out of the dispatch loop to win.
- Reference points for the next audit, all clang-built on this host on the
  merged tree (rebased onto the landed `2026-08-08l` chain): stage 1
  `5.114 G` instructions / `~255 k` minor faults / `~150.9 M` L1d load
  misses, Release `ide test` `6.730 G`, `ide bench` **`409.8 M`**
  instructions per run (`713.0 M` at the start of this audit — `-42.5%`
  across its changes), `BENCH_PARSE` median `~56-58 k ns`, `BENCH_IO`
  median `~226 k ns`, `IrInstruction` 144 bytes, byte-identical fixed
  point (`SELF_HOST deterministic bytes=28235824`). Stage 2 `72.2 G`,
  validation only.

`2026-08-08l` (Linux x86_64; the same branch as `2026-08-08j`, taking the
remaining ranked leftovers — arena reuse routing, the generated-blob
chunk-pointer table, and the codegen scan items — measured change-by-change
against the `2026-08-08j` reference points; every number from a clang-built
binary). Stage 1 `5.292 G` to `5.136 G` instructions (`-2.9%`, minor faults
`274.2 k` to `270.7 k`), Release `ide test` `7.047 G` to `6.822 G` (`-3.2%`,
faults `84.6 k` to `83.4 k`), sanitized `ide test` `101.006 G` to
`100.611 G`, `ide bench` instruction-neutral (`713.2 M`), byte-identical
fixed point after every change, all 29 modules in Debug, Release, sanitized,
GCC, and Zig trees. Stage 2 `78.7 G` to `76.0 G`, validation only.

- **The arena reuse pool takes non-default reservation sizes through an
  opt-in flag.** The driver's 8 GB per-translation-unit arenas paid a full
  mmap + first-touch zeroing + munmap cycle per unit compiled (once per
  driver test fixture, once per TU in multi-TU links); pooling stays keyed
  by exact reservation size and `pool_reuse` is set only by creation sites
  whose consumers never assume freshly zeroed pages. **A blanket
  generalization measured broken, not slow**: UI tests spun forever walking
  a box tree built from recycled dirty pages — custom-size arenas have
  latent fresh-mmap zero assumptions, which is exactly what the opt-in
  fences off. Release test faults `-1.2 k`, instruction-neutral.
- **Generated assembly blobs decode through chunk pointer/length tables.**
  `import_assembly_metadata`'s aarch64-only mode now regenerates the x86
  artifacts from the pinned reduced `x86_64-xed.jsonl` (new parser, proven
  by re-emitting the records and requiring the byte-identical checked-in
  corpus, and by an unchanged-template run reproducing the previous header
  bit for bit) — template changes no longer need a raw XED checkout. Every
  `*_blob_char` switch (the 560-way one included) became a table index, and
  `_u8_counted` reads its whole base64 group from one chunk. buster cc
  lowers the constant symbol-address tables through the existing
  static-initializer relocation path; the fixed point exercises it end to
  end. Landing this exposed a pre-existing quadratic it amplified:
  `c_ir_symbol_is_thread_local` scanned the whole entity and global tables
  per address-constant initializer element; every data-symbol site already
  records the flag on the IrSymbol, so it is one lookup now. Together with
  empty-side-table fast paths in the two label-metadata validators (with no
  metadata in a function they provably reduce to operand-existence checks
  plus the LABEL_ADDRESS rejection), stage 1 `-3.1%` and Release test
  `-3.2%` on their own.
- **The codegen per-function scan fusion is closed as measured-negative**:
  one combined instruction walk cost `+6 M` instructions and `+3.5%` min
  cycles versus the three separate tight scans (`2.736 G` to `2.838 G` min
  stage-1 cycles) — clang optimizes the simple opcode-only loops better
  than one branchy pass. Do not retry. What survived: the Windows x64
  outgoing-stack sizing pass already computes every call's layout, and a
  per-instruction cache now hands those to emission instead of recomputing
  per call (Linux-neutral; the win is the Win64 CI runner).
- **Still open, with fresh evidence.** `c_ir_query_execute` is now the top
  Release-test symbol (`11.55%`, mostly inlined `_attempt` bodies): the
  query machine deliberately re-runs a parent attempt from scratch after
  each missing sub-query completes, so the cost is restart re-execution,
  not the completed-list scan; a structural cut means resumable attempts,
  a deep redesign of the machine (the 08-08e result-memo remains negative —
  don't retry it). `c_parse_type_layout` (`~2.5%` of stage 1) pays an
  O(type-table) seeding pass per sizeof/alignof in constant expressions;
  the audit-proposed persistent cache must survive in-place type
  completion — incomplete enums carry a provisional 4/4 layout until their
  underlying type lands and character arrays infer bounds from
  initializers later, so only completed struct/union layouts are safely
  immutable, and a demand-driven rewrite has to carry the array-bound
  arm's embedded constant evaluator with it.
- Reference points for the next audit, all clang-built: stage 1 `5.136 G`
  instructions / `~270.7 k` minor faults, Release `ide test` `6.822 G` /
  `~83.4 k` faults, sanitized `ide test` `100.611 G`, `ide bench`
  `713.2 M` per run. Stage 2 `76.0 G`, validation only.

`2026-08-08j` (Linux x86_64; the IrValue label-metadata side table — the
structural item every audit since `2026-08-08g` listed as THE open shape
change, done as its own change per the `2026-08-08h` warning. Every number
from a clang-built binary). Stage 1 `5.473 G` to `5.292 G` instructions
(`-3.3%`, minor faults `287.1 k` to `274.2 k`), Release `ide test` `7.273 G`
to `7.047 G` (`-3.1%`, faults `85.9 k` to `84.6 k`), sanitized `ide test`
`103.677 G` to `101.006 G` (`-2.6%`), `ide bench` instruction-neutral
(`713.0 M`), byte-identical fixed point (`SELF_HOST deterministic
bytes=28718824`), all 29 modules in Release, sanitized Debug, GCC, and Zig
trees. Stage 2 `80.0 G` to `78.7 G`, validation only.

- **IrValue shrank 64 to 24 bytes.** The seven address-of-label provenance
  fields (`is_label_value`, `has_label_provenance`, `has_non_label_provenance`,
  `label_blocks`/count, `label_paths`/count) moved into `IrValueLabelMetadata`
  entries in a per-function side table sorted by value id
  (`ir_value_label_metadata_find`/`_ensure` plus a by-value getter that
  returns zero metadata for absent entries). Every validator and provenance
  helper keeps its exact logic reading through the table — including the
  `!first` operand rejections in `ir_label_metadata_transfer_valid` and the
  LABEL_ADDRESS/INDIRECT_BRANCH shape rules — so forged-metadata tests and
  malformed-IR validation behave identically; only the storage moved.
- **The C frontend now tracks label metadata only when the function can
  produce a label value.** `c_ir_lower_body_initialize`'s existing
  task-capacity token walk also spots `&&` followed by an identifier naming a
  defined label (a variable sharing a label's name after a logical `&&` only
  costs a spurious enable), and referencing a global that carries
  label-address relocations enables tracking lazily. Everything else skips
  the apparatus: no non-label provenance paths for ordinary pointer stores,
  no `c_ir_label_metadata_store_for_place` root walks per store
  (`emit_store_place`'s 95%-metadata cost), no per-function
  `label_metadata_store_*` scratch arrays (13 bytes/value zeroed or
  0xff-filled), and empty tables make the per-value label checks in both
  validators nearly free (`transfer_valid` was `13 ms`/run). Functions with
  label addresses run the identical tracking from the first statement, so
  mixing rejections and the dynamic-index failure messages are unchanged
  there. The observable delta in label-free functions: metadata-path soft
  `failure_message` text can no longer be planted, and the tracking-capacity
  hard failure cannot trigger.
- Remaining leads carried from `2026-08-08i`, all still open: routing the
  per-include c_lex arenas through the arena reuse pool (the rest of the
  memory-touch tax), the generated-blob chunk-pointer table (`*_blob_char`
  560-case switch, ~`20 ms`/test + cc), `c_ir_query_execute` (`41.5 ms`
  exclusive; memo negative in 08-08e), the `c_parse_type_layout`
  rollback-poisoned persistent cache, and fusing the three per-function
  codegen scans.
- Reference points for the next audit, all clang-built: stage 1 `5.292 G`
  instructions / `~1.1 s` wall / `~274 k` minor faults, Release `ide test`
  `7.047 G` / `~84.6 k` faults, sanitized `ide test` `101.006 G`,
  `ide bench` `713.0 M` per run. Stage 2 `78.7 G`, validation only.

`2026-08-08i` (Linux x86_64; started from the two 18:1x Superluminal captures
of Release `ide test` and the stage-1 `ide cc`, analyzed with
`SuperluminalCmd llm` per-line annotations plus full call-graph traversal —
the traversal attributed the kernel time exactly: `kernel_init_pages` was the
#1 stage-1 exclusive symbol at `114.5 ms`. Every number from a clang-built
binary, measured change-by-change). Stage 1 `6.450 G` to `5.473 G`
instructions (`-15.2%`, wall `~1.15 s` to `~1.04 s`, minor faults `309.5 k`
to `287.1 k`), Release `ide test` `7.629 G` to `7.273 G` (`-4.7%`, faults
`87.7 k` to `85.9 k`), sanitized `ide test` `121.695 G` to `103.677 G`
(`-14.8%`), `ide bench` instruction-neutral (`713.0 M` exactly), byte-identical
fixed point (`SELF_HOST deterministic bytes=28704144`), all 29 modules in
Release and sanitized Debug. Stage 2 `91.4 G` to `80.0 G`, validation only.

- **`__LINE__`/`__FILE__` were rebuilt from scratch on every logical line of
  every file.** `c_preprocess_builtins` ran at the top of each main-loop
  iteration: a `string_format` of the line number, a byte-by-byte re-quote of
  the file path, and two `c_macro_define` calls (each a hash find), all
  arena-allocated and immediately garbage. The two macros are now defined once
  with a `builtin` kind, the loop stores the current logical line/path into
  two head-of-list fields (`builtin_line`/`builtin_path` on the first
  `CMacro`), and `c_macro_replacement_tokens` — the single point both expander
  paths and conditional evaluation flow through — materializes the one-token
  replacement only when the macro actually expands. `#undef` skips
  builtin-kind macros (the per-line rebuild made them un-undefinable, so the
  observable behavior is preserved). Stage 1 `-9.3%` instructions and
  `-21.6 k` faults on its own; also killed the `string_format_va` `12 ms` the
  capture showed inside `c_preprocess`.
- **The self-host stage caught a real codegen bug in this change.** The first
  materialization used a hand-written backward `do/while` itoa
  (`digits[9 - length]`); clang-built `ide` compiled and ran it correctly, but
  the buster-compiled stage 1 miscompiled it and stage 2 failed with "could
  not lower logical expression core" on `__LINE__` — the diagnostic blames the
  consumer, not the macro. Minimal repro (25 lines, segfaults under
  `ide cc`, correct under clang): the same loop inside a struct-returning
  function; in `main` or with forward indexing it is correct. Follow-up task
  filed; the shipped materialization formats via `string_format` (runs
  per-expansion, so the win stands). Lesson repeated from `2026-08-08d`:
  **the fixed point is the dialect *and* codegen gate — run `test_self_host`
  after each frontend change, and never behind a pipe that eats the `cc:
  error` line** (a `| grep instructions` hid this failure for three
  measurement rounds).
- **`c_translate_source` was 55% of `c_lex`'s exclusive time, one byte at a
  time.** The phase-1 loop (CR/LF normalization, splice removal, line/column
  checkpoints) now finds the next `\r`/`\n`/`\\` by SWAR zero-in-word
  detection eight bytes per step and bulk-copies the clean run (checkpoint
  sequence provably identical: a clean run is exactly the case where
  offset/column stay linear). Stage 1 `-295 M` instructions (`-5.1%` with the
  asm-operand fix below). Two SWAR attempts in the same family **measured
  negative and were reverted**: the `c_lex` comment-body scan (`+12 M` —
  comments are only ~4% of `c_lex` and `/* ... */` borders are full of `*`
  bytes that defeat the probe) and a tight whitespace-run loop (`+6 M` —
  single spaces dominate, the extra compare loses to the loop re-dispatch).
- **`buster_x86_metadata_hash_string` zeroed a 4 KB stack buffer and copied
  the string byte-by-byte per hash** — 20 strings per form over 11013 forms
  at decode/validation, ~900 MB of memset per `ide test`; both captures put
  `__memset_avx512` under `validate_form_record`. It hashes the pool span in
  place now. Most of this round's sanitized drop beyond the builtins fix.
- Four smaller scans, in one batch worth test `-2.8%` / stage 1 `-1.5%`:
  `codegen_target_for_abi` re-folded a six-feature array per aggregate-ABI
  query (now a lazily built per-abi table); `c_ir_integer_literal_fits` did an
  `ir_type_from_id` chase per candidate per literal (a `literal_limits[kind]`
  max-value array filled at scalar-type creation answers it in one compare;
  zero means "not cached", falling back to the exact old path);
  `c_parse_lookup_entity` re-hashed the name once per scope level (hoisted —
  it is loop-invariant); and the IR-side group-scan walkers
  (`c_ir_scan_delimiter_group`, `c_ir_root_conditional`,
  `c_ir_expression_core_range`, `c_parse_asm_operand_name_token`) copied a
  48-byte `CToken` per scanned position to ask one punctuator question — they
  read the `punctuator` id through the pointer now (digraph ids are distinct,
  so the old one-character-spelling exclusion is preserved exactly).
- **Remaining shape, for the next audit.** The memory-touch tax is now the
  stage-1 headline by far: `kernel_init_pages` `114.5 ms` + fault plumbing +
  `__zap_vma_range` `37 ms` teardown in the capture (this round only took
  `-22 k` of the `309 k` faults). Attribution: `c_lex` include arrays
  `~26 ms`, `c_ir_emit_local` `23 ms`, `debug_model_build` `16.4 ms`, rest
  diffuse. The levers are row shrinking (the IrValue label-metadata sparse
  side table stays the top open structural item: 32 of 64 bytes per value,
  plus `ir_label_metadata_transfer_valid` `13 ms`/run and
  `c_ir_emit_store_place`'s 95%-on-metadata `19 ms` in the test capture) and
  routing the per-include `c_lex` arrays through the arena reuse pool.
  Second structural item: the generated-blob accessors
  (`*_blob_char` is a 560-case switch called four times per decoded byte over
  a 1.7 MB pool — `~20 ms` of every test run, also on the `cc` path); the
  generator should emit a chunk-pointer table so decode becomes per-chunk
  `memcpy` plus a flat base64 loop. Also open: `c_ir_query_execute` `41.5 ms`
  exclusive in the test capture (retry-shaped; memo measured negative in
  `2026-08-08e`, needs a structural cut, and `16 ms` of its time is
  `__memcmp` under `c_ir_type_name_internal_attempt`);
  `c_parse_type_layout` re-runs a whole-type-table builtin sizing pass plus
  fixpoint per static assert (`~20 ms` stage 1 — needs a rollback-poisoned
  persistent layout cache, fill header outside `CParseResult` like the
  aggregate probe); `codegen_generate_canonical_module` still runs three
  separate per-function instruction scans (`direct_call_uses` 14.4%,
  `saves_rbx` 6.8%, `x64_call_layout` 13.5% of its samples) that want fusing
  into one pass; lane-parallel per-function codegen remains the big blocked
  lever. Environment note: on this host the sanitized `ide` now needs
  `ASAN_OPTIONS=verify_asan_link_order=0` (a preloaded library wins the
  initial-library-list race); the suite itself is unaffected.
- Reference points for the next audit, all clang-built: stage 1 `5.473 G`
  instructions / `~1.04 s` wall / `~287 k` minor faults, Release `ide test`
  `7.273 G` / `~85.9 k` faults, sanitized `ide test` `103.677 G`, `ide bench`
  `713.0 M` instructions per run. Stage 2 `80.0 G`, validation only.

`2026-08-08h` (Linux x86_64; the structural follow-up that took three of the
`2026-08-08g` leftovers the same day — every number from a clang-built
binary, measured change-by-change). Release `ide test` `8.818 G` to
`7.628 G` instructions (`-13.5%`, wall `~0.86 s` to `~0.79 s`, minor faults
`101.6 k` to `87.7 k`), stage 1 `6.554 G` to `6.450 G` (`-1.6%`, minor
faults `326 k` to `309 k`, wall `~1.22 s` to `~1.15 s`), sanitized
`ide test` `141.964 G` to `121.695 G` (`-14.3%` across both rounds), `ide bench` instruction-neutral
(`713.0 M`), byte-identical fixed point after each change.

- **Preprocessed output is now a list of contiguous token ranges, and
  expansion-free lines never touch the expansion machinery.** A logical line
  whose identifiers name no defined macro expands to itself, so `c_preprocess`
  appends its staging array directly as a `CPreprocessTokenRange` (adjacent
  ranges merge); only lines that actually expand still run
  `c_preprocess_expand` and materialize one exact-size array from their node
  list. Final assembly is a few `memcpy`s instead of the per-token node-list
  walk that held `3.2%` of stage-1 leaf samples, and unexpanded tokens stop
  paying a task node plus an output node each. Stage 1 `-1.9%` instructions,
  `-18 k` faults, and most of this round's stage-1 wall drop.
- **The parse side got the IR side's matching-delimiter index.**
  `CTokenPositionIndex` now carries a whole-stream `matching_delimiters`
  table (closer position per properly nested `(`/`[`/`{`, built in the same
  lazy sweep as the spelling positions; a mismatched closer unmatches
  everything still open so malformed regions keep their exact scalar walks).
  The type-parse machine's PARAMETERS walk and BEGIN-stage bracket scan hop
  whole nested groups through it — a properly nested group is paren-balanced
  inside, so the hop leaves the depth walk identical. Release `ide test`
  fell `12.9%` — the machine's re-scans over nested declarator groups were
  far bigger than its `9.5%` self time suggested — for `+0.26%` stage 1
  (the 4 B/token table on the unity TU), the same trade the `2026-08-08f`
  position index made at ~50x return.
- **Destroyed default-shaped arenas park in a per-thread pool for reuse**
  (`ARENA_POOL_LIMIT` 16). The reservation and its faulted pages survive, so
  the next `arena_create` skips the `munmap`/`mmap` pair and the kernel's
  first-touch zeroing; a reused arena hands out dirty bytes — the same
  contract `arena_reset_to_start` already imposes everywhere. Thread-local
  (`BUSTER_THREAD_LOCAL_DECL`) so it degrades to a plain global in
  single-threaded builds and stays race-free in threaded ones; execute,
  locked, multi-arena, and custom-size reservations bypass it unchanged.
  Worth `-4 k` faults and `~15 ms` of `ide test` wall; instruction-neutral.
- **Measured negative, reverted: staging the per-function IR arrays in the
  lowering scratch with exact-size copy-out.** The theory (THP zeroes the
  worst-case capacity slack folio-by-folio) did not hold up: `-4.6 k`
  stage-1 faults for `+14 M` instructions, and `ide test` faults *rose*
  `13 k` because the enlarged scratch reservation fell out of the arena pool
  and churned per fixture. It also tripped the 64 MB scratch reservation on
  the hardening tests' giant functions. The real IrValue shape work — the
  sparse label-metadata side table (432 field references across four files)
  — remains open and should be done as its own change, not as a staging
  trick.
- Reference points for the next audit, all clang-built: stage 1 `6.450 G`
  instructions / `~1.15 s` wall / `~309 k` minor faults, Release `ide test`
  `7.628 G` / `~87.7 k` faults, sanitized `ide test` `121.695 G`,
  `ide bench` `713.0 M` instructions per run.
  Stage 2 `91.4 G`, validation only.

`2026-08-08g` (Linux x86_64; started from the two 15:58 Superluminal captures
of `ide test` and the stage-1 `ide cc`, then re-profiled after each round with
`perf record --call-graph fp` plus batch `llvm-symbolizer --inlines` over the
leaf sample addresses — the leaf histogram named four scans the function-level
capture view smeared. Every number from a clang-built binary). Eight fixes:
stage 1 `8.548 G` to `6.554 G` instructions (`-23.3%`, wall `~1.40 s` to
`~1.22 s`), Release `ide test` `9.150 G` to `8.818 G` (`-3.6%`), sanitized
`ide test` `141.964 G` to `139.852 G` (`-1.5%`), at the byte-identical fixed
point with all 29 modules passing in both configurations. `ide bench`
instruction-neutral (`713.1 M` to `713.0 M`). Stage 2 `121.6 G` to `92.2 G`
(`-24%`, validation only). Minor faults unchanged by design (`~325 k`) — this
audit took instruction quadratics; the fault volume remains the top leftover.

- **The file-scope redeclaration lookup was ~12% of stage 1 on its own.** For
  every file-scope declaration, `c_analyze_semantics` scanned the whole entity
  table for a same-named scope-0 entity (`existing`/`conflicting` selection).
  A name-keyed hash chain (`next_by_name` + `name_lookup_buckets`, filled in
  `c_parse_scope_add_entity` beside the existing scope- and typedef-chains)
  now serves it: candidates are gathered from the chain (newest-first) and
  replayed in ascending entity order so `existing`/`conflicting` pick the
  same entities the linear scan picked; a 64-candidate overflow falls back to
  the original scan. The same chain replaced the two linear entity scans in
  `c_parse_type_layout` (typedef probe via the existing typedef chain,
  enumerator/constexpr probe via `c_parse_first_constant_entity`) and the
  identical enumerator scan in `c_ir_array_bound_evaluate_attempt` — together
  the `c_parse_type_layout` static-assert path fell from `46.5 ms` exclusive
  in the capture to `~1.9%` of a shorter run, and stage-1 `__memcmp` samples
  fell to zero.
- **`c_parse_bind_function_body` classified every body token with all seven
  predicates.** `label_address` (a backward paren-matching scan per token),
  `asm_operand_name`, and `c_ir_named_label_at` were computed per token and
  then discarded unless the token was an identifier. The block is now gated on
  `token.kind == C_TOKEN_IDENTIFIER` (exact: every predicate implies it) with
  the scanning predicates evaluated last under `&&`. The stage-1 capture had
  charged `31 ms` to the label-address prefix scans alone.
- **Canonical IR validation ran twice per compile.** The driver validates
  with detailed diagnostics at driver.c:1355, then
  `codegen_generate_canonical_module` re-validated the same module
  (`~34 ms`/run each, and `ir_label_metadata_transfer_valid` is the dominant
  per-value cost). `CodegenModuleOptions.assume_validated` — set only by the
  driver — skips the second run; every other caller keeps the internal
  validation.
- **`codegen_record_canonical_locations` was locals x instructions.** Per
  debug local it scanned all instructions for the defining
  `LOCAL`/`ARGUMENT`; one pass now builds a `local id -> place` array
  (first-definition-wins, ids `>= local_count` keep the scan as fallback).
  `30.5 ms` exclusive in the stage-1 capture.
- **Codegen label-address relocation resolution rescanned every module
  relocation per function.** Label-address relocations only come from global
  initializers (computed-goto tables); a side index list built after global
  emission is walked and compacted per function instead. `~1.7%` of stage 1
  at the scan line.
- **The C-type -> IR-type mapping fixed point swept every type per pass.**
  `c_lower_to_ir`'s two-round pass loop now iterates a compacting worklist
  (`c_ir_type_mapping_pending`: unmapped, qualified-alias, or mapped
  aggregate with unresolved layout — the three gates the pass branches use).
  Pending is monotone within a round, so compaction cannot drop a type that
  could still act; the worklist preserves ascending type order. This was
  `~4.4%` of the Release test run (leaf samples at the function-type
  parameter walks) — tests run `c_lower_to_ir` per fixture.
- **Both lexers now skip literal bodies eight bytes per step** through the
  same table-AND idiom as the identifier runs (`2026-08-08e`): the buster
  tokenizer string scan (quote, backslash, CR/LF, recovery `;`, non-ASCII
  stay special so malformed-UTF-8 stepping is unchanged) and the C
  char/string literal scan (both quote kinds, backslash, newline).
  Instruction-neutral on `ide bench` (its corpus has short strings); the win
  is in `c_lex` and the string-heavy test fixtures.
- **Remaining shape, for the next audit** (unchanged fault volume is the
  headline): `kernel_init_pages` was `120 ms` of the stage-1 capture — the
  peak leftover, split across lexer arrays, `c_ir_emit_local` (`28 ms`),
  `debug_model_build` (`15 ms`); IrValue's inline label-metadata fields are
  the likely biggest slice and want a sparse side table. The preprocessor
  flattens its token-node list with a pointer-chase copy loop that alone held
  `3.2%` of stage-1 leaf samples (c.c `result.tokens[output_index++] =
  node->token`) — chunked output arrays would kill the copy and the 16-byte
  node headers. `ide test` still spends `~60 ms` in per-test arena
  create/destroy fault churn; arenas reset dirty today, so a reuse pool has
  the same contract. `c_type_parse_machine_run` is still the top Release-test
  symbol (`9.5%`, the PARAMETERS token walk) — a parse-side matching-paren
  index (the IR side already has one) is the structural fix. Lane-parallel
  per-function codegen remains the big blocked lever. Small residues:
  `c_macro_find` `1.6%` (hash tags), `codegen_record_line` `1.6%`,
  `buster_x86_metadata_decode_tables_once` runs on the `cc` path (`~1.2%`).
- Reference points for the next audit, all clang-built: stage 1 `6.554 G`
  instructions / `~1.22 s` wall / `~326 k` minor faults, Release `ide test`
  `8.818 G`, sanitized `ide test` `139.852 G`, `ide bench`
  `713.0 M` instructions per run. Stage 2 `92.2 G`, validation only.

`2026-08-08f` (Linux x86_64; the finds came from two fresh Superluminal
captures — the stage-1 compile and, for the first time, `ide test` itself —
cross-checked with perf + `llvm-symbolizer --inlines` because the hot step
functions are fully inlined and the capture attributes only their call lines.
Every number from a clang-built binary). One fix: Release `ide test`
`10.259 G` to `9.148 G` instructions (`-10.8%`, `c_frontend_tests` `428 ms`
to `386 ms`) and sanitized `ide test` `170.937 G` to `141.964 G` (`-17.0%`),
at the byte-identical fixed point with all 29 modules passing in both
configurations and GCC warning-clean. Stage 1 `8.498 G` to `8.548 G`
(`+0.6%`) — the accepted cost of the index build below on the unity TU.

- **Two "does this range contain spelling X" scans were ~10% of the Release
  test run.** The `ide test` capture put `c_type_parse_machine_run` first at
  `83.7 ms` exclusive and `c_parse_apply_vector_attribute` at `37.6 ms`;
  inline-aware symbolization placed the machine's time on the `_Alignas`
  parameter sweep and the vector-attribute call. Both asked the same question
  per declaration scan over overlapping token ranges. `CTokenPositionIndex`
  now records the sorted token positions of the two rare spellings once per
  parse (built lazily from the fixed token stream, header behind a stable
  pointer so speculative rollbacks cannot rewind or rebuild it), and both
  queries became a binary search returning the same lowest-position match the
  linear scans found. Sanitized fell `17%` because each scanned token was an
  out-of-line `-O0` predicate call there. The `+0.6%` stage-1 instruction
  cost is the one-time class computation for every identifier token of the
  unity TU; its Release-test return is 20x that.
- **Remaining shape, for the next audit:** the test capture shows
  `kernel_init_pages` `44.5 ms` plus `__zap_vma_range` `15 ms` inside
  `ide test` — per-test arena create/destroy churn, a different fault source
  than stage 1's, untouched. Stage 1's fault attribution is now diffuse
  (`c_ir_emit_local` `21 ms`, `debug_model_build` `15 ms`, lexer arrays
  `~34 ms`) — each needs its own array-shape change; no single fix remains.
  After the position index, the Release-test profile top is
  `c_type_parse_machine_run` `10.9%` (now genuinely the machine's own step
  dispatch), `c_ir_query_execute` `8.3%` (the retry-shaped machine, negative
  memo result recorded in `2026-08-08e`), and `c_lower_to_ir` `7.2%` with its
  cost smeared across call sites — structural work, not scan removal.

`2026-08-08e` (Linux x86_64, method of `2026-08-08d`; every quoted number from
a clang-built binary, `perf stat -e instructions,minor-faults` plus the same
`perf record`/annotate flow, buster-built stages fixed-point validation only).
This audit took the `2026-08-08d` leftovers plus the first slices of the
memory-shape and SWAR proposals, single-threaded throughout. Release
`ide test` `12.087 G` to `10.259 G` (`-15.1%`, `c_frontend_tests` `466 ms` to
`428 ms`, `x86_64_metadata_tests` `166 ms` to `68 ms`), sanitized `ide test`
`185.739 G` to `170.937 G` (`-8.0%`, `x86_64_metadata_tests` `1.60 s` to
`0.78 s`), and stage 1 held flat in instructions (`8.486 G` to `8.500 G`)
while its **wall fell `~1.50 s` to `1.24 s` (`-17%`) and minor faults fell
`426k` to `325k` (`-24%`)** — the win this round is memory shape, not
instruction count. Byte-identical fixed point, all 29 modules in Release and
sanitized Debug, GCC warning-clean, `ide bench` A/B `718.6 M` to `713.1 M`
instructions with `BENCH_PARSE` min `~92 us` to `~88.6 us`.

- **A memoized "pure" function must be keyed on its whole input closure.**
  `buster_x86_metadata_physical_register_view` resolves an operand's register
  class/width through ~60 case-insensitive literal probes; a memo keyed by
  `atom_offset` alone shipped six test failures because two branches also read
  `operand.width_offset` (variable-width GPR atoms). Keyed on the pair it is
  correct and worth most of the metadata module's drop in both configurations.
  The audit's `x86_64_metadata_test` cohort sweeps also computed the same nine
  per-form token answers twice (now one mask pass) and probed sixteen ` SCCn `
  tokens per form where one ` SCC` probe clears almost every form.
- **Two more negative results, measured and reverted rather than shipped.** A
  machine-lifetime success-only memo over the C query machine
  (`c_ir_query_execute`) measured `+0.4%` stage 1 and no Release change: the
  per-root completed window already captures intra-root reuse, cross-root
  repeats of the same token range are rare, and the hash costs more than the
  scans it saves. The per-token spelling-class memo (below) likewise costs
  `+0.2-0.4%` in Release where `string_equal`'s length early-out is already
  free — it stays because the sanitized run, where each predicate is an
  out-of-line `-O0` call, pays for it.
- **The parser's spelling predicates now read one lazily computed class byte
  per token** (`token_classes` in `CParseResult`, immune to speculative-parse
  rollback because spellings never change): declaration-keyword-for-dialect at
  four binder scan sites and the `vector_size` probe in
  `c_parse_apply_vector_attribute`. The three sites in the syntax-only
  declaration scanner keep the direct predicate — they have no `CParseResult`.
- **`c_translate_source` no longer writes a 16-byte location per source byte.**
  Locations are checkpoints where the offset/column linearity breaks (byte
  after a newline, byte after a splice) plus a cursor in `CLexResult` that
  amortizes lookups to one advance, since token and diagnostic offsets only
  grow. This is most of the fault reduction and the stage-1 wall drop; the
  worst-case checkpoint arrays are still reserved at source length but only
  one entry per line is ever touched.
- **Identifier runs scan eight bytes per step** in both lexers through a
  256-byte continue table built from the scalar predicate at first use (no
  drift), ANDing eight entries and falling back to the byte loop at the run's
  end. Worth `-5.5 M` bench instructions and part of the stage-1 wall drop;
  the full simdjson-style block classification (whitespace, comments, strings
  in one pass) remains open and is the natural next step of this line.
- `c_ir_add_array_type` got the pointer cache's watermark-sweep index keyed
  `(element, count)` — cold in every current profile, taken as insurance while
  the shape was fresh.
- **Leftovers for the next audit:** stage-1 instructions are flat and its
  profile has no dominating function left; the remaining big levers are
  structural — the lane-parallel per-function codegen/object path (blocked on
  wanting threads), the full SWAR lexer, and the rest of the fault volume
  (`325k` ≈ 1.3 GB touched: token arrays at 40 B per actual token, parse-side
  token-indexed arrays, IR/codegen buffers). `c_type_parse_machine_run` is now
  the top Release-test symbol (`14.8%`) — the type-parse machine re-scans
  declaration ranges per attempt and nobody has profiled *inside* it yet.
  Windows CI showed `compiler_driver_tests` `+20.7%` and stage 1 `+17%` on
  the `2026-08-08d` commit in the same job where `c_frontend_tests` fell
  `27%` — single sample on the noisiest runner, no mechanism identified;
  watch the next main runs before believing or dismissing it.
- Reference points for the next audit, all clang-built: stage 1 `8.500 G`
  instructions / `~1.24 s` wall / `~325k` minor faults, Release `ide test`
  `10.259 G` (`c_frontend_tests` `428 ms`, `x86_64_metadata_tests` `68 ms`),
  sanitized `ide test` `170.937 G` (`c_frontend_tests` `6.49 s`,
  `x86_64_metadata_tests` `0.78 s`), `ide bench` `713.1 M` instructions per
  run, `BENCH_PARSE` min `~88.6 us` at `files=61`. Stage 2 `121.6 G`,
  validation only.

`2026-08-08d` (Linux x86_64, method of `2026-08-08c` plus one addition: a
Superluminal capture of the stage-1 self-host compile, queried through
`SuperluminalCmd llm` — its inline-aware call graph and per-line timings
attributed the libc time that `--call-graph fp` smeared into whichever big
function last pushed a frame, and that attribution found four of the fixes
below. Every quoted number is from a clang-built binary; the stage-2 delta is
self-hosting validation only). Nine fixes in four commits' worth of changes:
stage 1 `12.753 G` to `8.486 G` instructions (`-33.5%`, wall `1.76 s` to
`1.50 s`), Release `ide test` `15.213 G` to `12.087 G` (`-20.6%`,
`c_frontend_tests` `746 ms` to `466 ms`, `parser_tokenizer_tests` `72.7 ms` to
`26.0 ms`), sanitized `ide test` `246.055 G` to `185.739 G` (`-24.5%`,
`c_frontend_tests` `9.15 s` to `6.84 s`), at the byte-identical fixed point
with all 29 modules passing in both configurations. `ide bench` unchanged by
instruction count (`720.3 M` to `718.6 M` for the whole run, A/B measured —
wall medians drifted `92-102 us` across runs on the same binary, which is why
the bench verdict is quoted in instructions). Stage 2 fell `188.4 G` to
`121.9 G` (`-35%`); the no-regalloc canonical path amplifies every removed
rescan.

- **One block-chain walk was a quarter of the Release test suite.** The
  assignment rewrite in `c_ir_lower_assignment_statement_step` pops the
  just-emitted load to recover its place, and found the predecessor of the
  block's last instruction by walking the chain from `first_instruction` —
  once per assignment, so quadratic in block length, and `26.4%` of Release
  `ide test` samples (`87.8%` of them on the walk's one `cmp`; the 4096
  compound assignments in `c_test_frontend_scratch_and_hardening` are exactly
  the shape that triggers it). The builder now tracks the predecessor of the
  last appended instruction; block switches and tail edits invalidate it, and
  the walk survives only as the fallback for the invalidated case. Release
  `ide test` `15.213 G` to `14.271 G`, `c_frontend_tests` wall `-29%`, and the
  time share was far larger than the instruction share — the walk was a
  dependent-load pointer chase, so profile *and* count, in both directions.
- **The libc time perf could not attribute was five separate finds, and the
  Superluminal capture named every one.** `perf` showed `~17%` of stage-1
  samples in unresolved `libc.so.6` hex offsets credited to whole phases;
  the capture's call graph split them: (1) `c_punctuator_length` compared all
  57 punctuator spellings by `memcmp` in enum order at every punctuator — a
  `;` sat behind ~52 failed probes, `51.6%` of `c_lex`'s inclusive time. A
  first-byte dispatch derived from the same table (so id and spelling still
  cannot drift) compares at most a handful of candidates byte-inline. (2)
  `c_parse_promoted_member_type` memset a visited slot per type in the unit on
  every `.field` designator lookup (`~56 ms`); the marks are generation
  stamps now — a query is a counter bump. (3) `c_declaration_keyword` walked
  66 `char const*` keywords calling `string_from_pointer` (strlen) on every
  candidate per identifier query from five parser scan loops; it is a
  once-built hashed set over `String8` spellings. (4) `object_from_canonical_
  codegen_module` resolved every relocation by three linear scans (entries by
  symbol id, then symbols by name twice), and (5) `object_append_dwarf` did
  the name scan again per DWARF relocation — together `~9%` of stage-1 wall.
  Both now use an entry-by-symbol-id array plus a name probe table recording
  the lowest defined and lowest undefined index per name, preserving the
  scans' first-match-in-index-order semantics exactly.
- **The buster tokenizer paid an out-of-line call per string-literal byte.**
  `tokenizer_utf8_sequence_length` was called for every byte inside
  string/char literals; the two 16.7 MB `TOKEN_MAX_LENGTH` overflow
  regressions made it `4.8%` of Release `ide test`. ASCII bytes now advance
  without the call (`parser_tokenizer_tests` `72.7 ms` to `26.0 ms`), and the
  bench A/B confirmed the added branch costs nothing on the corpus.
- **`c_parse_aggregate_lookup` took the `2026-08-08c` leftover index, but the
  rollback machinery dictated its shape.** `c_type_parse_rollback` restores
  `CParseResult` wholesale from a checkpoint copy while shared array storage
  keeps its contents, so an incrementally-maintained index can hold stale
  entries. The `(kind, tag)` probe table therefore validates its recorded id
  against the live type table on every hit and falls back to the old linear
  scan on staleness or saturation; its fill header lives *outside*
  `CParseResult` so a rollback cannot rewind the fill count while slots stay
  populated (that mismatch could overfill the table and hang the probe).
  Misses prove absence because every tagged type passes through
  `c_parse_add_type` and tags/kinds never mutate after creation — both
  verified, not assumed. Same-shape fixes: `c_ir_add_pointer_type` cache
  misses now sweep only types added since the last sweep instead of the whole
  table, and the hot `CToken` by-value copies in the two
  `c_type_parse_parenthesized_step` scan loops became pointer walks (each
  40-byte copy was an `__asan_memcpy` at `-O0`; `5.6%` of the sanitized run).
- **The self-host stage is also the dialect gate.** The first version of the
  object-symbol probe helper compiled under clang but failed `ide cc`: the C
  frontend rejects a statement that follows an if-`return` inside `for (;;)`
  (a `break`-exited `for (;;)` is fine). Rewriting the probe as a `while`
  compiled. Minimal repro is recorded in the follow-up task; until that gap
  is fixed, condition-less loops in this codebase need `break`-shaped exits
  or `while` spellings, and a trusted-compiler build proves nothing about
  self-hostability of new code.
- **Leftovers for the next audit, deliberately untaken:** `c_ir_add_array_type`
  still scans every type per call (was not hot in any profile here);
  `x86_64_metadata` retains `~9%` of Release `ide test`
  (`pool_string_equal_literal`, pattern probes); `c_ir_query_execute`
  type-prediction is `4.8%` of Release test; sanitized `string_equal` residue
  is `c_parse_apply_vector_attribute` `2.6%` and `c_ir_type_name_prefix`
  `2.2%`. The big structural one: the capture showed `~6.5%` of stage-1 wall
  in `kernel_init_pages` — first-touch zeroing of pages the compiler asks for
  and mostly never revisits. `c_lex` allocates one 40-byte `CToken` plus one
  16-byte `CSourceLocation` per *source byte* per file, and the parse-machine
  buffers are token-count-sized; the fix direction is arena reuse across
  files/phases and right-sizing those worst-case preallocations, which is a
  memory-shape change, not a scan removal.
- Reference points for the next audit, all clang-built: stage 1 `8.486 G`
  (wall `~1.50 s`), Release `ide test` `12.087 G` (`c_frontend_tests`
  `466 ms`, `x86_64_metadata_tests` `166 ms`, `compiler_driver_tests`
  `161 ms`), sanitized `ide test` `185.739 G` (`c_frontend_tests` `6.84 s`,
  `x86_64_metadata_tests` `1.60 s`), `ide bench` `BENCH_IO` median
  `~272 us` / `BENCH_PARSE` min `~92 us` at `files=61` (quote bench deltas in
  instructions: `718.6 M` per full run). Stage 2 `121.9 G`, validation only.

`2026-08-08c` (Linux x86_64, same method as `2026-08-08b`: `perf record -F 999
--call-graph fp`, instruction counts from `STEP_INSTRUCTIONS` and `perf stat -e
instructions`, every quoted number from a clang-built binary; buster-compiled
stage executables were used for fixed-point validation only). Seven commits:
stage 1 `16.999 G` to `12.713 G` instructions (`-25.2%`, wall `2.38 s` to
`1.83 s`), sanitized `ide test` `331.97 G` to `246.05 G` (`-25.9%`), Release
`ide test` `21.70 G` to `15.21 G` (`-29.9%`), at the byte-identical fixed point
with all 29 modules passing in Release and sanitized Debug and `ide bench`
unchanged (`BENCH_IO` median `~272 us`, `BENCH_PARSE` median `~98 us`,
`files=61`).

- **Four more translation-unit quadratics in `c_lower_to_ir`, all the shape the
  previous entries predicted** — a per-item query answered by rescanning a
  whole table. In fix order, with stage 1 after each: the per-function local
  count scanned every entity per lowered definition (`13.2%` of samples; one
  bucketing pass, `16.999 G` to `16.813 G` but wall `-11%` — the scan was
  memory-bound, so the win is time, not instructions); the string-literal
  array-bound inference scanned every declaration per unresolved array type
  from inside the round/pass/type fixpoint nest (CSR bucket of object
  declarations by type, `16.813 G` to `15.213 G`); five symbol/global passes
  joined declarations to entities by rescanning all declarations per entity
  (one CSR by entity, `15.213 G` to `14.371 G`); and
  `c_parse_scope_for_token` answered "deepest scope under root holding this
  token" by scanning every scope in the unit and walking each candidate's
  ancestors (`4.3%` of samples but `11.6%` of instructions — a
  children-by-parent index descended from the root instead, `14.371 G` to
  `12.706 G`; the during-parse static-assert caller keeps the linear scan
  because scopes are still growing there). Sample share and instruction share
  disagree in both directions across these — profile *and* count.
- **The x86 metadata module's remaining cost was rescanning and re-deriving
  static data per query.** Three commits, sanitized `ide test` after each:
  `string_offset_terminated` re-found each string's NUL by linear scan per
  call, mostly `physical_register_view` asking the same atom's length once per
  literal in its ~50-entry chain — a distance-to-NUL table filled in one
  backward pass at decode makes it one read (`331.97 G` to `306.80 G`);
  `copy_form` re-tokenized the form's whole pattern to normalize
  prefix/family metadata and filtering ran it per candidate per iteration —
  normalized forms are now cached per id (`306.80 G` to `277.32 G`); emit and
  coverage paths still re-parsed patterns per call and the test helper
  `x86_64_metadata_test_string_contains` compared all needle bytes at every
  offset — pattern semantics are memoized per form id behind a seed-field
  guard (a fabricated or edited form parses fresh) and the probe rejects on
  the first byte (`277.32 G` to `246.05 G`). `x86_64_metadata_tests`:
  sanitized `5.77 s` to `1.59 s`, Release `529 ms` to `166 ms`.
- Stage-1 profile after all fixes is genuinely flat: `c_lower_to_ir` fell from
  `26%` of samples to under `1.2%`, and no function exceeds `5%` (`c_lex`
  `4.9%`, `codegen_generate_canonical_module` `4.8%`,
  `object_from_canonical_codegen_module` `4.6%`). Sanitized shape:
  `__asan_memcpy` `~21%` (diffuse), `string_equal` `8.3%`
  (`c_parse_apply_vector_attribute` `2.4%`, `c_ir_type_name_prefix` `1.9%`,
  `c_parse_aggregate_lookup` `1.6%`), `c_ir_lower_assignment_statement_step`
  `5.0%`, `c_token_is_punctuator` `4.4%`.
- **Known remaining find, deliberately not taken:** `c_parse_aggregate_lookup`
  scans every type per tag lookup during parsing. It needs an incrementally
  maintained `(kind, tag)` hash index in `CParseResult` (the
  `entity_lookup_buckets`/`typedef_lookup_buckets` pattern), returning the
  oldest match; worth ~1.6% sanitized plus some parse-time stage 1. The
  remaining `string_equal` sites are spelling predicates — re-read the
  `2026-08-08` warning before touching them; only removing comparisons wins.
- Reference points for the next audit, all clang-built: stage 1 `12.713 G`,
  sanitized `ide test` `246.05 G` (`c_frontend_tests` `9.6 s`,
  `x86_64_metadata_tests` `1.6 s`), Release `ide test` `15.21 G`
  (`c_frontend_tests` `780-870 ms` run-to-run — quote instructions, not this
  wall spread), Release `BENCH_IO` median `272-280 us`, `BENCH_PARSE` median
  `98-100 us` at `files=61`. Stage 2 fell `262.2 G` to `187.7 G` — quoted as
  self-hosting validation only, since it runs on a buster-compiled stage 1.

`2026-08-08b` (Linux x86_64, `perf record -F 999 --call-graph fp`, instruction
counts from `STEP_INSTRUCTIONS` and `perf stat -e instructions`). Two
configurations were audited together for the first time: **compile throughput**
(clang-built Release `ide` compiling the unity TU, the `Self-host stage 1` step)
and the sanitized Debug `ide test` CI critical path. Every number here comes
from a clang-built binary; buster-compiled stage executables are self-hosting
validation only and none of their timings are quoted. Net result across six
commits: stage 1 `27.436 G` to `17.000 G` instructions (`-38.0%`, `3.19 s` to
`2.16 s`) and sanitized `ide test` `430.25 G` to `332.16 G` (`-22.8%`), at the
byte-identical self-host fixed point with all 29 modules passing in both
configurations and `ide bench` unchanged throughout.

- **Five whole-table scans, four of them quadratic, in one audit.** Each was a
  query answered by re-deriving a global property per item, and each is the
  shape the `2026-08-08` entry below told the next audit to look for. In the
  order they were fixed, with the stage 1 instruction count after each:
  - `c_ir_type_is_flexible_array` scanned every type and every member of every
    aggregate, from depth 5 inside the `type_mapping_round` / `pass` /
    `type_index` fixpoint nest — `21.2%` of the compile on its own.
    `c_ir_collect_flexible_array_types` marks the set once per round.
    `27.436 G` to `21.216 G` (`-22.7%`).
  - `c_ir_incomplete_array_has_initializer` scanned every declaration and every
    entity, once per array type. Now indexed by type in one pass.
    `21.216 G` to `20.549 G` (`-3.1%`).
  - `c_ir_function_signature` called `ir_prepare_program_abi`, sweeping every
    type in the program, once per function declaration. Removed outright:
    every ABI read outside the resolver goes through `ir_type_abi_value`,
    which resolves on demand, so the sweep was pre-warming only. `20.549 G` to
    `19.666 G` (`-4.3%`).
  - `c_ir_label_metadata_store_for_place` scanned every value in the function
    on every store, relocating each place to ask whether it shared the root
    just written. Places are now filed once into an intrusive list headed by
    their root. `19.672 G` to `17.000 G` (`-13.6%`), and sanitized `ide test`
    `366.44 G` to `332.16 G` (`-9.4%`) — it was the one hotspot both
    configurations shared.
  - `x86_64_metadata` string consumers read the pool one
    `buster_x86_metadata_string_byte` call per byte. See below.
- **The optimizer's inline hint does not exist in the tree CI measures.**
  `BUSTER_INLINE` expanded to nothing when `BUSTER_OPTIMIZE` was off, so the
  already-decoded guard `c343ab8` added to `buster_x86_metadata_decode_tables`
  was still an out-of-line call in sanitized Debug, at `3.2%` of `ide test`.
  That commit's sanitized win came from shrinking the guard's `-O0` frame, not
  from inlining. `BUSTER_ALWAYS_INLINE` now holds in every configuration and
  `BUSTER_INLINE` is defined in terms of it; `nm` on the sanitized Debug `ide`
  no longer lists the guard. Worth `-0.9%`. Reserve the new spelling for
  one-test guards on per-byte or per-record paths — forcing a large body inline
  everywhere costs Debug compile time and steppability.
- **The decoded string pool is contiguous, so stop reading it a byte at a
  time.** The one-time decode already flattens the chunked generated pool into
  a host array, but every consumer still went `string_byte` -> `pool_byte` ->
  the decode guard: three calls per byte over a `1.7 MB` pool, roughly `27%` of
  the sanitized run across substring searches, NUL scans, and the pattern
  tokenizer that walks all `11013` forms. `buster_x86_metadata_pool_span` /
  `_string_span` hand out a view once. Sanitized `ide test` `424.46 G` to
  `366.44 G` (`-13.7%`), `x86_64_metadata_tests` `8.21 s` to `5.17 s` (`-37%`).
  `string_span` is public because the metadata tests are a separate translation
  unit in non-unity builds.
- **A correct incremental fix can still be worthless; measure it.** The first
  attempt at the ABI sweep resumed from the longest already-resolved prefix,
  which is sound because a resolved ABI is never invalidated. It measured
  `-0.2%` and left `c_ir_function_signature` at `6.1%` of the profile, because
  `ir_resolve_type_abi` returns without resolving when a type's layout is
  unresolved, so one early incomplete type pins the prefix near zero. Profiling
  after the change, not just before it, is what caught this.
- Shares after all six commits, stage 1 out of `17.000 G`: `c_lower_to_ir`
  `24.3%`, `c_lex` `4.6%`, `c_ir_body_task_set_loop_cleanup_targets` `4.1%`,
  `object_from_canonical_codegen_module` `3.9%`,
  `codegen_generate_canonical_module` `3.6%`, `c_ir_lower_frame_fallback`
  `2.8%`. No single quadratic dominates any more and the profile is flat.
- Shares after all six commits, sanitized `ide test` out of `332.16 G`:
  `__asan_memcpy` `19.5%`, `string_equal` `7.1%`,
  `buster_x86_metadata_string_offset_terminated` `6.4%`,
  `buster_x86_metadata_emit_token_equal` `5.7%`,
  `x86_64_metadata_test_string_contains` `4.2%`,
  `c_ir_lower_assignment_statement_step` `4.1%`, `c_token_is_punctuator`
  `2.7%`. Module totals `c_frontend_tests` `8.70 s` and
  `x86_64_metadata_tests` `5.23 s`. `string_offset_terminated` *rose* in share
  because the run shrank around it: it re-finds a NUL from an offset every time
  a record's string length is wanted, and caching the length in the decoded
  record is the obvious next step.
- Reference points for the next audit, all clang-built: Release `ide test`
  `c_frontend_tests` `693 ms`, `x86_64_metadata_tests` `517 ms`. Release
  `ide bench` `BENCH_IO` median `263 us`, `BENCH_PARSE` median `92 us` — both
  unchanged by every commit here, which is the expected result and was checked
  each time rather than assumed.
- Process note: `--sanitize` is a sticky `generate`-time flag. A measurement
  script that runs the sanitized pass last and then measures Release without
  `--no-sanitize` silently profiles a sanitized tree; self-host aborts in
  `ld.so` and Release test times triple. Also do not build while a measurement
  runs — two overlapping runs produce plausible wall times that are pure
  contention. Instruction counts survive both mistakes; wall times do not.

`2026-08-08` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp`, `25650` samples, instruction counts from `perf stat -e
instructions`). Supersedes the shares in the `2026-08-07` entry below, whose
mechanism was fixed by `23a574e`: `CToken` no longer crosses the spelling
predicates by value, and ASan memcpy shadow scanning is `8.8%` of the run
rather than half of `c_frontend_tests`.

- **A lazily-decoded table's guard cost 20% of the whole test suite.**
  `buster_x86_metadata_decode_tables` decodes once behind
  `buster_x86_metadata_tables_decoded`, but every accessor called it
  out-of-line, including the per-byte string accessors, so the decode was free
  and the already-decoded test was not. Splitting it into a `BUSTER_INLINE`
  test plus a cold `buster_x86_metadata_decode_tables_once` body moved the run
  from `678.81 G` to `537.66 G` instructions (`-20.8%`) and `32.93 s` to
  `25.80 s`; `x86_64_metadata_tests` `14.90 s` to `8.25 s` (`-45%`) and
  `assembly_tests` `1.10 s` to `0.66 s` (`-40%`). Self-hosting stays at the
  byte-identical fixed point. Look for this shape wherever a self-initializing
  accessor is called per byte or per record.
- Module shares after that change: `c_frontend_tests` `61.6%` (`15.79 s`),
  `x86_64_metadata_tests` `31.8%` (`8.25 s`), `assembly_tests` `2.6%`. Before
  it the two were effectively tied (`15.79 s` against `14.90 s`), so
  "`c_frontend_tests` is the largest cost" became true only once the metadata
  module was fixed.
- **The sanitized tree runs UBSan as well as ASan, so each instrumented
  struct-field access costs roughly 20 instructions** — an inline
  `__ubsan_handle_type_mismatch_v1` null/alignment test plus an ASan shadow
  test. `c_token_is_punctuator` is 94 instructions and `string_equal` is 274.
  The consequence is counter-intuitive and was measured three times: **splitting
  an aggregate into separate field accesses is slower than passing it by
  value**, because the by-value copy is one instrumented `memcpy` while each
  field read pays the full pair of checks. Against a `678.81 G` baseline, a
  length/first-byte fast path in `c_token_spelling_equal` measured `695.19 G`,
  the same path with `BUSTER_INLINE` on both predicates `701.03 G`, and routing
  the comparison through a scalar-argument helper `577.96 G` against the
  `537.66 G` state it was applied to. All three were reverted. Do not retry a
  local rewrite of these predicates; only removing the comparison entirely wins.
- **Giving the token a `CPunctuator` id removed another 20% of the suite.**
  `c_token_is_punctuator` reaching `string_equal` had been `14.1%` of the whole
  run, `~19%` counting its own frame and `string_equal`'s ASan fake stack, from
  `1155` call sites over a closed set of 56 spellings. `c_punctuator_length`
  now returns the id it already matched, `CToken` carries it in a `u16` that
  shares the word `pack_alignment` used to own — so `CToken` stays 48 bytes,
  locked by a `BUSTER_CT_CHECK` — and the predicate is `token->punctuator ==
  punctuator`. The run went from `537.91 G` to `430.69 G` instructions
  (`-19.9%`), `24.98 s` to `20.9-21.8 s`; `c_frontend_tests` `15.27 s` to
  `10.7-11.4 s` (`-26%`, and quote the instruction count instead — the wall-time
  spread across two runs of the identical binary is that wide). Compile
  throughput moved with it: self-host stage 1 `29.50 G` to `27.25 G` (`-7.6%`)
  and stage 2 `442.85 G` to `394.65 G` (`-10.9%`), at the byte-identical fixed
  point.
- Two properties make the single compare sound, and a change that breaks either
  one is silently wrong rather than loud. **Only a `C_TOKEN_PUNCTUATOR` token
  may carry a nonzero id**, so the predicate needs no kind test; every site that
  retypes an existing token assigns `punctuator` alongside `kind`.
  **Digraphs keep ids distinct from the punctuators they spell** (`<:` is not
  `[`), because every caller was asking about a spelling. The one place that
  edits a spelling in flight, `c_ir_compound_assignment_operator`, drops the
  `'='` and hands the result to the spelling-driven `c_conditional_operator`, so
  it clears the id that no longer describes it.
- Converting the punctuator tests that went through `c_token_spelling_equal`
  was worth `0.04%` (`430.84 G` to `430.68 G`): they sit on the directive path,
  not the hot loop. It was kept for the invariant, not the number. The
  `C_CONDITIONAL_MATCH` chain in `c_conditional_operator` is still a linear walk
  of string compares and was deliberately left alone — it does not appear in the
  profile, and converting it means reworking the spelling truncation above.
- After the change `string_equal` is `5.6%` and no longer punctuator work: it is
  `c_parse_apply_vector_attribute` and `c_ir_type_name_prefix` matching keywords
  and type names. The largest single buster frame is now
  `c_ir_label_metadata_store_for_place` at `7.4%`, and module shares are
  `c_frontend_tests` `51.5%` (`10.72 s`) against `x86_64_metadata_tests` `39.9%`
  (`8.31 s`) — the two modules are close to tied again, so the next audit should
  not assume the C frontend is where the time is.
- Self-host stage ratio, for the canonical codegen path that runs no register
  allocation: stage 1 `3.45 s` / `29.50 G` instructions, stage 2 `37.96 s` /
  `442.85 G`. That is `11.0x` by wall time but `15.0x` by instructions retired;
  quote the instruction ratio, since wall time on this host varies ~10%.

`2026-08-07` (Linux x86_64, sanitized Debug clang, `perf record -F 999
--call-graph fp` on a quiet host, `65466` samples). This is the CI critical
path; `ide test` totals `~65 s` here:

- `c_frontend_tests` is `72.7%` of the sanitized run (`47.7 s` by
  `TEST_MODULE_TIMING`); `x86_64_metadata_tests` is the only other large module
  at `23.5%` (`15.4 s`). Everything else together is under `4%`.
- **Half of `c_frontend_tests` is ASan's memcpy shadow check.**
  `QuickCheckForUnpoisonedRegion`, inlined into `___interceptor_memcpy` in
  `libclang_rt.asan-x86_64.so`, is `50.3%` of the module's samples.
- Charging that sanitizer cost to the buster frame that provokes it, the module
  is dominated by two one-line predicates in
  `src/buster/lib/compiler/frontend/c/c.c`:
  `c_token_is_punctuator` (`46.3%`) and `c_token_spelling_equal` (`21.4%`),
  then `string_equal` (`8.9%`), `c_ir_label_metadata_store_for_place` (`3.2%`),
  `c_ir_lower_assignment_statement_step` (`2.8%`) and
  `c_type_parse_parenthesized_step` (`2.7%`).
- Mechanism: `CToken` is a 40-byte aggregate passed **by value** into
  `c_token_is_punctuator`, which forwards it **by value again** to
  `c_token_spelling_equal`. At `-O0` clang materializes each by-value aggregate
  argument with a real `call memcpy` (visible in the disassembly of
  `c_token_is_punctuator`, alongside a per-call `__asan_stack_malloc_1`), and
  under ASan every one of those goes through the interceptor and its shadow
  scan. The predicate itself is one enum compare plus one string compare.
- The same profile of an unsanitized Debug build has the same shape without the
  amplification: `c_token_is_punctuator` `38.9%`, `string_equal` `16.8%`,
  `c_token_spelling_equal` only `4.7%`. Sanitizing multiplies the module by
  `3.8x` overall and concentrates the extra cost on the by-value token copies.
- Cost by call site inside `c_frontend_tests` (sanitized): `c_test.c:7229`
  `30.1%`, `:6974` `18.1%`, `:7166` `14.6%`, `:6726` `13.3%`, `:6312` `9.1%`,
  `:7227` `4.3%`. Five of these six are the `c_lower_to_ir`/`c_parse` calls of
  the inline stress blocks; `c_lower_to_ir` accounts for roughly `68%` of the
  module and `c_parse` for `21%`.
- Method validation (independent, non-sampling): direct
  `timestamp_take()` brackets around those call sites in the *same* process
  agree with the sampled shares to within `0.04` percentage points sanitized
  (`7229` `30.99%` measured vs `31.03%` sampled; `6974` `19.76`/`19.77`; `7166`
  `14.59`/`14.61`; `6726` `12.24`/`12.25`; `6312` `8.45`/`8.42`; `7227`
  `4.01`/`4.03`) and to within `0.3` points unsanitized. Every extracted return
  address disassembles to the instruction immediately after a `call` to the
  callee the callchain names.
- **Correction to an earlier note:** `c_test.c:7227` is *not* ~70% of
  `c_frontend_tests`. It is `4.3%` sanitized and `3.6%` unsanitized
  (`0.49 s` of `13.8 s` by direct timing). The `~70%` figure came from
  `perf script -F ip,sym,srcline`, whose raw-return-address resolution shifts
  every call site by 1–2 lines. The dominant single site is `c_test.c:7229`,
  the `c_lower_to_ir` of the same vla-assembly stress block — so the block was
  identified correctly and the line and magnitude were not.

`2026-08-02` (Linux x86_64, Release + Debug, with `--instrument --time-trace`):

- `build/Release` compile time is currently dominated by the unity TU:
  `CMakeFiles/ide.dir/Release/src/buster/apps/ide/ide.c.o` at `20613 ms`
  in `ninja_log_summary build --limit 20`.
- `build/Debug` split compilation hotspots (same summary):
  `parser.c.o` (`707 ms`), `codegen.c.o` (`582 ms`), `analysis.c.o` (`475 ms`),
  `ir.c.o` (`361 ms`).
- Pre-refactor `bench_all` result snapshots (the default was the I/O mode):
  - Release: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=398673 median_ns=418340`.
  - Debug: `BENCH_IO parse_all_tests iterations=200 files=59 min_ns=941358 median_ns=975512`.
- In `BUSTER_INSTRUMENT` mode:
  - Tokenize: `55_363 ns` median (Release), `297_607 ns` median (Debug).
  - Parse: `48_272 ns` median (Release), `340_853 ns` median (Debug).
- Pre-refactor `BENCH_FILE` output was consistently slowest-first; `tests/basic_variadic.bbb`
  is the dominant long-tail input across both configs.
- Superluminal capture of the old `ide bench` shows `parser_parse_bench` paths flowing
  heavily through `file_read -> os_file_open -> __libc_open64`, indicating
  benchmark procedure repeatedly performs disk reads instead of amortizing source
  input.
- The two-mode benchmark now separates parser throughput from filesystem input;
  compare `BENCH_PARSE` and `BENCH_IO` using their mode-specific phase and
  per-file rows when investigating parser throughput.

`2026-08-02` (Linux x86_64, Release `bench_all`, `BUSTER_INSTRUMENT=1`):

- Before the two-mode refactor, an opt-in preloaded-source benchmark path produced:
  - `BENCH_IO parse_all_tests ... median_ns=421736` (baseline: `file_read` each iteration).
  - `BENCH_PARSE parse_all_tests ... median_ns=90831` (files memory-mapped once, then reused).
- The preloaded benchmark reduced total wall time by roughly `78.5%` (`421736 -> 90831`).
- Tokenize/parse split moved only modestly:
  - Baseline `BENCH_IO_PHASE tokenize` median: `57241`, parse median: `48711`.
  - Preloaded `BENCH_PARSE_PHASE tokenize` median: `50023`, parse median: `39756`.
- The remaining gap between modes is mostly filesystem read overhead in the old
  path; parser/tokenization itself improves only ~12–19%.
- The dominant input is still `tests/basic_variadic.bbb`, but its parse path also
  drops (median `11171 -> 10430`) under preloaded-source reuse.
