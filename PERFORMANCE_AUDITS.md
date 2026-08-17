# Performance audit notes

Performance audit history for this repository, newest first. Every entry is a
record of what was measured, what was fixed, and what the numbers were at the
time; the methodology for taking new measurements — which benchmark to trust,
how to profile the sanitized and Release trees, how to symbolize — stays in
the "Benchmarking and diagnostics" section of `AGENTS.md`.

Entries written before the C-only consolidation on 2026-08-14 may mention the
removed experimental frontend, its fixtures, or its editor model. The `.bbb`
fixtures and historical documentation remain in-tree for future work, while
the C implementation and C tests are gone. Older entries remain historical
measurement context rather than current implementation guidance.

**Read the newest entry before starting performance work.** It carries the
reference points the next audit is measured against, the finds that were
deliberately left untaken, and the mistakes an earlier audit already paid for.
When an audit lands, add a new dated entry at the top and leave the older ones
as written — they are a record, not documentation to keep current.

`2026-08-17g` (Linux x86_64, Zen 4 7940HS; **shape-cache hashing,
metadata-address borrowing, and x86 function-shape scans**). The post-17f
trusted Release profile leaves native code generation centered on
`buster_x86_metadata_emit_exact_machine` (5.41% self cycles),
`machine_encode_x86_64` (4.72%), `machine_select_canonical_function` (3.77%),
generic metadata scratch emission (3.52%), FAST placement (2.92%), metadata
binding (2.58%), and FAST prepass (1.72%). Branch and L1d miss rates remain
small enough that duplicated work, rather than either miss class by itself,
is still the first-order cost.

The x86 machine shape cache now computes its independent signature and
collision guard in one operand pass. Both accumulators keep their original
seeds and exact per-operand update order; the cache key, slot layout, collision
check, and token remain unchanged. This shares mnemonic/feature
classification, attribute packing, operand loads, kind dispatch, and loop
control without weakening the guard. Seven interleaved non-multiplexed
instruction rounds, with byte-identical output in every pair, reduce the
median from 14,923,271,474 to 14,850,746,516, **-72,524,958 (-0.486%)**.
Branches fall 0.455%, branch misses 0.354%, and L1d loads 0.533%; L1d misses
rise 0.848%. Nine cycle pairs are mixed, so no isolated cycle claim is made.

The metadata address encoder now borrows the effective normalized form and
pattern instead of receiving their 264- and 144-byte records by value. The
tuple-scale helper follows the same borrowed view, and both callers pass the
locals after all prefix/AMX mutations, so displacement, tuple, SIB/VSIB, and
fallback behavior are unchanged. Existing Release disassembly showed the
aggregate stack copies around this call. The isolated seven-round gate saves
about 5.4 M instructions (**-0.036%**) and 0.294% of L1d loads; branches and
L1d misses are flat, while the nine-round cycle median is directionally lower.

Finally, x86 canonical preparation no longer scans ordinary IR rows twice or
binary-searches sparse instruction-extra metadata for non-assembly rows. One
function-shape walk validates the inline-assembly storage it actually reaches
and reports whether atomic or assembly constraints require RBX. It retains the
old early-return order: an atomic/RBX requirement found before later malformed
assembly still stops the scan exactly where it did. Checking the opcode before
the sparse lookup saves 11.28 M instructions (**-0.076%**); folding the
remaining RBX and validation walks saves another 7.42 M (**-0.050%**), with
branches and L1d traffic also lower.

The clean merged-main versus final-candidate cumulative A/B uses seven
interleaved single-event rounds per counter and byte-identical output. Median
instructions are 14,921,435,408 -> 14,824,924,645, **-96,510,763
(-0.647%)**; branches 2,752,821,094 -> 2,736,345,920 (**-0.599%**); branch
misses 28,357,071 -> 28,271,660 (**-0.301%**); L1d loads 5,377,087,741 ->
5,340,120,396 (**-0.688%**); and L1d misses 139,249,802 -> 137,881,685
(**-0.983%**). Nine cycle rounds favor the candidate in seven pairs, with
sorted medians 5,450,610,224 -> 5,373,478,804 (**-1.42%**).

Two allocator shapes were measured and reverted. Ping-ponging the stable
retro-edit merge buffers removes each pass's explicit copy but adds about
5.6 M instructions (**+0.038%**); the original copy-back loop is better for
this bounded stream. Iterating only the set bits in the eviction candidate
mask adds about 3.3 M (**+0.022%**) versus the predictable bounded register
scan. These are not retained as SIMD-readiness wins: the current scalar
throughput rule rejects them even though their source shapes appear more
branchless or copy-light.

Final `test_self_host` reaches a byte-identical 35,514,248-byte fixed point:
stage 1 retires 14,816,467,146 instructions, stage 2 retires 105,139,106,837,
and all 1,441,827 exact attempts succeed. Release `test_all` passes 309,175
assertions across 39 modules, including 877 metadata tests and the 34,043-row
completion census. The complete local combination matrix also passes: Clang
unsanitized and sanitized Debug/Release, GCC and Zig Debug compilation, the
canonical 309,177-assertion table-audit run, and Clang static analysis with
zero warnings.

`2026-08-17f` (Linux x86_64, Zen 4 7940HS; **exact-emitter validation,
dispatch, and staging**). Four independently measured changes reduce the
remaining native x86 encoding and FAST-placement work without adding a byte
authority. Exact-plan publication caches the ordinary-policy integrity byte in
the existing tail padding of its immutable 232-byte record; token validation
still proves prewarm, slot, policy, readiness, identity, form, pattern,
operand count, hash, APX policy and the supplied integrity byte before use.
The scalar machine-fast binding loop combines its duplicated operand-kind
dispatch and repeated GPR-class test while preserving the exact mismatch
versus unsupported/fallback split, including malformed unknown binding kinds.

`machine_x64_emit_exact_form` now gives the metadata bridge the final bounded
encoder span instead of copying successful output through a second 16-byte
array. The metadata scalar and generic paths retain their private transactional
scratch, so every late range/length failure still leaves caller output
untouched; the public metadata API and its overlap behavior are unchanged.
Finally, FAST placement initializes its four block-by-register owner/dirty
arrays with guarded contiguous bulk fills instead of a scalar four-store loop.
This is deliberately a runtime-wide-memory primitive rather than a mandated
fixed-width batch: tails and irregular active sets remain compatible with the
mask-oriented SIMD direction.

All isolated A/Bs used saved trusted Clang Release compilers, identical final
source/flags/output paths, CPU 2 pinning, seven interleaved non-multiplexed
instruction rounds, and byte-identical outputs. Cached token integrity is
15,005,566,058 -> 14,960,561,444, **-45,004,614 (-0.300%)**; it trades
branches **+0.108%**, while branch misses are noisy. Merged binding dispatch is
14,971,079,862 -> 14,965,695,287, **-5,384,575 (-0.0360%)**; branches rise
0.062%, branch misses fall 0.38%, and L1d loads fall 0.18%. FAST bulk fill is
14,986,440,016 -> 14,971,125,884, **-15,314,132 (-0.102%)**; branches fall
0.058% and branch misses fall directionally, while median L1d misses rise
1.46%. Removing the outer byte copy is 14,966,077,687 -> 14,915,105,194,
**-50,972,493 (-0.341%)**, with branches **-0.841%** and L1 counters flat
within noise.

The clean merged-main versus final-candidate cumulative A/B is instructions
15,031,788,627 -> 14,915,062,140, **-116,726,487 (-0.777%)**; cycles
5,550,876,883 -> 5,479,017,618, **-1.295%**; branches 2,768,725,057 ->
2,748,531,379, **-0.729%**. Branch misses rise 28,376,850 -> 29,778,318
(**+4.94%**), while L1d loads and misses are nearly flat at +0.16% each. The
miss-rate tradeoff is recorded rather than hidden: the candidate removes
enough dependent validation/copy work to win retired instructions and cycles,
but its changed code layout is less friendly to this run's branch predictor.

One cache-oriented selector experiment was rejected. Materializing all sparse
IR row IDs once for two reverse alias sweeps saved 0.083% instructions, 0.042%
branches and 0.139% L1d loads, but raised branch and L1d misses about 0.67%
each and cycles 0.62%; retaining only a per-block working set is better for the
current cache shape. The debug-off declaration-position and machine-line-mark
work remains deferred because the mandatory self-host gate carries debug info,
so that series needs a separate `-g0` benchmark rather than a source-only
claim.

Final `test_self_host` holds the byte-identical fixed point at 35,523,192
bytes: stage 1 retires 14,915,336,386 instructions, stage 2 retires
106,134,385,650, and all 1,442,309 exact attempts succeed. Release `test_all`
passes 309,177 assertions across 39 modules, including 877 metadata tests and
the 34,043-row completion census. The complete local combination matrix also
passes: Clang unsanitized and sanitized Debug/Release, GCC and Zig Debug
compilation, and Clang static analysis with zero warnings.

`2026-08-17e` (Linux x86_64, Zen 4 7940HS; **machine-path dead work and
SIMD-oriented dataflow**). Five independently measured changes remove work
that the native machine path either ignored or recomputed. Target selectors no
longer construct a 96-byte generic rule context and run the generated
nine-rule matcher for every typed instruction when the returned rule does not
control their explicit lowering switch. The matcher and its full fact prepass
remain available and directly tested; production selectors use a new minimal
fail-closed prepass that retains only the four definition/use arrays they
consume, avoiding about 40 MB of aggregate fact writes on the unity workload.
FAST placement folds its implicit callee-saved clobber reduction into the
existing prepass instead of walking every machine row again. Selectors count
SIMD operations during their existing typed-IR walk instead of making codegen
rescan successful functions.

Canonical fallback preparation is now lazy. Machine selection runs before
canonical value-slot, direct-call-use, aligned-local, f80 ABI, frame and call
layout construction; only the three fallback functions in the 3,661-function
self-host workload pay for it. Machine-success unwind storage is sized from
the retained placement after encoding. This intentionally makes `-v`'s stack
diagnostics path-specific: machine-emitted functions report their actual
placement frame in both stack counters, while fallback/NONE continues to
report canonical value/frame sizing. The old FAST numbers were 26,881,584
value bytes, 26,899,216 frame bytes and 1,213,184 maximum; the retained machine
placement reports 5,463,584 / 5,463,608 / 596,376. Code bytes, debug/unwind
data, exact/fallback telemetry and artifacts remain unchanged.

Each step used saved trusted Clang Release compilers, the same candidate unity
source for both sides, identical flags and output path, CPU 2 pinning, seven
interleaved rounds, and byte-identical output. Isolated instruction results
are: folded FAST clobber reduction **-9.84 M (-0.0625%)**; dead matcher removal **-358.88 M
(-2.281%)**; folded SIMD statistic **-0.95 M (-0.0062%)**; lazy canonical
preparation **-174.50 M (-1.135%)**; minimal selector prepass **-165.82 M
(-1.090%)**. Their cache/branch shapes agree with the intended flatter data
flow: matcher removal also cuts branches 1.83% and L1d loads 2.95%; lazy prep
cuts branches 1.28% and L1d misses 4.36% (while branch misses rise 1.67%);
the minimal prepass cuts branches 1.00%, branch misses 6.77% and L1d loads
1.42%.

The final same-source cumulative A/B is instructions 15,772,095,450 ->
15,071,527,053, **-700,568,397 (-4.442%)**; cycles **-4.06%**; branches
2,898,206,945 -> 2,782,296,116 (**-4.00%**); branch misses 31,261,966 ->
28,838,073 (**-7.75%**); L1d loads 5,664,010,706 -> 5,370,879,452
(**-5.18%**); L1d misses 149,491,096 -> 139,298,277 (**-6.82%**); generic
cache references **-4.85%** and misses **-10.88%**. Cycle distributions are
still noisier than retired instructions, but this series' direction is
consistent.

A post-change 999 Hz cycle sample (1,364 samples, trusted Release compiler)
puts the remaining native-codegen leaves at `buster_x86_metadata_emit_exact_machine`
8.61%, `machine_encode_x86_64` 4.31%,
`machine_select_canonical_function` 3.87%, FAST placement 3.06%, generic
metadata scratch emission 2.89%, binding 1.94%, and FAST prepass 1.89% of the
whole compile. The cumulative counters put branch misses at about 1.04% of
branches and L1d load misses at about 2.59% of loads. Misses matter and their
absolute counts fell materially, but neither miss class alone is the dominant
bottleneck; the remaining center is x86 exact-metadata encoding plus machine
selection/placement work.

One density follow-up was rejected. Removing the now-zero 240-byte matcher
counter blocks from selector/result structures was instruction-flat
(-0.0001%) but raised L1d loads 0.28%, L1d misses 2.69% and branch misses
1.32%, so the cold reserved storage remains to preserve the favorable measured
layout. Focused minimal/full-prepass differential tests compare all retained
facts, prove omitted arrays stay absent, and require the same fail-closed
invalid-value result.

Final `test_self_host` holds the byte-identical fixed point at 35,523,192
bytes: stage 1 retires 15,032,061,403 instructions, stage 2 retires
106,748,338,681, and all 1,442,321 exact attempts succeed. Release `test_all`
passes 309,177 assertions across 39 modules. The complete local combination
matrix also passes: Clang unsanitized and sanitized Debug/Release, GCC and Zig
Debug compilation, and Clang static analysis with zero warnings.

`2026-08-17d` (Linux x86_64, Zen 4 7940HS; **native codegen verifier and
selector edge construction**). The machine path no longer verifies every
selected function twice: `codegen_generate_canonical_module_attempt` retains
the first `MachineVerifyResult.error` for both fallback telemetry and the
placement gate. Both x86-64 and AArch64 selectors now append CFG edges and
parallel-copy sources to the `MachineFunctionBuilder` streams before the one
final flatten, instead of arena-allocating a one-row-larger array and copying
the complete prefix for every append. Traversal order, copy offsets and
failure behavior stay unchanged.

Same-source A/B used the saved pre-change and final trusted Clang Release
compilers, both compiling the final unity TU to the same path, seven
interleaved rounds. Median instructions are 16,456,348,477 -> 16,308,620,307,
**-147,728,170 (-0.898%)**; branches are 3,039,388,760 -> 3,013,774,374
(-0.843%), and branch misses are 31,434,542 -> 30,673,174 (**-2.42%**).
Cycle medians read -0.82%, but the distributions overlap and no wall/cycle
claim is made. A separate five-round cache series records L1d loads
5,949,562,885 -> 5,910,992,196 (-0.65%), L1d misses 192,531,364 ->
187,236,027 (**-2.75%**), generic cache references 417,673,066 -> 407,665,803
(-2.40%), and generic cache misses 14,628,202 -> 13,894,142 (**-5.02%**).
Both compilers produced byte-identical output. The verifier removal supplies
145.7 M of the instruction win; the selector stream conversion supplies a
smaller 2.09 M (-0.013%) while deleting the quadratic growth path and 42
lines of copy logic.

Two measured candidates were reverted. A CSR-parallel first-edge index for
FAST replaced four repeated edge scans but cost **+11.06 M instructions
(+0.068%)**, +2.28 M branches and +0.54 M branch misses: building and probing
the index costs more than the short edge arrays it replaces on this workload.
Hoisting x86 metadata aggregate-block topology work removed 13 K branch
misses but added 49 K instructions and 8 K branches because it made a query
operand scan unconditional; the miss delta is 0.04% and far below cycle noise.

Final `test_self_host` holds the byte-identical fixed point at 35,497,344
bytes: stage 1 retires 16,308,885,038 instructions, stage 2 retires
114,940,178,696, and all 1,440,839 exact attempts succeed. Release `test_all`
passes 308,861 assertions across 39 modules, including the complete x86
metadata census and the AArch64 selector/encoder suites. The complete local
combination matrix also passes: Clang unsanitized and sanitized Debug/Release,
GCC and Zig Debug compilation, and Clang static analysis with zero warnings.

`2026-08-17c` (Linux x86_64, Zen 4 7940HS; **cache/branch investigation**;
[PR #441](https://code.buster14a.com/buster/buster/pulls/441),
[PR #442](https://code.buster14a.com/buster/buster/pulls/442),
[PR #443](https://code.buster14a.com/buster/buster/pulls/443),
[PR #444](https://code.buster14a.com/buster/buster/pulls/444), and
[PR #445](https://code.buster14a.com/buster/buster/pulls/445)).  Each candidate
was measured serially against the trusted compiler with the same source,
flags, output path, and CPU pin: five interleaved rounds, with every output
byte-identical.  The accepted final medians were: #441 DWARF facts, **-3.003%
instructions, -1.936% cycles, -18.709% L1d misses**; #442 typedef facts,
**-0.121% instructions, -0.229% cycles, -1.472% L1d misses**; #443 constants,
**-0.264% instructions, -0.639% cycles, -1.709% L2 accesses, -19.836% L2
misses**; #444 operand facts, **-0.153% instructions, -0.891% L1d misses,
-0.039% branches, -0.123% branch misses** (the two-cycle result was noisy at
**+0.513%**, so no cycle claim); and #445 stateless picker, **-0.013%
instructions, -0.189% median cycles, -0.115% branches, -1.604% branch
misses** (mixed L1 was **+0.793%** median / **+0.337%** paired, so no L1
claim).

Rejected experiments were the first constant name-chain version (**+2.439%
L2 misses** despite **-0.162% instructions**), an ordinal alias that failed
the fixed point with NUL source, a maintained free mask (**+0.358%
instructions, +0.907% cycles, +0.525% L1d** despite **-2.945% misses**), an
append-time slot flag (**+0.015% instructions, +0.649% L1d** despite
**-0.898% misses**), and a local branchless slot fold that was statically
worse because it added unconditional stores.  The direct-call scan stayed
unchanged: it has no reusable state, and a new stream would trade branches for
cache traffic.

`2026-08-17b` (Linux x86_64, Zen 4 7940HS; **x86 metadata cache compaction,
second pass**).  Release `.bss` falls from the `2026-08-17a` baseline of
16,356,016 to 14,847,456 bytes, **-1,508,560 bytes (-9.2%)**.  Across both
passes it is 20,193,552 -> 14,847,456, **-5,346,096 bytes (-26.5%)**.

Four independently bounded redundancies leave.  Per-form facts no longer
reserve three sixteen-byte operand arrays: the flags array had no reader, and
the two live source bytes now occupy one two-byte row per actual generated
operand.  The cache is 649,767 -> 186,769 bytes.  Publication first proves
that generated operand ranges are an exact partition, and fabricated forms
must match `operand_first`; malformed future tables retain the generic path.
Parsed pattern rows use the measured schema maxima (eight opcode bytes, one
trailing selector, two immediates), 178 -> 144 bytes and -374,442 cache bytes.
Every parser insertion is bounds checked, including a new fail-closed check
on the fixed-byte fallback.  Operand-view ownership and validity collapse
from u32 + u8 to one u16 `form_id + 1` sentinel, 164,065 -> 65,626 bytes and
one fewer hit-path load.  Finally, coverage rows and their hash index are no
longer copied from immutable generated blobs into BSS; access stays on the raw
generated records and the existing one-time validity table, removing 572,676
bytes without deriving or weakening the independent coverage schema.

Same-source A/B used the trusted `2026-08-17a` compiler and the final
candidate, both compiling the final unity TU to the same path, five
interleaved rounds.  Median instructions are 16,472,484,907 ->
16,458,050,713 (**-14.43 M, -0.088%**).  L1d misses are flat (186.81 M ->
186.87 M); cycles overlap run to run, so no wall or cycle claim is made.
An intermediate bounds check repeated inside the hot facts loop measured
**+0.0075% instructions**; moving that proof to cache publication recovered
the regression while retaining fail-closed access.

Final `test_self_host` holds the byte-identical fixed point at 35,497,824
bytes: stage 1 retires 16,458,333,744 instructions, stage 2 retires
115,854,728,928, and all 1,441,047 exact attempts succeed.  Release metadata
and completion-census tests preserve all counts and digests.  The full local
matrix passes Clang sanitized/unsanitized, GCC and Zig in Debug/Release plus
Clang static analysis.

`2026-08-17a` (Linux x86_64, Zen 4 7940HS; **x86 metadata cache compaction**).
The Release executable's `.bss` falls from 20,193,552 to 16,356,016 bytes,
**-3,837,536 bytes (-19.0%)**.  The two bulk savings are the string-pool NUL
distance table, narrowed from u32 to u16 (6.91 -> 3.45 MB), and sparse exact
plans, whose normalized operands now borrow the immutable prewarmed operand
view table instead of copying sixteen slots into every plan.  The exact-plan
record is 608 -> 232 bytes, **ten cache lines -> four**; a compile-time ceiling
keeps it at no more than four lines.  A future pool string of 65,535 bytes or
more saturates to the u16 invalid sentinel and fails metadata validation rather
than truncating.  The checked-in maximum is 276 bytes.

The last internal 120-byte `BusterX86MetadataEmitQuery` hop is borrowed too.
Its four callers already own stable locals for the duration of emission, so
the form worker no longer takes another by-value copy.  This is the smaller
copy left open by `2026-08-16b`; unlike moving the generated-form load behind
the policy branch, it survives measurement.  That branch rewrite measured
**+0.064% instructions** and was reverted.

Same-source A/B used separate trusted Clang Release compiler binaries, both
compiling the post-change unity TU to the same output path, five interleaved
rounds.  Median instructions are 16,484,634,274 -> 16,483,503,575
(**-1.13 M, -0.007%**).  Cycles, L1d misses and wall are flat: their
distributions overlap, so this is a footprint/cache-density change rather than
a claimed throughput win.  The profile that selected it records 16.47 G
instructions and 1,443,159 successful exact emissions, with
`buster_x86_metadata_emit_exact_machine` at 7.83%,
`emit_form_to_scratch` at 3.68%, and `emit_bind_form` at 2.21%.

Validation: `test_self_host` holds the byte-identical stage-1/stage-2 fixed
point (1,443,079 exact attempts, zero failures); Release `test_all` passes
308,861 assertions across 39 modules including the x86 completion census; and
`test_all_combinations` passes Clang sanitized/unsanitized, GCC and Zig in
Debug/Release plus Clang static analysis.

`2026-08-16e` (Linux x86_64, Zen 4 7940HS; **`CToken` 16 -> 12 lands** — the
one candidate `2026-08-16d` deferred). Same-source A/B against the merged
main (d91617cd + 794150ff included), both compilers clang-built Release
compiling the post-change unity TU with `-g`, seven interleaved rounds:
instructions 16.5729 G -> 16.8201 G (**+1.49%**), L1d misses 219.4 M ->
210.5 M (**-4.1%**), LLC flat, and cycles/wall statistically flat — the
distributions overlap run to run (an earlier five-round set against the
pre-merge base read wall -2.2%; treat both as inside this desktop's noise
and gate on the counters). The instructions-for-misses shape is the one
`CToken` 48 -> 16 (PR 221) and the `2026-08-16d` batch paid, at a weaker
exchange rate than either: the token arrays are a smaller share of the miss
profile than the structures those changes shrank. `test_self_host` fixed
point holds; `test_all_combinations` and the `_ci` variant green, 308,861
assertions (an early matrix run died on a gcc -Wsign-conversion in the new
fixtures — the clang-only inner loop cannot see gcc's warning set).

**What landed.** `pack_alignment` left the token row for a sorted
(token index, alignment) change list on `CPreprocessResult`, recorded where
tokens land in the final stream — directive pragmas and mid-expansion
`_Pragma` markers funnel through one lazy comparison, so the list stays
sorted and deduplicated by construction — and binary-searched by its one
reader, the struct/union layout pass (`c_preprocess_pack_alignment`).
`length` narrowed to u16 behind `C_TOKEN_LENGTH_OVERSIZED`: the sentinel is
stored only for terminated string/character literals, whose exact length
`c_token_length` re-derives by scanning the spelling to its closing
delimiter — the spelling's own last byte, so the scan never reads past it
and needs no side table, which retires the deferred design note's
offset-keyed-table problem outright (the first attempt's length-prefix
design failed on tokens whose offsets point at raw source text; the scan is
indifferent to where the spelling lives). Oversized identifiers and numbers
are a diagnosed implementation limit (`C_DIAGNOSTIC_TOKEN_TOO_LONG`),
oversized pastes fail as invalid pastes through the relex they already run,
and oversized `_Pragma` operands drop their marker like malformed ones. The
compaction emitter's row store interleaves the 12-byte rows through two
vpermi2d over masked 32-bit lanes — 64 + 32 stored bytes per eight rows, no
slop, the same instruction budget as the old 16-byte vpermi2q pattern.
Fixtures cover spelling lengths 65534/65535/65536/70003 through lex,
preprocess, parse and IR decode, the SIMD/scalar differential over each,
stringify- and paste-built oversized literals, an unterminated oversized
literal, and both too-long diagnostics.

**The mistake already paid for.** The first A/B measured **+6.49%**
instructions and +4.5% wall: `c_token_length` out of line was 4.5% of
stage-1 instructions by itself, because the sentinel guard runs on every
hot spelling read and clang kept the call at several hundred sites even
inside the unity TU. Inlining the fast path in c.h (the arena.h
header-inline pattern) over the extern cold half recovered it. The lesson
generalizes: a one-compare accessor in front of a field this hot must be
proven inline, not assumed — the gate would have shipped a regression
dressed as a shrink.

`2026-08-16d` (Linux x86_64, Zen 4 7940HS; **the `2026-08-16c` shrink batch
lands** — eight of the survey's nine candidates, one deferred). Same-source
A/B, both compilers clang-built Release compiling the post-change unity TU
with `-g`: instructions 16.4429 G -> 16.4741 G (**+0.19%**), L1d misses
244.5 M -> 216.9 M (**-11.3%**), LLC misses -13%, wall 1936 ms -> 1897 ms
median-of-3 (**-2.0%**), cycles -0.8%. The instructions-for-misses trade is
the same shape CToken 48 -> 16 paid in PR 221. `test_self_host` fixed point
holds; Release `test_all` 308,781 assertions green; Win64 `-g` fixtures run
under wine.

**What landed.** `IrInstruction` 96 -> 64 (one cache line: id dropped —
always its array index, `ir_instruction_self_id` recovers it by pointer
difference; seven enums as u8 behind CT range checks; target/immediate
counts u16 with the switch/computed-goto/inline-asm/aggregate lowerings
failing their existing error paths at 64K+ instead of truncating).
`DebugSourceLocation` 48 -> 20 (the embedded `String8 path` was a per-record
interned copy of what `source` already names; offset/length u64 -> u32;
`line == 0` is the no-declaration signal) — shrinks DebugType 176 -> 144,
DebugVariable 120 -> 88, DebugTypeField 96 -> 64, DebugScope 88 -> 56 and
deletes one `debug_string` per record. Entity lookup buckets sized from
interned names instead of identifier tokens (3 x 8 MB memset per parse ->
~1.5 MB total). Line rows 16 -> 12 (source/column u16, saturated in
`codegen_record_line`). `CodegenModuleRelocation` 40 -> 32 (addend first,
source as u8). `IrValue` 20 -> 16 (category as u8).

**Not taken, and why.** `CType`/`IrType`: every safe narrowing lands in
align-8 padding (the records stay 80/128; reaching 72/120 needs u16
member/enum counts that could silently overflow on generated code, or a
~30-site `IrType.id` removal, for ~2.8 MB tables). Relocation 32 -> 24
means finishing the documented kind migration that retires the six legacy
bools — 113 consumer sites, its own change. `CToken` 16 -> 12 is deferred
with a design note: lexed token offsets can point at raw source text, not
only the writable spelling space, so the u16-length escape needs an
offset-keyed side table (or unconditional length array) and dedicated
64 KB-token fixtures before it can be trusted — token spelling identity
feeds the fixed point.

`2026-08-16c` (Linux x86_64, Zen 4 7940HS; **struct-size / cache-behavior
survey** — measurement only, ranked shrink candidates for the
bulk-allocated compiler structs; the entry above records the landing). Baseline for the workload surveyed: clang
Release `ide` compiling the unity self-host TU with `-g` runs 16.42 G
instructions / 6.42 G cycles (IPC 2.56), 5.90 G L1d loads with **244.9 M L1d
misses (4.1%)**, 15.6 M LLC misses, 1.7 M dTLB misses, peak RSS 1.31 GB.
The LLC number says arena-sequential access prefetches well; the headroom is
L1/L2 line traffic, which is exactly what in-place struct narrowing buys.

**The census.** pahole over a `clang -g -O0` unity object plus temporary
count prints (since reverted) on one self-host compile:
2,923,747 preprocessed tokens (3,143,419 lexed; `CToken` 16 B), 47,317
entities / 37,123 scopes / 35,355 `CType` / 250,712 identifier uses on the
parse side; 3,642 functions, **1,220,033 `IrInstruction` (96 B each = 117
MB)**, 1,016,724 `IrValue` (20 B), 21,363 `IrType` (128 B), 17,526 `IrSymbol`
(80 B) in the IR; 68,941 `CodegenModuleRelocation` (40 B) and 443,120 line
entries (16 B) in codegen; 21,364 `DebugType` (176 B), 35,684 `DebugVariable`
(120 B), 6,156 `DebugScope` (88 B) in the `-g` debug model. The L1d-miss
profile: `dwarf_build_model` **18.0%** — three times the next symbol —
then `machine_select_canonical_function` 5.7%,
`codegen_generate_canonical_module_attempt` 5.1%, `c_preprocess` 3.7%,
`machine_selection_prepass_build` 3.5%.

**Find 1 — `DebugSourceLocation` carries a name it doesn't need (48 B →
~20 B).** It embeds a full `String8 path` (16 B) next to the `u32 source`
file id that already names the file, plus `u64 offset`/`u64 length` for
sources that are at most tens of MB. Narrowing it shrinks every struct it is
embedded in: `DebugType` 176 → ~144, `DebugTypeField` 96 → ~64,
`DebugVariable` 120 → ~92, `DebugScope` 88 → ~60. With the top miss symbol
being the DWARF walk over exactly these records, this is the first thing to
take.

**Find 2 — `IrInstruction` fits in one cacheline (96 → 64).** Thirteen
fields are 4-byte enums or counts whose ranges fit u8: `IrOpcode` has ~47
enumerators, `IrBinaryOperation` ~73, `IrConversionOperation` 15,
`IrUnaryOperation` 11, `IrAtomicOperation` ~10, `IrMemoryOrder` 6; the three
operand/target/immediate counts fit u16. `id` is pure redundancy —
`ir_function_add_instruction` (ir.c ~3045) always stores the instruction at
`instructions[id.value]` with `id.value = instruction_count++`, so it equals
the array index everywhere. The atomic trio (`memory_order`,
`failure_memory_order`, `atomic_operation`) could instead move wholesale to
the existing rare-payload side table (`IrInstructionExtra`,
`ir_instruction_extra_find`). At 1.22 M instances, 96 → 64 removes ~39 MB
of touched bytes and halves the lines per visit. This is **not** the
`2026-08-09b` SoA row split that measured +2.1% instructions — that split
added base pointers and address computations; in-place narrowing keeps one
address stream and makes lines denser. Precedent says the trade wins:
CToken 48 → 16 (PR 221) cost +2.2% instructions and paid −16% faults. Use
plain u8/u16 fields, not bitfields, and gate on the stage-1 counter.

**Find 3 — the entity lookup tables are sized off token occurrences, not
names (25 MB of 0xff).** `c_parse` sets `entity_capacity = identifier_count
+ 1` where `identifier_count` counts identifier *tokens* (~1.05 M), then
sizes three bucket arrays to the next power of two of twice that: 3 × 2 M
buckets × 4 B = 25 MB, `memset` to 0xff at every parse, then probed cold —
for 47,317 actual entities (load factor 2.3%, and every probe is a
guaranteed miss on an 8 MB cold table). A bound from unique names (the
symbol table exists before parse) would put all three tables around 1.5 MB
total, L2-resident. The fix is one sizing expression at c_parse.c ~10418.

**Find 4 — token and line rows have removable tails.** `CToken` 16 → 12:
`pack_alignment` occupies 2 B on all ~6 M lexed+preprocessed tokens but is
read only at type-definition-start tokens (c_parse.c ~1003) — a sorted
(token, value) side array covers it; `length` → u16 with an escape for the
rare oversized literal. `CodegenLineEntry`/`DwarfLineEntry` 16 → 12 by
narrowing `source` (228 files) and `column` to u16 — 443 K rows exist twice.
`CodegenModuleRelocation` 40 → 24 by moving `s64 addend` first (kills the
4-byte hole), `source` enum → u8, six bools → one flags byte. `IrValue`
20 → 16 (`category` → u8, `alignment` → log2 u8, bools → flags).
`CType` 80 → 72 and `IrType` 128 → ~112 the same way (kind/CC → u8, bools
→ flags; `CTypeKind` has 29 enumerators, `CEntityKind` 7).

**Left untaken, deliberately.** Inlining small operand arrays into
`IrInstruction` — 19 of the 47 `IrValueId` operand allocations in c_gen.c
are 1-element and 12 are 2-element, so `operands` is an 8-byte pointer to a
4-8-byte arena block chased on every visit; an inline-2 union would remove
the chase and the block, but it touches every operand consumer and belongs
in its own measured change. Likewise any DWARF *emission* restructuring
(dwarf_build_model re-walks the whole just-built model; a line-table delta
encoding would shrink 443 K × 16 B to a byte stream) is an audit of its own
in the `2026-08-15` metadata-emission mold. Aggregate for the finds above if
they all hold: roughly 90-100 MB less touched memory per self-host compile
out of 1.31 GB peak RSS, concentrated where the miss profile already points.

**Method.** Layouts from `pahole --sizes`/full dump over a single
`clang -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1
-DBUSTER_INCLUDE_TESTS=0 -g -O0 -c src/buster/apps/ide/ide.c` object (3.6 s
to produce); instance counts from `-v` `CODEGEN` statistics plus three
temporary `string_print` censuses in `c_analyze`, the driver's post-codegen
block, and `dwarf_build_model`, reverted after the run; miss attribution
from `perf record -e L1-dcache-load-misses` on the clang-built Release
`ide` (buster-built stages stay validation-only per the standing rule).

`2026-08-16b` (Linux x86_64, Zen 4 7940HS; **compiler throughput** — the
shared selection prepass stops rescanning the function per promotable local,
based on `ac583f40`). Stage 1 `18,717,675,527` -> `16,289,240,873`
(**-13.0%**), stage 2 `131,730,693,610` -> `119,829,765,574` (**-9.0%**),
`test_self_host` stage-1 wall 1.87 s -> 1.78 s.

**A quadratic in the shared prepass, and it was the top symbol.**
`machine_selection_prepass_build` was **10.8%** of a stage-1 compile, more
than twice the next symbol, and ~85% of its samples sat on the two compares
at the top of one loop. That loop asked, for every local it had just marked
promotable, whether every use of that local is a legal load/store place — by
walking the whole function's instruction and operand lists once per
candidate.

The early exit makes the cost shape counterintuitive: the scan stops at the
first disqualifying use, so the locals that cost the most are exactly the
ones that *survive* to be promoted. A function with N surviving candidates
and M operands paid N*M.

The question is per (use, local), not per local. One pass over every operand
now answers it for all candidates at once — look the operand's value up, skip
it unless it is still a live candidate, clear the flag when its place is
illegal — and a function with no promotable local skips the pass outright.
Clearing is monotone and the per-use verdict does not depend on the order
uses are visited, so the final flags are identical.

**The x86 selector already had the linear form.** `machine_x86_64.c` derives
its own target-refined promotion set and disqualifies inside the operand walk
(around line 3806), which is why only the shared pass was quadratic. Worth
knowing before assuming the two are the same code twice.

**A leftover 264-byte copy per emission.**
`buster_x86_metadata_emit_machine_fast` took its `BusterX86MetadataForm` by
value while the caller held `record->form`, a pointer into the same immutable
generated table; it read about two dozen scalar fields out of the copy. This
is exactly the leftover `2026-08-15i` named when it converted the candidate
loop to the borrow accessors — the fast emitter was simply never converted.
Small: `-31.6 M` (-0.19%). The remaining copies on this path are the 120-byte
`BusterX86MetadataEmitQuery` built at `x86_64_metadata.c:8512` and passed by
value at `:8539`, which is the single hottest instruction in
`buster_x86_metadata_emit_exact_machine`; it was left untaken because
`emit_form_with_form` has four call sites and the measured return on the
larger 264-byte copy was already only 0.19%.

**Method note — do not A/B the self-host output against itself.** The
self-host executable *is* the compiler's own source, so a change to any
compiler file legitimately changes those bytes: comparing a pre-change and
post-change self-host output measures the source edit, not codegen, and the
first reading here was a false alarm (a 64-byte size delta that was entirely
the added code). The valid comparison runs **both compiler binaries over the
same source tree**. Beware two more confounders met on the way: the output
path is embedded, so both sides must write to the same path, and 9 of the 55
`tests/basic_c_*.c` fixtures are negative tests that produce no object, so a
fixture sweep must gate on exit status or it will report nine phantom
differences.

**Verified equivalent rather than argued.** The rewrite looked obviously
equivalent and the first byte comparison appeared to contradict that, so the
flags were compared directly: a probe build ran both the old and the new
derivation over every function of the self-host unit and reported zero
divergences in flags or widths. Both changes then passed byte-identity —
identical input, pre- and post-change compilers, byte-identical self-host
executable and byte-identical objects for all 46 compiling fixtures with
matching exit status on the 9 negative ones. Stage-1/stage-2 fixed point
holds (`bytes=35869408`, both stages converging on 2.948.479 tokens), full
Release `test_all` green: 308,714 assertions across 39 modules.

`ide bench` is unchanged and is expected to be: both finds are in codegen and
x86 metadata, and the benchmark measures the C frontend path only. Its
run-to-run median spread on this host was 1.05-1.23 ms, wider than any effect
these changes could have there.

**What the profile looks like now.** Flat, and more so than before: the top
symbol is `buster_x86_metadata_emit_exact_machine` at 6.1% self, and nothing
else exceeds 4.3%. By phase, codegen is 47%, `c_lower_to_ir` 24.7%,
`c_analyze_semantics` 9.9%, `c_preprocess` 8.2%. Checked and found *not*
worth taking: the FAST allocator's placement loops are bounded by operand
count, not function size; `c_ir_constant_initializer_bytes_legacy` is a real
fallback for non-brace initializers rather than a duplicate of the new path;
and arena allocations are explicitly dirty (`arena.c` reuse contract), so the
selectors' per-function zero-init loops are required, not redundant. One
latent non-performance bug was noted in passing: `register_allocator_fast.c`
compares a `u8` slot against `UINT32_MAX` (~line 1692), a guard that can
never fire and is harmless today only because the following loop's own filter
covers it.

**The standing lever is now a fourth instance of the same shape.** An
immutable or per-use answer re-derived inside a loop was three for three
across `2026-08-15j` and `2026-08-16a`; this makes four, and it is still the
first thing to look for in this tree. A same-bound nested-loop scan over
`src/buster/lib/compiler/**` is a cheap way to generate candidates, but it
reports mostly false positives (sibling loops and loops bounded by operand
counts of eight), so each hit needs reading before it means anything.

`2026-08-16a` (Linux x86_64, Zen 4 7940HS; canonical AArch64 form validity is
derived once instead of per operation, based on `ae39b781`). Whole local
Release suite `7.48 s` -> `3.74 s` across the two finds below; cumulative with
the 2026-08-15j audit, `17.54 s` -> `3.74 s` (**-78.7%**).

`a64_canonical_form_valid` was **43.4%** of the entire suite's samples --
27.5% in lane workers and 15.9% on the main thread. It fully revalidates a
form's field, segment, source and constraint-program tables, and
`buster_a64_direct_simd_encode`, `buster_a64_direct_simd_decode_internal` and
every field accessor call it once per operation, inside the innermost loops of
the combinatorial aarch64 audits.

The answer cannot change. Everything it walks is `static const` generated
data, so a form's validity is fixed for the life of the process. It is now
derived once into a per-form byte and the predicate is a bounds check plus a
load:

```
aarch64_complex_simd       1.318 s -> 0.097 s   (13.6x)
aarch64_direct_simd        0.926 s -> 0.081 s   (11.4x)
aarch64_memory_semantics   1.638 s -> 0.821 s   ( 2.0x)
```

**This answers the question the audit opened with.** The three aarch64 suites
were 61% of the sanitized configuration and looked like irreducible
combinatorial coverage; they were not, and no generated case had to be given
up. It is the same shape as the two finds in `2026-08-15j` -- an immutable
answer re-derived in a loop -- which is now three for three, and is the first
thing to look for in this tree.

Published per the module rule: `buster_aarch64_prewarm` fills the table on the
calling thread under `BUSTER_CHECK_SERIAL_INITIALIZATION()`, sets its flag
last, and `test.c` calls it beside `machine_x86_64_exact_prewarm` before
`lane_run`, because every lane in that gang is an aarch64 suite. The lazy path
keeps the exact original derivation and **never writes the cache**, so a
caller that never prewarms stays race-free and merely pays the old cost.

**The same shape again, one layer down, and taken.**
With the validity cache in, `buster_a64_semantic_blob_byte` became 16.1% of
the suite (13.9% in lane workers) and was the whole reason memory semantics
moved only 2x. The generated semantic tables ship **base64-encoded in the
source**, so every byte access decoded a four-character group -- a divide and
a modulo by three plus four character lookups -- and `_u16`/`_u32`/`_u64`
redecoded the same group two, four and eight times over.

Each blob is now decoded once at prewarm and read by index, which costs
**0.91 MB** of zero-initialized storage (`VALUE_ATOM` 404,880, `OPERAND`
338,472, `FIELD` 93,696, `VALUE_ENTRY` 92,172, `SEGMENT` 23,428) and is paid
only by callers that prewarm. Memory semantics `0.821 s -> 0.343 s`, suite
`4.25 s -> 3.74 s`, and in the sanitized configuration the module is `32.1 s`
against the `78.4 s` it cost on CI before this audit.

The accessors keep the encoded blob as well as the decoded array and test the
ready flag, so an unprewarmed caller still decodes and still gets the same
answer -- the decoded arrays are zeroed until the flag is published, so
reading them early would silently return zeros rather than fail.

**The trio is no longer the tail.** It was 52% of the local suite
(3.882 s of 7.48 s) and is now 12% (0.457 s of 3.74 s), and no module
dominates what remains: machine 0.706 s, census 0.636 s, aarch64 encoding
0.381 s, C frontend 0.358 s, memory semantics 0.343 s, os 0.336 s.

Validation: full local `test_all_combinations_ci` green, 0 failing modules,
308,714 Release assertions across 39 modules. Test-visible behavior is
unchanged -- the cached and derived paths return the same answer -- and no
aarch64 case count was reduced.

`2026-08-15j` (Linux x86_64, Zen 4 7940HS; **CI test time, not compiler
throughput** — the two modules that had become most of it, based on
`a4049376`). Local Release module totals `17.54 s` -> `7.48 s`; in the
sanitized Debug configuration that gates every CI runner,
`x86_64_completion_census_tests` and `machine_tests` together fell from about
`300 s` to `47 s`.

Per-push CI wall time had roughly quadrupled in a week — `x86_64-linux`
2.6 -> 12.5 min, `x86_64-windows-znver5` 6.0 -> 19.6 min — and every runner
ended with one sanitized `test_all` step running alone on an otherwise idle
box for 39-49% of the job. `test_timing_summary` over the job logs put five
modules at 88% of all test time, with the top two at 56%.

**The census was 13 full-table scans and gaining one per feature commit.**
Each `buster_x86_completion_census_run` walks all 11,013 forms and assembles
every one in both dialects; ten of the thirteen existed only so a feature
group could show that removing its 1-3 CPU features moves exactly its 2-25
form ids. The count went `2 -> 13` on 2026-08-15 alone, one per
"Add x86 `<FEATURE>` model defaults" commit, so the module grew without bound
by construction.

The gate is now bought once for all groups: one merged baseline scan with
every group's features removed proves that nothing outside the union of the
group lists moves, and a per-group isolation pass re-evaluates just the
union's 67 forms under each group's own removal to prove each form is gated
on its own group's features and no other's. Those two statements are what
the ten scans asserted. `3.47 s -> 0.636 s` (**-81.7%**) with *more*
assertions than before, 31,160 -> 34,043. A new feature group is a table row
and costs no scan.

One correction worth recording: the baseline class is **not** uniformly
`SOURCE_POLICY_REJECTED`. The SHA512/SM3/SM4 rows are reached through more
than one feature gate and land elsewhere when their feature leaves, so the
isolation pass compares each form against its own classification under the
full target rather than against a fixed class. Asserting the fixed class
fails on 24 of the crypto group's 25 rows.

**`machine_tests` was quadratic in the source-authority audit.**
`machine_test_x86_source_authority_audit` was 55.4% of the module and
`machine_test_source_function_body_from` was 49.9% of that. For every
`codegen_canonical_x64_*`/`x64_emit_*` occurrence it searched *forward to
end-of-file* for a definition, but codegen.c holds 1,597 occurrences of only
86 distinct names — so ~1,511 call sites each scanned ~485 KB with a `memcmp`
per byte, about 730 MB of byte-at-a-time scanning per configuration, ASan
instrumented on the critical path. The name already starts at the offset in
hand, so it now asks whether a definition begins *there*: `8.15 s -> 0.797 s`
(**-90.2%**), and `122.32 s -> 12.38 s` in the sanitized configuration.

That also fixed a latent hole in the gate. A call site that resolved to a
later definition advanced the cursor past that definition's body, so owners
could be skipped without trace. Every definition is a prefix match at its own
offset, so the new form finds all of them and the audited set strictly
widened; `forbidden_count == 0` and `owners_found` still hold.

Validation: full local `test_all_combinations_ci` green twice (8
configurations, 0 failing modules), 308,699 Release assertions across 39
modules. Both changes are test-only, so the self-hosting fixed point is
untouched — `BUSTER_INCLUDE_TESTS=0` compiles neither file. aarch64 and
Windows verdicts come from remote CI.

**The census then moved to one tree per platform.** It is a whole-table audit
— its answer is a function of the generated metadata tables alone, so no
compiler or configuration can change it — and it was re-derived in all 8-10
of them. It now runs on the same single canonical tree that already owns
`clang_analyze` (unsanitized optimized Clang), through a `table_audit` flag on
the test descriptor and a `BUSTER_TEST_TABLE_AUDITS` env the superbuild sets
per tree. The default is *on*, so a bare `ide test` or any runner that does
not set it keeps full coverage; only the matrix opts a tree out. Verified on
the local matrix: census ran once instead of eight times, seven trees report
38/38 modules and one reports 39/39.

**Why that was worth more than its CPU number.** Counted as CPU it is only
~75 s of 542 s (Linux) — but only three modules are lane-eligible
(`test_parallel_lane` dispatches the two aarch64 SIMD suites and the memory
semantics suite; everything else runs serially), so the census's 41.8 s in the
sanitized Debug tree was **serial time on the critical path**. That tree is
129.3 s of serial modules plus a gang whose wall time is its slowest member,
78.4 s. Removing the census takes the serial part to 87.5 s.

**What is left in the tail, and it is not scheduling.** The trailing sanitized
step was 49% of the Linux job before this audit and 33% after; the earlier
reading that it was a scheduling problem was wrong. Trees are already declared
longest-first, and the tree's own floor is ~166 s: ~87 s of serial modules it
cannot overlap plus the 78.4 s slowest lane. Going below that needs either a
wider lane-eligible set — today the eligible descriptors must be *contiguous*
in `test_descriptors` and hand-listed in `TestDescriptorParallelKind`, and
`os_tests` must never overlap the gang — or fewer generated cases in the
aarch64 trio, which is 61% of the config and is combinatorial coverage rather
than waste. Neither is a free win; do not go looking for a scheduling bug.

`2026-08-15i` (Linux x86_64, Zen 4 7940HS; the form-selection candidate loop
stops re-deriving loop-invariant work, based on `91eba52e`). Stage 1
`15,991,731,490` -> `15,600,981,922` instructions (**-2.4%**) measured
single-lane, and `test_self_host` stage-1 wall 1.653 s. Cumulative across the
2026-08-15 audits: stage 1 `63,683,992,612` -> `15,600,981,922`, **-75.5%**.

All three finds are in `buster_x86_metadata_select_form`'s per-candidate loop,
which runs on the ~24.5% of emissions the byte table misses. Nothing in the
loop's *result* changed — see the verification below.

**`emit_parse_pattern` was already cached, so its cost was the copy.** It
returns `BusterX86MetadataPatternSemantics` **by value**, and that record is
176 bytes; the cache hit is a 176-byte copy out of a table the caller could
read in place. `buster_x86_metadata_pattern_semantics_borrow` already existed
for exactly this and was already used by `emit_bind_form` and
`emit_form_to_scratch` — the candidate loop was simply never converted. It
called `emit_parse_pattern` **three times per candidate** (the filter pattern,
the implicit-one test, and the FMA4 tie-break) and read four fields, one
field, and two fields from the three copies. This is precisely the leftover
`2026-08-15g` pointed at when it said to check whether any other consumer
still derives the per-form facts the long way.

**A 2,304-byte dead zero-initializer.** `candidate_operands[16] = {0}` zeroed
sixteen 144-byte operands per candidate, and both writers `memcpy` the query's
whole operand prefix before touching it. The array is now uninitialized; the
one later read is guarded by `inferred_memory_width`, which is set only on
those memcpy paths, so no uninitialized byte is ever read. Note that the
matrix's sanitizers are ASan+UBSan, neither of which detects an uninitialized
read, so this was established by reading every use rather than by a run.

**Three loop-invariant probes.** `aggregate_memory_source_topology_internal`,
`block_memory_source_topology_internal` and `prepare_typed_memory_query` each
begin by rejecting on conditions that depend only on the query, and the query
does not change across candidates — so each candidate paid a call and an
80-byte query copy to re-derive one constant answer. The query-side tests are
now hoisted out of the loop. They are used as a **necessary** condition, so
the guard can only skip a call that would have returned false; exhaustiveness
was not required and was not claimed. The two families are mutually exclusive
by construction, the topology pair requiring no decorator at all and the typed
probe requiring the broadcast decorator.

**Verification that selection did not move.** The byte-identity gates here are
stronger than a test count, because this loop chooses instruction encodings.
The pre-change and post-change compilers, given identical input, produce
identical output: 46 of 46 `tests/basic_c_*.c` fixtures compile to
byte-identical objects with matching exit status, and the complete self-host
unit compiles to a byte-identical **45,626,304**-byte executable. A change
that altered form selection anywhere in that 27 MB of code could not produce
that result.

Validation: byte-identical stage-1/stage-2 fixed point, byte-identical output
at 1, 4 and 16 lanes, 305,278 Release assertions across 39 modules, the
cross-compiler byte comparison above, and a full local
`test_all_combinations_ci`. That matrix is Linux-only, so the aarch64 and
Windows verdicts come from remote CI.

The remaining profile is flat: after this, no single symbol outside
`codegen_canonical_x64_metadata_emit_attributes` exceeds 8% of retired
instructions. That function is now the standing lever and is **not** a
cache-miss problem any more — it is the ~430 instructions per emission spent
building the key (packing up to 80 words, two hash chains over them, then a
ten-multiply value fold per operand) before either table is touched. Shortening
the value fold's dependent chain by mixing the five per-operand values with one
linear combination was considered and **not** taken: it would collapse a
nonlinear compression into a linear form whose collisions are constructible,
and a collision here is a silent miscompile, not a slow path.

`2026-08-15h` (Linux x86_64, Zen 4 7940HS; the byte-template table is probed as
a cache instead of a dictionary, based on `37d59296`). Stage 1
`18,150,530,759` -> `15,900,336,190` instructions (**-12.4%**) measured
single-lane, and `test_self_host` stage-1 wall 1.795 s. Cumulative across the
2026-08-15 audits: stage 1 `63,683,992,612` -> `15,900,336,190`, **-75.0%**.

The dominant find was the probe loop, not the hash and not the transform.
`codegen_canonical_x64_template_entry` walked up to **64** slots and inserted
only into an empty one. Both halves of that are wrong once the table fills,
and it filled: instrumented on one stage-1 self-compile at the then-current
65536 ceiling, the table reached **100% occupancy** (65,536 of 65,536), took
its last insertion at that point and never took another, **32.8%** of
4,920,459 lookups walked all 64 slots to return nothing, and the average
lookup cost **22.15 probe steps** for 109,010,573 steps in total. A third of
the lookups were futile by construction and the table had frozen on whatever
the module emitted first.

It is now a bounded window with replacement: probe two slots from home, and
when both belong to other keys, claim one of them. A key therefore always
lives within `home .. home + 1`, which is the invariant the lookup relies on
and the insertion maintains. The same instrumentation after: **1.34** steps
per lookup (6,193,886 for 4,614,842 lookups, a **17.6x** reduction) and the
hit rate up from 64.6% to **75.5%**, with 322,049 evictions showing the table
now tracks the working set.

The victim is picked from the guard rather than fixed at the home slot, so
eviction spreads across the window instead of one slot absorbing every
conflict. It stays a pure function of the key, which matters: cache contents
now depend on how work is split across lanes, so the emitted bytes must not.
Verified directly — the same unit compiled at 1, 2, 3, 5, 8 and 16 lanes
produces six byte-identical executables.

**The window is narrow, and that is measured, not assumed.** Probe steps are
paid on every emission while associativity only buys hits. Swept at the 65536
ceiling: 17.86 G instructions at a window of 1, **17.73 G at 2**, 17.74 G at
4, 17.83 G at 8, 18.07 G at 16. One slot loses too many hits; past two the
walk costs more than it wins.

**Bounding the probe also overturns `2026-08-15e`'s capacity ceiling, which
should now be read as a consequence of the old probe.** That entry chose 65536
because a larger table cost memory "for no measured wall improvement" — but
under a 64-slot never-replacing probe, a larger table helped mainly by staying
unsaturated. With the window bounded, only the slots a key actually lands on
are ever touched, so capacity costs address space rather than pages until it
outgrows the codegen arena's own high-water mark. Re-swept with peak RSS taken
from `VmHWM`:

```
entries   per lane   stage-1 instructions   peak RSS
  65536      2 MiB       17,728,627,486     1,338,940 kB
 262144      8 MiB       15,900,614,196     1,338,848 kB
 524288     16 MiB       15,568,244,627     1,393,996 kB
```

262144 is the largest capacity that is **free** in resident memory, and the
ceiling moved there; `2026-08-15e` had rejected it at an assumed 128 MiB cost.
524288 buys a further 2.1% for 55 MB and was not taken. The instrument is
known to respond: 1048576 entries reads 1,394,468 kB, so the flat middle row
is a real result rather than a measurement that never moves. `_ENTRIES_PER_FUNCTION` stays
64; the unit's 3,517 functions reach the new ceiling too.

Two smaller things. `c_parse_label_address_prefix` and its `_with_typedef`
sibling now take `CPreprocessResult` by const pointer. The struct is **688
bytes** and both are called once per identifier token, `c_ir_unsupported_gnu_
construct` calling the first once per token of every function body. Worth
**-0.14%**, which is far less than the by-value signature suggests and is
recorded so the next reader does not re-derive it: the win is real but the
copies were mostly already elided.

And a **negative result — do not repeat it**. Computing the value key early
and issuing a `BUSTER_PREFETCH` for its template slot before the form table's
probe, so the two independent misses overlap, measured **+0.38%** instructions
with no clock improvement. The branch on the form entry is predictable, so the
out-of-order window was already running both loads concurrently; all the
prefetch added was the value hash on the 16.5% of rows that return before
needing it.

Validation: byte-identical stage-1/stage-2 fixed point, 304,452 Release
assertions across 39 modules, lane-count byte-identity as above, and a full
local `test_all_combinations_ci`. That matrix is Linux-only, so the aarch64 and
Windows verdicts come from remote CI.

`2026-08-15g` (Linux x86_64, Zen 4 7940HS; selection stops re-deriving each
candidate's operand count, based on `2aa6930f`). Stage 1 `19,082,380,558` ->
`18,150,283,024` instructions (**-4.9%**) measured single-lane.

The change was first measured at -3.6% against `cc570ee2`, before the
byte-template table sizing of `2026-08-15e` had merged. Rebased onto a base
that carries it, the same change is worth more: the table absorbs most
emissions, so a larger share of what remains is selection, which is where this
saving lands. The numbers above are the rebased ones.

Letter `f` is left for the AArch64 index-stride audit still in flight on a
concurrent branch; this entry took the next free one rather than collide.

`buster_x86_metadata_emit_form_operand_count` re-parsed the form's pattern and
re-derived its moffs, maskmov, VSIB, writemask and per-operand visibility facts
on every call, and `buster_x86_metadata_select_form` calls it **once per
candidate it considers**. It was 3.01% of a quiet 36,354-sample profile, which
is a lot for answering "how many operands does this row expose".

For a row the facts table already marks `BIND_SIMPLE`, the answer is just
`form.operand_count`, and no derivation is needed at all. That shape excludes
moffs, maskmov and VSIB and requires every operand visible with no per-operand
flags, so none of the general path's skip or default branches can fire: the
moffs and maskmov supplemental skips need those forms, the implicit-operand
skip needs a hidden operand, the writemask default needs a writemask operand,
and the VSIB index default needs the VSIB field flag. Every operand is
therefore counted, which is the row's own count. The general path is retained
unchanged for every other row.

This is the same re-derivation that `2026-08-15a` and `2026-08-15b` removed
from `emit_form_to_scratch` and `emit_bind_form`; this call site was simply
missed at the time. It is worth checking whether any other consumer of the
per-form facts still derives them the long way.

Byte-identical fixed point, 304,062 Release assertions across 38 modules, and a
full local `test_all_combinations_ci`, all re-run on the rebased base. That
matrix is Linux-only, so the aarch64 and Windows verdicts come from remote CI.

`2026-08-15f` (Linux x86_64, Zen 4 7940HS; the AArch64 large-stride gap
`2026-08-15d` recorded is closed, based on `38907fbb`). **Not a throughput
change** — it is recorded here because the previous entry left the gap open and
pointed forward to this.

The canonical AArch64 backend emitted an indexed access's element stride as one
`movz`, and rejected outright — `CODEGEN_ERROR_INVALID_IR`, reported as *"error
4 ... opcode 22, operation 73"* — any element whose size did not fit that
16-bit immediate. A stride is an arbitrary 64-bit constant, so it now
materializes as the shortest `movz`/`movk` chain. The machine-IR AArch64
selector never had the limit; it goes through `machine_a64_emit_immediate` and
caps at `INT32_MAX`. Only the canonical path, which is what actually emits,
carried it.

**Nothing that already compiled changed a byte.** The new
`a64_emit_constant_compact` starts at the lowest halfword that carries a bit,
so every stride below 2^16 still costs exactly the one `movz` the site used to
write inline, with identical bits. `a64_emit_constant` keeps its fixed
four-word shape for its own callers.

**No stage-1 delta is claimed, because none can exist**: an x86-64 self-compile
emits no AArch64 code, so this change does not run. The `test_self_host` stage-1
reading on this base is `18,696,842,949`; do not difference it against
`2026-08-15g`'s `18,150,283,024`, which is a single-lane measurement of a
different thing. This branch was rebased four times as the 2026-08-15 series
landed, and the same bytes measured `25,968` to `26,136` M at the base before
last — a reminder that a reading is only comparable to one taken the same way
on the same base.

`IR_OPCODE_FIELD` was checked and never had the problem: it already reached
`a64_emit_constant` for offsets past the `add` immediate.

The `2026-08-15d` workaround stays. `x64_metadata_caches` remains one pointer
per lane rather than one strided block: it costs nothing measurable and the
indexed stride stays eight bytes however far the cache grows. Its comment now
says that is a choice rather than a forced one.

Validation: byte-identical stage-1/stage-2 fixed point, 304,442 Release
assertions across 39 modules, a full local `test_all_combinations_ci`, and a
new `aarch64_stride_tests` module carrying strides of 8, 32,768, 65,520,
65,536, 65,544, 1 MiB and 1 MiB + 24 through both AArch64 backends. It asserts
the materialized value *and* the word count, so the sub-2^16 cases pin the
single-`movz` shape as tightly as the large ones pin the fix; with the fix
reverted, exactly the four large cases fail. The `2026-08-15d` caveat still
holds — the matrix is Linux-only and the aarch64 verdict comes only from the
remote `aarch64-macos-mini` job, which must be waited for.

`2026-08-15e` (Linux x86_64, Zen 4 7940HS; the byte-template table is sized
from the module, based on `6aad801c`). Stage 1 `26,223,851,886` ->
`19,523,164,737` instructions (**-25.6%**) and stage 2 `385,481,602,329` ->
`291,795,218,843`. Cumulative across the 2026-08-15 audits: stage 1
`63,683,992,612` -> `19,523,164,737`, **-69.3%**, and `test_self_host` stage-1
wall 2.679 s -> 1.867 s, **-30.3%**.

`2026-08-15d` fixed the value-keyed table at 4096 entries. That number was
carried over from the census that sized the *measurement*, not from any
property of the workload, and it was starving the table. Sweeping it on the
self-host unit:

```
entries   per lane   stage-1 instructions
   4096    128 KiB      25,940,657,745
  16384    512 KiB      21,875,845,404
  65536      2 MiB      18,970,096,260
 262144      8 MiB      16,575,967,181
 524288     16 MiB      16,266,344,167
```

**Wall time is flat across that entire range** — 1,811 to 1,915 ms on repeated
best-of-three runs, inside this box's run-to-run spread. The instruction
counter improves monotonically and the clock does not, so the ceiling is chosen
on memory: 65536 entries is 2 MiB per lane and 32 MiB across a sixteen-lane
gang, proportionate to what a unit of this size already costs elsewhere
(27 MB of stack values, 26 MB of code). Going to 262144 keeps paying in
instructions but costs 128 MiB for no measured wall improvement, so it was not
taken.

The table is now sized from `module->function_count` rather than fixed, at 64
entries per function between a 4096 floor and that ceiling. A fixed constant
either starves a large translation unit or makes a ten-function one pay for a
large one; measured, a small compile is unchanged at 606,061,711 instructions
and 6,006 page faults against 606,012,154 and 6,007 before.

Two measurement traps were paid for here and are worth repeating. **Task-clock
is not wall time for a multi-lane run**: at 16 lanes a larger table lowered
task-clock while doing nothing for the clock, because the cost it adds is
memset and page-fault work spread across lanes. And a single `test_self_host`
stage-1 reading of 3.327 s looked like a 78% wall regression from the table; it
was contention from other work on the box, and a repeat at the same setting
gave 1.867 s. Confirm a wall regression against a quiet machine before
believing it.

Two other things were tried at the top of this profile and **measured
negative or neutral; do not repeat them**. Folding the mnemonic and feature
spellings as one or two 64-bit words instead of a byte at a time was **+0.26%**
(a variable-length `memcpy` costs more than the short byte loop clang already
handles well). Passing the physical query to the three key helpers by const
pointer instead of by value was **+0.001%** — clang was already passing those
by reference, the `vmovdqu64` pairs in the annotation notwithstanding.

`2026-08-15d` (Linux x86_64, Zen 4 7940HS; a second, value-keyed byte-template
table, based on `ec884958`). Stage 1 `30,171,862,831` -> `26,751,605,860`
instructions (**-11.3%**) and stage 2 `429,417,305,821` -> `389,039,815,381`.
A single-lane stage-1 compile measured 3,255 ms -> 2,721 ms of task-clock
(-16.4%). Cumulative across the four 2026-08-15 audits: stage 1
`63,683,992,612` -> `26,751,605,860`, **-58.0%**, and `test_self_host` stage-1
wall 2.679 s -> 1.858 s, **-30.6%**.

**This replaces the one-patch-slot design `2026-08-15b` proposed; do not build
that.** A hit census settled it. Over 4,864,621 canonical emissions in one
stage-1 self-compile, the existing value-free key hits `4,838,212` times
(**99.46%**) but only 16.5% of emissions are value-free and therefore
templatable under `2026-08-15c`. Hashing the same key *plus* the operand values
matches **60.9%**. So rather than teaching a patch slot to re-derive the
computed displacement inside the byte-authority module, the values simply go in
a key — and then the byte string is fully determined and a hit has nothing to
patch at all.

The two tables answer different questions and are sized for it. The existing
one answers *which form is this*, which operand values do not change, so it
stays value-free and keeps its 99.46% hit rate feeding
`buster_x86_metadata_emit_form_selected`. The new one answers *what are the
bytes*, which values do change; it is keyed by shape plus every operand payload
that can reach the bytes without changing the shape (signed and unsigned
immediates, the memory displacement, and both addends). One key build serves
both. A template miss falls through to the form table, so this cannot make
emission worse than not having it. Capacity is 4096 entries of 32 bytes per
lane; the fragment-arena reservation already scales with
`sizeof(CodegenX64MetadataCache)` and needed no change.

Value-free rows keep their template on the form entry rather than moving to the
value table, because the shape alone pins them and nothing there can evict
them.

One correctness gap is closed explicitly rather than by inference. A **symbol**
is the one operand payload the key cannot carry: two different symbol names
give the same shape and the same values. Such a row also needs a relocation and
this path offers no relocation capacity, so it fails before reaching the
capture — but the guard now sits next to the key instead of resting on that,
at a measured cost of 0.13%. The same reasoning that pins the bytes in
`2026-08-15c` otherwise carries over unchanged.

The value hashes then stopped walking the key words a second time. The shape
hashes already summarize every word, so folding the operand values into those
gives the same separation for half the work: `26,495,655,699` ->
`25,935,567,890` instructions, a further **-2.1%**, for the stage-1 total
above.

**A local `test_all_combinations_ci` is not sufficient for a change that alters
a shared structure's size, and this audit is the proof.** The matrix is
Linux-only; aarch64 and Windows exist only in remote CI. The value-keyed table
took `CodegenX64MetadataCache` past 64 KiB, and the macOS self-host then failed
to compile `codegen_canonical_parallel_lane` for aarch64 with *"C code
generation failed with error 4 ... opcode 22, operation 73"* — buster could no
longer compile itself. Reduced to a fixture, **the AArch64 backend cannot lower
a variable index into an array whose element stride reaches 2^16**: 65,520
bytes compiles, 65,536 fails. That is a pre-existing backend gap that any array
of >=64 KiB structs indexed by a variable will hit, recorded here because this
is where it surfaced; it is not fixed by this audit. The cache array became one
pointer per lane, each cache its own allocation, so the indexed stride is 8
bytes whatever the cache grows to. That costs nothing measurable
(`25,935,803,166` against `25,935,567,890`).

Validation: byte-identical stage-1/stage-2 fixed point, 302,625 Release
assertions across 38 modules, and a full local `test_all_combinations_ci` —
with the caveat above that the aarch64 and Windows verdicts come only from
remote CI, and must be waited for.

`2026-08-15c` (Linux x86_64, Zen 4 7940HS; value-free emissions are memoized as
byte templates, based on `879d3e00`). Stage 1 `32,738,139,154` ->
`30,171,862,831` instructions (**-7.9%**) and stage 2 `466,777,002,988` ->
`429,417,305,821`. Cumulative across the three 2026-08-15 audits: stage 1
`63,683,992,612` -> `30,171,862,831`, **-52.6%**.

The census in `2026-08-15b` found that 17.1% of canonical emissions write no
operand-derived value field. For those the emitted byte string is a pure
function of the codegen cache key, so the entry now retains the bytes and a
later hit copies them instead of running the transform. The remaining 82.9%
are untouched and still emit normally.

The soundness argument is worth stating because it is not "the key looks
specific enough". `buster_x86_metadata_emit_write_le` is the only path that
writes an operand *value*, and `value_field_count` counts its calls, so zero
means no value reached the bytes. Everything else that varies per query does
reach the bytes — register numbers through ModRM/SIB/REX, and the displacement
size class through the ModRM mod field — but all of it is written with
`emit_write_byte` from inputs the key already carries: `reg.index`, the memory
base and index registers, the scale, and the displacement and immediate size
classes. So the byte string is pinned by the key exactly when
`value_field_count` is zero. Templates are also capped at 15 bytes and skipped
for any row with a relocation.

Capture happens after a successful emission rather than at insertion, so a
first pass with no output buffer does not lose the chance; a later untemplated
hit fills it.

Validation beyond the usual: the stage-1 binary that this path produces is
itself the compiler that produces stage 2, so a template emitting wrong bytes
would have to survive compiling the whole compiler and still land on a
byte-identical fixed point. That held, alongside 302,625 Release assertions
across 38 modules and the full matrix including the x86 completion census.

`2026-08-15b` (Linux x86_64, Zen 4 7940HS; the canonical x86-64 path takes the
prepared binding route, based on `01758505`). This is the lever `2026-08-15a`
identified and left untaken, plus what profiling it exposed underneath.

Stage 1 falls from `41,169,326,171` to `32,738,139,154` instructions and from
2.679 s to 1.917 s; stage 2 from `550,581,511,720` to `466,777,002,988`.
Measured against the pre-audit base of `2026-08-15a`, the two audits together
are **-48.6% stage-1 instructions and -28.4% stage-1 wall**. Every step below
reached a byte-identical stage-1/stage-2 fixed point, and the Release suite
passed 301,723 assertions across 38 modules.

| change | stage 1 | delta |
|---|---|---|
| base `01758505` | 41,169,326,171 | — |
| prepared binding shape from the facts table | 42,745,188,367* | -7.6% |
| binding-field indices in the facts table | 42,173,920,620 | -1.3% |
| cache hit skips the re-derived ISA gate | 40,835,168,833 | -3.2% |
| **normalized operand-view cache** | 33,483,566,251 | **-18.0%** |
| borrow the form row instead of copying it | 33,172,440,226 | -0.9% |
| borrow the parsed pattern | 32,643,825,328 | -1.6% |
| borrow the form into binding | 32,507,449,877 | -0.4% |

*measured against the preceding single-lane figure of 46,279,216,101; the
column is the direct `BUSTER_TEST_JOBS=1` measurement, which runs slightly
below the `test_self_host` number in the prose.

The dominant find was not the planned one. **`buster_x86_metadata_operand()`
had no cache at all**, despite the prewarm's comment claiming it filled an
operand-view cache: every call rebuilt the normalized view, and resolving an
imported width token is several string comparisons. Binding asks for every
operand of a row on every emitted instruction, so this one omission was worth
more than every other change here combined. Views are now keyed by generated
operand record (32,813 of them, ~790 KB) with the owning form recorded beside
each entry, because nothing in the snapshot guarantees a record range belongs
to exactly one form; a mismatch simply recomputes.

The planned lever worked as predicted but smaller: the facts table gained each
row's per-operand flags, the simple-binding shape bit, and which operand fills
each encoding field, all derived exactly as `exact_plan_prepare` derives them
so a prepared plan and the table cannot disagree. Binding now returns before
the generic hidden-operand walk and emission skips five linear binding scans.
**Preparing real exact plans for the canonical backend was not needed and
should not be attempted** — the 1024-entry plan capacity never came into it,
because the facts table is form-indexed and already covers all 11,013 rows.

`buster_x86_metadata_emit_form_selected` is a new public entry for a caller
that memoizes a checked selection under a key covering the mnemonic, operands,
attributes and target features; it skips the coverage/execution-mode/feature
re-derivation that selection already performed, and retains every structural
check. This is the same reasoning that removed the mnemonic lookup in
`2026-08-15a`.

**Correcting `2026-08-15a` on struct copies.** That entry ruled out
pass-by-pointer for the ~256-byte `BusterX86MetadataForm` at "~0.4%". That
number was extrapolated from a profile in which other costs dominated, and it
was wrong once they were removed: `perf annotate` later attributed **63.5% of
`emit_form_exact_policy` to three `vmovups %zmm` struct copies**. Borrowing the
form and the parsed pattern through `emit_form_with_form` and `emit_bind_form`
cut that function from 9.47% to 4.50% of the stage. The instruction-count gain
is small (-2.9% across the three borrow steps) because a 64-byte move is one
instruction; the win is in cycles and wall time. Read that as a reminder that
this profile's cycle shares and instruction shares diverge sharply — the same
divergence `2026-08-15a` recorded for the pool-string helpers, in the opposite
direction.

Where the remaining time sits: the metadata emitter plus canonical codegen is
now **52.7% of the stage, down from about 75%**, and frontend/IR work
(`c_ir_lower_frame_fallback`, `ir_validate_canonical_module`, `c_preprocess`)
is visible again. `emit_form_to_scratch` is still the single largest entry at
18.76%, and what is left there is the encoding logic itself — prefix, REX,
ModRM/SIB, displacement and immediate construction with its validity checks —
not argument passing or re-derivation. Cutting it further means giving the
canonical cache the byte-template mechanism the MIR path already has
(`machine_fast_kind` SCALAR/TEMPLATE: prepared bytes plus the offset and width
of the fields to patch). Stage 1 is still about 3.6x the 9.02 G pre-routing
baseline, and that mechanism is where the rest of it is.

**Measure the template's shape before building it; a census was taken and it
is not what the paragraph above assumes.** Every little-endian value field goes
through `buster_x86_metadata_emit_write_le` and nothing else does, so tagging
its five call sites counts exactly which emissions depend on an operand value.
Over one single-lane stage-1 self-compile, of 4,863,969 relocation-free
canonical emissions:

```
no value field at all                                829,657   17.1%
computed address displacement (emit_address)       3,469,582   71.3%
immediate                                            387,094    8.0%
relative                                             177,619    3.7%
both displacement kinds                                   17    0.0%
```

Three things follow. **A template with no patch slot covers only 17.1%** of
emissions — but see `2026-08-15c`: that still measured **-7.9%** of stage-1
instructions, because those emissions each cost the full transform and the
template replaces it with a `memcpy`. The "2-3% of the stage" first written
here was wrong; it divided the emission share by total stage work instead of
weighting by per-emission cost. **Essentially every
emission needs at most one patch slot** (82.9% have exactly one value field and
17 have two), so the descriptor table can be a single slot rather than a list.
And the dominant field is the **computed** displacement written from
`address.displacement`, which `buster_x86_metadata_emit_address` derives — it
is not the raw `memory.displacement` of the operand. A patch slot therefore
cannot be filled by re-injecting an operand field; the value has to come from
re-running that derivation, which is its own 1.54% of the stage. A template
that skips everything except `emit_address` is still the right target, but it
is a narrower win than "skip emission", and anyone sizing this work should
start from these numbers rather than from the sentence above.
**[Superseded by `2026-08-15d`: do not build the patch slot. Putting the
operand values in a second key removes the need to patch anything, covers 60.9%
rather than the 17.1% above, and needs no value re-derivation inside the byte
authority. The census numbers in this paragraph stand; the design conclusion
drawn from them does not.]**

`2026-08-15a` (Linux x86_64, Zen 4 7940HS; canonical x86-64 emission stops
re-deriving per-form metadata on every instruction, based on `134b96b0`).

**The stage-1 regression this entry attacks was 7x, and it is only partly
repaid.** A trusted Clang Release stage-1 self-compile retired
`63,291,586,657` instructions against the `8,966,954,627` recorded in
`2026-08-14b`. A four-point bisect over the `route x86 ... through metadata`
series located it, measuring the same single-lane stage-1 command on each
build:

```
f8f3a2b3  9,022,417,769   before the routing series      (baseline)
649d7596  624,118,764,392 routed, no cache               69x
f4b8bfe8  646,549,725,603 routed, no cache               72x
6416d299  64,740,628,621  canonical form cache landed     7.2x
134b96b0  63,291,586,657  head at audit time              7.0x
```

The recent `codegen: cache canonical x86 metadata forms` commit and its three
follow-ups are therefore a **fix, not a cause**: they recovered a 10x factor.
The residue is that the canonical backend (`allocator=none`, the path stage 1
and stage 2 use — the MIR path with its prepared plans is a separate `machine
stage`) has no prepared exact plan, so every cache *hit* still re-derived each
per-form fact through the plan-less side of the `plan ? cheap : derive`
branches in `emit_form_to_scratch`/`emit_bind_form`, each a string comparison
against an iclass or category spelling.

A 120,383-sample retired-instruction profile of that stage attributed 16.30%
to `emit_form_to_scratch`, 13.27% to `emit_bind_form`, 10.17% to
`codegen_canonical_x64_metadata_query_hash`, 5.04% to
`pool_string_equal_literal`, 3.43% to `lookup_text`, 2.62% to
`emit_effective_field_source` and 2.52% to `emit_is_maskmov` — roughly 75% of
the whole compile inside the metadata emitter. Four changes followed, each
measured on its own and each holding the self-host fixed point:

| change | stage 1 | delta |
|---|---|---|
| base `134b96b0` | 63,291,586,657 | — |
| packed cache key, one dual-chain hash | 61,278,652,257 | -3.2% |
| prewarmed per-form facts table | 55,283,996,391 | -12.7% |
| re-emit through the durable form key | 46,342,991,578 | -26.8% |
| hoisted feature-ladder span resolution | 46,279,216,101 | -26.9% |

The cache key was two byte-at-a-time FNV passes over the query's raw struct
bytes; it is now built once as packed 64-bit words and hashed word at a time
with two independent chains, which also keeps structure padding out of the
hash. The per-form facts table records the ten derived facts the plan-less
path recomputed (`moffs`, `maskmov`, hidden segment override, `notrack`,
`loop`, `jecxz`, `requires_dfv`, `dataxfer`, the pattern control blocker, and
both field-source arrays), filled by the prewarm loop that already walks all
11,013 forms and published after it. It mirrors the **plan-less** spellings
deliberately: the binding loop rewrites an operand's field source to the
pattern-aware value and only then reads the effective source, and an exact
plan's `effective_field_sources` is a different function, so substituting it
would change emitted bytes. The largest single win was re-emitting a cache hit
through `buster_x86_metadata_emit_form_exact` with the stored
`{form_id, stable_hash}` instead of `emit_form`: the latter re-ran
`lookup_mnemonic` — a 256-byte buffer clear, a normalize, a binary search, then
a scan of every candidate — purely to re-verify a mnemonic the cache key
already contains, and the durable key is a strictly stronger identity check.

Cumulative: stage 1 `63,683,992,612` -> `46,724,514,531` (**-26.6%**) and
stage 2 `881,471,005,675` -> `650,331,336,690` (**-26.2%**) as reported by
`test_self_host`. Every step reached a byte-identical stage-1/stage-2 fixed
point (final `SELF_HOST deterministic bytes=45458416`), and the Release suite
passed 301,723 unit assertions across 38 modules. Machine-stage instructions
are unchanged at `338.2 G` (`1,461,017` exact attempts, zero failures), as
expected: that path already had prepared plans and none of this touches it.

Two results worth keeping. The instruction count is **not** inflated by worker
spin — the same compile retires `63,291,586,657` at one lane and
`63,685,166,439` at sixteen, so this is real work and `BUSTER_TEST_JOBS=1` is
the right way to profile it without smearing samples. And the feature-ladder
hoist returned only -0.14% of instructions despite `pool_string_equal_literal`
showing 5-6% of *cycles*: that cost is cache misses on the per-pool-byte
`nul_distances` table, not instructions retired. Do not read cycle shares off
this profile as instruction shares; they diverge badly in the string helpers.

**The remaining 5x is one identified lever, deliberately left untaken here.**
`emit_form_to_scratch` (14.8%) and `emit_bind_form` (15.0%) are still the top
two, and their cost is the function bodies, not argument copying — the ~256
byte `BusterX86MetadataForm` passed by value through the chain accounts for
only about 0.4%, so a pass-by-pointer refactor is not the answer and should
not be attempted on that theory. **[Superseded by `2026-08-15b`: that 0.4% was
extrapolated from a profile where other costs dominated and is wrong once they
are gone; the copies were later measured at 63.5% of one of these functions
and borrowing them was worth taking. The rest of this paragraph stands.]** The
answer is that a prepared plan lets
`emit_bind_form` return from its `if (plan)` block before the binding loop
runs at all. Getting one requires preparing exact plans for the shapes the
canonical backend selects during the serial prewarm, the way
`machine_x64_metadata_shape_cache_prewarm` already does for the MIR path;
`BUSTER_X86_METADATA_EXACT_PLAN_CAPACITY` is 1024 and the canonical backend's
distinct form set should fit, but that must be measured before relying on it.
Do not instead widen the plan-less fast paths ad hoc, and do not reintroduce
target-local byte templates, which `2026-08-14b` already ruled out.

`2026-08-14b` (Linux x86_64, Zen 4 7940HS; single metadata authority for the
x86-64 machine encoder, based on `3dec8b04`). The MIR x86-64 encoder no longer
contains a second opcode/prefix/REX/ModRM/EVEX byte construction path. Its
dense 122-row registry now contains 77 exact forms, 19 exact sequences, and 26
metadata-driven expansion policies, with zero `LEGACY_RAW` rows. All complete
instruction byte strings come from the x86 metadata emitter; the remaining
machine-buffer writes are only post-emission relative branch, call, TLS, and
block-fixup displacement fields. `MachineInstruction` remains 24 bytes.

Serial target prewarm resolves immutable exact plans and compact machine
tokens before workers start, publishes the maps ready-last, and audits all 96
exact rows plus all 26 expansion rows. Expansion shapes use a 166-row prepared
cache, with zero invalid rows. The trusted machine entry preserves register,
operand, feature, APX/EGPR, address, immediate/range, capacity, and relocation
validation. Exact and generic helper failures set encoder overflow rather than
silently omitting an instruction. Differential coverage includes scalar,
memory, immediate, relative, implicit DIV/IDIV, SSE, EVEX/mask, atomic/lock,
prologue/epilogue, aggregate-copy, variadic, and fixup-bearing expansions.

One same-session, isolated Release self-host A/B was taken against the
then-current `origin/main` (`e7df02b5`); the documented gate stops repetitions
when the machine-stage regression exceeds 10%. Both sides reached a
deterministic stage-1/stage-2 fixed point. Baseline output was 43,255,256 bytes
and 2,837,403 preprocessed tokens; the candidate was 43,835,808 bytes and
2,880,239 tokens.
Stage-1 instructions rose from 8,802,558,370 to 8,966,954,627 (+1.868%), and
stage 2 from 124,542,574,336 to 126,821,664,800 (+1.830%). Machine-stage
instructions rose from 222,226,384,606 to 251,061,881,870 (+12.976%). The
candidate performed 1,387,553 exact attempts with 1,387,553 successes and zero
failures, versus 1,054,098/1,054,098/0 for the baseline. The standalone
machine-stage benchmark remained instruction-flat at 937,534,186 baseline
versus 937,534,036 candidate.

The regression is therefore compiler work, not generated-code quality: exact
emission traffic rises 31.6% because every former family/expansion byte route
now crosses the metadata contract. Normalized machine-stage work rises 11.30%
per preprocessed token, while ordinary stage work rises about 0.3% per token.
An explicit prepared-token classifier was measured separately and rejected:
it reduced machine-stage instructions by only 0.029% while adding a large
hand-maintained shape classifier. Prepared fixed/relative templates and scalar
DIV/IDIV projection were retained because they preserve the single byte
authority and reduce the metadata fallback path without duplicating byte
logic. The remaining cost is recorded as the deliberate price of removing the
redundant encoder; future work should optimize the metadata emitter itself,
not reintroduce target-local byte templates.

Debug and Release suites each passed 268,929 assertions across 38 modules; an
isolated `BUSTER_SINGLE_THREADED=ON` Release suite passed 268,912 assertions
across the same modules. Release and single-threaded self-hosts produced the
same 43,835,808-byte fixed point, the same 2,880,239-token stream, and
1,387,553/1,387,553/0 exact results. The full local compiler/platform matrix
passed all 13 lanes: Clang and GCC Debug/Release, Zig Debug/Release, sanitized
Clang Debug/Release, fuzz-enabled builds/tests, static analysis, completion
census, and the existing-artifact self-host fanout. The ordinary lanes passed
268,929 assertions across 38 modules; sanitizer/fuzz lane totals differ only
by their configured test surface. The completion census emitted all 10,607
normalized forms with zero blocked forms.

`2026-08-14a` (Linux x86_64, Zen 4 7940HS; exact machine-encoder dispatch and
prepared-token fast path, based on `00ed8ca5`). A 118,983-sample retired-
instruction profile of a trusted Clang Release FAST self-compile attributed
9.018% of the stage to `x86_64_metadata.c`: 2.126% in
`buster_x86_metadata_emit_form_to_scratch`, 1.137% in `emit_bind_form`, 0.551%
in pattern parsing, 0.514% in field binding, 0.389% in generic physical-query
validation, and 0.350% in the prevalidated exact entry point. Durable plan
lookup itself was only 0.055%; the repeated generic/static policy setup and
binding work, not the registry lookup alone, was the measured target.

The change builds a dense 122-entry, 16-byte opcode projection during serial
prewarm and resolves the 30 unique migrated exact forms to immutable 4-byte
machine tokens. Token creation validates the durable form identity, ordinary
coverage, 64-bit mode/address policy, feature policy, and REX2/APX requirement;
the hot bridge retains operand, register, addressing, range, dynamic EGPR/APX,
instruction-length, output-capacity, and relocation checks. An integrity byte
binds a token's slot and policy to the prepared form, so accidental policy-bit
mutation fails closed. The public checked and prevalidated exact APIs are
unchanged. Exact output is copied into the machine buffer in one `memcpy` only
after metadata emission and capacity validation. `LOAD_INCOMING` remains the
sole legacy DIRECT recipe because its required disp32 shape still differs from
metadata's canonical disp8 relaxation.

Three same-session Release self-host measurements per side were stable. The
unmodified base's machine-stage instruction counts were
`228,051,829,742`, `228,051,695,910`, and `228,051,708,897` (median
`228,051,708,897`). The candidate counts were `222,156,465,779`,
`222,156,498,981`, and `222,156,511,247` (median `222,156,498,981`):
**-5,895,209,916 instructions, -2.585%**. The candidate performed 478,593
exact attempts with 478,593 successes and zero failures; the base performed
477,730/477,730/0, with the count difference coming from compiling the added
source itself. The machine-stage benchmark remained flat at median
`5,936,804,903` base versus `5,936,667,261` candidate instructions.

The ordinary self-host fixed point remained byte- and token-identical between
stage 1 and stage 2. Base deterministic output was `47,775,536` bytes and
3,202,289 preprocessed tokens; candidate output was `47,842,352` bytes and
3,205,978 tokens. The source addition raises median stage-1 compiler work from
`10,144,779,854` to `10,156,799,956` instructions (+0.118%) and stage 2 from
`143,157,363,931` to `143,314,754,783` (+0.110%); these are recorded rather
than hidden. Debug and Release suites passed 282,317 assertions across 46
modules; a single-threaded Release build passed 282,302 assertions across the
same modules. The full Linux `test_all_combinations_ci` matrix passed Clang,
GCC, Zig, sanitized Debug/Release, fuzz, graphical smoke, static analysis, and
its tests-disabled artifact-fanout self-host. Wall time and parser-only
`ide bench` were not used as encoder gates.

Deliberately left for a later measured change: specialize the remaining
dynamic binding/scratch path or precompute static selector facts. The profile
still attributes more time to `emit_form_to_scratch`/`emit_bind_form`, while
the unrelated machine selection prepass alone is 9.97% of the sampled stage.
Do not weaken the public exact API, move worker-mutated state past prewarm, or
resume FAMILY migration until the specialized path has its own differential
and self-host measurements.

`2026-08-13d` (Linux x86_64; file-map `madvise(MADV_SEQUENTIAL)` A/B experiment,
based on `053de57e169095f9e642b9d4275f8170307bf5c1`, not landed). The proposed
change was deliberately minimal: call `madvise(mapped, size, MADV_SEQUENTIAL)`
immediately after a successful read-only `MAP_PRIVATE` in the Linux/macOS
`file_map_read` path, ignore failure, and make no API or permanent build option.
The experiment used temporary compile definitions only; all source and harness
changes were removed after measurement.

The mandatory trusted Release self-host baseline passed before the experiment:
deterministic executable bytes `46,914,520`; stage 1 retired
`9,424,888,673` instructions in `1.461397 s`; stage 2 retired
`132,100,519,514` instructions in `13.040186 s`. The unmodified Release
`bench_all` reported `BENCH_IO` min/median `1,773,162/1,916,265 ns` and
`BENCH_PARSE` `1,574,141/1,716,834 ns` (relative corpus paths).

The ordinary `parser_file_test_cases` corpus is relative, and therefore its
`file_map_read` calls intentionally fall back to `file_read` on POSIX. A
`strace -c` of the unmodified `ide bench` confirmed `12,399 openat` calls but
only `109 mmap` calls (shared libraries/arenas; no `tests/` mappings), so its
`BENCH_IO` line cannot be used as mmap evidence. For a representative mapped
run, a temporary benchmark-only harness canonicalized each corpus path once
with `os_path_absolute` before calling `file_map_read`, and counted successful
mappings/bytes. Both variants mapped all `12,200` IO loads (`61 files * 200
iterations`) totaling `2,123,800` bytes, and the preloaded parse mode mapped
`61` files totaling `10,619` bytes. This is a warm-cache, many-small-files
workload; no cold-cache claim is made.

Five interleaved clean executions of the trusted Clang Release binaries gave
the following mapped-corpus `BENCH_IO` medians (nanoseconds):

```
             1       2       3       4       5       median-of-runs
OFF       579719  614034  594097  576743  588065       588065
MADV      671415  631258  669430  622230  618754       631258
```

The MADV medians were +7.3% at the median-of-runs (and slower in every pair).
The corresponding `BENCH_IO` minima were OFF `545744, 566273, 552998, 539031,
540443` and MADV `620426, 573697, 613924, 587815, 572565` ns. `BENCH_PARSE`
was also slightly slower with MADV: medians OFF `71026, 66256, 68421, 68721,
69473` versus MADV `72248, 69763, 69764, 70074, 69713` ns. A direct `perf
stat` spot check (one process per variant) showed identical major faults (`0`),
identical minor faults (`12,583` each) and near-identical instructions (OFF
`344,520,210`, MADV `344,741,536`); task-clock was `129.8 ms` versus `154.1 ms`.
`/usr/bin/time` was unavailable on this host, so no peak-RSS field is reported.

There is no repeatable benefit for the actual small, warm corpus; the measured
effect is a clear regression. **Do not land; do not try `MADV_WILLNEED` or a
combination on this workload. Revisit only with a separately justified large,
cold, sequential workload where the mapping branch is proven to dominate.**

`2026-08-13c` (Linux x86_64; C frontend true 3-TU split, based on
`39bc0ffa`, uncommitted during measurement). Non-unity CMake now compiles
`c_source.c`, `c_parse.c` (parser plus semantic analysis), and `c_gen.c`; the
unity aggregator retains their historical include order and `c.c` diagnostics
mapping. Three fresh default-parallel Debug split-vs-baseline pairs (Clang 22,
tests/Vulkan/shaders off) measured generate+build wall: baseline 2.9855/2.9526/
3.1921 s, split 2.4730/2.6503/2.6103 s (means 3.0434 vs 2.5779 s, -15.3%).
Ninja sums were baseline 20,237/20,121/21,555 ms versus split
20,312/21,664/21,934 ms; maxima baseline 1,958/1,957/2,096 ms versus split
1,479/1,662/1,568 ms. Frontend object sums rise because three TUs each parse
the private contract: baseline c.c 2,020/2,015/2,156 ms versus split
source+parse+gen 3,340/3,714/3,820 ms. Three -j1 comparisons similarly show
the expected aggregate-work increase (baseline 9,714 ms total/1,450 ms
frontend; split 10,044/1,805 ms). The parallel critical-path wall improvement
is the relevant split-build result; the aggregate work tradeoff is recorded.

Exact paired fresh Release unity self-hosts on the same base/config remained
deterministic: baseline bytes 46,898,816 and 3,121,216 tokens, split bytes
46,900,352 and 3,122,265 tokens; stage-1 instructions 9,412,710,684 versus
9,414,576,385 (+0.02%), stage-2 131,954,589,274 versus 131,967,674,970
(+0.01%). Unity bench medians were baseline IO/parse 1.786656/1.605340 ms and
split 1.791887/1.588326 ms. Both split and unity test suites passed 278,208
assertions and 46/46 modules. The instruction deltas are deterministic and
attributable to the 1,049 added unity tokens; the source-layout/header token
delta is recorded rather than hidden.

`2026-08-13b` (Linux x86_64; bounded Clang split-TU PCH A/B experiment,
based on `aba451f3`, not landed). The opt-in experiment used one stable header
containing only `base.h`, `arena.h`, `integer.h`, and `string.h`, and an
explicit 11-source allowlist: `arena.c`, `float.c`, `hash.c`, `integer.c`,
`string.c`, `truetype.c`, and the `arena`, `file`, `hash`, `os`, and `string`
tests. `ide.c`, `test.c`, compiler and generated metadata/AArch64 sources,
platform/window/rendering sources, and other include-order-sensitive sources
were skipped. OFF and ON Debug split configurations preserved the ordinary
compile definitions and flags; ON compile commands carried `-include-pch`
only for those 11 sources. The optimized unity configuration was not used for
the measurement.

Three interleaved clean Debug split-build pairs (Clang 22, Ninja Multi-Config,
same host) showed no repeatable benefit. OFF critical-path wall times were
3.152 s, 3.064 s, and 3.173 s (Ninja max edges 2,168/2,177/2,199 ms; sums
37,328/37,002/37,626 ms). ON was 3.161 s, 3.142 s, and 3.115 s (Ninja max
edges 2,117/2,222/2,176 ms; sums 37,273/37,193/37,764 ms). The additional
PCH-generation edge cost 106/53/47 ms. The aggregate differences are within
run-to-run noise, so this experiment provides no evidence to land the option.
OFF and ON `test_all` both passed (277,422 unit assertions, 46/46 modules,
0 external tests). Direct binary `cmp` differed in each pair (separate build
directories change debug/link metadata); stripping debug sections still left
different bytes, so no stronger equivalence claim is made. **Do not land;
revisit only with a broader measured header strategy.**

`2026-08-13a` (Linux x86_64; instruction-selection and scheduling foundation,
based on `33e4b016`). Baseline Release self-host before the change produced
45,381,688 deterministic bytes; stage 1 retired 9,082,017,170 instructions in
0.928327 seconds and stage 2 retired 127,455,862,452 instructions. The change
preserves the 24-byte machine row, moves x86 scalar ALU two-address operations
to tied three-operand SSA, expands AArch64 remainder before allocation, adds
function-owned edge/block-parameter parallel copies, declarative shared
selection facts/rules, metadata-driven operand constraints, and per-register-
class scheduling pressure. Debug and Release integration validation each
passed 276,763 assertions across 44 modules. Release self-host remained
deterministic at 45,704,896 bytes; stage 1 retired 9,142,625,938 instructions
and stage 2 retired 128,295,924,084 instructions. Relative to the pre-change
tree, stage-1 instructions rose 0.67% and stage-2 instructions rose 0.66%.
Wall-clock readings were noisy across otherwise byte-identical runs, so the
retired-instruction counts are the comparison metric for this entry.
The MIR_STACK machine soak selected one more function (fallbacks 53 vs. 54)
and reduced encoder fallbacks to 39 from 40. Future audits should retain the
pressure-first scheduler acceptance discipline.

`2026-08-11l` (Linux x86_64, Zen 4 7940HS; register-allocator live-range
splitting — the plan-stage-7 capability the span-pin design deferred and
the lever `2026-08-10l` named after measuring every static refinement of
pin economics negative. Rebased twice past concurrent landings: built
first on `690a43b`, then rebased onto `c81a974` (audit `2026-08-11l`
was numbered `j` at that point), and rebased a second time onto the
current tip `a838308` — which carries `11f`'s frequency-aware pin
economics and `11h`'s shared-prepass refactor, both editing the same
`register_allocator_quality.c` this entry also edits. The rebase folded
splitting into the frequency-weighted design rather than keeping its
own separate constants: a split's boundary edits (entry installs,
landing-pad stores) are priced by the same per-instruction frequency
weight `11f` already prices everything else in, since they provably sit
outside the region by construction and therefore price at a *lower*
weight automatically — no separate iteration-credit constant needed.
**The rebase surfaced and fixed a real, pre-existing nondeterminism bug
in `11f`'s own code — see below — independent of and predating this
entry's own work, worth its own report.**)

- **The bug: an unguarded read of dirty arena memory.** `11f`'s
  "marginal candidate" early-reject reads `foreclosure_prefix` rows
  directly, without going through the lazy-build guard (`prefix_built_mask`)
  every other reader of that array respects. `arena_allocate_bytes`
  (`arena.c`) is a pure bump allocator — reused arena memory is
  documented as handing out **dirty, not zeroed, bytes** — so probing an
  unbuilt row reads whatever an earlier function's scratch pass left
  there. This is exactly the shape of bug that produces run-to-run,
  build-to-build nondeterminism: the QUALITY byte-identity soak
  (clang-built `ide` vs. self-hosted stage-2-built `ide`, both compiling
  the same `ide.c` under `-fregister-allocator=quality`) intermittently
  mismatched during this rebase, traced to exactly this read. Routing it
  through the same `machine_quality_foreclosure_prefix_ensure` helper
  every other consumer already uses made the mismatch disappear,
  repeatably, across every subsequent run. The fix is folded into this
  commit rather than filed separately since it sits directly in the code
  this entry rebases through; a standalone report is also filed for
  whoever owns `11f`'s branch history, since the bug predates this entry
  and is not caused by splitting.
- **What splitting is, unchanged from its first landing.** A pin
  candidate whose whole loop-extended interval cannot be placed may pin
  exactly one merged loop region of it instead — the sub-span where its
  weighted traffic actually sits. A split span being a full merged
  region preserves the closure every span already leans on (a span
  meeting a loop covers it), so the per-instruction active masks, the
  foreclosure prefixes, the budget, and the pin verifier all apply
  unchanged. Entry installs ride the existing edge-contract conform on
  the region head's entering edges (backward edges satisfied by a
  span-invariant skip, installing nothing per iteration); values living
  past the region store back at landing-pad blocks — exit successors
  whose every predecessor is inside the region and which no region
  contains — so both classes of boundary edit execute at most once per
  function invocation, because a merged region can never be re-entered
  once control passes its end.
- **The frequency-weighted fold.** `11f` prices every spill/reload edit
  by `4^depth` of the block it executes in (capped), both for candidacy
  and for the whole-placement acceptance test. Since a split's boundary
  rows sit outside the region by construction, they land at a strictly
  lower enclosing depth than the traffic the split removes — so bucketing
  a candidate's baseline edits into per-region weighted sums (reusing the
  exact `instruction_weights` array `11f` already built) and comparing
  that against the boundary rows' own weighted cost is a strictly more
  principled gate than this entry's original ad hoc iteration-credit
  constant, and needed no floor: `best_traffic <= entry_weight +
  exit_weight + prologue_cost` rejects, matching the whole-placement
  acceptance's own units exactly. A marginal candidate (admitted on
  frequency alone, raw edit count under three) keeps its caller-saved-only
  restriction through the split probe for the same reason `11f` imposed
  it on whole placement: a static loop is not evidence the loop runs.
- **Measured, frozen `a838308` ide.c as the workload (min of 3, same
  session, both sides).** QUALITY stage `40.903G` to `40.881G`
  (**`-22M`**, pins 10,415 to 10,642, **244 splits** firing), static
  traffic essentially flat (reloads 55,847 to 55,790, spills 84,089 to
  84,148, the difference absorbed by the boundary rows: boundary_spills
  14,623 to 14,759). Quality-mode compile cost `9.1898G` to `9.3882G`
  (**`+2.2%`**, the region metadata, the per-candidate region-traffic
  matrix, and the split probing on top of `11f`'s own frequency-weighted
  economics; quality stays opt-in). Canonical compile cost flat
  (`5.87000G` to `5.87001G`), fast-mode flat (`7.3598G` to `7.3933G`,
  inside session noise); FAST's placement is untouched, proven the
  strong way: the FAST stage built from the frozen source by this tree's
  compiler is byte-identical to the one main's compiler builds. Pressure
  corpora: scalar (`91.32M`/`82.12M` FAST/QUALITY) and vector
  (`91.08M`/`84.44M`) both repeat to the instruction with zero splits —
  single-region bodies whose whole file is already contended over that
  one region, so there is nothing to split into. Every paired execution
  (main vs. this tree, FAST vs. QUALITY, scalar vs. vector) agrees.
- **The isolated shape still wins, unchanged in kind from the first
  landing.** A two-phase body — values defined before a call-free hot
  loop, live across a later call loop, past both targets' register files
  — is the shape full pins cannot serve: calls foreclose the
  caller-saved file over every whole interval and the callee-saved file
  holds five. At corpus scale the isolated effect is still a real win
  (QUALITY-without-splits to QUALITY-with-splits materially fewer
  executed instructions on the driver in `PERFORMANCE_AUDITS.md`
  `2026-08-11j`'s original measurement); the self-host workload's
  residue is the same finding as before — the allocation-side levers are
  exhausted on this workload, restated now under frequency weighting
  rather than static counts.
- **Gates.** test_all green (29,430 unit, 33 module — the split-shape
  corpus function and its QUALITY-vs-NONE executing differential from
  the original landing, unchanged by the rebase), `test_self_host`
  green and deterministic (`SELF_HOST deterministic bytes=32372256`,
  with the machine-stage bench), all three soaks — MIR_STACK, FAST,
  QUALITY — byte-identical against the freshly rebuilt stage-2
  reference (only reliably true *after* the arena-read fix above — this
  is the gate that caught it), FAST cross-tree byte-identity,
  `test_all_combinations_ci` green before the push.
- Reference points for the next audit, frozen `a838308` ide.c as the
  workload: QUALITY stage `40.881G`, pins 10,642 / splits 244, compile
  cost canonical `5.8700G` / fast `7.3933G` / quality `9.3882G`.
  Pressure corpus: FAST `91.32M`, QUALITY `82.12M`, splits 0. Vector
  corpus: FAST `91.08M`, QUALITY `84.44M`, splits 0.

`2026-08-11d` (Linux x86_64, Zen 4 7940HS; the merged-tree re-baseline the
stage-9 and stage-10 entries both demanded, plus two of stage 10's recorded
leftovers: 64-bit-lane VBINARY, and union-with-vector locals. The VBINARY
hunt found that **the canonical emitter's 512-bit vpaddq/vpsubq and the
packed-double arithmetic have been emitted with EVEX.W0, and Zen 4 raises
#UD on those encodings** — a latent canonical miscompile for every 64-byte
vector with 64-bit lanes that no fixture covered; fixed with the W bit set
exactly where the SDM demands it, and gated by a new executing
differential and fixture lanes chosen to carry across bit 32. **Merged-tree
baselines (frozen `df11728` ide.c, same-session absolute paths): stage
compiles FAST `40.0248G` / QUALITY `39.2505G`, stage text 18,254,016 /
17,937,360, compile cost canonical `5.7077G` / fast `7.2241G` / quality
`9.3556G`; scalar corpus FAST `91,322,243` / QUALITY `82,123,447` / clang
-O2 `42,737,499`; vector corpus MIR_STACK `237,124,007` / FAST
`106,691,253` / QUALITY `103,155,456` / clang -O2 -march=native
`30,220,921`.**)

- **The merged tree re-measured, as both entries required.** The corpus
  driver reproduces `10m`/`10o` to within a few instructions (FAST
  `91,322,243` against `10o`'s recorded `91,322,244`), so the scalar
  corpus is genuinely unmoved by the merge, and the vector corpus QUALITY
  lands on `103,155,456` — the digit `10o` recorded — confirming `10n`'s
  body-by-body finding that scheduling has nothing to move there. The
  interaction the entries could not measure is in quality-mode compile
  cost: `9.3556G` against `10n`'s `9.3207G`, because stage 9's second
  placements are now also paid on the kernels' newly vector-selecting
  functions (frozen-tree QUALITY schedules 208 / keeps 183 against
  `10n`'s 207/182; traffic reloads 51,660 / spills 81,700 / remats 4,457
  / pins 7,939). FAST frozen traffic: 84,868 / 102,264 / remats 9,174.
  Frozen census: 30 fallbacks in both modes. Stage-compile absolutes are
  not comparable across sessions — the workload is invoked by absolute
  path, so the session's directory length shifts the lexed bytes; the
  reference block below is what the next audit compares against, taken in
  one session with everything else.
- **W0 is not a relaxed W1: the hardware probe.** The SDM lists
  vpaddq/vpsubq as EVEX.W1-only, and this vocabulary's uniform-W0 claim
  was tested before touching anything: a SIGILL-guarded probe executing
  the exact byte sequences shows Zen 4 **#UDs W0 D4, W0 FB, and W0
  66-prefixed 58** (register and mem+disp32 forms alike), while the W1
  forms execute as the qword operations; `llvm-mc` calls the W0 bytes an
  invalid encoding. So the machine path's 64-bit lanes could never have
  shipped W0 — and the canonical native path had already been emitting
  exactly those bytes at `codegen.c`'s `x64_emit_vector_native_binary_operation`
  for u64/s64 lanes (D4/FB) and f64 lanes (66-prefixed 58/5C/59/5E) at
  512 bits. Nothing executed them: `basic_c_vector.c` carried only int
  and byte lanes at 64 bytes, which is the whole reason the bug was
  invisible. The 16-byte legacy-SSE and 32-byte VEX paths have no W to
  get wrong.
- **The fix, in one place per path.** The canonical emitter derives the W
  bit from the form (`D4`/`FB` always; `58/5C/59/5E` only under the 66
  prefix), which covers the canonical module path and the buster-language
  backend through their shared helper. The machine path's VBINARY row
  carries the wide bit in payload bit 8 — the FARITH convention — set by
  selection for 64-bit lanes, and `machine_x64_emit_evex` gained the W
  parameter (false at every other call site: the rest of the vocabulary
  is genuinely W0/WIG). The 64-bit-lane VBINARY forms now select instead
  of rejecting; the bitwise trio stays W0 (`vpandd` and friends decode
  for any lane width).
- **Coverage that would have caught it.** `machine_test`'s vector corpus
  gained `vqarith` — a u64x8 add/subtract/xor/and loop — selected,
  verified, placed, encoded, and run through the four-mode executing
  differential whose oracle is the canonical NONE path, so the oracle's
  own W1 forms execute too. `tests/basic_c_vector.c` gained a `Long8`
  section whose lane zero carries across bit 32 in the sum and borrows
  across it in the difference — a dword-lane interpretation cannot pass —
  executed natively by the driver test and compiled at every march with
  the statistic assertions updated (baseline splits 6→9, haswell 4→7,
  znver5 natives +7, vzeroupper 5, forwarded 4).
- **Union-with-vector locals: canonical measured first, then mirrored.**
  What the canonical frame layout actually does with an over-aligned
  local was probed, not assumed: for *any* local whose IrValue alignment
  exceeds sixteen — `basic_c_simd.c`'s `Lanes` union and plain `Simd512`
  locals alike, since the C frontend stamps every local with its type's
  layout alignment — canonical allocates `size + alignment - 1` raw
  bytes and emits lea/add/and at the LOCAL instruction, storing a
  runtime-aligned pointer through which every access then indirects
  (`codegen.c`'s `aligned_local_offsets` and the `alignment > 16`
  indirect-place tests). **`10o`'s claim that the canonical frame layout
  "clamps vector alignment to sixteen" is wrong about locals** — the
  record stands corrected here. The machine path now mirrors the
  indirect shape for over-aligned non-vector locals: classification
  gives them a padded slot deliberately kept out of `value_stack_slots`
  plus a GENERAL vreg holding the aligned pointer, the LOCAL row emits
  LEA_FRAME(+alignment-1) / MOV_RI(-alignment) / AND64, and every
  consumer dispatches down the same pointer paths a GLOBAL's address
  already takes — so a missed consumer can only fall back, never read
  the padded slot as data. Promotion now runs before the alignment
  check: a promotable over-aligned local promotes, its alignment
  unobservable without an address.
- **The vector-local clamp stays, and the divergence is now recorded.**
  Stage 10's sixteen-byte clamp for `IR_TYPE_VECTOR` locals is kept: all
  machine vector accesses are unaligned forms, the kernels' vector locals
  promote anyway, and re-routing them through the indirect shape would
  tax exactly the slots stage 10 measured. The honest cost is that a
  non-promoted machine-path vector local sits at a sixteen-aligned slot
  where canonical hands out a 64-aligned address — observable only by a
  program inspecting the address. The new `vunion` differential returns
  `(u64)&lanes & 63` alongside member reads and writes, so the union
  path's alignment contract *is* executed under all four modes against
  the canonical oracle.
- **What moved, measured.** On the frozen workload nothing did, by
  construction: census stays 30 in both modes (`ide.c` has no
  union-with-vector locals or 64-bit-lane vector arithmetic), traffic and
  placements are unchanged, canonical output is byte-identical to the
  pre-change compiler's, and the mode costs pay for the new dispatch
  tests alone — fast `7.2269G` (`+0.04%`), quality `9.3584G` (`+0.03%`),
  canonical flat. Stage text grows +9,792 FAST / +9,600 QUALITY of new
  selection code. `basic_c_simd.c`'s FAST census keeps 4 fallbacks but
  the LOCAL rejection is gone — the union function now walks past its
  local and stops at its vector-ABI CALL, which is the recorded remaining
  frontier. Corpus numbers repeat to the digit.
- **A methodology trap, paid and recorded.** The first soak comparison
  ran the stages with absolute paths and all three "failed": the
  self-host reference stages are built with repo-relative paths, so
  `__FILE__` spellings and DWARF paths differ and the bytes legitimately
  part. Soak stage compiles must use the exact repo-relative self-host
  invocation; with it, all three modes are byte-identical.
- **Gates, every commit:** test_all green (29,125 unit assertions, 32
  modules, including the new W1 differentials and the union executing
  differential), self-host fixed point deterministic on the rebased tree
  (`SELF_HOST deterministic bytes=31949472`, with `2026-08-11`'s
  machine-stage bench edge passing over this entry's selector), all three
  soaks — MIR_STACK, FAST, QUALITY — byte-identical against the freshly
  rebuilt `build/self-host/Release/ide-stage2`, the frozen-tree canonical
  outputs byte-identical between the pre-change and post-change compilers
  and between the compiler and both stages, `test_all_combinations_ci`
  green before the push.
- **Left untaken, in causal order — updated at rebase time.** The `ide
  bench` machine-path miscompile (`10o`) was fixed concurrently and
  merged mid-session (`2026-08-11` and `2026-08-11b` below, PRs
  261/262); this entry rebased onto that main, so its reference block
  describes the combined tree — the zero-fill rows move the
  machine-built stages, and the stage compiles, stage text, and
  machine-mode costs below were re-taken after the rebase, while the
  corpus numbers and canonical cost reproduced unchanged. This entry
  then rebased a second time onto a main that had, concurrently,
  already taken the zmm16-31 widening (`2026-08-11c`), the System V
  vector ABI (`2026-08-11i`), the a64 promotion port (`2026-08-11a`),
  frequency-aware pin economics (`2026-08-11f`), and the remaining
  non-vector selection gaps (`2026-08-11g`) — read those, not this
  paragraph, for their own numbers; none of that work's measurements
  are included in this entry's reference block above, which stays a
  description of this entry's own tree at its first landing. The union
  function's vector-ABI stopping point named below is therefore already
  gone (lifted by `2026-08-11i`). The vector-local address-alignment
  divergence recorded
  above is still live: the one place the machine path knowingly differs
  from canonical, worth folding into the indirect mechanism only if a
  real program is ever found observing those addresses.
- Reference points for the next audit, frozen `df11728` ide.c as the
  workload (same-session invocation, absolute paths), taken on the
  post-rebase tree that combines this entry with the `2026-08-11`
  zero-fill fix — the zero-fill rows account for nearly all the movement
  over the pre-rebase figures quoted in the bullets above (stage text
  +452K, stage compiles +584M, machine-mode costs +67M fast / +244M
  quality; canonical and both corpora byte- and digit-identical across
  the rebase): **FAST stage `40.6088G`, QUALITY stage `39.8348G`**,
  stage text 18,715,976 / 18,396,008, compile cost canonical `5.7076G`
  / fast `7.2945G` / quality `9.6022G`, frozen census 30 both modes,
  QUALITY traffic reloads 51,705 / spills 81,705 / remats 4,500 / pins
  7,947 / scheduled 209 / kept 184; FAST 84,978 / 102,295 / remats
  9,221. Scalar corpus: FAST `91,322,243`, QUALITY `82,123,446`, clang
  `42,737,499`. Vector corpus: MIR_STACK `237,124,007`, **FAST
  `106,691,255`**, **QUALITY `103,155,458`**, clang `30,220,921`.
  Corpus-driver traffic: scalar FAST 71/88, QUALITY 33/53 with 20 pins;
  vector MIR 987/883, FAST 70/90, QUALITY 62/79 with 5 pins (driver
  files, not the bare fixtures).

`2026-08-11f` (Linux x86_64, Zen 4 7940HS; frequency-aware pin economics —
the lever `2026-08-10l` and `2026-08-10n`/`2026-08-10o` both named:
`MachineBlock.frequency_class` populated and QUALITY's pin economics priced
in it, on the merged `b7ba8ea` main carrying stages 9 and 10, whose
baselines no prior entry described when this work started and were re-taken
first. Developed concurrently with the rest of the `2026-08-11` series and
rebased twice onto main as it moved — first onto the PR-261/262 merge, with
correctness gates re-run there, and again onto the tree carrying `11a`
through `11k` (AArch64 promotion/AAPCS64, ZMM0-31, vector span pins, the
shared prepass, System V vector ABI) with every gate re-run a second time;
the numbers below are as taken on `b7ba8ea` and include no sibling's
effect. `11c` and `11e` rework the same pin machinery and the same vector
corpus this entry improves, and `11h`'s shared prepass changed the
placement-build call this entry's acceptance sits on top of (merged
cleanly, no logic change on this entry's side) — the merged tree
re-measures before combining any of these entries' numbers. **The corpus
harness reproduces `10o`'s numbers to within two instructions (scalar FAST
`91,322,242` / QUALITY `82,123,445` / clang `42,737,994`; vector FAST
`106,691,252` / QUALITY `103,155,457` / clang -O2 -march=native
`30,221,411`). The shipped form: vector pressure corpus QUALITY
`103,155,457` to `100,051,463` (`-3.0%`), frozen-tree QUALITY stage
`39.9270G` to `39.8853G` (`-41.5M`, `-0.104%`, interleaved repeat band
~10K), stage text 17,977,680 to 17,971,552, scalar corpus flat
(`82,123,845`, `+400`), FAST stages byte-identical to the reference
compiler's, canonical outputs byte-identical between the stage compilers,
canonical and fast-mode compile cost flat, quality-mode `9.5066G` to
`9.6294G` (`+1.29%`). The audit's own negatives — measured and reverted —
are as load-bearing as the win: the exact shape `10l` suggested loses the
stage by `+284M`, and what rescues it is confining the frequency-admitted
pins to caller-saved registers.**)

- **The classes.** `machine_function_stamp_frequency_classes` (machine.c)
  stamps every block's loop-nesting depth: backward block references —
  block-ref operands and switch-case targets naming a block at or before
  their own — approximate a loop over the block range [target, source];
  per head only the widest span counts, so a multi-latch loop stays one
  loop; a difference array turns span cover into depth, capped at eight.
  Only QUALITY consumes the classes, so only QUALITY stamps — stamping at
  selection cost every machine mode the walk, measured `+67M` (`+0.91%`)
  of fast-mode compile cost for a field FAST never reads, and moving the
  stamp behind QUALITY's switch-table bail returned fast mode to flat.
  The machine tests pin `sum_to` at maximum class 1 and a nested pair at
  class 2 over depth-0 entries.
- **The economics, and the one form that survived measurement.** Every
  spill and reload edit is priced at `4^depth` of the block holding its
  instruction (`MACHINE_QUALITY_FREQUENCY_WEIGHT_SHIFT`, capped at
  `1 << 12`): the weighted per-value traffic drives candidacy and heap
  priority, and the acceptance compares both modeled placements as
  weighted totals — a placement that moves traffic out of a loop and into
  the entry path now wins at an equal count, which is exactly the
  blindness `10l` diagnosed. The prologue push/pop pair stays at weight
  one; it executes once per call. Values below the raw-count three enter
  candidacy on frequency alone — one in-loop edit clears the weighted
  bar — but may only take **caller-saved** pin registers: the reservation
  costs no prologue save, and the caller-saved foreclosure prefixes
  reject any span crossing a call for free. On the unity build this
  admits 2,303 marginal pins (8,008 to 10,311) and rewrites the traffic
  mix (reloads 52,968 to 54,443, spills 84,224 to 81,841): raw reloads
  rise, weighted cost falls, and the stage executes `-41.5M` fewer
  instructions.
- **The negatives, in causal order — do not retry these.** (1) The
  unrestricted weighted form — weighted candidacy over the full pin file,
  the literal shape `10l`'s conclusion suggested — wins the vector corpus
  `-3.8%` and loses the stage `+284M` (pins 8,008 to 14,873, static
  traffic *down* 5,766): `10l` experiment 1's signature at scale, because
  a static loop is not evidence the loop runs, and a callee-saved pin
  buys its prologue with edits a cold loop may never execute. (2) Raw
  candidacy with weighted priority and acceptance keeps pins at 20 on the
  scalar corpus yet loses it `+0.97%` (`82,924,247`) — the reorder alone
  demotes the pre-loop feeders, `10l` experiment 2's negative
  reproduced — and reverts the vector corpus to baseline exactly: the
  marginal pins *are* the entire vector win. (3) Weight `2^depth` loses
  both ways (scalar `+0.97%`, vector only `-1.5%`); `8^depth` places
  byte-identically to `4^depth` — the same weight-insensitivity plateau
  `10l` measured, but here the plateau is the winning form. (4) The
  stage-9 couplings: pricing the schedule-keep compare in the weighted
  currency rejects ten schedules (183 to 173) and costs `+4.8M` on the
  stage; weighting the scheduler's total-excess gate is a placement-level
  no-op at any weight. Both reverted; the scheduler's model is peak
  pressure, not traffic, and frequency has nothing to add to it at this
  stage.
- **Compile cost.** Canonical `5.6886G`-class flat (`5.8097G` on this
  workload against `5.8100G` reference — the absolutes moved with the
  workload, the delta is noise), fast-mode flat (`7.35347G` against
  `7.35358G`), quality-mode `9.5066G` to `9.6294G` (`+1.29%`): the stamp
  walk, the weight tables, and the weighted edit walks, after an
  outcome-neutral trim that rejects marginal candidates on the O(1)
  caller-saved foreclosure prefixes before the O(span) budget walk
  (`-30M`). The remaining trim, if a future audit wants it: the stamp
  duplicates the backward-edge scan QUALITY's loop extension already
  does; merging the walks must preserve the extension's exact merged
  regions (touching spans merge in one and not the other), which is why
  it was not taken here.
- **Corpus traffic, for the record.** Scalar: reloads 33 to 35, spills
  53 to 55, pins 20 to 20 (`+400` executed — the weighted acceptance
  accepts a placement two static edits heavier whose weighted total is
  lower; recorded, not explained away). Vector: reloads 62 to 61, spills
  79 to 75, pins 5 to 9 — the four new pins are the call-free
  wide-vector-loop scalars (masks, the accumulator, loop state) whose
  in-loop round-trips the local scan was paying every iteration; the
  call-crossing loop's marginals stay unpinned by the caller-saved rule,
  which is the difference between this form and the `+284M` one.
- **Gates, every commit:** test_all green (29,064 unit assertions, 32
  modules, including the new frequency-class tests), self-host fixed
  point deterministic, all three soaks — MIR_STACK, FAST, QUALITY —
  byte-identical against the freshly rebuilt stage-2 reference,
  `test_all_combinations_ci` green before the push (its qemu fixture
  differentials are the AArch64 gate; the classes stamp identically
  there and the a64 QUALITY placements re-verify under the same pin
  checks). FAST's stage is byte-identical to the reference compiler's by
  direct compare — the strong form of the soak guarantee — and the
  QUALITY stages' canonical outputs are byte-identical to the reference
  stages'.
- **Left untaken, in causal order.** Real profile input: the classes are
  static depth, and the stage's residual gap to clang is now bounded by
  exactly the cases depth cannot see — a loop that runs once (the `+284M`
  failure mode) and a hot straight-line function called from a loop
  elsewhere (invisible to any within-function weight); block-parameter
  counts or call-graph frequency would be the next static refinements,
  and both should be measured against this entry's decomposition before
  trusting any of it. The vector corpus's remaining 3.3x gap to clang
  keeps `10o`'s levers (zmm16-31, vector ABI, split-aware spill
  placement). The scalar corpus's `+400` says the weighted acceptance
  can still accept a dynamically-neutral-to-slightly-worse trade the
  static weights call a win; a dominance acceptance (raw and weighted
  both improve) was not measured and is the cheap next probe if the
  corpus ever regresses further.
- Reference points for the next audit, frozen `b7ba8ea` ide.c as the
  workload (same-session reference stages, absolute paths, min of
  three): **FAST stage `40.7230G` (byte-identical stages), QUALITY stage
  `39.8853G`**, stage text FAST 18,294,336 / QUALITY 17,971,552, compile
  cost canonical `5.8097G` / fast `7.3535G` / quality `9.6294G`, unity
  traffic reloads 54,443 / spills 81,841 / remats 4,282 / pins 10,311 /
  scheduled 208 / kept 183. Scalar corpus: FAST `91,322,242`, **QUALITY
  `82,123,845`**, clang `42,737,994`, traffic 35/55 with 20 pins.
  Vector corpus: FAST `106,691,252`, **QUALITY `100,051,463`**, clang
  `30,221,411`, traffic 61/75 with 9 pins.
`2026-08-11i` (Linux x86_64, Zen 4 7940HS; register-allocator stage 10
follow-on — the System V vector ABI, closing the one remaining vector-typed
fallback class. Developed against the stage-10 merge and re-based and
re-measured on main `c81a974` after the zero-fill fix and the `11b` payoff
audit landed mid-session; suffixes `c` through `h` are claimed by concurrent
unmerged branches, so this entry took `i`. 64-byte vector parameters and returns now travel
in ZMM registers through the machine path: **both vector-ABI fixtures
execute correctly under all three machine modes;
`basic_c_wide_vector_argument`'s census goes 5-of-5 functions fallback to
zero and `basic_c_simd`'s to the single union-local `main`; the scalar and
vector corpora, the frozen `c81a974` machine stages, and the three soak
binaries are all byte-identical to the unmodified compiler's; compile cost
moves ±0.03%, inside alignment noise.**)

- **Shapes from the IR classification, as specified.** `machine_x64_value_shape`
  gains the vector class: a type the IR classifies as one 64-byte
  `IR_ABI_CLASS_VECTOR` part under the System V convention (and only when the
  target carries AVX512F+BW — a model without the width keeps the canonical
  fallback, whose register split this subset does not reproduce). The part is
  float-class for placement because System V draws vectors and scalar floats
  from one SSE sequence, so `machine_x64_place_argument` needed no new
  counters — only the stack-overflow rule: a vector that misses the eight
  SSE registers 64-aligns the outgoing eightbyte cursor, exactly the padding
  `codegen_canonical_x64_stack_argument_offset` writes, so caller and callee
  count the same eightbytes. Machine selection is gated to
  `CODEGEN_ABI_X86_64_SYSTEM_V`, so the hardcoded convention is correct by
  construction, as it already was for the aggregate shapes.
- **Staging through the unified file, with two one-line allocator holes
  closed.** Entry captures are `VMOV_RR vreg ← ZMM(n)` rows in the *integer*
  capture pass — every register vector parameter binds in place before any
  free pick exists — and the FAST scan's capture-bind-in-place and
  stage-in-place special cases, which tested only `copy_opcode`, now cover
  `vector_copy_opcode` (MIR_STACK's copy-into-physical rule likewise): without
  them a reload or free pick could land on an argument ZMM whose own capture
  or call had not executed. A stack vector parameter reads its 64-aligned
  incoming eightbytes whole through `LEA_OFFSET rbp+16+offset` plus
  `VLOAD_PTR` — safe to free-pick because a stack vector implies the SSE
  sequence is exhausted, so no float or vector register capture can follow
  it. Call sites stage `VMOV_RR ZMM(n) ← vreg` (the allocator relocates the
  source into the exact target register), a stack vector argument bounces
  through a dedicated 64-byte frame slot into eight `PUSH_FRAME` rows, and
  returns stage and capture through physical ZMM0. Variadic calls with
  vector arguments stay canonical (their AL protocol counts vector
  registers; the subset keeps the zero-vector convention), as does any call
  whose stack vector argument would open an alignment gap the push machinery
  cannot produce.
- **vzeroupper became row-conditional, not unconditional.** The encoder used
  to emit vzeroupper before every CALL and RET in a vector-touching function
  — sound while every vector value was dead at those points, and exactly
  wrong for a value *staged* there: vzeroupper zeroes bits 128+ of ZMM0-15,
  so a staged argument or a live ZMM0 return would arrive truncated. A new
  per-row flag (`MACHINE_X64_INSTRUCTION_FLAG_VECTOR_LIVE`, set by the
  selector on calls with register vector arguments and on vector-returning
  RETs) suppresses that row's vzeroupper. This is the same answer the
  canonical path reaches by ordering — its lazy vzeroupper fires before the
  call's staging code — and the same one clang implements as
  no-vzeroupper-when-ZMM-live. A vector *returning* call keeps its
  vzeroupper: it fires before the call, and ZMM0 arrives after.
- **Found en route: INDEX on a 512-bit rvalue mis-selected as a pointer
  copy.** `machine_x64_select_place_address_offset` treats any vreg-held
  base as an address to copy — meaningless for a vector-class register, and
  unreachable until vector-ABI functions selected, whereupon
  `basic_c_wide_vector_argument`'s `main` (an `identity[lane]` read on a
  call result the frontend loads whole) selected its way into a wild load
  and SIGSEGVed under every machine mode. The base now snapshots into a
  dedicated 64-byte slot and hands out that slot's address — sound because
  the SSA value is immutable — which is also what lets `vector_mixed`'s
  lane reads select. Array-typed rvalue INDEX keeps its reject.
- **Census, before → after** (`-march=native -fregister-allocator=fast -v`):
  `basic_c_wide_vector_argument` fell back whole — 5 of 5 functions, CALL
  and signature rejects on type-kind VECTOR — and now selects **all five
  including `main`** (the ninth-argument call stages eight ZMM registers
  plus one 64-byte stack argument at offset zero; machine-mode code bytes
  5,756 → 1,512). `basic_c_simd`: `vector_identity`, `vector_ninth`,
  `vector_mixed`, and `fill` select; `main` keeps its union-with-vector
  LOCAL fallback (alignment past sixteen, by design, opcode=1 count=1).
  ide.c's census is unchanged at 30 — it has no vector-ABI signatures —
  proven stronger below by stage byte-identity.
- **The differential extended to the ABI boundary.** The stage-10
  machine_test corpus gains `vident` (vector parameter and return),
  `vmix` (integers and vectors advancing both files), `vninth` (nine vector
  parameters, the ninth read back from the caller's stack eightbytes), and
  `vabi`, a u64-returning wrapper driving all of them — including the
  nine-argument call, so the differential executes the machine caller *and*
  machine callee sides of the stack convention against the canonical NONE
  oracle under MIR_STACK, FAST, and QUALITY on every mask probe. All four
  select, verify, place, and encode on every host; execution keeps its
  cpuid gate. test_all is 29,102 unit assertions across 32 modules, up 74.
- **Measured, byte-identity first.** Both register-pressure corpora
  (scalar and vector, 200 iterations of the three bodies at 2,000 rounds)
  compile byte-identical to the unmodified `c81a974` compiler's output
  under all three machine modes, so the tree's executed numbers carry over
  by construction; re-measured min-of-five as **vector MIR_STACK
  `237,125,789` / FAST `106,692,454` / QUALITY `103,156,260`** and **scalar
  FAST `91,323,447` / QUALITY `82,124,246`** — `10o`'s figures inside the
  ~2K driver-startup band, recorded here for the post-zero-fill tree. The
  frozen `c81a974` machine stages (this compiler and the unmodified one
  compiling the same archived tree) are byte-identical in all three modes,
  so every stage reference — including `11b`'s bench payoff rows — carries
  over unchanged. Compile cost on the frozen workload (min of five, same
  session): canonical `5.8083G` vs `5.8084G` (flat), **fast `7.4223G` vs
  `7.4204G` (+1.9M, +0.026%)**, **quality `9.7473G` vs `9.7502G` (−2.9M,
  −0.030%)** — opposite-sign shifts at the alignment-noise floor. These are
  this tree's own cost baselines; they sit above `10o`'s because the
  workload's source now carries stages 9 and 10, the zero-fill fix, and
  the `11b` audit text.
- **Gates, every commit:** test_all green (29,102 unit, 32 module,
  including the new ABI differentials), self-host fixed point
  deterministic, all three soaks — MIR_STACK, FAST, QUALITY —
  byte-identical against the freshly rebuilt stage-2 reference,
  `test_all_combinations_ci` green before the push.
- **Left untaken, in causal order.** The machine caller does not reproduce
  the canonical caller's *absolute* 64-alignment of an outgoing area
  holding a stack vector argument (the RSP save-align-restore): offsets
  match the canonical layout exactly and every in-tree consumer reads
  vmovdqu8, so agreement is byte-exact in any buster-linked program, but a
  foreign callee reading its ninth vector argument with an *aligned* move
  would need the aligned-area machinery — take it when such a link exists.
  Gapped stack vector arguments (a scalar stack argument preceding the
  vector) keep their calls canonical; the variadic AL-counting protocol
  keeps variadic vector calls canonical; the `ide bench` machine-path
  miscompile hunt from `10o` remains the open gate on dynamic parser-path
  claims. The union-with-vector local still holds `basic_c_simd`'s `main`
  canonical, exactly as `10o` recorded.
- Reference points for the next audit: unchanged from `2026-08-10o` for the
  stage compiles and text and from `2026-08-11b` for the machine-stage
  bench rows (frozen-stage byte-identity carries both), plus this tree's
  costs above (canonical `5.8083G` / fast `7.4223G` / quality `9.7473G` on
  the `c81a974` workload) and corpora: scalar FAST `91,323,447` / QUALITY
  `82,124,246`, vector MIR_STACK `237,125,789` / FAST `106,692,454` /
  QUALITY `103,156,260` (this session's driver; `10o`'s digits reproduce
  within ~2K of startup).

`2026-08-11h` (Linux x86_64, Zen 4 7940HS; where quality-mode compile cost
goes, and the register-allocator plan's stage 12 settled. Developed
against the stage-9+10 merged `b7ba8ea`; rebased twice as main moved
underneath it — first onto `c81a974` (PRs 261/262, aggregate zero-fill and
its payoff measurement), then onto `30aceb9` (PRs 263 ZMM0-31 widening /
`11c`, 264 AArch64 promotion+AAPCS64 / `11a`, 265 vector span pins /
`11e`, and the `11k` bench-figure correction) — **every number below is
from the second rebase, the tree this entry actually ships on.**
Baselines on the frozen `df11728` ide.c workload (same-session invocation,
absolute paths, `perf stat` instructions:u, min of three): **canonical
`5.7069G` / fast `7.2879G` / quality `9.5985G`** — this entry's own
re-baseline on `30aceb9` (`11c`'s narrower-scope reference, taken at
`079add9` before 264/265/`11k` landed, was canonical `5.8073G` / fast
`7.3475G` / quality `9.5032G`; the two disagree because `11c` measured a
different, now-superseded point on main, not because either is wrong).
**After this entry's algorithmic trims: canonical `5.7069G` (flat, `-3.5K`
within the run-to-run band), fast `7.1344G` (`-153.6M`, `-2.11%`), quality
`8.7324G` (`-866.1M`, `-9.02%`), placements byte-identical in every mode —
the compile outputs of the trimmed compiler `cmp` equal to the untrimmed
`30aceb9` compiler's under none, fast, and quality on the frozen
workload, and all three soaks hold.**)

- **The profile, before anything moved** (perf record instructions:u at
  fixed period 100003, leaf attribution symbolized per the AGENTS.md
  recipe; quality mode, shares of the pre-trim `9.356G` measured at
  `b7ba8ea`, ahead of the widening — the profile was not retaken after
  263/264/265 since none of them touch the scan/quality-pass hot lines
  this entry trims, only the register-file width they iterate over,
  which the shares below are insensitive to at the row level):
  `machine_fast_placement_build_pinned` **23.2% (`2.15G`)**,
  `machine_quality_placement_build` self 6.0% (`0.56G`),
  `machine_schedule_function` self 1.9% (`0.18G`),
  `machine_fast_conform_edge` 1.0% (`0.10G`); selection/verify/encode
  ~9% and flat against fast mode. The quality-minus-fast delta
  (`2.131G`, `+29.5%` over fast) is 95% those first three rows: the scan
  runs twice per function (baseline and pinned, call-site frames split
  ~50/50), the quality pass's own walks re-derive what the scan already
  knew, and the scheduler's kept candidates each pay a full second
  quality placement. Line-level, the scan's cost was spread across five
  pre-pass walks (rematerialization recipes, predecessor counts, cold
  blocks, two liveness passes, next-call) plus the main scan — every one
  a separate full decode of the operand stream — and 27% of the quality
  pass's self cost was one line: the eager foreclosure prefix build
  summing all fourteen allocatable rows.
- **The stage-9 scheduling block decomposes as assumed, and the DAG was
  never the story.** Its `+11.2%` is ~`0.18G` of pass self-cost (gates,
  queue, demand updates — the never-profiled DAG build is a minor slice
  of even that) and ~`0.76G` of second placements for the kept scheduled
  functions, which the acceptance rule pays by design; the two excess
  gates stay exactly as `2026-08-10n` measured them mandatory. Nothing
  here was trimmed.
- **Trim 1, one prepass instead of five walks, shared across every scan
  of a function.** Everything the scan derives that no pin set changes —
  rematerialization recipes, predecessor offsets/list, cold entries,
  defining blocks, last uses, escapes, next-call indices, the
  class-trimmed register-file width — now builds once in
  `machine_fast_prepass_build` as two merged walks (facts a single
  forward pass can collect, then the adjacency/escape/span walk that
  needs completed defining blocks), and both of QUALITY's scan runs read
  the same prepass; FAST builds it once inside the unchanged
  `machine_fast_placement_build_pinned` wrapper and gets the walk merge
  for free. The merge is byte-identity-safe by construction: every moved
  accumulation is order-free (remat's last-write-wins is a function of
  definition count alone; min/max/count/first-def-wins commute), and the
  escapes rule keeps its two-phase shape because layout order does not
  prove a def precedes its uses. Masks throughout are `u64` (the widened
  unified file, `11c`) — `machine_fast_prepass_build`'s general-register
  top-bit scan already used the widened type; the placement-build
  wrapper and the prepassed entry point take `pinned_mask`/
  `pin_active_masks` as `u64` to match.
- **Trim 2, the prepass also feeds QUALITY's own front matter.** The
  quality pass's interval walk (starts/ends/constrained
  disqualification) and its two backward-edge walks (count, then span
  collection) were the same operand decode again; the prepass now
  carries `interval_starts`/`interval_ends`/`disqualified` and the raw
  span array, and the quality pass starts at the sort/merge.
- **Trim 3, foreclosure prefix rows build lazily on first probe.** The
  pin file is walked front-first and most functions place their pins
  within the callee-saved prefix, so eagerly summing all fourteen
  allocatable rows was mostly work nobody read. Rows are pin-independent
  and persist across both attempts; values are identical to the eager
  build's, so probes and placements are unchanged. `11c` independently
  scoped the prefix table's width to `prefix_register_limit` (the
  highest allocatable general index, still fourteen on x86-64) rather
  than the full widened file; the two changes compose directly — lazy
  build over the narrower table.
- **Stage 12's verdict: closed with no SIMD kernels, premise void.** The
  plan assumed dense per-block liveness/interference bitsets would
  dominate and specified host-dispatched scalar/AVX2/AVX-512 kernels for
  the `new_in = use | (live_out & ~def)` chains. The shipped allocators
  never built those structures — the scan's cross-block state is
  per-block contracts over a ≤16-entry register file and the quality
  layer's span legality is prefix counts — and the profile confirms
  nothing dense-and-wide remains: after the trims the scan core is
  14.6% of quality mode as an irregular serial state machine (LRU
  picks, contract conforms, per-row operand decoding with data-dependent
  branching), the prepass is 2.9% of linear decode, and the one loop
  that ever matched the stage-12 shape — the fourteen-lane prefix sum —
  is exactly what trim 3 removed rather than vectorized. A 512-bit
  kernel has nothing to run on; the acceptance bar (whole-allocator Zen
  5 win, Zen 4 break-even, bit-identical output) is unreachable when no
  candidate loop is dense, so the stage closes as understood-and-
  recorded rather than built. The nearest miss is recorded for
  completeness: `machine_fast_conform_edge`'s kept-check compares a
  resident value against the 14-entry contract row (~0.5% of quality
  mode, one `vpcmpeqd` in shape) — too small to clear the
  whole-allocator bar on any host.
- **What stays, understood.** The dual scan per quality function is the
  candidate-selection design (`2026-08-09ai`: measured traffic beats
  every guessing heuristic) — the prepass halves its overhead without
  touching the decision. The kept schedules' second placements are the
  stage-7 acceptance currency. The main scan's remaining cost is the
  allocation itself; its loops are the register file's width and carry
  per-iteration side effects, so neither wider hardware nor a kernel
  changes them. `frequency_class` remains unwritten and is a lever named
  by prior entries, not this one.
- **Gates, on the twice-rebased trimmed tree:** Release `test_all` green
  (29,298 unit assertions, 33 modules), self-host fixed point
  deterministic (`SELF_HOST deterministic bytes=32184608`), all three soaks — MIR_STACK, FAST,
  QUALITY — byte-identical against the freshly rebuilt canonical stage-2
  reference, and the frozen-workload compile outputs of the trimmed
  compiler byte-identical to the untrimmed `30aceb9` compiler's in every
  mode. `test_all_combinations_ci` green before the push.
- Reference points for the next audit, frozen `df11728` ide.c as the
  workload (same-session invocation, absolute paths): compile cost
  **canonical `5.7069G` / fast `7.1344G` / quality
  `8.7324G`**; allocator traffic on that workload reloads 51,705 /
  spills 81,705 / remats 4,500 / pins 7,947 / scheduled 209 / kept 184
  (unchanged by the trims by construction). Buster-built stage compiles
  on this tree: FAST `40.6087G`, QUALITY `39.8348G` (current-tree
  stages, frozen workload, min of three — the tree's own source grew
  across the merges and this patch, so these sit beside, not against,
  `11c`'s frozen-tree numbers).

`2026-08-11k` (Linux x86_64, Zen 4 7940HS; correction record, no code
change: the `2026-08-11` entry's sentence "its `bench` medians 1.58 ms IO /
1.40 ms parse against canonical 0.9 ms" mislabels its comparison point —
**0.9 ms is not a canonical-stage figure**. The df11728-plus-fix context was
rebuilt exactly as that entry ran it (scratch clone at `df11728`, `5da81e8`
cherry-picked, clang Release ide, stages built with the self-host recipe
`cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1 -DBUSTER_INCLUDE_TESTS=0
-g [-fregister-allocator=<mode>] src/buster/apps/ide/ide.c`, `chmod +x`,
`bench` from the repo root, five runs per stage, idle machine) and the
canonical stage's bench medians are **1.712 ms IO / 1.505 ms parse**
(median-of-five; run medians 1,706,882–1,722,021 / 1,498,234–1,516,539 ns;
min-of-five 1,678,087 / 1,485,049 ns). Canonical is *slower* than the
MIR_STACK stage on this workload, not 1.6x faster — the comparison the
sentence implied is inverted.)

- **The rebuilt context is the one the entry measured.** Three
  cross-checks pin it: the MIR_STACK control stage rebuilt in this
  context benches medians 1.551 ms IO / 1.349 ms parse against the
  entry's recorded 1.58 / 1.40 (within ~3%, and `2026-08-11b` already
  reproduced the same reference across the b7ba8ea re-freeze at
  1.634 / 1.414); the clang-built Release ide benches parse min
  `44,525` / IO min `210,672`, matching `2026-08-11b`'s `45,167` /
  `206,717`; and the canonical figure itself sits where `2026-08-11b`'s
  frozen-b7ba8ea canonical row sits (parse min `1,527,202` / IO min
  `1,754,128`). The `2026-08-11b` observation that prompted this check —
  canonical measuring ~1.65 ms parse while "canonical 0.9 ms" stood on
  the books — was not workload drift: canonical never measured 0.9 ms.
- **Where the stray figure came from.** FAST and QUALITY stages built in
  this same context bench medians **0.967 ms IO / 0.783 ms parse**
  (both modes, within a thousandth of each other; mins FAST
  `952,631` / `769,772`, QUALITY `949,895` / `766,986` — the same band
  as `2026-08-11b`'s pre-stage-10 column, FAST `993,378` / `822,970`).
  "0.9 ms" is the machine-built FAST/QUALITY band — the two modes that
  `2026-08-11` session had just un-crashed and was benching alongside
  MIR_STACK — recorded against the wrong label.
- **Corrected reading of the `2026-08-11` reference points.** On the
  df11728-plus-fix workload: FAST/QUALITY ~0.97 IO / ~0.78 parse,
  MIR_STACK 1.551 / 1.349, canonical 1.712 / 1.505, clang Release ide
  0.215 / 0.046 (all medians, ms). The machine-built MIR_STACK stage is
  ~10% *faster* than canonical, FAST/QUALITY roughly 2x faster — the
  same ordering `2026-08-11b` measured on frozen `b7ba8ea`.
- **Method note: the first measurement attempt was garbage.** The first
  bench pass ran against a load-average-33 machine (concurrent clang
  builds from another session) and inflated every stage roughly 2x with
  medians drifting run to run; the same contention OOM-killed the first
  Release builder compile in a tmpfs work directory. Numbers here were
  taken at load ~1.1, gated on the MIR_STACK and clang-ide cross-checks
  landing on their references. Run-to-run median band on the idle
  machine: ~1%.
- **Entries are records.** `2026-08-11` stays as written; this entry is
  the correction. Nothing below it changes: `2026-08-11b`'s rows were
  measured on frozen `b7ba8ea` with re-baselined references and are
  unaffected by the mislabel.

`2026-08-11a` (Linux x86_64, Zen 4 7940HS; the AArch64 machine backend's
second installment — local promotion brought to x86-64 parity, then the
AAPCS64 shape machinery the `2026-08-10h` census ranked as the next lift,
in two gated commits on top of `b7ba8ea`. This is a coverage-and-identity
entry, not a throughput audit: qemu-aarch64 is the only execution vehicle
on this host, so every claim below is coverage, byte-identity, or a
differential; a64 throughput lands where the aarch64-macos runner does
native self-host. **Fixture-corpus machine coverage (FAST, aarch64-linux,
`cc -v` fallback census): 88 to 99 of 211 functions, signature rejections
52 to 21. Stage 1 `5.8075G` to `5.8314G`
instructions (`+23.9M`, `+0.41%`), all of it source growth: preprocessed
tokens `1479081` to `1484788` (`+0.39%`), instructions per token
`3926.4` to `3927.4` (`+0.03%`) — the new selector code compiles for
x86-64 and executes nothing there. Self-host fixed point deterministic (`SELF_HOST deterministic
bytes=32072088`), all three byte-identity soaks green, qemu fixture
differential 32 fixtures x 4 allocator modes, 0 mismatches.**)

- **Promotion ships as the trio or not at all.** The port carries the
  x86-64 promotability scan (4- or 8-byte scalar locals whose address
  never escapes a same-width non-volatile load or store, the
  `!volatile_access` guard included), classification of promoted locals
  into general vregs, and load aliasing — block-confined load results and
  every load of a single-store local share the local's vreg, chains
  extending one level through DEREFERENCE — in one commit because
  `2026-08-10i` measured the first without the third at **+2.8% worse**:
  the copy a promoted load lowers to never coalesces. On AArch64 the
  same reasoning holds structurally (the copy is an orr-move instead of
  a mov), so the pair never existed apart here.
- **Porting promotion required porting the fused-read sinking check.**
  The a64 compare/branch fusion sank its reads to the branch row under a
  "every vreg is single-definition" argument (`2026-08-10m`'s soundness
  section). Promotion makes that argument false — promoted locals are
  exactly the multi-definition vregs — so the fusion walk now stamps
  every promoted local's latest store ordinal and the commit rejects any
  fusion whose read (through the load-alias root) was re-stored past the
  deepest absorbed member, byte-for-byte the x86-64 rule. Cross-block
  stores still need no check: a jump re-enters at a block head, above
  the compare, never between it and the branch.
- **Stage 9's finding (5) is resolved: AArch64 now schedules.** The
  32-product tree32 body that selected to a ~6-vreg-peak block of frame
  traffic now selects its locals into 32 live virtual registers against
  the 25-register allocatable file, and the stage-9 scheduler moves and
  keeps it under the same strict placement-improvement acceptance as
  x86-64. The machine test that documented a64 inertness now asserts
  movement on both targets.
- **AAPCS64 shapes, by census mass.** Signatures classify through the
  IR-owned `ir_type_abi_value` AAPCS64 machinery: float scalars travel
  as 64-bit bit images in general registers and bridge through the
  FMOV_TO_VEC/FROM_VEC rows at ABI boundaries (the bits above a 32-bit
  float are unspecified at every AAPCS64 passing site, the same
  argument the x86-64 movq bridges lean on); one-or-two-part register
  aggregates ride X registers; HFAs ride up to four consecutive V
  registers, with a new 32-bit LOAD_FRAME32 row because an f32 part at
  a 4-byte offset fails the 64-bit frame form's alignment contract; and
  large results travel through the X8 hidden pointer — which AAPCS64,
  unlike System V, does not return, so the epilogue writes no X0.
  Variadic AAPCS64 calls pass anonymous floats and aggregates exactly
  like named ones; Darwin variadic and Windows-on-ARM stay canonical as
  `2026-08-10h` scoped them.
- **Aggregate flow needed copy rows, and they clobber nothing.** An
  aggregate parameter stored to its shadow local is a slot-to-slot
  copy, so the shapes are unusable without COPY_FRAME_FROM_FRAME /
  COPY_FRAME_FROM_PTR / COPY_PTR_FROM_FRAME. The a64 forms chunk
  through the reserved X17 data scratch (X16 stays the large-offset
  address scratch), descending 8/4/2/1 so every access stays aligned —
  and unlike their x86-64 counterparts they carry no clobber mask, so
  the allocators keep every allocatable register live across them. The
  three rows join the scheduler's memory chain; the FMOV bridges were
  already in its float-state chain. Taking the copy rows also cleared
  the census's aggregate-load line (6 to 0) in passing.
- **Where the census mass went.** Signature rejections fell 52 to 21
  (the residue: vectors, indirect parameters, >8-register arities), and
  the freed functions moved inward exactly as the x86-64 stage-3 ABI
  work did: ARRAY/AGGREGATE literals now hold 31 fallbacks, float
  CAST/BINARY bodies 17 (a64 has no FARITH analog yet), and the
  legitimate tail (inline assembly 10, label addresses 8, TLS 4,
  atomics 6, STACK_ALLOCATE 4) is unchanged. CONSTANT_FLOAT cleared to
  0 by sharing the CONSTANT_INTEGER bit-image materialization.
- **Directed differential beyond the corpus.** A shape fixture in the
  corpus style — two-part aggregates, a 12-byte mixed aggregate, f32
  HFA2/HFA3, f64 HFA4, 32-byte indirect results, aggregate re-stores,
  float scalars, and a mixed signature interleaving a Pair between
  float and integer arguments — agrees across NONE/MIR_STACK/FAST/
  QUALITY under qemu, with 13 of its 17 functions machine-selected and
  the rest exercising the canonical/machine ABI boundary from both
  sides.
- **Gates, both commits:** test_all green (29,076 unit assertions, 32
  modules — kagg_take and big_make joined the stage-11 selection list),
  self-host fixed point deterministic, all three soaks — MIR_STACK,
  FAST, QUALITY compilers' canonical outputs byte-identical to the
  stage-2 reference's — `test_all_combinations_ci` green before the
  push (its qemu fixture differentials are the a64 gate). The
  byte-identity soak for the AArch64 target itself still only runs on
  an aarch64 host, as `2026-08-10h` recorded.
- **Left untaken, in census order.** ARRAY/AGGREGATE literals (31) are
  now the top body mass; float bodies (17) need the FARITH/FCMP
  analog; stack arguments and indirect arguments (the caller-side
  defensive copy) would lift the remaining signature mass; float
  locals stay unpromoted (the scan is integer-only — the float-scalar
  vreg model makes extending it mechanical once float bodies select);
  and a64 QUALITY/FAST throughput against canonical on native hardware
  is unmeasured — the aarch64-macos runner is where that number lives.
  `frequency_class` remains unwritten on both targets.
- Reference points for the next audit: fixture-corpus machine coverage
  99 of 211 (FAST, aarch64-linux), signature rejections 21, stage 1
  `5.8314G` instructions / `1484788` tokens, qemu differential 32
  fixtures / 4 modes. Reproduction: compile fixtures with `-target
  aarch64-linux` (the target must precede any `-march`) at each
  `-fregister-allocator=` mode and run under qemu-aarch64; the census
  is the summed `CODEGEN_FALLBACK` lines of a `-v -c` compile per
  fixture.


`2026-08-11e` (Linux x86_64, Zen 4 7940HS; register-allocator — QUALITY's
span pins extended to the vector register file, the lead `2026-08-10o`
recorded as "QUALITY grew no vector pins". **Measured negative, no code
change lands** — built and measured first against pre-widening main
`690a43b` (corpus QUALITY `-10.1%`, the number this entry originally
reported), then `2026-08-11c` (ZMM0-31 widening, PR 263) merged
concurrently and the re-measurement against the merged tree erases the
win entirely and replaces it with a small real regression. Frozen-tree
`bench`, min of eight, clearly separated bands: **`3,362,190,466`
(unmodified) vs `3,363,535,303` (with the extension), `+1,344,837`
instructions (`+0.04%`), FAST byte-identical between the two, isolating
the effect to QUALITY.**)

- **Why the pre-widening win evaporates.** `2026-08-10o`'s corpus
  (`wide_vector_loop`, eighteen 64-byte values live across a call-free
  loop) was sized against the sixteen-register ZMM0-15 file: two past
  capacity, forcing spills the extension's caller-saved pins could
  remove. `2026-08-11c` widened the file to ZMM0-31 concurrently — the
  *other* named lever off the same `10o` paragraph — and eighteen fits
  thirty-two outright. Confirmed at the byte level, not just the
  counter: the corpus driver's QUALITY binary, the raw fixture's
  QUALITY binary, and the fixture's `CODEGEN_ALLOCATOR` traffic line
  are all identical whether the vector-pin candidacy code is present
  or absent (reloads 20, spills 39, pins 5 — all general-class, the
  vector segment of the pin file never receives an assignment anywhere
  in the corpus). The call-crossing body was already flat by
  construction pre-widening — foreclosure still forbids pinning across
  a call — and stays so.
- **The regression that survived the no-op.** Zero vector-class pins
  land anywhere, but the code is not inert: admitting a vector
  candidate into the heap sets `heap_has_vector`, which reroutes each
  instruction's general-register budget calculation — vector operand
  slots stop counting against the general spare — for *every*
  function that has a vector virtual register, whether or not any
  vector candidate ends up pinned. The compiler's own two
  vector-selecting functions (`tokenize_compact`,
  `tokenizer_classifier_load`) both qualify, so their general-class
  pin candidacy shifts slightly even though the feature they exist to
  serve places nothing. This is invisible to `cc ide.c` (a compile
  that never executes the buster-language parser) and to the
  differential and byte-identity soaks, which is exactly the blind
  spot `2026-08-11`/`2026-08-11b` mapped for this pair of kernels:
  only `bench` runs them. Frozen-tree QUALITY stage traffic ticks by
  single digits (reloads 53,035 to 53,038, spills 84,262 to 84,261,
  boundary_spills 15,604 to 15,602) with the *total* pin count
  unchanged at 8,024 — a redistribution, not a growth — and the whole
  compiled stage differs by sixteen text bytes, all inside those two
  functions. Small as the static delta looks, it costs `+1.34M`
  executed instructions where the kernels actually run.
- **Diagnosis, confirmed three ways.** (1) Byte comparison of the
  corpus and fixture QUALITY binaries built with and without the
  change: identical. (2) Byte comparison of the whole frozen-`ide.c`
  FAST stage built by both compilers: identical, ruling out any
  non-vector-path change. (3) `bench`, min of eight repeats each,
  non-overlapping bands (unmodified `3,362,190,466`-`3,362,355,827`,
  modified `3,363,535,303`-`3,363,709,664`) — a `+1.34M` gap against a
  `~170K` band on each side, well outside noise. Same-compiler-twice
  determinism was checked directly (`cc ide.c` output stable byte for
  byte) before trusting the diverging pair, since an earlier session's
  entries flagged embedded-path drift as a false-difference trap; this
  comparison used identical relative invocations and the divergence
  held regardless of output path or directory, so it is real.
- **Conclusion, per the `2026-08-10l` discipline.** The lead is dead on
  the current corpus and actively costs a little on the workload that
  matters: **no code change lands.** `register_allocator_quality.c` is
  reverted to its pre-branch state. The useful output is the causal
  picture, recorded so nobody re-opens this lever against the current
  file width: caller-saved vector span pins are only ever profitable
  when live call-free vector pressure exceeds the file, and `10c`'s
  widening to thirty-two moved that bar past every fixture this repo
  has. A future corpus or real workload with more than thirty-two
  call-free live vector values — or a callee-saved vector convention,
  which System V does not offer — would need to exist before this is
  worth re-measuring; the incidental general-budget coupling this
  attempt exposed (any function admitting a vector candidate silently
  reprices its general-class pin budget) is a design trap for whoever
  tries again and should be designed out from the start, not
  patched onto the class split after the fact.
- **Gates:** test_all green, self-host fixed point deterministic, all
  three soaks — MIR_STACK, FAST, QUALITY — byte-identical against the
  freshly rebuilt stage-2 reference, `test_all_combinations_ci` green
  before the push. The tree ships no allocator change, so FAST and
  QUALITY behavior are untouched by construction against `2026-08-11c`'s
  merged reference.
- Reference points for the next audit: unchanged from `2026-08-11c`
  and `2026-08-11b` below — this entry adds no new baseline, since it
  ships nothing.

`2026-08-11c` (Linux x86_64, Zen 4 7940HS; register-allocator stage 10
follow-on — the machine vector file widens from ZMM0-15 to ZMM0-31, the
`10o` entry's first recorded lever. Baselines re-taken same-session on a
frozen `b7ba8ea` main with a clang-built frozen compiler; the corpus
driver (`tests/basic_c_vector_register_pressure.c`, main stripped, 200
iterations of the three bodies at 2,000 rounds) reproduces `10o`'s
numbers within ~2K instructions on the frozen binary, so it is the same
instrument: **frozen vector corpus MIR_STACK `237,125,812` / FAST
`106,692,452` / QUALITY `103,156,258` / clang -O2 -march=native
`30,224,870`; frozen `b7ba8ea` stage compiles FAST `40.7039G` / QUALITY
`39.9089G`, stage text 18,222,268 / 17,905,612; compile cost canonical
`5.8073G` / fast `7.3510G` / quality `9.5040G`.** The stage's own
numbers: **vector corpus FAST `91,085,049` (`-15,607,403`, `-14.6%`) and
QUALITY `87,548,849` (`-15,607,409`, `-15.1%`), the two modes removing
the identical traffic to within six instructions — the eighteen-value
working set now sits fully register-resident, corpus-driver traffic FAST
73/91 to 31/51 reloads/spills and QUALITY 63/79 to 21/39; the gap to
clang narrows 3.53x to 3.01x (FAST) and 3.41x to 2.90x (QUALITY);
MIR_STACK, the scalar corpus, the frozen-tree stage compiles, and
compile cost are all flat.**)

- **A contract widening, not a mask.** The unified register file grows to
  48: general 0-15, ZMM0-31 at unified indices 16-47, exactly as `10o`
  filed the lever. `MACHINE_TARGET_REGISTER_LIMIT` rises to 48 and every
  allocator mask widens u32 to u64 — the description's allocatable,
  callee-saved, and vector masks, the opcode table's clobber masks, the
  placement's callee-saved output, the QUALITY pin plumbing
  (`pinned_mask`, the per-instruction span and entry masks, the
  foreclosures), and the FAST scan's candidate, reserved, contract, and
  conform-edge `pending` masks, whose `1u <<` shifts by a register index
  were the undefined-behavior trap the widening had to audit. The
  architecture stays `10o`'s: one file, one scan, one edit stream, the
  candidate set class-filtered and nothing else. ZMM16-31 need no new
  feature gate — they are architectural wherever EVEX itself is
  (AVX512F in 64-bit mode), which the vocabulary's selection gate
  already requires before any vector virtual register exists.
- **Encoder: three EVEX extension bits.** The machine EVEX emitter had
  R' and V' pinned high and X̄ fixed. Now R' carries the reg field's bit
  4, V' the vvvv bit 4, and X̄ doubles as the rm bit 4 in
  register-direct forms — memory bases are general registers and never
  reach 16, so one expression serves both addressing shapes. kmovq's VEX
  forms are untouched (masks travel general-to-k1 only), and the
  vzeroupper-at-call/ret policy survives unchanged: vzeroupper clears
  bits 128+ of the VEX-visible ymm0-15 only and ZMM16-31 have no legacy
  or VEX encodings to transition against — the high registers still
  flush at every call through the all-caller-saved System V contract,
  which is the widened file's remaining measured cost (the call-crossing
  corpus body improves only through lighter contention, not contract).
- **Costs held flat where the `10o` trims predicted.** The per-function
  `active_register_count` trim keeps every all-scalar function's scan
  loops at the general file, so the widening prices only functions with
  vector virtual registers; QUALITY's foreclosure prefix table now stops
  at the highest allocatable register instead of the unified file's end,
  which pays for the width it would otherwise have bought. Measured on
  the frozen workload, same-session: canonical `5.8073G` flat to 91
  instructions, fast `7.3475G` (`-0.05%`), quality `9.5032G`
  (`-0.01%`).
- **What did not move, verified at the byte level.** Scalar placement is
  untouched by construction and the proof is stronger than a corpus
  re-run: the frozen-workload stage compilers built by the frozen and
  the widened compiler differ in exactly 87 bytes, all inside
  `tokenize_compact` and the classifier — the only vector-selected
  functions in ide.c — with stage text byte-count identical (18,222,268
  / 17,905,612) and the scalar register-pressure fixture binaries
  byte-identical under both modes. The frozen-tree stage compiles are
  flat (FAST `40.7040G`, `+0.0004%`; QUALITY `39.9089G`, `-0.0001%` —
  the kernels lex buster source and `cc ide.c` never executes them) and
  both stage compilers' canonical outputs are byte-identical. The
  stage absolutes sit above `10o`'s references because the frozen
  workload moved from `df11728` to the merged `b7ba8ea` main — workload
  reasons, exactly as `10n` documented for its own re-freeze.
- **Tests follow the claim.** The machine_test vspill differential
  flips: eighteen accumulators against the widened file must produce
  zero vector-class edits under FAST (the remaining scalar traffic is
  the loop-carried counters and the frontend's stack save/restore
  bracket, unchanged), and a placement scan asserts the high file is
  actually handed out — eighteen live values cannot fit ZMM0-15, so
  residency plus a `>= ZMM16` operand register is proof the extension
  is real. The executing differential then runs those encodings against
  the canonical NONE oracle on every AVX-512 host, which is where the
  new EVEX bits are actually gated; hosts without the features still
  verify selection, placement, and encoding shape.
- **Gates, this commit:** test_all green Debug and Release (29,059
  unit, 32 module, vector differentials in all four modes), self-host
  fixed point deterministic, all three soaks — MIR_STACK, FAST,
  QUALITY — byte-identical against the freshly rebuilt stage-2
  reference, frozen-tree canonical stage outputs byte-identical,
  `test_all_combinations_ci` green before the push (AArch64 keeps an
  untouched 32-register description under the 48 limit).
- **Left untaken, updated from `10o`'s list.** The bench-miscompile
  hunt is fixed separately (`2026-08-11`, PR 261) and its dynamic
  payoff measured (`2026-08-11b`, PR 262, below) — that entry's
  merged-tree stage bench is the current execution reference. Vector
  ABI stays the fixtures' whole remaining fallback. Span-scoped
  caller-saved vector pins are now the corpus's dominant residual: the
  call-crossing body pays the full working-set round-trip per
  iteration by contract, and with the file no longer the bottleneck
  that cost is most of the remaining 2.9-3.0x to clang. 64-bit-lane
  VBINARY (W1 forms) and union-with-vector locals stand as before.
- Reference points for the next audit, frozen `b7ba8ea` ide.c as the
  workload (same-session invocation, absolute paths): **FAST stage
  `40.7040G`, QUALITY stage `39.9089G`**, text 18,222,268 / 17,905,612,
  compile cost canonical `5.8073G` / fast `7.3475G` / quality
  `9.5032G`. Vector corpus: MIR_STACK `237,125,808`, **FAST
  `91,085,049`**, **QUALITY `87,548,849`**, clang `30,224,870`.

`2026-08-11g` (Linux x86_64, Zen 4 7940HS; the remaining non-vector selection
gaps of the `2026-08-10o` census, lifted in measured order on merged main
`b7ba8ea`. Developed concurrently with the bench-crash fix (PR 261, entry
`2026-08-11`, merged while this ran) and its payoff measurement
(`2026-08-11b`, merged), the ZMM0-31 widening (PR 263, `2026-08-11c`),
the stage-10 leftovers (`2026-08-11d`), the vector span pins (PR 265,
`2026-08-11e`), and the frequency-aware pin economics (`2026-08-11f`) —
none of their numbers are included here and the merged tree re-measures. Baselines were re-taken on `b7ba8ea`
since the workload grew past `10o`'s frozen tree: **frozen `b7ba8ea` stage
compiles FAST `40.7039G` / QUALITY `39.9089G`; compile cost canonical
`5.8074G` / fast `7.3510G` / quality `9.5040G`.** The entry's own numbers:
**ide.c fallback functions 30 to 14; frozen-tree stage compiles FAST
`40.6981G` (`-5.70M`, `-0.0140%`) / QUALITY `39.9038G` (`-4.91M`,
`-0.0123%`), same-session interleaved min of three; stage text FAST
21,552,104 to 21,472,792 (`-0.37%`), QUALITY 21,249,520 to 21,166,216
(`-0.39%`); compile cost canonical flat, fast `7.3607G` (`+0.13%`),
quality `9.5331G` (`+0.31%`); every gate green and the three soaks
byte-identical.**)

- **Census before code, and the census ranked the work.** The `10o` census
  named 30 fallback functions with one reason each; a temporary
  per-function name print at the fallback-count site (removed before
  commit) confirmed all 30 on `b7ba8ea` to the function. Three gaps were
  lifted, in measured-effect order; the executed-instruction story is
  brutally front-loaded: **the INDEX batch is `-5.58M` of the `-5.70M`**,
  because its five functions (`x86_64_cpu_features_from_cpuid` and the
  target-feature family, 4,255 IR instructions) run at every compile's
  target setup, while the TLS batch measured `-0.14M` and the CALL batch
  `-12K` — inside the run band. Static mass predicted none of that.
- **INDEX on rvalue arrays: one condition in the base-address rule.**
  `machine_x64_select_place_address_offset` formed frame addresses only
  for LOCAL-defined slots; the canonical INDEX base rule says an array or
  vector *value* (`IR_VALUE_VALUE` category) is its storage. The
  extension mirrors that rule exactly — slices and struct values keep the
  loaded-pointer path, matching the canonical emitter's per-opcode base
  handling — and the five compound-literal feature-table functions
  select. Payload: `-5.58M` executed on the FAST stage compile.
- **Thread-local GLOBAL: the canonical local-exec pair as one row.** A new
  `MACHINE_X64_LEA_TLS` row (def, rematerializable, payload indexing
  call_targets like `LEA_SYMBOL`) encodes `mov dest, fs:[0]` plus
  `lea dest, [dest + tpoff]` byte-for-byte as the canonical sequence but
  into any allocated register, with the SIB fixup for the r12/rsp column.
  The `MachineCallSite` reserved word became `is_thread_local` and the
  x86-64 call-site loop carries it onto the module relocation, which the
  existing TPOFF resolution consumes unchanged; selection accepts TLS
  symbols only for Linux/Android targets, where that sequence is the
  canonical form. The five arena/thread-context functions select; the
  soak executes them on every allocation the stage compiler makes.
  Measured: `-0.14M` — hot functions, cold TLS fraction.
- **CALL shapes: probed first, and the probe named two cheap fixes.** The
  temporary reject print showed exactly two causes across the five CALL
  functions: a callee returning a 16-byte `IR_TYPE_INTEGER` (u128 —
  `timestamp_take`), and variadic calls carrying seventeen arguments
  against the sixteen-slot cap (`string_format` call sites in
  `analysis_serialize_module_interface` and `run_c_compiler`).
  `MACHINE_X64_MAX_ARGUMENTS` went 16 to 24 — the arrays it sizes are
  per-selection stack storage, and no placement logic keys on the
  constant — and a 128-bit integer became the System V two-eightbyte
  INTEGER pair in `machine_x64_value_shape` (parts at offsets 0/8 over a
  16-byte slot, the same parts the canonical integer-aggregate rule
  builds), with classification giving u128 ARGUMENT/LOAD/CALL results
  slots like any two-eightbyte aggregate. All five CALL functions select,
  and `timestamp_ns_between` came along free. Measured: `-12K`, i.e.
  nothing — none of these run meaningfully inside `cc` — recorded here so
  the next audit does not re-derive that.
- **What moved forward, not away.** Three of the eleven `10o`
  "variadic-signature" rejects were really u128 signatures:
  `timestamp_take` and the two `string_format_u128_parts` helpers now
  select their signatures and stop at their bodies' 128-bit `CAST`
  (opcode 27) — the census now says what they actually need.
- **Costs, and where they come from.** Canonical compile cost is flat to
  the megainstruction. Fast `+9.75M` (`+0.13%`) and quality `+29.0M`
  (`+0.31%`) are sixteen more functions through selection, placement, and
  encoding — including the two biggest fallbacks (`analysis_serialize_
  module_interface` 1,950 IR, `run_graphical_app` 951) — priced at the
  same rate the stage-10 widening paid. The widened argument arrays'
  zero-fill is noise at this scale.
- **Nothing else moved, proven byte-for-byte.** Both pressure corpora
  compile byte-identical under MIR_STACK, FAST, and QUALITY from a
  pristine-`b7ba8ea`-built compiler and this change's compiler — every
  previously selecting function keeps its exact code, because each lifted
  gap was previously a whole-function reject. The canonical stage outputs
  are byte-identical between the compilers (the soaks below).
- **Left untaken, sized.** The 128-bit `CAST` tail (3 functions, 132 IR)
  needs the extend/truncate arms — the sign-extend high half wants a
  cqo-shaped row the vocabulary lacks — and the parts helpers likely hit
  128-bit `BINARY` right behind it; dynamic effect ≈ 0. The variadic
  signatures (7 functions, ~210 IR) plus `VA_ARG` (`string_format_va`,
  2,888 IR) are the register-save-area prologue, va_list initialization,
  and the va_arg branch machine — real new machinery, none of it falling
  out of existing shapes, for formatters a quiet compile never calls;
  sized and deliberately not taken. `cpuid`/`xgetbv` (inline assembly)
  and `os_flush_instruction_cache` are outside the machine path by
  design. The census floor on ide.c is now 14 = 7 variadic + 3 CAST-128 +
  1 VA_ARG + 2 inline-asm + 1 icache.
- **Gates, every commit:** test_all green (29,066 unit, 32 module,
  including four new lifted-gap selection differentials in
  machine_test.c: `tls_bump`, `rv_lit`, `u128_ferry`, `call_seventeen` —
  link-time relocations keep their execution with the soak, where every
  one of these shapes runs in the compiler's own hot path);
  test_self_host deterministic (stage 1 `5.8093G` on the grown tree);
  all three soaks — MIR_STACK, FAST, QUALITY — byte-identical against
  the freshly rebuilt `build/self-host/Release/ide-stage2`;
  `test_all_combinations_ci` green before the push.
- Reference points for the next audit, frozen `b7ba8ea` ide.c as the
  workload (same-session invocation, absolute paths): **FAST stage
  `40.6981G`, QUALITY stage `39.9038G`**, stage text 21,472,792 /
  21,166,216, compile cost canonical `5.8074G` / fast `7.3607G` /
  quality `9.5331G`, ide.c fallback functions 14.

`2026-08-11b` (Linux x86_64, Zen 4 7940HS; the measurement `2026-08-10o`
left blocked — the vector subset's dynamic payoff in buster-built stages —
taken now that the aggregate zero-fill fix (`2026-08-11`, below) lets a
machine-built stage run `bench` at all. Frozen-tree methodology, re-scoped
from compile cost to stage execution: one `git archive` of main `b7ba8ea`
(the stage-10 merge) is the source, `build/generated` copied in, and stage
binaries are built from that frozen source twice with the exact self-host
recipe (`cc -Isrc -Ibuild/generated -DBUSTER_UNITY_BUILD=1
-DBUSTER_INCLUDE_TESTS=0 -g -fregister-allocator=<mode>`) — once by a
pre-stage-10 compiler (clang-built Release at `1e37e12` in a scratch clone,
the zero-fill fix cherry-picked on top, without which every machine-built
stage still crashes `bench`) and once by current main's compiler (this
tree, which carries the same fix) — then every stage runs `bench` from the
frozen repo root under `perf stat -e instructions:u`, min of five, 61-file
corpus, 200 iterations. No pre-PR-261 machine-stage bench number is a
reference (they all ran the corrupt parser), so every row below is
re-baselined from scratch. **FAST-built stage: BENCH_PARSE min_ns
`822,970` to `688,051` (`-16.4%`), BENCH_IO `993,378` to `859,660`
(`-13.5%`), executed instructions `3.5830G` to `3.3116G` (`-271.4M`,
`-7.6%`). QUALITY: parse `795,989` to `682,781` (`-14.2%`), IO `975,362`
to `866,193` (`-11.2%`), instructions `3.6355G` to `3.3622G` (`-273.3M`,
`-7.5%`). MIR_STACK: parse `1,395,670` to `1,346,615` (`-3.5%`), IO
`-3.7%`, instructions `6.2585G` to `6.1418G` (`-116.7M`, `-1.9%`). The
canonical stage — byte-identical between the two builder compilers — is
the fixed reference at parse `1,527,202` / IO `1,754,128` / `6.8926G`:
machine-built stages now parse the corpus in 45% of canonical's time.**)

- **Why `bench` is the workload, restated.** `tokenize_compact` and
  `tokenizer_classifier_load` lex *buster* source, so they execute only
  in the buster-language parser that `bench` drives; `cc ide.c` never
  runs them, and `2026-08-10o` already measured the frozen-stage
  compiles flat to the digit. This entry is the other half of that
  audit: the same static change, measured where it executes.
- **The payoff is the kernels going register-resident.** The two
  builders differ only in the stage-10 vector subset (`1e37e12` is
  `b7ba8ea`'s parent; the zero-fill fix is applied to both), so the
  deltas are attributable by construction. Under FAST and QUALITY the
  canonical fallback's frame round-trip per SIMD operand becomes a
  straight ZMM dataflow, and the bench workload drops `-271.4M` /
  `-273.3M` executed instructions — `-7.6%`/`-7.5%` of the whole stage,
  `-16.4%`/`-14.2%` of BENCH_PARSE min wall time. MIR_STACK selects the
  same rows but keeps its per-row stack round-trips, and still gains
  `-116.7M` (`-1.9%`, `-3.5%` parse) over its canonical-fallback self.
  The static side agrees: stage text FAST `-56,912`, QUALITY `-59,024`,
  MIR_STACK `-6,808` — the same shape `10o` measured statically on the
  `df11728` workload.
- **Why the percentages differ in scope.** The instruction count is
  whole-process (startup plus both bench phases); the min_ns numbers
  are per phase. The kernels run in *both* phases — BENCH_IO
  re-tokenizes from the filesystem, BENCH_PARSE from preloaded bytes —
  so the `-271M` splits across them, and the per-phase wall drops
  (`-13.5%`/`-16.4%` FAST) are consistent with that split at roughly
  flat IPC: the phase percentages look larger than the process
  percentage because the denominator sheds the startup and non-kernel
  mass, not because the eliminated instructions were disproportionately
  slow ones. The pre-existing FAST/QUALITY
  inversion also survives the vector subset: QUALITY executes *more*
  instructions than FAST on this workload (`3.6355G` vs `3.5830G`
  before, `3.3622G` vs `3.3116G` after — both drop the same `~272M`)
  yet wins parse wall time in both generations (`795,989` vs `822,970`;
  `682,781` vs `688,051`) — instruction count is not the scoreboard
  between those two, only within a mode across compilers.
- **Cross-checks.** The `-v` census on the frozen workload pins the
  attribution directly: fast-mode fallback_functions 33 to 30 (the two
  kernels plus `mask64_count`), machine-mode canonical simd_operations
  112 to 0, code_bytes 12,421,734 to 12,364,822 — `10o`'s static
  fingerprint, reproduced on this workload. The canonical stages built
  by the two compilers are byte-identical (one md5 covers both),
  re-confirming stage 10 changed no canonical output; and this session's
  current-main MIR_STACK stage benches at medians 1.634 ms IO /
  1.414 ms parse against the `2026-08-11` entry's 1.58 / 1.40 on the
  older `df11728`-plus-fix workload — the reference reproduces across
  the re-freeze. `test_self_host`'s new machine-stage bench row on this
  tree counts `6.1421G` instructions against this entry's frozen-tree
  `6.1418G` — two independent trees, 250K apart. Bands: executed
  instructions repeat within `~200K` (0.006%), min_ns within `~2%`
  across the five runs.
- **Distance to the ceiling, as context.** The clang-built Release ide
  runs the same bench at parse `45,167` / IO `206,717` / `338.5M`
  instructions: the best machine-built stage still executes 9.8x the
  instructions and 15.2x the parse wall time. That gap is the levers
  `10o` recorded, now with a dynamic number attached: vector ABI,
  zmm16-31, and scheduling over vector rows all live between these two
  columns.
- **Method note.** PR 261 (the zero-fill fix, the `2026-08-11` entry)
  was still open when this was measured; this branch carries its
  commits because no machine-built stage runs `bench` without them, and
  this entry sits above that one deliberately — merge order must keep
  it that way. Buster-built stages come out of `cc` without the execute
  bit; `chmod +x` before invoking them.
- **Gates:** documentation-only on top of the PR-261 tree; test_all,
  test_self_host (now itself benching a mir-stack stage), the three
  byte-identity soaks — MIR_STACK, FAST, QUALITY against the canonical
  stage-2 reference — and `test_all_combinations_ci` all green on this
  tree before the push.
- Reference points for the next audit, frozen `b7ba8ea` as source and
  its 61-file bench corpus as workload (min of five, this machine):
  canonical stage parse `1,527,202` / IO `1,754,128` / `6.8926G`;
  **FAST parse `688,051` / IO `859,660` / `3.3116G`**; **QUALITY parse
  `682,781` / IO `866,193` / `3.3622G`**; MIR_STACK parse `1,346,615` /
  IO `1,538,093` / `6.1418G`; clang Release ide parse `45,167` / IO
  `206,717` / `338.5M`. Stage text (BSD `size`): canonical
  `27,780,812`, FAST `18,672,532`, QUALITY `18,352,740`, MIR_STACK
  `27,886,076`.

`2026-08-11` (Linux x86_64, Zen 4 7940HS; correctness, not throughput: every
machine-register-allocator-built stage crashed `ide bench` — MIR_STACK with a
SIGSEGV walking a corrupt `AstType` chain in `finish_type_ranges`, FAST and
QUALITY with the `parser_source_range_set_end` assertion — pre-existing on
pristine `df11728` and fixed here. The x86-64 machine selector's
`IR_OPCODE_AGGREGATE` wrote only the member operands and never zero-filled
the slot; the canonical emitter zero-fills `layout.size` first. Post-fix all
three modes run `bench` clean and the path is no longer dark in CI.)

- **Why three soak generations never saw it.** The byte-identity soaks
  prove a machine-built stage reproduces canonical output for `cc ide.c`
  — the C frontend — and `test_self_host` benches only the canonical
  stage. `state_push` lives in the *buster* parser, which only `bench`
  executes, so the miscompile sat in every machine-built binary with a
  green soak next to it. The differential unit harness had no union
  case, and for structs whose initializers cover every field the missing
  zero-fill is invisible.
- **The defect.** `*state = (ParserState){0}` reaches the selector as
  AGGREGATE with operands for `id` plus the union's first member — 16
  bytes of a 104-byte object. Canonical (codegen.c) zeroes the whole
  result slot before writing members; the machine handler
  (machine_x86_64.c) wrote members only, so 80 bytes of union tail took
  whatever the probe stack held, and the parser later dereferenced a
  garbage `AstType*` (fault address 0x51). The fix mirrors canonical:
  one MOV_RI zero into a synthesized vreg, then chunked STORE_FRAME
  8/4/2/1 rows across `layout.size`, before the member writes. ARRAY
  needs nothing — the frontend materializes every element including the
  zero tail (and the handler already says so).
- **Method note: bisecting a miscompiled function out of 3,174.** An
  env-gated filter at the codegen.c machine gate, compiled into the
  *clang-built* compiler, so each probe is one `ide cc` plus one
  `bench`. A global-ordinal counter does not work — codegen runs once
  per function and the trace showed interleaved ordinal streams — so
  the filter that converged is stateless: allow the machine path only
  for function names lexicographically inside an env-provided
  `[LO, HI)`, and binary-search over the sorted name list. Twelve
  probes isolated `state_push`; machine-enabling that one function
  reproduced the exact crash, and its disassembly against the
  canonical build showed the twelve missing `xor`-store quads
  directly. The instrumentation was reverted after use; the technique
  lives in this entry.
- **Gates so the path stays lit.** (1) `union_tail` in machine_test.c: a
  union whose zero literal covers one byte of forty-eight, executed
  machine-vs-canonical in the differential harness — fails at the
  differential compare without the fix, passes with it. (2)
  `test_self_host` (and the superbuild edge in
  `test_all_combinations_ci`) now compiles a third stage with
  `-fregister-allocator=mir-stack` using stage 2 and runs `bench` on it,
  on every non-Windows platform — Windows stays out because its x86-64
  ABI keeps the machine path entirely on the canonical fallback. Cost on
  this machine: 7.86 s compile (`92.05G` instructions, 3,174 machine
  functions, 64 fallbacks) plus 0.62 s bench.
- **Reference points, post-fix.** Measured on `df11728` plus this fix,
  before the stage-9/10 merges (`2026-08-10n`/`o`) landed beneath this
  entry in the rebase; the rebased tree re-passed every gate, with the
  QUALITY scheduler running over the zero-fill rows. MIR_STACK-built
  stage compiling ide.c canonically: `73.62G` executed instructions
  (repeat band ~5 K); its `bench` medians 1.58 ms IO / 1.40 ms parse
  against canonical 0.9 ms.
  The zero-fill's pre/post execution delta was not isolated: pre-fix
  machine stages carried a corrupt buster parser, so no prior
  machine-stage number was a trustworthy reference anyway — the next
  machine-mode audit re-baselines from here. Canonical output is
  untouched by construction (the fix is machine-selection-only), and
  the token and executable fixed points held: `test_all` 28,915/28,915,
  byte-identity soaks in all three allocator modes against the
  canonical reference, `test_all_combinations_ci` green with the new
  machine bench rows in its self-host edge.

`2026-08-10o` (Linux x86_64, Zen 4 7940HS; register-allocator stage 10 —
vectors, run as selection coverage first and allocation second, on the
merged `df11728` main that carries `10j`+`10k`+`10l`+`10m`. Developed
concurrently with the stage-9 scheduling entry (`2026-08-10n`, QUALITY-only,
its own branch) against the same `df11728`; neither entry's numbers include
the other's effect and the merged tree re-measures. Baselines were
re-taken on that merge since no prior entry describes it: **pressure corpus
FAST `91,322,243` / QUALITY `82,123,446` / clang -O2 `42,738,012`; frozen
`df11728` stage compiles FAST `39.930G` / QUALITY `39.247G`; compile cost
canonical `5.6886G` / fast `7.1697G` / quality `8.3879G`; the corpus
driver reproduces `10m`'s numbers to the instruction.** The stage's own
numbers: **both Validark lexer kernels now select and run fully
register-resident under FAST/QUALITY; a first vector pressure corpus
measures MIR_STACK `237,124,019` / FAST `106,691,250` / QUALITY
`103,155,456` / clang -O2 -march=native `30,221,439` executed
instructions; the scalar corpus and the frozen-stage compiles are
unchanged to the digit; and the hunt surfaced a pre-existing machine-path
miscompile on unmodified main — every machine-built compiler crashes
`ide bench` — that no existing gate exercises.**)

- **Census before code, as specified.** `ide cc -v`'s fallback census on
  ide.c: 32 of 3,168 functions fall back, one reason each. The
  vector-typed mass is exactly the two Validark lexer kernels —
  `tokenize_compact` (1,952 IR instructions) and
  `tokenizer_classifier_load` (216) — both rejected at their 64-byte
  vector LOCAL by the frame's sixteen-byte alignment cap before any
  vector operation was even reached. Everything else is non-vector:
  thread-local GLOBAL x5, INDEX-on-rvalue-array x5 (the compound-literal
  feature tables), CALL shapes x5, variadic signatures x11, inline
  asm/VA_ARG/icache x4. The fixture corpus concurs: basic_c_simd.c and
  basic_c_wide_vector_argument.c fall back nearly whole on vector ABI
  signatures plus the vector local. Two structural facts set the design:
  the canonical frame layout *clamps vector alignment to sixteen* and
  uses unaligned vmovdqu8 throughout, so vector slots need no frame
  realignment and no slot-alignment widening — only 64-byte sizes; and
  the canonical SIMD lowering round-trips every operand through its frame
  slot into fixed ZMM0-2/k1, so register-resident vector values are the
  entire machine-path payoff.
- **One unified register file, not a second allocator.** ZMM0-15 join the
  x86-64 file as unified indices 16-31 — exactly filling
  `MACHINE_TARGET_REGISTER_LIMIT`, so every mask stays u32 and the scan,
  edge contracts, LRU, and edit stream carry both classes unchanged; only
  the candidate set is class-filtered (`vector_allocatable_mask`, all
  caller-saved on System V, so the existing call flush spills the whole
  vector file with no new code). Vector spill homes are 64-byte dedicated
  slots outside the 8-byte pool; masks travel in GENERAL registers as the
  vocabulary's Mask64 design intends, staged through k1 inside the
  encoder only. The scalar-float rows' hidden XMM scribbles became
  declared clobbers (units 16/17; the float-argument bridge 16-23), and
  functions that touch the vector file end their AVX-512 regions with
  vzeroupper at calls and returns, where every vector value is dead by
  the caller-saved contract.
- **Selection: fifteen vector rows plus scalar POPCNT.** Whole-vector
  moves and frame/pointer loads and stores (masked forms included), the
  full sixteen-operation SIMD vocabulary (vpermt2b and vpternlogd read
  their first source in place, so selection copies it into the
  destination first and the copy coalesces when the source dies — the
  four-source shapes fit the four-operand row that way), a three-address
  VBINARY for 8/16/32-lane add/subtract and the bitwise trio, and
  POPCNT32/64 — the kernels' `mask64_count` was the one *scalar* gap
  blocking selection. Vector locals promote under the same
  address-never-escapes rule as scalars, which is where the kernels'
  named chunk variables stop round-tripping: the promoted form of the
  classifier is a straight ZMM dataflow with zero vector frame traffic.
  The 64-bit-lane VBINARY forms stay outside the subset (their EVEX
  encodings are W1 where this vocabulary is uniformly W0), as do vector
  ABI signatures and union-with-vector locals (alignment past sixteen,
  canonical parity).
- **The differential caught a real miscompile before it shipped.** The
  new machine_test section — a znver5 vector corpus selected, verified,
  placed, and encoded on every host, executed against the canonical NONE
  oracle under all four modes on hosts whose cpuid carries the features —
  failed for MIR_STACK on every probe: the placement builder's *spill*
  loop still used the scalar per-slot scratch for vector definitions, so
  a 512-bit result spilled as an 8-byte mov of whatever RAX held. The
  reload side was class-aware; end-to-end fixture runs had passed on
  accidental frame reuse. One line (class-aware spill scratch) fixes it;
  the per-function differential now pins all four modes.
- **Measured, on the new vector pressure corpus**
  (tests/basic_c_vector_register_pressure.c: eighteen 64-byte values live
  across a loop — two past the file — eight live across a scalar call
  per iteration, and a deep tree; self-checking, zero fallbacks in every
  mode; driver = 200 iterations of the three bodies at 2,000 rounds):
  MIR_STACK `237,124,019`, **FAST `106,691,250`**, **QUALITY
  `103,155,456`**, clang -O2 -march=native `30,221,439` (min of five).
  Fixture-main traffic: FAST reloads 68 / spills 84, QUALITY 64/78 with
  3 pins, MIR_STACK 975/866. The 3.53x gap to clang has two named
  causes: clang allocates zmm16-31 (its eighteen accumulators never
  spill where this file holds sixteen), and System V's all-caller-saved
  vector file makes every call a full working-set round-trip — the
  extended file and split-aware spill placement are the recorded levers.
- **What did not move, exactly as predicted or better.** The scalar
  corpus repeats to the digit (FAST `91,322,244`, QUALITY `82,123,446`)
  and the three soak binaries are byte-identical — scalar placement is
  untouched by construction. The frozen-stage compiles are flat (FAST
  `39.930G`, QUALITY `39.247G`, canonical outputs byte-identical to the
  reference stages'): the kernels lex *buster* source, and `cc ide.c`
  never executes them — their static mass moves off the canonical
  emitter (ide.c fallback functions 32 to 30, machine-mode canonical
  simd_operations 112 to 0, stage text `-27,504` FAST) but their
  executed-instruction win lives in the parser path, which leads to:
- **Found: machine-built compilers crash `ide bench` on unmodified
  main.** A compiler built with `-fregister-allocator=mir-stack`
  SIGSEGVs in `finish_type_ranges` (parser.c:3747, corrupt AstType
  chain); FAST and QUALITY builds assert in
  `parser_source_range_set_end` (parser.c:3739). Reproduced with a
  pristine `df11728` compiler built from a clean archive — pre-existing,
  not introduced here. The canonical stage-2 binary runs bench clean, so
  a machine-selected parser.c function is miscompiled, and nothing gates
  it: the soaks only exercise `cc ide.c` (the C frontend), and
  test_self_host benches only the canonical stage. Until that hunt
  lands, the kernels' dynamic payoff in buster-built stages cannot be
  measured; the vector corpus above carries this entry's execution
  claims.
- **Compile cost, and the width tax paid back.** The first cut measured
  fast-mode `+1.66%` and quality-mode `+3.87%` on the frozen workload —
  the widened file's loops taxing every all-scalar function. Three
  scoped trims (a per-function active register count that stops the
  scan's loops at the general file when no vector vreg exists, skipping
  QUALITY's foreclosure prefixes for non-allocatable registers, and a
  popcount for its foreclosure counts) land the final cost at canonical
  `5.6886G` (flat), **fast `7.2128G` (`+0.60%`)**, **quality `8.4084G`
  (`+0.24%`)**, byte-identical placements throughout.
- **Gates, every commit:** test_all green (29,028 unit, 32 module,
  including the new vector differentials), self-host fixed point
  deterministic, all three soaks — MIR_STACK, FAST, QUALITY —
  byte-identical against the freshly rebuilt stage-2 reference, the
  frozen-tree canonical stage outputs byte-identical between the
  compilers, `test_all_combinations_ci` green before the push (its qemu
  fixture differentials are the AArch64 gate; AArch64 keeps zero vector
  selection and an untouched description).
- **Left untaken, in causal order.** The bench miscompile hunt gates
  everything dynamic in the parser path and is filed separately. Vector
  ABI (arguments and returns in ZMM registers) is the fixtures' whole
  remaining fallback and what a vector-heavy call graph needs; zmm16-31
  would halve the corpus pressure at the cost of EVEX R'/V' handling and
  a wider register limit than u32 masks allow; QUALITY grew no vector
  pins (System V has no callee-saved vectors, so only call-free spans
  could ever earn one — the corpus's call-crossing body is the measured
  reason to build span-scoped caller-saved vector pins); 64-bit-lane
  VBINARY needs the W1 forms; and the union-with-vector local keeps
  basic_c_simd's main canonical until slots past sixteen-byte alignment
  are worth their frame contract.
- Reference points for the next audit, frozen `df11728` ide.c as the
  workload (same-session invocation, absolute paths): **FAST stage
  `39.930G`, QUALITY stage `39.247G`**, text 18,061,298 / 17,851,578,
  compile cost canonical `5.6886G` / fast `7.2128G` / quality `8.4084G`.
  Scalar corpus: FAST `91,322,244`, QUALITY `82,123,446`, clang
  `42,738,012`. Vector corpus: MIR_STACK `237,124,019`, **FAST
  `106,691,250`**, **QUALITY `103,155,456`**, clang `30,221,439`.
`2026-08-10n` (Linux x86_64, Zen 4 7940HS; register-allocator stage 9 —
pressure-aware scheduling over the machine IR, built with the
accept-cheaper-placement discipline and shipped in QUALITY only. Every
number below is against baselines re-taken on the merged main `df11728`
(the `10k`/`10l`/`10m` entries all warned no entry describes the merged
tree), with the frozen workload re-frozen from that main — it now
carries the JIT, so the stage absolutes sit above `10m`'s references
for workload reasons, not regression. **Frozen-tree stage comparison
(same-session reference stages, absolute paths, min of three, band
~30K): QUALITY `39.2297G` to `39.1399G` (`-89.8M`, `-0.229%`), stage
text 17,810,938 to 17,706,794 (`-0.58%`), traffic reloads 56,183 to
51,460 (`-8.4%`), spills 85,635 to 81,469 (`-4.9%`),
rematerializations 9,314 to 4,457 (`-52%`), pins 7,957 to 7,928, with
207 of 3,136 machine functions scheduled and 182 kept. Quality-mode
compile cost `8.3855G` to `9.3207G` (`+11.2%`); canonical `5.686G` and
fast-mode `7.167G` are flat to the reference within 1M. FAST measured
the same absolute win — `39.9123G` to `39.8231G` (`-0.224%`) at
`+7.0%` fast-mode cost — and ships without it: its stage is
byte-identical to the reference compiler's, proven by direct compare.
The pressure corpus is untouched in every body (FAST `91.32M`, QUALITY
`82.12M`, clang `42.74M` — local-toolchain figure, `10k`), and that is
a finding, not a miss.**)

- **What stage 9 is.** A pass between selection and placement that
  reorders rows within a block to sink definitions toward their first
  use: a backward walk over a dependence DAG of scheduling units, each
  unit emitted once everything depending on it is placed. The hard
  constraints all come from `machine.h`/`machine.c` metadata:
  terminators stay last; a FLAGS_USE row glues to the FLAGS_DEFINE row
  immediately above it into one unit (the selectors always emit the
  pair adjacent — a block where they did not keeps source order);
  CALL, SIDE_EFFECTS, and terminator rows and any row naming a
  physical register operand are barriers ordered both ways, which
  freezes call sequences whole; memory-touching rows chain in source
  order (no alias analysis); the float-state rows (XMM bridges,
  FARITH, the conversions) chain the same way; and a multi-definition
  virtual register — promoted locals, two-address chains — keeps every
  touch in source order. Side tables remap: definition points and line
  marks move with their rows, and marks re-sort.
- **Acceptance is the stage-7 rule, paid at full price.** A scheduled
  function is kept only when its placement models cheaper than the
  unscheduled placement — reloads plus spills plus a push/pop pair per
  bound callee-saved register, strict inequality — so the pass cannot
  lose by construction. The price is a second full placement per
  candidate, and that price is what shaped everything else: ungated,
  dual placement on every moved function cost **`+20.8%`
  fast-mode / `+32.1%` quality-mode compile time** for near-noise wins
  (`-0.47M` executed under the first heuristic). The audit's compile-
  cost warning was the binding constraint of the whole stage.
- **The depth-first heuristic measured negative; greedy min-growth is
  what works.** The first scheduler was backward LIFO — depth-first,
  Sethi-Ullman-shaped, ideal on expression trees (a 32-product
  combine's FAST traffic falls 11 reloads/14 spills to 0/3, modeled
  excess 20 to 0). On real code it was backwards: of 213 functions
  with over-file block pressure in the unity build, it *worsened* the
  modeled peak in 208 (base 42 becoming 122 was typical — a producer
  with several consumers gets dragged above whole unrelated subtrees).
  Selecting instead the ready unit whose placement grows the live set
  least — definitions demanded below leave the set, operands not yet
  demanded enter it, ties to the most recently updated — improves 201
  of 210 candidates and turns the stage win real. The selection runs
  in a 33-bucket queue over the growth range, entries staled by
  per-unit sequence numbers and re-pushed eagerly on each demand
  transition through per-value touch lists (capped at 16 touchers per
  value; wider values keep stale growths, which only the tie-break
  ever sees), so it stays near-linear where the naive ready-scan
  measured `+282M` of quadratic overhead.
- **Two pressure gates make the acceptance affordable.** Only the
  excess above the allocatable file can become traffic, so the pass
  computes per-block peak live-window overlap (first-to-last touch per
  value) and (1) refuses to schedule any block — or function — whose
  peak the file already covers, with a free short-circuit for blocks
  no larger than the file, and (2) discards a schedule that fails to
  lower the total excess before the caller pays for placement. The
  gates cut candidates from 2,515 moved functions to 207 pressured
  ones while the kept count *rose* (182 against the ungated LIFO's
  320 luck-wins), and the shipped quality-mode cost lands at `+11.2%`
  — the pass itself is ~`+0.2G`, the 207 second placements the rest.
- **Why the corpus does not move, body by body.** `wide_live_loop`'s
  sixteen accumulators are loop-carried: every order leaves the same
  excess and the gate rejects the schedule — correctly, since no
  reorder can shorten a loop-carried interval. `call_crossing_loop` is
  frozen by its call barriers. And `deep_tree`'s pressure turns out to
  live *across its eight calls* — the `mix()` sequences are barrier
  regions, the schedulable tail fits the file — so the body named as
  scheduling-shaped by `2026-08-09ao` has nothing a sound scheduler
  may touch; its traffic is call-crossing, stage-8's territory. The
  machine test carries the shape that does move (32 independent
  products, one combine), asserting the structural contracts and the
  strict placement win on x86-64.
- **AArch64 is structurally inert, and the test documents it.** That
  selector has no local promotion, so C locals live in frame slots:
  the same 32-value body selects to a 323-row block whose
  virtual-register peak is 6 against 25 allocatable registers, and
  every frame access rides the memory chain. Zero functions schedule
  on a64 anywhere in the matrix. Scheduling becomes meaningful there
  only after promotion reaches that target; until then the qemu
  differentials gate the pass's a64 no-op-ness.
- **Where the QUALITY win comes from.** Not the corpus shapes but the
  unity build's arena walkers, hash mixers, and wide struct
  initializers: 207 functions with genuine straight-line excess, 182
  kept, and over half the rematerialization traffic gone — a
  rematerialized constant is exactly the kind of value greedy
  scheduling sinks to its use instead of holding across a pressured
  region. FAST-with-scheduling measured the same `-89M` (traffic
  reloads 84,633 to 79,839, spills 101,994 to 97,873, remats 9,174 to
  4,120, kept 183/207) and stays off: `+7.0%` on the default `-O`
  allocator for `-0.22%` is the wrong trade while QUALITY exists as
  the opt-in tier, and wiring it nowhere keeps FAST's stage
  byte-identical — the strong form of the soak guarantee, preserved.
- **Gates, every commit:** test_all green (28,908 unit assertions, 32
  modules, including the new scheduling tests), self-host fixed point
  deterministic (`SELF_HOST deterministic bytes=31613680`), all three
  soaks — MIR_STACK, FAST, QUALITY — byte-identical against the
  freshly rebuilt stage-2 reference, `test_all_combinations_ci` green
  before the push (its qemu fixture differentials are the AArch64
  gate). The scheduled stages' canonical outputs are byte-identical to
  the reference stages' outputs on the frozen workload, and canonical
  NONE-mode compile cost is untouched — the emitter never sees the
  pass.
- **Left untaken, in causal order.** The memory chain is the binding
  freedom constraint on real code: load-load reordering is sound
  without alias analysis and would loosen the densest blocks, but it
  was left inside the conservative contract this stage was specified
  under — measure it as its own experiment before trusting it. The
  excess proxy is blind to live-through values (a window is first to
  last in-block touch), which understates pressure in blocks that pass
  values through; a lifted gate threshold or block-boundary liveness
  would catch candidates the gate currently drops. AArch64 promotion
  is the precondition for any a64 scheduling win. And the QUALITY
  acceptance still weighs a cold-path edit equal to a hot one —
  `frequency_class` remains unwritten, the `10l` conclusion standing.
- Reference points for the next audit, frozen `df11728` ide.c as the
  workload (same-session invocation, absolute paths): **FAST
  `39.9123G`** (unscheduled, byte-identical to reference), **QUALITY
  `39.1399G`**, stage text FAST 18,020,258 / QUALITY 17,706,794,
  canonical-mode compile cost `5.686G`, fast-mode `7.167G`,
  quality-mode `9.3207G`, QUALITY traffic reloads 51,460 / spills
  81,469 / remats 4,457 / pins 7,928 / scheduled 207 / kept 182.
  Pressure corpus unchanged: FAST `91.32M`, QUALITY `82.12M`, clang
  `42.74M` (local toolchain), traffic FAST 71/88, QUALITY 33/53 with
  20 pins.

`2026-08-10m` (Linux x86_64, Zen 4 7940HS; instruction selection — the
compare/branch fusion `2026-08-10j` recorded as its top selection lead,
on both machine targets. Developed concurrently with `2026-08-10k`
(shortest-form immediates, PR 254) and `2026-08-10l` (pin economics,
PR 256) against the same `bbb319d` main, so every number below shares
their baseline and none of the three includes another's effect. **Pressure corpus (200 iterations of the three
bodies at 2,000 rounds, executed instructions): FAST `98.53M` to
`91.32M` (`-7.3%`), QUALITY `87.73M` to `82.12M` (`-6.4%`), clang -O2
`47.54M`, so QUALITY's pressure gap closes from 1.85x to 1.73x.
Buster-built stage comparison on a frozen current-main ide.c: FAST
`49.017G` to `39.560G` (`-19.3%`), QUALITY `48.337G` to `38.885G`
(`-19.6%`), against same-session reference stages that reproduce
`2026-08-10j`'s numbers within 0.2% (the +0.09G systematic offset is
the absolute-path invocation, identical on both sides). Text
13,653,610 to 12,544,794 FAST (`-8.1%`), 13,428,186 to 12,328,170
QUALITY (`-8.2%`). Repeat band ~2 instructions on the corpus, ~6 K on
the stages.**)

- **What the ladder was.** A C loop head lowers its controlling
  expression as compare → widen → `(!= 0)` → BRANCH_IF, and every link
  materialized: CMP + SETCC (whose encoding carries its own MOVZX), a
  MOVZX widening cast, a 10-byte movabs zero, a second CMP, a second
  SETCC, then TEST + JNE at the branch — ten instructions per loop
  iteration where clang emits cmp+jcc. The fix is selection-only: when
  every member of that chain has exactly one use and sits in the
  branch's own block, the branch re-selects the innermost comparison
  as CMP (x86-64) or CMP (AArch64) immediately before JCC/BCC, and the
  members select into nothing. A chain that bottoms out in something
  that is not a comparison keeps a residual truthiness test — TEST on
  x86-64, the CMP_ZERO row AArch64 already branched through — so
  `if (ptr)` and `while (n)` fuse too, and the movabs zero dies with
  the compare that read it.
- **The walk is an invariant, not a pattern.** The marking pass keeps
  "branch outcome equals truthy(chain value) xor negate" through
  `(!= 0)`/`(== 0)` against a literal zero (the zero may hide behind
  one widening cast — extending zero is zero), truthiness-preserving
  zero/sign extensions, and BOOLEAN_NOT, absorbing each member;
  an integer comparison that is none of those terminates the walk as
  the fused CMP with its condition xor'd by negate (both targets'
  condition codes pair as exact complements, so negation is `cc ^ 1`),
  and anything else terminates as the truthiness test. Types stay
  integer-class scalars throughout: float compares keep their FCMP_SET
  materialization and feed the chain only as its bool. The 64-bit
  truthiness test is exact because sub-64-bit values sit extended with
  clean upper bits — the same invariant the old TEST-on-the-condition
  already leaned on.
- **Flag safety is by construction, verified against every edit form.**
  The fused compare is the row immediately before the terminator, so
  only allocator edits can land between the FLAGS_DEFINE and its
  FLAGS_USE (the machine.c attributes already stated the contract;
  nothing consumed them). Every edit form is a flag-preserving move on
  both targets: frame load/store movs, register-copy movs, and movabs
  rematerialization on x86-64; ldr/str, orr copies, and movz/movk
  rematerialization on AArch64. The disassembly shows the shape
  working: a spill mov sits between `cmp` and `jb` in the fused
  wide_live_loop head, exactly the allowed intruder.
- **Sinking the reads is the one soundness obligation.** The fused
  compare reads its operands at the branch row instead of the
  member's, and on x86-64 promoted locals are the one source of
  multi-definition vregs: a store between the compare and the branch
  would redefine what the sunk read sees. The marking walk stamps
  every promoted local's latest store ordinal and the commit rejects
  any fusion whose read (through the load-alias root) was re-stored
  past the deepest absorbed member. Cross-block stores need no check
  for the same reason load aliasing is sound — a jump re-enters at a
  block head, above the compare, never between it and the branch. The
  AArch64 selector has no promotion, so every vreg is
  single-definition and sinking is unconditionally safe.
- **The stage win dwarfs the corpus win, and the traffic says why.**
  The corpus is arithmetic-dense, so fusion trims only its loop heads
  (`-6.4%` QUALITY); a compiler is comparison-dense — parsers,
  kind-switch guards, bounds checks — and the frozen-stage comparison
  drops `-19.3%`/`-19.6%`. Allocator traffic on the corpus is near
  identical before and after (reloads 34/spills 53/pins 20 against
  35/51/20): the win is rows that no longer exist, invisible to every
  allocator statistic, which is why `2026-08-10j` could only see it in
  the disassembly.
- **Compile cost went down, not up.** The marking pass adds two linear
  IR walks per function, and quality-mode compile cost on the frozen
  workload still falls `8.628G` to `8.320G` (`-3.6%`), fast-mode
  `7.241G` to `7.112G` (`-1.8%`): the absorbed members are rows the
  allocator and encoder never see. Canonical NONE-mode cost is
  untouched at `5.637G`, as it must be — the canonical emitter never
  runs machine selection.
- **What stays materialized, correctly.** Value-context bools (a
  compare result stored, merged, or returned — main's epilogue chain
  in the fixture), multi-use zeros, and loop-variable init zeros keep
  their SETCC/movabs forms: fusion only fires where the single
  consumer is the branch. The residual `movabs $0` sites in the fused
  fixture are all inits or value-context, none are branch fuel.
- **Gates, every commit:** test_all green (28,809 unit, 31 module),
  self-host fixed point deterministic, all three soaks — MIR_STACK,
  FAST, QUALITY — byte-identical against the freshly rebuilt stage-2
  reference, `test_all_combinations_ci` green before the push (its
  qemu fixture differentials are the AArch64 gate; the fused a64
  fixture also runs clean under qemu locally, loop heads at
  `cmp w0,w1; b.lo` and truthiness at `cmp x1,#0; b.ne`). The fused
  stages' own canonical outputs are byte-identical to the reference
  stages' outputs on the frozen workload — the differential the soak
  structure exists to catch.
- **Left untaken.** The movabs-immediates lead `2026-08-10j` named is
  taken concurrently by `2026-08-10k` (PR 254), and the three pin
  leads by `2026-08-10l` (PR 256) — measure any interaction on the
  merged tree, not by adding these entries' numbers. New here:
  compare-fed conditional *values* (selects/phis once block parameters
  enter the subset) still materialize through SETCC, and the fused CMP
  cannot yet fold an immediate operand — it reads two registers even
  when one side is a constant, so `x < 16` still materializes the 16
  (the shortest-form encoders shrink that materialization; a CMP_RI
  row would remove it). Traffic priorities remain static counts with
  `MachineBlock.frequency_class` unused, the frequency-blindness
  `2026-08-10l` measures as the residual QUALITY gap.
- Reference points for the next audit, frozen current-main ide.c as
  the workload (same-session invocation, absolute paths): **FAST
  `39.560G`**, **QUALITY `38.885G`**, canonical-mode compile cost
  `5.637G`, quality-mode `8.320G`, fast-mode `7.112G`, text FAST
  12,544,794 / QUALITY 12,328,170. Pressure corpus: **FAST `91.32M`**,
  **QUALITY `82.12M`**, clang `47.54M`, allocator traffic reloads 34 /
  spills 53 / pins 20.
`2026-08-10l` (Linux x86_64, Zen 4 7940HS; stage-8 pin economics — the
three leads `2026-08-10j` left untaken, each built and measured on both
corpora. Developed concurrently with `2026-08-10k` (shortest-form
immediates, PR 254) and `2026-08-10m` (compare/branch fusion, PR 257)
against the same `bbb319d` main and rebased onto their merge
afterwards; the measurements are as taken on the pre-merge base and
include neither sibling's effect. **All three are negatives or no-ops;
no code change lands.** The useful output is the causal picture: every
static refinement of the pin economics is now measured, and the
residual QUALITY gap is execution-frequency blindness, which no static
count fixes.)

- **Harness, reproduced before anything moved.** The `2026-08-10j`
  pressure references reproduce exactly on this tree: FAST `98,528,653`,
  QUALITY `87,729,054`, clang -O2 `47,542,697`, traffic reloads 35 /
  spills 51 / pins 20, repeat band ~10 instructions. The frozen-tree
  comparison was re-frozen on today's main `bbb319d` (one merge past
  `10j`'s freeze), so its absolutes shift a hair and these are the pair
  every experiment below is read against: QUALITY stage `48.312G`
  (pins 7,912, quality-mode compile cost `8.626G`), FAST stage
  `48.992G`, stage repeat band ~25M.
- **Experiment 1, candidate threshold 3 -> 2: wins pressure, loses the
  self-host stage, both ways it was built.** The hypothesis was sound —
  the threshold predates prologue-free pins, and a call-free 2-edit
  value can sit in a caller-saved register for free. Plain `>= 2`:
  pressure `87.33M` (`-0.46%`, pins 20 -> 26), but stage `48.385G`
  (**`+73M`**, pins 7,912 -> 13,899, compile cost `+0.8%`). Gating the
  2-edit candidates on crossing no call (an O(1) call-prefix probe over
  the loop-extended interval) keeps the same pressure win with half the
  damage — stage `48.355G` (**`+43M`**, pins 11,186) — still a loss.
  Static traffic drops in every accepted function (reloads 58,246 ->
  55,541, spills 83,266 -> 80,198), executed instructions rise anyway:
  the marginal pins' saved edits sit in code that rarely runs, and what
  they displace does not. The `MACHINE_QUALITY_MAXIMUM_CANDIDATES`
  cap never saturated even at threshold 2 (probed per function over the
  whole unity build), so the cap is not the story and stays at 4096.
- **Experiment 2, loop-weighted priorities: negative, and insensitive to
  the weight.** `MachineBlock.frequency_class` exists but nothing writes
  it, so the proxy is whether an edit's instruction lies inside one of
  the merged loop regions the extension pass already computes (binary
  search per edit, regions are disjoint and sorted). Weighting in-loop
  edits by 2, 4, 8, and 16 produces byte-identical placements — pressure
  `88.53M` (**`+0.91%`**) at every weight — because the corpus's
  contended decisions flip the same way at any weight above one: the
  reorder demotes the pre-loop feeders whose early packing the current
  order gets right, and the weight magnitude never gets a vote. Applying
  the weighted count to the threshold as well (one in-loop edit earns
  candidacy) lands between: `88.13M`, still a loss. Stage flat (`-7M`,
  inside the band) in the priority-only form. Static edit order is
  already the right order on this corpus; do not retry loop weighting
  as a pure reorder.
- **Experiment 3, copies and rematerializations in the acceptance
  metric: a measured no-op, which is itself the finding.** Adding
  `copy_count + rematerialize_count` to both totals moves pins by ten
  in 7,912, the stage by `-0.8M` (band ~25M), and the pressure corpus
  not at all. The latch permutation that motivated the lead was
  invisible to the old metric, but stage 8's span pins already removed
  it; in what remains, copy and remat counts track reload+spill closely
  enough that the extra terms never flip an accept. Kept out of the
  tree to keep the metric the simple thing `2026-08-09an` specified. It
  also acquits the metric of the remat-inflation suspicion: experiment
  1's extra pins trade counted reloads for uncounted remats (9,266 ->
  9,836), yet counting them changes nothing — the static win is real.
- **The combination does not rescue experiment 1.** Call-free threshold
  2 plus the copy-aware acceptance: pressure `87.33M` (same win), stage
  `48.360G` (`+48M`, pins 11,250). The copy term vetoes almost none of
  the marginal pins because their placements are statically better in
  every counted dimension; the loss is dynamic. **The conclusion across
  all three: the acceptance model's blind spot is not what it counts
  but where — static edit counts weigh a cold-path edit equal to a hot
  one, and the profitable next lever is execution frequency (populate
  `frequency_class` from real block structure in the selector, or
  stage-9/10 splitting with frequency-aware spill placement), not
  another static term.**
- **Gates:** test_all green (28,809 unit, 31 module), self-host fixed
  point deterministic (`SELF_HOST deterministic bytes=31289552`), all
  three soaks — MIR_STACK, FAST, QUALITY — byte-identical against the
  freshly rebuilt stage-2 reference, `test_all_combinations_ci` green
  before the push. The tree ships no allocator change, so FAST and
  QUALITY behavior are untouched by construction.
- Reference points for the next audit, re-frozen on main `bbb319d` as
  the workload: FAST stage `48.992G`, **QUALITY stage `48.312G`
  (`-1.39%`)**, pins 7,912, quality-mode compile cost `8.626G` against
  FAST-mode `7.238G`. Pressure corpus unchanged: FAST `98.53M`,
  **QUALITY `87.73M`**, clang `47.54M`, pins 20.
`2026-08-10k` (Linux x86_64, Zen 4 7940HS; the second `2026-08-10j`
selection lead taken — every integer immediate spent the ten-byte
movabs, and the machine encoders now pick the shortest form that
reproduces all sixty-four result bits. Developed concurrently with
`2026-08-10m` (compare/branch fusion, PR 257) against the same
`bbb319d` main and rebased onto its merge afterwards; correctness
gates were re-run on the rebased tree, the measurements are as taken
on the pre-fusion base, and neither entry's numbers include the
other's effect — the next audit re-measures the merged tree.
**Executed instructions do not
move and were never expected to: the pressure corpus is
instruction-for-instruction identical before and after (FAST
`98.53M`, QUALITY `87.73M`), the frozen-tree stage compiles repeat
inside the band (FAST `49.018G`, QUALITY `48.337G`). Text and fetch
move instead: stage text FAST `20,028,267` to `18,977,035` (`-5.2%`),
QUALITY `19,814,923` to `18,759,691` (`-5.3%`), and stage-compile
cycles at those flat instruction counts FAST `20.058G` to `19.653G`
(`-2.0%`), QUALITY `19.736G` to `19.106G` (`-3.2%`), min of five. The
canonical stage stays byte-identical across the compiler change.**)

- **What changed.** `machine_x64_emit_immediate` — the single emitter
  behind `MACHINE_X64_MOV_RI` rows and `MACHINE_EDIT_REMATERIALIZE`
  re-emissions, so the fast allocator's remat recipe shortened with it
  for free — picks the five-or-six-byte zero-extending `mov r32,imm32`
  when the value fits unsigned thirty-two bits, the seven-byte
  sign-extended `mov r64,imm32` when it fits signed thirty-two, and the
  ten-byte movabs only past both. Every form is flag-neutral, which is
  a hard constraint and not a preference: rematerializations land
  between arbitrary rows, including a compare and its branch, so
  xor-zeroing is unavailable. On the corpus binaries the movabs count
  falls 60 to 2 (the two genuinely 64-bit seeds) and the fixture text
  drops `-8.6%` FAST / `-9.1%` QUALITY. The `emit_immediate` comment
  had claimed since birth that remat reloads already took the
  five-byte form — it was aspirational; nothing did.
- **AArch64 had the matching gap, taken in the same change.** The seed
  movz always landed on halfword zero — a wasted word whenever that
  halfword is zero — and a negative cost movz plus three movk. The
  emitter now counts zero and all-ones halfwords, seeds movz or movn
  by whichever fill covers more, lands the seed on the first halfword
  differing from the fill (the top one when none does, which makes
  zero and minus-one single instructions with no special case), and
  movk-patches only the differing rest: `-1` and `-100` are one movn
  where they were four words. Desk-checked value-exact and
  never-longer across seed and boundary cases; the qemu fixture
  differentials in the matrix are the execution gate. The canonical
  `a64_emit_constant` keeps its movz-first shape untouched.
- **The corpus understates the fetch win; the stages state it.** The
  three bodies are uop-cache resident, so corpus cycles move only
  `-1.1%` FAST / `-0.7%` QUALITY (min of seven) against the stages'
  `-2.0%`/`-3.2%` over nineteen megabytes of text. Quality-mode
  compile cost also got `10M` cheaper (`8.628G` to `8.618G`): fewer
  bytes through the emit stream outweigh the form-selection branches.
- **Methodology drift, recorded for the next audit.** The frozen
  workload here is `bbb319d` — it contains the stage-8 allocator
  source, so the baselines legitimately sit above `2026-08-10j`'s
  references (FAST `48.923G` to `49.018G`, QUALITY text `19,726,835`
  to `19,814,923` before this change); every pair above is measured on
  this one frozen tree. The corpus driver was reconstructed from the
  `10j` description (main stripped, accumulator feeding each seed, 200
  iterations of the three bodies at 2,000 rounds) and reproduces the
  recorded FAST/QUALITY numbers to within ten instructions, which
  validates both driver and band; the local clang -O2 reference
  measures `42.74M` against the recorded `47.54M` — toolchain drift,
  not workload drift, since the buster numbers land exactly.
- **Left untaken, in causal order.** The executed-instruction half of
  the increment lead still stands: every materialized int immediate
  trails a `movslq` (`mov $0x1,%eax; movslq %eax,%rcx`) because
  selection materializes then sign-extends instead of folding the
  known immediate's extension into the materialization. The
  setcc/movzx/cmp loop-head ladder from `10j` is taken concurrently by
  `2026-08-10m` (PR 257), whose fused CMP still reads two registers —
  its noted CMP_RI lead would remove the very materializations this
  change shortens, and wins over both entries where it fires. And the
  switch compare chain still spends a full `movabs` into RCX per case
  constant — it mirrors the canonical chain's shape, so shortening it
  is legal for the machine path but was left with the selection leads.
- **Gates:** test_all green (28,809 unit, 31 module), self-host fixed
  point deterministic, all three soaks — MIR_STACK, FAST, QUALITY —
  byte-identical against the freshly rebuilt stage-2 reference, the
  frozen-tree canonical stage byte-identical between the two
  compilers, `test_all_combinations_ci` green before the push.
- Reference points for the next audit, frozen `bbb319d` ide.c as the
  workload: **FAST `49.018G`, text `18,977,035`**; **QUALITY
  `48.337G`, text `18,759,691`**; quality-mode compile cost `8.618G`.
  Pressure corpus: FAST `98.53M`, QUALITY `87.73M`, clang -O2 `42.74M`
  (local toolchain; `10j` recorded `47.54M`), corpus cycles FAST
  `21.72M`, QUALITY `20.75M`. (`2026-08-10m` merged first and its
  reference points measure the fusion tree without this change; no
  entry's references describe the merged tree — re-measure.)

`2026-08-10j` (Linux x86_64, Zen 4 7940HS; register-allocator stage 8 —
live-range-scoped pins, and the first QUALITY win under real register
pressure. **Pressure corpus (200 iterations of the three bodies at 2,000
rounds, executed instructions): QUALITY `94.93M` to `87.73M` (`-7.6%`),
now below the pre-promotion allocator's `91.34M` and `-11.0%` against
FAST's unchanged `98.53M`; clang -O2 `47.54M`, so the gap closes from
2.00x to 1.85x. Buster-built stage comparison on a frozen current-main
ide.c: QUALITY `48.529G` to `48.244G` (`-0.59%`, pins 4,116 to 7,900),
FAST byte-identical to main's stage at `48.923G`. Repeat band ~120
instructions on the corpus, ~25M on the stages.**)

- **What stage 8 is here.** Not a second allocator: the stage-7 pin is
  re-scoped from "this register belongs to the value for the whole
  function" to "for the value's loop-extended live interval". A
  per-instruction register mask paints where each pin holds; the local
  scan owns the register at every instruction no span covers, its picks
  and coalescing consult the mask, block contracts refuse registers a
  span holds at their entry, edge repairs skip registers a span holds at
  the repair's write point, and a span opening mid-block evicts the
  local owner at a point the extension closure proves cannot re-execute.
  With the whole-function reservation gone, the pin file widens from two
  registers to the entire callee-saved set (five on x86-64, nine on
  AArch64) — exactly the widening `2026-08-09al` measured as a 9%
  regression when every pin starved the scan everywhere.
- **Caller-saved spans are the pressure win.** A span that crosses no
  call may take a caller-saved register, which costs no prologue save.
  Legality is per-instruction foreclosure — a call forecloses the whole
  caller-saved file, a constrained row the scratches of its populated
  register slots, a physical operand or declared clobber its named
  register, the float bridge and indirect-call staging their fixed
  register — answered per candidate in O(1) through per-register prefix
  counts. On `wide_live_loop` ten of the sixteen accumulators end up
  register-resident for the whole loop (five callee-saved, five
  caller-saved); the latch conform that used to permute the entire
  working set every iteration is eight instructions, and the loop
  touches nine stack slots instead of rotating all sixteen values.
- **The budget that makes over-pinning impossible is per-row, not
  global.** Every instruction keeps its own free-pick need above its
  pins: its virtual-register operand count plus a margin of two, and
  zero on rows whose operands are all forced into fixed registers —
  calls, constrained layouts, the staging forms. The first draft
  reserved a flat four registers everywhere and a call row that
  forecloses all nine caller-saved members then allowed one pin —
  blocking the five callee-saved pins that had been carrying
  call-crossing values since stage 7 (total pins fell 21 to 9 on the
  corpus). The margin measured: 0 gives `93.73M` (22 pins,
  over-pinned), 1 gives `88.93M` (21), **2 gives `87.73M` (20)**, 3
  gives `88.53M` (19).
- **Measured negative, recorded: caller-saved-first packing order.**
  Handing out the caller-saved file first — attractive because those
  pins are prologue-free — measured `95.73M` against `87.73M` for
  callee-first on the same tree: it eats the local scan's working pool
  while the unpaid callee-saved members sit excluded by the scan's
  own don't-pay-a-push preference. The callee-saved file packs first,
  period. (The pick's preference filter also gained the fallback that
  keeps it from emptying the candidate set when caller-saved pins
  surround it — behavior-identical for FAST, proven by the byte-identity
  soak.)
- **An accept-or-degrade ladder replaces all-or-nothing acceptance.** A
  full pack whose placement fails the stage-7 modeled-improvement test
  (or whose pins fail verification) retries with the callee-saved file
  alone before falling back to the local allocator, so over-pinning one
  hot loop cannot cost a function the pins that were winning. The
  acceptance itself is unchanged except that caller-saved pins add no
  prologue cost, which falls out of the callee-saved-mask filter.
- **Two latent holes closed on the way.** The loop extension ran once in
  block order, so overlapping goto loops could leave an interval
  under-extended — harmless while pins blocked their register
  everywhere, a liveness overlap between two same-register spans once
  they scoped. The fix computes the exact fixed point by merging the
  backward-edge spans into disjoint regions first, one ascending pass,
  after measuring the naive iterate-to-fixpoint at **+13.9%**
  quality-mode compile cost for a confirming pass that almost never
  found work; the merged form is byte-identical in output and the final
  mode cost lands at `8.611G` against main's `8.145G` (**+5.7%**, the
  foreclosure prefixes and budget being the remainder — quality stays
  opt-in). And the span-aware pin verifier now also rejects a copy
  reading a pinned register as its source, a path the location-only
  check never saw.
- **Gates, every commit:** test_all green (28,809 unit, 31 module),
  self-host fixed point deterministic, all three soaks — MIR_STACK,
  FAST, QUALITY — byte-identical against the freshly rebuilt stage-2
  reference, `test_all_combinations_ci` green before the push (its qemu
  fixture differentials are the AArch64 gate; both allocators are
  target-parameterized and the a64 pin file is X27 down to X19).
  FAST's placement behavior is untouched, proven the strong way: the
  FAST stage built from the frozen source by this tree's compiler is
  byte-identical to the one main's compiler builds.
- **Left untaken, in causal order.** The residual pressure gap to clang
  is mostly selection, not allocation now: the loop-head compare chain
  spends ~10 instructions on a setcc/movzx/cmp ladder where clang emits
  cmp+jae, and every immediate materializes through a 10-byte movabs —
  both invisible to allocator traffic. The candidate threshold of three
  edits predates prologue-free pins (a call-free value costing two
  edits is now worth pinning); traffic priorities are still static
  counts, so a pre-loop init edit weighs the same as one inside the
  loop — `MachineBlock.frequency_class` exists and is unused; and the
  acceptance metric still ignores copies, which is exactly why the
  latch permutation this stage removed never showed in it.
- Reference points for the next audit, frozen current-main ide.c as the
  workload: FAST `48.923G`, **QUALITY `48.244G` (`-1.39%` against
  FAST)**, quality-mode compile cost `8.611G`, text 19,726,835.
  Pressure corpus: FAST `98.53M`, **QUALITY `87.73M`**, clang `47.54M`,
  allocator traffic on it reloads 35 / spills 51 / pins 20.

`2026-08-10i` (Linux x86_64, Zen 4 7940HS; the promotion/edge-contract
pair `2026-08-09ap` specified, landed — and the third leg neither entry
predicted, without which the pair measures as a regression. **Buster-built
stage comparison, one frozen pre-change ide.c compiled as the fixed
workload, executed instructions: FAST `48.849G` to `48.219G` (`-1.29%`),
QUALITY `47.832G` — the first QUALITY win over FAST on the self-host
corpus (pins 765 to 4,110) — canonical-built `76.380G`, so FAST stands at
`-36.9%` and QUALITY at `-37.4%` against the canonical emitter. Text
`-2.1%`. Every number below on the same frozen workload; the measurement
band across repeat runs was `~20 K`.**)

  (This entry was developed and measured against pre-`2026-08-10h` main
  `3a4e001` and rebased onto the AArch64 merge afterwards; the letter
  moved from `h` to `i` in the rebase, the same renumbering `2026-08-10g`
  records. Correctness gates were re-run on the merged tree; the
  measurements are as taken on the pre-merge base.)

- **Stage-5 edge contracts, as specified.** A block's contract is the
  register file it may assume at entry — owner and dirtiness per physical
  register — fixed once when the block is scanned, satisfied by every
  edge into it. Backward and statically-cold edges conform inline at
  their terminator; forward edges retroactively when the successor fixes
  its contract, full parallel-copy repair on single-successor jmp edges
  and always-safe spills elsewhere, merged behind the in-order edit
  stream by a stable sort. Cold blocks — switch targets, both targets of
  a conditional whose successors both precede it — keep the old
  write-back semantics. Returns stop writing back entirely: the frame
  dies with them. Gated on `boundary_spills` as `2026-08-09ap` ordered:
  **2,996 to 1,536 (`-48.7%`)** before promotion, instructions flat
  (`+0.07%`) exactly as predicted, because a two-instruction temporary
  has nothing to retain.
- **Promotion, rebuilt from the `2026-08-09ap` recipe.** Scalar 4- or
  8-byte locals whose every use is the place of a same-width load or
  store become virtual registers; loads and stores lower to copies; the
  address helper refuses promoted locals defensively. Correct on arrival
  (all soaks byte-identical) and **`+2.8%` slower alone (`50.211G`)** —
  worse than the `+3.4%` the reverted original measured, with boundary
  spills no longer the story (22 K against the original's 26.7 K).
- **The third leg: load aliasing.** A per-function profile diff (perf at
  a fixed instruction period, symbolized per `perf script` +
  `llvm-symbolizer` since buster ELFs carry no symtab) showed the cost
  spread as a flat tax over every hot leaf — `ir_type_from_id` alone
  `+17%` — and the disassembly showed why: the copy a promoted load
  lowers to never coalesces, because its source is the local and the
  local lives on. Every read was one instruction before and two after.
  The fix: a load result whose every use sits in the load's own block
  before the local's next store — or any load of a single-store local,
  the saved-parameter shape that compilers read everywhere — shares the
  local's virtual register outright and the load selects into nothing.
  The block-local containment is what makes the layout reasoning sound
  (a jump re-enters at a block head, never between a load and its use),
  and the chain extends one level through IR_OPCODE_DEREFERENCE, whose
  selection is the same plain copy: without that second sweep the copy
  does not die, it just moves from the load to the address staging.
  Aliasing bought back `1.18G` and is what turns the pair positive;
  the deref extension alone was worth `778 M` of it.
- **Two encoder clobbers the longer lifetimes surfaced, fixed in the
  allocator's model.** `xchg` writes the swapped-out word back into the
  value register, so `ATOMIC_STORE_XCHG`'s staged value is gone after
  the row; the branchy unsigned/float conversions scribble RCX. Both
  were latent for the same reason: before promotion every staged value
  died at its row, so nothing ever read the leftover. The atomics
  differential caught the first as a real miscompile (`atomic_ops`
  returning 24 for 51 — the argument's register silently swapped with
  uninitialized stack); the conversions were found by auditing every
  constrained sequence for operand-register mutation. Any future
  macro-op sequence needs the same audit before promotion-era vregs
  flow through it.
- **Measured on the final shape and reverted — do not retry as first
  written.** A function-wide `next_call` horizon, so promoted locals
  prefer callee-saved registers, loses `64 M`: the push/pop pairs of
  the extra bindings outweigh the flush survivals, the same
  over-binding `2026-08-09v` measured before the per-block crossing
  heuristic existed. Carrying clean contract entries into blocks a back
  edge reaches loses `20 M`: the loop reloads them every iteration
  whether or not the body wants them. Both looked like fixes while the
  copy tax was still misattributed to retention; only dirty entries
  cross into loop headers, and clean entries carry everywhere else.
- **The pressure corpus tells the stage-8 story, honestly.** On the
  16-live-values fixture (driver: 200 iterations of the three bodies at
  2,000 rounds), the pair is a regression: FAST `91.34M` to `98.53M`,
  QUALITY `94.93M`, clang -O2 `47.54M`. The fixture overflows the
  register file by design, and the local scan then rotates the whole
  working set through the latch conform every iteration — the
  disassembly shows a full register permutation per loop. QUALITY
  beating FAST here and on the self-host corpus is the signature
  `2026-08-09ap` predicted: the global layer finally has long-lived
  values to hold. Recoloring and live-range splitting (stages 8/9) now
  have their corpus and their headroom — the gap to clang is 2.0x on
  pressure, and the remaining gap is assignment quality, not lowering.
- **Allocator traffic on the final pair** (ide.c, FAST): reloads 86,615
  / spills 96,507 / boundary_spills 21,829 / boundary_reloads 4,807 /
  boundary_copies 3,052 / copies 59,853. The `CODEGEN_ALLOCATOR` line
  now reports the boundary reload/copy splits. Boundary spills sit at
  22 K because promotion multiplies the dirty values that cross edges;
  the contracts absorbed what retention can absorb, and the residual is
  eviction pressure inside blocks, which is stage-8 recoloring's target.
- **Gates:** test_all green (28,643 unit, 31 module — the machine
  differential suite caught the xchg miscompile), self-host fixed point
  deterministic, all three soaks byte-identical against fresh canonical
  references at every step, `test_all_combinations_ci` green before
  push. One trap re-paid: a `-Wshadow` error left a stale Release
  binary that measured as a perfect no-op — `ninja` the Release target
  explicitly and check its exit before trusting any number.
- Reference points for the next audit, buster-built stage comparison on
  the frozen workload: canonical `76.380G`, **FAST `48.219G`
  (`-36.9%`)**, **QUALITY `47.832G` (`-37.4%`)**, text 13,465,473
  (canonical-era baseline 13,759,169). Pressure corpus: FAST `98.53M`,
  QUALITY `94.93M`, clang `47.54M`. Volatile scalar locals are
  indistinguishable from plain ones in IR and promote; the C frontend
  does not carry the qualifier to LOAD/STORE, so a volatile-correctness
  pass needs frontend work first, recorded here rather than fixed.
`2026-08-10h` (Linux x86_64, Zen 4 7940HS; register-allocator stage 11 —
the AArch64 machine backend, in four gated commits: the allocators
target-parameterized with an x86-64 byte-identity proof, the scalar
selector/encoder under MIR_STACK, the call/symbol/index census lifts, and
FAST/QUALITY enabled. **Stage 1 `5.5456 G` to `5.6077 G` instructions
(`+62.2 M`, `+1.12%`) against main `3a4e001`, all of it source growth:
preprocessed tokens `1488309` to `1507202` (`+1.27%`), instructions per
token `3726.075` to `3720.620` (`-0.15%`) — the machine tables and the new
selector compile cheaper per token than the tree average, and the machine
path executes nothing under NONE.**)

- **Stage 11A — no x86 assumption left in the common allocator code.** The
  per-opcode authority the allocators held in x64 switch functions moved
  into the static metadata (a CONSTRAINED attribute and a `clobber_mask`;
  the unused informational `implicit_mask` is gone), and a
  `MachineTargetDescription` carries the register file and the
  special-opcode identities (copy, constant, indirect call and its fixed
  register, float bridge and its staging register, QUALITY pin registers).
  The selector stamps it into every `MachineFunction`; both allocators and
  the now target-independent `machine_stack_placement_build` read only the
  description. **Proof it changed nothing:** ide.c compiled under
  mir-stack, fast, and quality by the refactored compiler is byte-identical
  to the unmodified-main compiler's output with identical
  `CODEGEN_ALLOCATOR` statistics, and the same cross-check was repeated
  against a pre-change compiler after every later stage.
- **Stage 11B — the target-specific pieces, MIR_STACK first.** ~50
  `MACHINE_A64_*` opcodes (three-address rows, no tied operands; the only
  constrained macro-ops are the remainders, whose div-then-msub needs three
  distinct registers — a64 shifts and divides take any register, so the
  x86 scratch pressure largely disappears), a scalar integer selector, and
  a word encoder whose prologue/epilogue mirror the canonical AArch64 shape
  byte for byte (stp x29/x30; mov x29; probed 4080-byte sub chunks; x28
  saved at the frame top and repointed; epilogue restores through
  `mov sp, x28`), so the module wiring's unwind actions carry the canonical
  meaning and the encoder returns per-return epilogue offsets for the
  Windows unwind data. SP travels through adds, never the orr-based moves
  — register 31 there is the zero register, which is why stack save/restore
  needed dedicated READ_SP/WRITE_SP rows (found by the census: every scoped
  block emits a save/restore pair).
- **Stage 11C — census-driven lifts.** The `cc -v` fallback census over the
  cross-compiled fixture corpus put the non-signature mass on GLOBAL (45),
  FUNCTION (23), and INDEX (7): direct calls lower to fixed-register copies
  plus a bl-relocated row, indirect calls ride X16 into blr, symbol
  addresses use the canonical inline-literal form (ldr-literal over a
  branch over an absolute eight-byte relocation; `MachineCallSite` gained
  an `absolute` flag), and INDEX composes from existing rows. **The trap
  this stage paid for:** a FUNCTION value used once as a direct callee must
  materialize zero exactly like the canonical `direct_call_uses` rule —
  emitting its symbol address instead creates an absolute relocation to a
  possibly-undefined symbol that the static ELF link refuses.
  `basic_c_multi_main` compiled alone caught it: NONE linked (canonical
  emits no such relocation) and MIR_STACK failed with
  `LINK_ERROR_RELOCATION`. Coverage moved 30 → 80 of 203 fixture functions;
  the remaining mass is signature rejections (52 — floats and aggregates
  need the AAPCS64 shape machinery, the a64 analog of the x64 stage-3 ABI
  work), ARRAY/AGGREGATE literals (17), aggregate loads (6), and the
  legitimate tail (inline assembly 10, label addresses 8, TLS globals 4,
  STACK_ALLOCATE 4).
- **Stage 11D — both allocators unchanged on the new rows.** The one
  missing piece was the callee-saved save area: the a64 frame grows eight
  bytes per bound register, reusing exactly the offsets the shared frame
  layout reserved for the x86-64 pushes, so one placement contract feeds
  both encoders; saves are scaled SP stores at the top of the frame area
  with exact-offset SAVE_REGISTER actions, restored in every epilogue. On
  the two-function probe FAST cuts reloads 63 → 1 and code bytes 968 → 492
  against MIR_STACK; QUALITY is bit-for-bit FAST here, as on x86-64 at
  this corpus size. **The C corpus never binds a callee-saved register on
  its own** — the same two-instruction-lifetime shape `2026-08-09ao`
  recorded — so a directed test pins a value to X27 through the newly
  declared `machine_fast_placement_build_pinned` and asserts the exact
  save/restore words, executing natively on AArch64 hosts.
- **Gates, every commit:** `ide test` green (28658 → 28809 unit
  assertions as the AArch64 sections landed, 31/31 module); self-host
  token and executable fixed points; byte-identity soaks in all three
  allocator modes against a canonical reference rebuilt from the current
  tree; x86-64 mode outputs byte-identical against a pre-change compiler
  on the same tree after every stage; qemu-aarch64 execution differential
  over the cross-compiled fixture corpus — 31 of 31 executable fixtures
  behave identically under NONE, MIR_STACK, FAST, and QUALITY (15
  fixtures do not cross-compile to aarch64-linux: driver/SIMD/dynamic
  cases). `test_all_combinations_ci` run before the push. qemu-aarch64 is
  the only local execution vehicle on this x86-64 host; aarch64-macos-mini
  is the CI job that exercises the native execution differentials.
- **Scope decisions, recorded for the next session:** Windows on ARM stays
  canonical (its packed-epilogue unwind shapes are not modeled in the
  machine wiring; falling back preserves its unwind requirements
  trivially); Darwin variadic calls stay canonical (anonymous arguments go
  on the stack there); frames whose x28 save offset exceeds the one-word
  scaled store (32744 bytes) bail to canonical because the module wiring's
  prologue-cursor accounting assumes one word per prologue instruction;
  ide.c cannot cross-compile to aarch64-linux from this host (no aarch64
  glibc headers), so the self-compile census and byte-identity soak for
  the AArch64 target itself only run on an aarch64 host.
- Reference points for the next audit (the AAPCS64 shape machinery is the
  next lift by mass): stage 1 `5.6077 G` instructions / `1507202` tokens /
  `3720.620` per token, fixed point deterministic, fixture-corpus machine
  coverage 80 of 203, `ide bench` untouched (parser unchanged).
`2026-08-10g` (Linux x86_64, Zen 4 7940HS; rebase onto main, and the
numbers refreshed on the merged tree.)

- **Why the entries above are renumbered.** Main independently used
  `2026-08-09h`, `i` and `j` while this branch was building `h` through
  `am`. The branch's block moved up by three letters, `h` becoming `k`
  and `am` becoming `ap`, with every cross-reference between entries
  shifted to match; main's three entries are unchanged. Nothing else in
  those entries was edited — the measurements are as they were taken.
- **One semantic conflict git could not see.** Main added `simd_tests`
  and this branch added `machine_tests`, each bumping the module-count
  assertion from 29 to 30 in different commits. The two merged cleanly
  into an array of 31 with an assertion demanding 30. The count is 31
  now, and the check earned its keep.
- **Refreshed numbers on the merged tree** (compiling ide.c, whose
  sources main grew, so absolute counts are not comparable with the
  entries above — the ratio is): canonical **73.44G** instructions,
  **fast 47.07G (-35.9%)**, quality **47.03G**. Coverage 2994 of 3025
  functions (**99.0%**). Both track the pre-rebase -35.5% and 99.0%.
- **Gates on the merged tree:** test_all green (20454 unit, 31 module),
  all three soaks — MIR_STACK, FAST and QUALITY — byte-identical against
  a fresh canonical reference, self-host fixed point holds.

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

`2026-08-09ap` (Linux x86_64, Zen 4 7940HS; local promotion built,
measured, and reverted — with the sequencing it proves.)

- **What was tried.** The pass `2026-08-09ao` called for: at selection,
  every C local of register width whose address never leaves a load or a
  store becomes a virtual register instead of a frame slot, so loads and
  stores of it become copies. Roughly 90 lines in the selector: a
  two-pass promotability scan (eligible by type, then disqualified by any
  use that is not the place of a same-width scalar load or store), vreg
  creation in classification, copy lowering in the LOAD and STORE cases,
  and a refusal in the address helper so a promoted local can never
  acquire an address behind the analysis's back.
- **It was correct.** test_all green and the FAST soak byte-identical —
  the promoted compiler still reproduces canonical stage-2 bytes exactly.
- **It was slower, on both corpora.** Self-host: **46.72G** instructions
  against 45.17G (+3.4%), text +0.9%. Pressure corpus: FAST **212.3M**
  against 186.9M, QUALITY **205.8M**.
- **Why, in one number.** Boundary spills went from 2,891 to **26,672**,
  a factor of nine; reloads tripled, 32,205 to 95,957. Promotion does
  exactly what it promises — it turns short two-instruction lifetimes
  into long-lived values — and the local scan then writes every one of
  them back at every block boundary, which for a loop body is the store
  and load per iteration that promotion was supposed to remove, plus the
  copies.
- **The sequencing this proves.** On the pressure corpus QUALITY beat
  FAST for the first time with promotion in place (205.8M vs 212.3M):
  the global layer only has something to hold once values are long-lived.
  So the two changes are a pair. Promotion alone is a regression;
  promotion plus cross-block register assignment is the design clang is
  beating us with. **Land them together, or not at all**, and the
  gating measurement is boundary spills, not instruction count.
- **What is needed for the pair.** Full stage-5 edge contracts: agreeing
  a register assignment across each edge and resolving the difference
  with parallel copies, so a promoted local stays in its register through
  a loop instead of round-tripping at every boundary. Stage 8 recoloring
  refines that; it does not substitute for it.
- **State of the tree.** Reverted — the branch keeps the 45.17G / -35.5%
  configuration. The patch is described here in enough detail to rebuild
  in an afternoon, and should be rebuilt only alongside edge contracts.

`2026-08-09ao` (Linux x86_64, Zen 4 7940HS; the pressure corpus, and the
finding that reframes the rest of the register-allocator plan.)

**Read this before starting stage 8, 9, or any further allocator work.**

- **What was built.** `tests/basic_c_register_pressure.c`: sixteen values
  live across a loop (more than the allocatable file), eight values live
  across a call in every iteration, and a deep expression tree whose
  intermediates all survive to one combine. It self-checks, so it is an
  execution test under every mode as well as a benchmark. Built because
  `2026-08-09al` and `ak` could not evaluate QUALITY honestly on the
  self-host sources, which keep almost nothing live.
- **Numbers** (200 iterations of the three bodies, executed
  instructions): canonical **395.95M**, MIR_STACK **595.95M**, FAST
  **186.92M (-52.8% against canonical)**, QUALITY **186.92M**, and
  `clang -O2` **95.37M**. So FAST more than halves the canonical
  emitter's work under real pressure — a far larger margin than the
  -35% seen on the self-host sources — and lands within 1.96x of clang.
- **QUALITY is bit-for-bit FAST here, on the corpus written to favour
  it.** Its allocator traffic explains why: over the whole corpus the
  local scan emits **1 reload, 24 spills, 0 pins**. There is nothing to
  allocate globally.
- **Root cause, and it is not in the allocator.** The C frontend keeps
  every local in memory. `a0 += a1 ^ a15` lowers to load, load, compute,
  store, so the only values the allocator ever sees are the temporaries
  between one load and the next store. The generated loop body is a
  procession of `mov -0x10(%rbp),%r8` / `mov %r8,-0x28(%rbp)` pairs.
  Sixteen simultaneously live C variables are, to the allocator, sixteen
  independent two-instruction lifetimes.
- **What this means for the plan.** Stage 8 (recoloring, regional graph
  colouring) and stage 9 (pressure-aware scheduling) both optimize the
  assignment of long-lived values to registers. Neither can pay while
  there are no long-lived values. The missing pass is promotion of
  address-not-taken locals to virtual registers — mem2reg — and it is
  worth more than every remaining allocator stage combined: it is the
  bulk of the 1.96x gap to clang, and it is the precondition that makes
  QRA's machinery meaningful. **Do that first.**
- **Corollary for the wins already banked.** The gains from
  `2026-08-09ag` (copy coalescing) and `ag`/`ah` (folded addressing) were
  large precisely because they attack the short-lifetime, copy-heavy
  shape this lowering produces. That was the right thing to optimize for
  the IR as it stands, and it stays right after promotion lands.
- **Gates:** test_all green, the corpus executes identically under NONE,
  MIR_STACK, FAST and QUALITY.

`2026-08-09an` (Linux x86_64, Zen 4 7940HS; register-allocator stage 7 —
QUALITY stops guessing and starts measuring, and finally wins.)

- **What changed.** Every heuristic from `2026-08-09al` is gone —
  crossing a call, loop residency, occurrence weights, the register-file
  cap. QUALITY now runs the local scan once with no pins, counts the
  memory edits each value actually cost, and pins by that measured
  traffic: a value must have cost at least three edits, because the pin
  it earns adds a push and a pop. The priority heap is keyed on real
  traffic instead of a guess.
- **Acceptance on modeled improvement.** The pinned placement is
  compared against the baseline it was derived from and kept only if
  `traffic_after + 2 * newly_reserved_registers < traffic_before`.
  A pin also raises local pressure, which can add traffic elsewhere, and
  this test sees that because it measures the whole placement rather
  than the intended saving. QUALITY therefore cannot be worse than FAST
  by construction, which is what stage 8's "accepted only on modeled
  improvement" rule asks for, applied a stage early.
- **Numbers** (compiling ide.c): QUALITY **45.137G** instructions
  against FAST's **45.169G**, text 19,445,006 against 19,447,822.
  Reloads 30,303 vs 32,205 (-5.9%), spills 76,403 vs 77,012. Pins fall
  from 12,116 under the heuristics to **640** — two orders of magnitude
  more selective, and the first configuration to come out ahead.
- **Honest scale.** The win is 0.07%. The self-host corpus is not
  register-pressure-bound, so there is little for a global allocator to
  recover; the result that matters is that the mechanism is sound,
  self-limiting, and measured rather than assumed. A pressure corpus is
  needed before stage 7's acceptance criterion can be called met in
  spirit as well as in letter.
- **Gates:** test_all green, MIR, FAST and QUALITY soaks all
  byte-identical on fresh references, self-host fixed point holds.

`2026-08-09am` (Linux x86_64, Zen 4 7940HS; register-allocator stage 13
— optimization intent selects the allocator.)

- **What was built.** The driver understands `-O` levels now, and they
  map to the only budget this compiler currently spends: `-O0` (and no
  flag) keeps the canonical stack emitter, while `-O1`, `-O2`, `-O3`,
  `-Os`, `-Oz`, and `-Ofast` take FAST. An unknown level is an error
  rather than a silent no-op. QUALITY is deliberately left out of the
  mapping: it does not beat FAST on a measured corpus
  (`2026-08-09al`), so it stays reachable only by naming it with
  `-fregister-allocator=quality`.
- **Reproducible flags.** `-fregister-allocator=none|mir-stack|fast|
  quality` selects a mode directly and overrides nothing else;
  `mir-stack` is the internal verification mode that routes the machine
  path with every value in a slot. `cc -v` reports the mode, the
  per-opcode fallback census, and allocator traffic, which is what every
  entry above was measured with.
- **Default unchanged.** With no optimization flag the compiler still
  uses the canonical path, so nothing about existing invocations moves.
- **Gates:** test_all green, the `-O2` soak byte-identical against a
  fresh canonical reference (the mapping really does route through
  FAST), self-host fixed point holds.

`2026-08-09al` (Linux x86_64, Zen 4 7940HS; register-allocator stage 7 —
QUALITY exists, is verified, and does not beat FAST here.)

- **What was built.** `machine_quality_placement_build`: a live interval
  and a weight per virtual register, loop extension across every backward
  edge, a max-heap priority walk, and greedy assignment of the
  highest-weight non-overlapping intervals to callee-saved registers for
  the whole function. The local scan then places everything else around
  the pins and never picks, binds, spills, or reloads a pinned value.
  Callee-saved is the class that makes a whole-function binding sound
  without a clobber analysis: calls preserve it and every encoder scratch
  and macro-op sequence in this backend works out of the caller-saved
  half. Functions with a case table opt out, because switch targets live
  outside the block-ref operands the loop extension walks.
- **A pin verifier is part of the mode, not a debug aid.** After
  placement, every operand naming a pinned register must belong to that
  pin's value, and no edit may land on one; a violation falls back to the
  local allocator. It earned itself immediately: copy coalescing steals a
  dying source's register, and when that source was pinned it handed a
  globally-reserved register to an unrelated value. The symptom was
  `lea r15,[rbp-0x310]` / `mov r15,rax` / `mov [r15],r15` — a store
  through a pointer that had just been overwritten by the value being
  stored. Coalescing now refuses pinned registers.
- **The negative result, measured four ways.** Compiling ide.c, FAST is
  **45.16G** instructions. QUALITY: five callee-saved registers ungated
  **49.28G**; gated on crossing a call **45.74G**; also requiring loop
  residency **45.64G**; capped at two registers **45.35G**. Every
  loosening costs more, and the limit of the series is FAST itself.
- **Why.** A pin costs a push and a pop on every entry, and it removes
  the register from the local pool for the whole function — allocator
  traffic confirms the pressure, with spills and copies rising as pin
  count rises. FRA already binds callee-saved registers for call-crossing
  values where it locally pays, so it collects most of the benefit
  without the function-wide reservation. The self-host corpus is not
  register-pressure-bound, which is exactly the corpus the plan's
  stage-7 acceptance criterion ("quality wins over FRA on pressure
  corpus") asks about.
- **Status against the plan.** Stage 7 lands its scaffolding — exact-ish
  intervals, priority queue, hints, budget bound
  (`MACHINE_QUALITY_MAXIMUM_CANDIDATES`), rematerialization (already in
  FAST from `2026-08-09ai`) — but **not** its acceptance criterion.
  QUALITY must stay opt-in and must not become any optimization level's
  default until it wins on a pressure corpus. Eviction cascades, live
  range splitting, and stage 8 recoloring are the remaining levers that
  could change the verdict; splitting is the one that directly attacks
  the cost measured here, since it would let a pin cover only the loop
  that needs it instead of the whole function.
- **Gates:** test_all green (a corpus assertion holds QUALITY's traffic
  at or below the everything-in-slots baseline), all three soaks —
  MIR_STACK, FAST, QUALITY — byte-identical on fresh references,
  self-host fixed point holds.

`2026-08-09ak` (Linux x86_64, Zen 4 7940HS; selection quality — member
addresses become one instruction.)

- **What was built.** The address-placement helper takes a constant byte
  offset now: a direct local folds it into the frame displacement of its
  `lea` (the same payload convention the sized frame stores already
  use), and a pointer folds it into `lea dst, [base + disp]` through a
  new LEA_OFFSET opcode. Only a zero offset on a pointer stays a plain
  copy, which the allocator can then coalesce away. Field selection is
  one row end to end, down from three before `2026-08-09aj`.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): instructions **45.45G -> 45.09G (-0.8%)**, against canonical's
  70.05G now **-35.6%**. Text 19,419,666 (canonical 25,699,964,
  **-24.4%**).
- **Gates:** test_all green, MIR and FAST soaks byte-identical on fresh
  references, self-host fixed point holds.

`2026-08-09aj` (Linux x86_64, Zen 4 7940HS; selection quality — folded
address arithmetic.)

- **What was built.** Two opcodes that take their constant inline:
  `add r64, imm` and `imul r64, r64, imm` (imm8 form when it fits).
  Field offsets and index element scales used to materialize the
  constant into a scratch virtual register first, so every struct member
  access cost `mov reg, imm` + `add`, and every array subscript cost
  `mov reg, imm` + `imul`. Both are now single rows. That halves the
  instruction count of the commonest address form in the language and,
  just as valuable, removes two synthesized virtual registers per
  access from the allocator's pressure.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): instructions **47.07G -> 45.45G (-3.4%)**, against canonical's
  70.05G now **-35.1%**. Text 19,487,253, from 20,038,638 (canonical
  25,699,964, **-24.2%**). Allocator traffic is flat, as expected — the
  win is fewer rows, not less spilling.
- **Gates:** test_all green, MIR and FAST soaks byte-identical on fresh
  references, self-host fixed point holds. Both allocators benefit,
  since this is selection, not placement.

`2026-08-09ai` (Linux x86_64, Zen 4 7940HS; register-allocator stage 7
lead — constant rematerialization, taken early because it is local.)

- **What was built.** A pre-pass marks every virtual register whose
  entire definition is one constant materialization; a second definition
  of any kind disables the recipe, since which constant is current would
  then depend on the path. Such a value never pays for a store and never
  occupies a frame slot: eviction drops it silently and a later reload
  becomes `MACHINE_EDIT_REMATERIALIZE`, which re-emits the immediate.
- **Two latent bugs fixed on the way.** The slot-needed derivation keyed
  on every edit's `subject`, but a copy edit's subject is a *physical
  register* and a rematerialization's is an *immediate index*. With few
  virtual registers in a function that write ran past the end of the
  array — a live out-of-bounds write introduced with copy edits in
  `2026-08-09v` and never triggered because the arrays were usually long
  enough. The derivation now filters on edit kind. Separately, the first
  draft of the remat table let a constant definition *after* a
  non-constant one enable the recipe; a definition-seen flag fixes it.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): spills **90,157 -> 77,394 (-14.2%)**, reloads **41,253 ->
  32,693 (-20.7%)**, with 8,570 rematerializations taking their place —
  21,323 memory operations removed for 8,570 immediate loads. Frame
  bytes 3,663,056 -> 3,602,392. Instructions 47.07G (canonical 70.05G,
  **-32.8%**), text 20,038,638.
- **Honest scale note:** the instruction win is small (-0.3%) because a
  rematerialization replaces a reload one-for-one and only the dropped
  *stores* are pure profit. The memory-traffic and frame-size wins are
  the real result, and the out-of-bounds fix is worth the patch on its
  own.
- **Gates:** test_all green, MIR and FAST soaks byte-identical on fresh
  references, self-host fixed point holds.

`2026-08-09ah` (Linux x86_64, Zen 4 7940HS; register-allocator stage 5 —
allocator traffic metrics, and two negative results they explain.)

- **What was built.** `cc -v` now reports `CODEGEN_ALLOCATOR reloads=
  spills= boundary_spills= copies=` summed over the machine-emitted
  functions, splitting boundary write-backs from eviction pressure
  because the two want different fixes. Also a small correctness
  refinement: once an instruction's uses and defines are placed, a
  value whose last use *is* that instruction has been read for the last
  time, so the call flush and the block write-back drop its store
  instead of keeping the strict "past the last use" test.
- **The measurement that matters.** FAST against MIR_STACK on the same
  compile: reloads **41,253 vs 988,799 (-95.8%)**, spills **90,157 vs
  968,541 (-90.7%)**, plus 58,268 register copies FAST introduces and
  MIR_STACK cannot. Only **2,805 of 90,157 spills (3.1%)** come from
  block boundaries.
- **Negative result 1 — deferring the boundary write-back.** When a
  block's sole successor inherits its register file, the write-back can
  defer down the chain. Implemented, byte-identical, and worthless:
  spills moved 90,154 -> 90,157 and text grew 26KB from shifted
  allocation decisions. The 3.1% boundary share is the reason, and the
  earlier flat result for straight-line inheritance (`2026-08-09w`) has
  the same root: this frontend's block graph has almost no
  single-predecessor/single-successor chains. Reverted. **Do not
  retry any boundary-write-back optimization without first moving that
  3.1%.**
- **Negative result 2 — a loop-reentry floor for escaping values.**
  Escaping values can never be declared dead, which looked like the
  reason spills (90K) run 2.2x reloads (41K). Added a floor — the
  lowest instruction any backward edge can land on — so an escaping
  value whose last use sits below it retires for good. Zero effect
  (90,157 -> 90,161). Reverted. The ratio needed no fix: read-modify-
  write rows re-dirty a value after each reload, so
  `spills ~= reloads + values-ever-spilled` is the expected shape, and
  49K distinct values spilling once each accounts for it exactly.
- **Numbers unchanged** at 47.20G instructions (the +34KB of text
  against `2026-08-09ag` is the statistics code itself entering the
  self-compiled binary, not worse codegen).
- **Gates:** test_all green, MIR and FAST soaks byte-identical on
  fresh references, self-host fixed point holds.

`2026-08-09ag` (Linux x86_64, Zen 4 7940HS; register-allocator stage 5 —
copy coalescing: the largest single quality win of the project.)

- **What was built.** Two halves. The encoder skips a full-width
  MOV_RR whose operands landed on the same register (the narrower
  moves are *not* identities — they clear the upper bits, so only the
  64-bit form qualifies). The allocator then makes that case common:
  when a copy's source virtual register has no use after this row —
  non-escaping and its last use is this instruction, already consumed
  by the use pass — the destination binds to the source's register and
  ownership transfers directly, without routing through an eviction
  that would write a dying value back to a slot nobody reads.
- **Why it pays so much here.** The selector mirrors the canonical
  path, which materializes a copy for nearly every value-producing
  construct (casts, unary setup, address staging). Under MIR_STACK
  those copies are load/store pairs; under FRA they were
  register-to-register movs; now most of them are nothing at all.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fast-built compiler **47.21G instructions, from 53.28G
  (-11.4% in one step)** — against canonical's 70.05G that is
  **-32.6%**. Text 20,057,883, from 20,787,008 (canonical 25,699,964,
  **-22.0%**).
- **Gates:** test_all green with a new regression guard (the fast
  encoding of the loop corpus function must stay strictly smaller than
  the stack encoding, so a future change cannot silently disable
  coalescing), MIR and FAST soaks byte-identical on fresh references,
  self-host fixed point holds.

`2026-08-09af` (Linux x86_64, Zen 4 7940HS; register-allocator stage 6 —
spill-slot reuse by defining block.)

- **What was built.** A non-escaping value's every edit sits inside the
  block that defines it, so two such values from different blocks never
  hold their slots at the same time: they draw from one shared pool
  sized to the busiest single block, indexed by a per-block cursor.
  Escaping values keep dedicated slots — proving their ranges disjoint
  needs the cross-block liveness the global stage brings. No sort is
  involved, so the pass stays a single linear walk.
- **Numbers.** Summed prologue allocations across the fast-built
  compiler's 4.9k framed functions: **4,184,496 -> 3,663,056 bytes
  (-12.5%)**, mean frame 847 -> 756, and 93 fewer subtract chunks
  (frames dropping under a page boundary). Instructions are unchanged
  at 53.28G and text moved +3.6KB — expected and worth stating plainly:
  the edit stream is identical, only its displacements change, so this
  buys stack footprint, not instruction count. The displacement mix
  shifts slightly toward disp32 for the dedicated tail, which is where
  the text bytes went.
- **Gates:** test_all green, FAST soak byte-identical on fresh
  references, self-host fixed point holds.

`2026-08-09ae` (Linux x86_64, Zen 4 7940HS; register-allocator stage 6 —
machine-path debug line entries reach canonical parity.)

- **What was built.** Selection records one MachineLineMark per lowered
  IR instruction (its first machine row plus the canonical source
  position), the encoder returns per-row code offsets ahead of each
  row's reload edits, and the module emitter turns marks into ordinary
  CodegenLineEntry rows through codegen_record_line — same dedupe, same
  capacity budget (one mark per IR instruction at most).
- **Parity note:** on C-frontend inputs both paths currently produce
  the same function-level rows, because the frontend's on-demand
  location recovery keeps most IR instructions without canonical
  sources — the machine path now consumes exactly what canonical
  consumes, so any future frontend location enrichment benefits both
  sides equally. Verified with objdump --dwarf=decodedline on a
  multi-statement probe: identical tables under NONE and FAST.
- **Gates:** test_all green, MIR and FAST soaks byte-identical on
  fresh references, self-host fixed point holds.

`2026-08-09ad` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
array loads, bit scans, and unsigned-64 float conversions: ninety-nine
percent.)

- **What was built.** Three lifts from the reject-detail census.
  (1) Array-typed loads join the slot pre-pass, so struct-member array
  copies stop rejecting (15 functions). (2) count-trailing/leading-
  zeros select as bsf, and bsr xor width-1, mirroring canonical —
  undefined on zero like the builtins (4 functions). (3) The branchy
  unsigned-64 float conversions land as constrained macro-ops in the
  encoder (precedent: the atomic retry loop): u64-to-float halves with
  a sticky bit and doubles when the sign bit is set; float-to-u64
  subtracts the 2^63 threshold and sets the top bit past it — the
  canonical byte sequences with RAX/RCX/XMM0/XMM1 scratches (10
  functions, mostly ui).
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 52 -> 28, coverage **2914 of 2942 (99.0%)**.
  Fast-built compiler 53.27G instructions (canonical 70.05G,
  **-24.0%**), text 20,783,355 (canonical 25,699,964, -19.1%).
- **Gates:** test_all green (a ucvt differential crosses the 2^63
  threshold in both directions), MIR and FAST soaks byte-identical on
  fresh references, self-host fixed point holds.
- **The remaining 28 are the legitimate tail:** thread-local globals
  (fs-segment addressing), label addresses/computed goto, the va_start
  machinery, clear-instruction-cache, and a few residual call shapes.
  Coverage work stops here; stage 5/6 quality and latency (spill-slot
  reuse, debug locations, edge contracts) and the QRA stages are next.

`2026-08-09ac` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
bit-field aggregate literals: coverage reaches ninety-eight percent.)

- **What was built.** Bit-field members of an aggregate literal
  accumulate per storage unit, mirroring the canonical shape: a zeroed
  register takes each member masked to its width and shifted to its bit
  position (a power-of-two multiply — the machine set's shifts are
  CL-constrained and a scaled multiply is equivalent), the unit stores
  once at its byte offset, and initializers materialize every member so
  the accumulated word is the whole unit. Mixed structs interleave unit
  accumulation with the plain member writes.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 83 -> 52, coverage **2890 of 2942 (98.2%)**.
  Fast-built compiler 53.42G instructions (canonical 70.05G,
  **-23.7%**), text 20,937,787 (canonical 25,699,964, -18.5%).
- **Gates:** test_all green (a bit-field literal differential joined
  the corpus), MIR and FAST soaks byte-identical on fresh references,
  self-host fixed point holds.
- **Remaining census:** LOAD 14, no-opcode 11, CAST 10, GLOBAL 4,
  UNARY 4, va 3, CALL 1, icache 1, misc — diminishing per-item mass;
  the next session should weigh finishing the tail against returning
  to stage 5/6 quality work (spill-slot reuse, debug locations, edge
  contracts) and the QRA stages.

`2026-08-09ab` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
variadic aggregate arguments: coverage reaches ninety-seven percent.)

- **How the target was chosen.** Reject-site instrumentation (local,
  removed) put 67 of the 68 CALL-bucket fallbacks on one condition: the
  call lowering refused aggregate arguments past the callee's named
  parameter count. That is every `{S8}`-style format call — String8
  passed through `...` — which this codebase does everywhere.
- **What was built.** The exclusion is gone. Variadic aggregates
  already worked structurally: the shape classifies from the value's
  own type, register parts place by the same all-or-nothing rule, and
  aggregate float parts already counted toward the variadic AL setup.
  A corpus differential was attempted and dropped — the module harness
  cannot host variadic definitions (the canonical module path errors
  and the zero-fallback assertion forbids the fallback callee); the
  soak exercises this path through every formatted diagnostic instead.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 146 -> 83, coverage **2859 of 2942 (97.2%)**.
  Fast-built compiler 54.78G instructions (canonical 70.05G, -21.8%),
  text 21,164,686 (canonical 25,699,964, -17.6%).
- **Gates:** test_all green, MIR and FAST soaks byte-identical on
  fresh references, self-host fixed point holds.

`2026-08-09aa` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
guard-page probes lift the frame bail; coverage reaches ninety-five
percent.)

- **How the target was chosen.** The fallback census is a permanent
  verbose statistic now (`CODEGEN_FALLBACK opcode= count=` lines plus a
  `CODEGEN_FALLBACK_STAGES verify= placement= encode=` roll-up under
  `cc -v`), replacing the throwaway env knob rebuilt three times. It
  immediately showed the biggest bucket was not an opcode at all:
  placement=151, every one the stage-2 guard-page frame bail (frames
  >= 4080 fell back to canonical).
- **What was built.** The machine prologue's stack allocation mirrors
  the canonical chunked form byte-for-byte in structure: at most a page
  per subtract (imm8 or imm32 by chunk), a `testb $0,(%rsp)` probe
  touch after each, one ALLOCATE_STACK unwind action per chunk at its
  exact subtract-end offset. Both placement builders drop their 4080
  bail. Small frames switch from the fixed imm32 subtract to imm8 with
  a probe (prolog sizes 4/12/15; the corpus assertion updated).
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 297 -> 146, coverage **2796 of 2942 (95.0%)**.
  Fast-built compiler 55.74G instructions (canonical 70.05G,
  **-20.4%**), text 21,441,846 (canonical 25,699,964, -16.6%) — the
  151 recovered functions carry the biggest frames and the most spill
  traffic, so this single lift bought 2.7G instructions.
- **Gates:** test_all green, MIR and FAST soaks byte-identical on
  fresh references, self-host fixed point holds.
- **Remaining census:** CALL 68, AGGREGATE 31 (bit-field members),
  LOAD 14, no-opcode 11, CAST 10, GLOBAL 4, UNARY 4, va 3, icache 1.

`2026-08-09z` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
array literals: coverage reaches ninety percent.)

- **What was built.** IR_OPCODE_ARRAY selection as a sibling of the
  aggregate case, sharing a member-write helper: elements land in the
  value's slot at position times element size — scalar elements store
  sized, slot-backed elements copy through the element's address. The
  frontend materializes every element including the zero tail, so no
  descriptor components are needed for the C subset (LENGTH/SLICE
  consumers stay outside it). ARRAY-typed results join the slot
  pre-pass.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 575 -> 297, coverage **2620 of 2917 (89.8%)**.
  Fast-built compiler 58.40G instructions (canonical 70.05G, -16.6%),
  text 23,198,204 (canonical 25,699,964, -9.7%).
- **Gates:** test_all green (arr_lit standalone differential added),
  MIR and FAST soaks byte-identical on fresh references, self-host
  fixed point holds.
- **Remaining fallback mass** (from the `2026-08-09y` census, post-
  aggregate): CALL 55, CAST 10, UNARY 4, GLOBAL 3, LOAD 2, plus the
  bitfield-aggregate rejects and the untyped remainder — re-census
  before choosing the next lift.

`2026-08-09y` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
aggregate literals and debug traps lift coverage past eighty percent.)

- **How the target was chosen.** A local rejection dump over the unity
  compile (temporary `BUSTER_MACHINE_REJECTS` knob, removed) showed the
  fallback mass was never the exotic constructs: IR_OPCODE_AGGREGATE
  (struct literals) caused 1142 of 1321 fallbacks, and behind it
  DEBUG_TRAP 208 and ARRAY 391 (ARRAY needs the four-component
  collection value model and stays open).
- **What was built.** AGGREGATE selection: field-by-field construction
  into the value's slot — scalar members store sized at their offsets,
  aggregate members copy from their own slots through the field's
  address, bit-field members reject to canonical (masked insertion is
  not worth mirroring yet). DEBUG_TRAP selects int3; UNREACHABLE now
  emits ud2 instead of a full return epilogue (smaller, faults loudly,
  canonical bytes).
- **The bug that cost a day of soak red: narrowing casts kept the
  discarded bits.** INTEGER_TRUNCATE and POINTER_TO_INTEGER selected as
  plain 64-bit copies, violating the register model's zero-extension
  invariant. The failure needed a source with meaningful high bits
  feeding a 64-bit consumer: `u32 id = (u32)length_and_id` followed by
  `table->names[id]` scaled the full `length<<32|id` word — the symbol
  table's rehash scattered writes 400GB past the heap and the crash
  surfaced two calls later in a different field. Found by extracting
  `c_symbol_intern` verbatim into a standalone probe after three
  hand-built probe shapes all passed: the trigger was the long-name
  lookup path my simplifications had dropped. Narrowing casts now emit
  mov r32 / movzx by destination width. Lesson recorded: when a
  bisected function's obvious new construct probes clean, extract the
  function verbatim before reading more disassembly — the defect was in
  a years-old-looking line the simplified probes silently fixed.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 1321 -> 575, coverage **2342 of 2917 (80.3%)**.
  Fast-built compiler 59.41G instructions (from 63.58G; canonical
  70.05G — **15.2% below the canonical stack emitter**), text
  23,662,996 (canonical 25,699,964, -7.9%).
- **Gates:** test_all green (20098 unit / 30 module, kagg aggregate
  differential routed through the module section where its call
  relocation resolves), MIR and FAST soaks byte-identical on fresh
  references, self-host fixed point holds.

`2026-08-09x` (Linux x86_64, Zen 4 7940HS; register-allocator stage 6 —
R12/R13 complete the callee-saved file; ModRM base quirks fixed.)

- **What was built.** `machine_x64_emit_memory_modrm` now handles the
  RSP/R12 SIB and RBP/R13 no-disp0 encodings for any base at any
  displacement, the pointer loads/stores route through it instead of
  raw ModRM bytes, and R12/R13 join the allocatable and callee-saved
  masks with prologue pushes, epilogue pops, and unwind actions
  (machine-path unwind capacity 8). The helper's old comment claimed
  quirk bases could never appear — with allocated copy-loop bases that
  was one allocator change away from being false, so the fix removes a
  loaded trap even where the measure is flat.
- **Honest measure:** flat — 63.60G instructions, +3.6KB text against
  `2026-08-09w`, run-to-run noise territory. Few blocks keep more than
  three call-crossing values live at once, so the fourth and fifth
  callee-saved members rarely bind, and [r12]/[r13] addressing costs an
  extra byte per touch.
- **Gates:** test_all green, FAST soak byte-identical on fresh
  references, self-host fixed point holds.
- **Next lever by mass:** the 1321 fallback functions. The selector
  gives no rejection breakdown; instrument locally before choosing the
  next stage-3 construct to lift.

`2026-08-09w` (Linux x86_64, Zen 4 7940HS; register-allocator stage 5 —
straight-line edge contracts land structurally, and measure flat.)

- **What was built.** Block boundaries write back instead of flushing:
  dirty live values reach their slots at the terminator, but the
  register mappings survive, and a block whose only predecessor is its
  layout neighbor inherits the file instead of starting cold (switch
  targets over-count predecessors conservatively; the entry block always
  starts cold).
- **Honest measure:** flat — 63.59G instructions and +5KB text against
  `2026-08-09v`, both noise-level. The canonical C frontend does not
  split blocks needlessly, so single-predecessor layout-neighbor edges
  are rare: conditional joins and loop headers all have two
  predecessors and start cold. The value is structural: the write-back
  contract and predecessor census are the hooks a real trace/edge-
  contract pass needs, and the change survives the full gate battery.
- **Gates:** test_all green, FAST soak byte-identical on fresh
  references.
- **Where the next instruction win actually is:** loop headers and
  join blocks need either edge parallel copies (full stage-5 contracts)
  or the QRA path; within FRA, the remaining cheap lever is R12/R13
  (ModRM base quirks in the pointer load/store encoders) widening the
  callee-saved file.

`2026-08-09v` (Linux x86_64, Zen 4 7940HS; register-allocator stage 6 —
callee-saved registers, and register-copy edits instead of memory moves.)

- **What was built.** Three coupled pieces. (1) RBX/R14/R15 join the
  allocatable file (R12/R13 wait on their ModRM base quirks): the
  encoder pushes the placement's callee-saved mask after the frame
  pointer in fixed order and restores through `lea rsp,[rbp-8N]` plus
  reversed pops; unwind actions cover each push (X64Register and the
  DWARF CFI map gained R12-R15; the machine-path unwind capacity rose to
  six). Call flushes keep the callee-saved members. (2) A per-block
  next-call pre-pass: only values that stay live past the next call bind
  callee-saved registers — the first, indiscriminate version *lost* 1.8%
  instructions and 38KB of text to push/pop pairs on values that never
  crossed a call. (3) `MACHINE_EDIT_COPY`: when a value must move to a
  fixed register (argument staging, encoder scratches) and already lives
  in a register, the placement emits one register copy carrying its
  dirtiness instead of a park store plus reload through its slot.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fast-built compiler 63.58G instructions (canonical 70.05G,
  **-9.2%**; the previous entry stood at 63.62G), text 24,739,532
  (canonical 25,699,964, -3.7%). Callee-saved alone was net negative
  even with the crossing heuristic (63.67G) — the copy edits are what
  turn retention into profit, because a retained value's next use
  usually stages into a fixed argument register.
- **Gates:** test_all green, FAST soak byte-identical on fresh
  references, self-host fixed point holds.

`2026-08-09u` (Linux x86_64, Zen 4 7940HS; register-allocator stage 6,
first slice — spill slots only for values that touch memory.)

- **What was built.** The fast placement's frame layout moved after the
  scan: every touch of a vreg slot flows through the edit stream, so
  only edit subjects get backing slots and values that never left their
  registers cost no frame bytes. The guard-page-probe bail (frames
  >= 4080 fall back to canonical, stage-2 restriction) now applies to
  the compacted frame.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fallbacks 1373 -> 1321 — 52 functions whose all-vregs frames
  exceeded the probe limit now fit, coverage **1596 of 2917 (54.7%)**.
  Fast-built compiler 63.62G instructions (from 63.76G), text
  24,784,468 (from 24,958,388; canonical 25,699,964).
- **Gates:** test_all green, FAST soak byte-identical on fresh
  references, self-host fixed point holds.
- **Left open in stage 6:** spill-slot reuse across dead ranges,
  callee-saved RBX/R12-R15 with prologue saves and unwind actions,
  within-block debug location ranges, and making FAST cover the full
  canonical self-host path.

`2026-08-09t` (Linux x86_64, Zen 4 7940HS; register-allocator stage 5,
first slice — liveness-driven spill kills put FAST ahead of canonical.)

- **What was built.** Two linear pre-passes over the machine IR before
  the forward scan: defining block per vreg (defs dominate uses but
  layout order does not prove precedence, hence the separate pass), then
  last textual use and a block-escape flag. A dirty register whose
  non-escaping owner is strictly past its last use drops its spill store,
  and eviction prefers dead owners over LRU (displacing them costs
  nothing). Escaping values always reach their slots, which keeps
  loop-carried values and goto layout-order hazards safe by construction
  — the block-boundary flush contract is unchanged.
- **Numbers** (buster-built stage comparison, compiling ide.c under
  NONE): fast-built compiler 63.76G instructions vs canonical 70.05G —
  **9.0% below the canonical stack emitter**, from +1.0% in
  `2026-08-09s`; the everything-in-slots penalty was +14.6%. Text
  24,958,388 vs canonical 25,699,964 (−2.9%). Block-local expression
  temporaries dominate the vreg population, so killing their dead stores
  converts most of MIR_STACK's store traffic into pure register traffic
  at 52.9% machine coverage.
- **Gates:** test_all green (20088 unit / 30 module), FAST soak
  byte-identical on fresh references, self-host fixed point holds.
- **Left open for the rest of stage 5:** traces and edge location
  contracts (values still reload once per block), per-preg future-active
  buckets, loop-carried retention in registers, and edge parallel-copy
  resolution.

`2026-08-09s` (Linux x86_64, Zen 4 7940HS; register-allocator stage 4 —
the FAST local allocator, first allocator to survive the soak.)

- **What was built.** `register_allocator_fast.c`: a forward block-local
  scan over the machine IR producing the same placement contract the
  encoder already consumes (per-slot `operand_registers` plus point-sorted
  reload/spill edits), so the encoder is byte-for-byte shared with
  MIR_STACK. Allocatable file {RAX,RCX,RDX,RSI,RDI,R8-R11}, LRU eviction
  by age clock, lazy dirty spills at BEFORE points, constrained opcodes
  keep their canonical scratch layout, calls and terminators flush
  (block-local state; everything caller-saved). Fixed-register hazards
  all bind in place: a MOV_RR use into a fixed physical destination
  stages its source in that register, and a MOV_RR def *from* a fixed
  physical source (entry captures, call results) binds the vreg in the
  source register itself.
- **Four placement/encoder bugs found by the soak, none by unit tests
  at first.** (1) The encoder's edit-merge loops assumed RELOAD@BEFORE /
  SPILL@AFTER and emitted FRA's BEFORE-point spills as loads — both loops
  dispatch on `edit->kind` now. (2) LEA_FRAME/LEA_SYMBOL dropped REX.R,
  so `lea → r10` encoded as `lea → rdx`. (3) An argument-staging reload
  free-picked a register that already held a placed argument
  (os_file_write wrote base64 garbage instead of an ELF). (4) The entry
  argument captures free-picked destinations in enumeration order, so
  capture #2 bound RCX and destroyed incoming argument 4 before its own
  capture read it — os_reserve passed flags=0 to mmap (EBADF) and the
  fast-built compiler died in arena creation. A function needs four or
  more register arguments to expose it; the corpus had `six()` but the
  FAST section never executed it. The FAST differential now runs six()
  (red before the fix: none=654321 fast=650341, exactly d replaced by b).
- **Gates:** machine_tests 20088-suite green including the new capture
  differential; test_all green; fixed point holds; MIR soak and **FAST
  soak both byte-identical** (fast-built compiler reproduces canonical
  stage-2 bytes exactly). Debug bisection knob removed from codegen.c.
- **Stage-4 quality numbers** (buster-built stage comparison — validation
  metrics, not clang-binary perf tracking): compiling ide.c, the
  canonical-built compiler executes 70.05G instructions, the
  mir-stack-built compiler 80.26G (+14.6%), the fast-built compiler
  70.77G (+1.0%) — FRA recovers ~93% of the everything-in-slots penalty
  at 52.9% machine coverage. Text bytes: canonical 25,699,964; mir-stack
  26,602,780 (+3.5%); fast 25,676,908 (marginally below canonical).
- **Method note, paid for twice:** rebuild the byte-identity oracle from
  the current tree immediately before every bisection run. A stale
  reference (or a reference the failing chain never wrote) makes `cmp`
  fail universally and fingers an innocent function; both false leads
  cost real time.

`2026-08-09r` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
indirect calls, aligned locals, and atomics.)

- **What was built.** FUNCTION references became ordinary rip-relative
  symbol leas so function pointers flow like any pointer, with
  CALL_INDIRECT carrying the callee in R10 (immune to both the argument
  registers and the variadic AL setup); stack slots carry per-slot start
  alignment up to sixteen (the frame base is sixteen-aligned, so aligned
  offsets land aligned in memory); and the atomic set landed mirroring
  the canonical sequences — plain loads, xchg for sequentially consistent
  stores, a lock-cmpxchg retry loop with R8 for fetch-op/exchange,
  compare-exchange with the expected value staged into RAX, and mfence
  for sequentially consistent thread fences.
- **Gates:** `machine_tests` 470/470 (stored-function-pointer calls,
  _Alignas(16) locals, and the full atomic operation set executing
  differentially); test_all green; fixed point `bytes=27171784`; soak
  byte-identical. Coverage: 1373 fallbacks — **1544 of 2917 (52.9%)**;
  these constructs mostly co-occur with the remaining mass. Two frontend
  discoveries recorded: the canonical inline-assembly template subset
  rejects plain mov templates (the unsupported representative is computed
  goto now), and `long double`/`__builtin_alloca` do not survive the C
  frontend at all. Remaining stage-3 mass: va_start machinery, inline
  assembly, STACK_ALLOCATE, label addresses, and machine-path debug
  locations — after which stages 4-6 (FRA) begin against a majority-
  coverage soak corpus.

`2026-08-09q` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
stack arguments and the memory-class aggregate ABI, crossing half the tree.)

- **What was built.** Stack arguments both directions (the canonical
  all-or-nothing placement rule, right-to-left pushes from value slots
  with alignment padding and explicit RSP cleanup, LOAD_INCOMING reads of
  the caller-pushed area) with the argument-list cap at sixteen; then
  memory-class aggregates: large results return through a hidden RDI
  pointer saved into a frame slot and copied through at return with RAX
  carrying the pointer, callers pass the result slot's address in the
  first integer register (synthesized scratch storage when the result is
  unused), and memory-class arguments travel by value in outgoing stack
  eightbytes. No new encoder forms were needed for the indirect return —
  it composes from the existing lea/load/copy/copy-through-pointer rows.
- **Gates:** `machine_tests` 451/451 (24-byte struct make/sum/round-trip
  executed typed against the oracle, machine-to-machine); test_all green;
  fixed point `bytes=27138592`; soak byte-identical. Coverage: fallbacks
  1650 → 1615 (stack args) → 1377 — **1540 of 2917 (52.8%)
  machine-compiled**, the majority of the unity tree. Remaining mass:
  indirect calls, atomics, va_start machinery, inline assembly,
  over-aligned locals, STACK_ALLOCATE, label addresses, and machine-path
  debug locations.

`2026-08-09p` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
scalar float operations and the XMM ABI.)

- **What was built.** Float values travel as IEEE bit patterns in general
  registers and slots (canonical-identical); FARITH/FCMP_SET/conversion
  rows bridge through XMM0/XMM1 internally, with the canonical NaN-parity
  fixups and the 64-bit convert forms (unsigned-64 sequences stay outside
  the subset). Then the ABI: shapes carry per-part classes, parameters and
  call arguments use per-class running register assignment (six integer,
  eight XMM), entry captures integer parts before float parts because
  float captures scratch general registers, float parts bounce through
  RAX via MOVQ_TO/FROM_XMM rows, variadic AL carries the true XMM count,
  and one/two-part returns place integer parts in RAX/RDX and float parts
  in XMM0/XMM1.
- **Gates:** `machine_tests` 439/439 with typed float-signature
  executions (mixed argument sequences, all-float and tagged aggregates,
  aggregate float returns, machine-to-machine float calls); test_all
  green; fixed point `bytes=27064000`; soak byte-identical. Coverage
  1729 → 1650 fallbacks: **1267 of 2917 (43.4%)** machine-compiled.
  Remaining mass: stack arguments (>6 integer parts — the `seven`
  representative), atomics, va_start machinery, inline assembly,
  over-aligned locals, STACK_ALLOCATE, indirect calls, label addresses,
  and machine-path debug locations.

`2026-08-09o` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3 —
switch chains, variadic calls, and the aggregate ABI, taking the machine
path to 40.7% of the unity tree.)

- **What was built.** SWITCH as one terminator row over a cold case side
  table (encoder emits the canonical movabs/cmp/je chain); UNREACHABLE as
  a never-executed return; variadic direct calls with AL zeroed; and the
  big one — the aggregate subset: `MachineX64ValueShape` derives one- and
  two-part integer shapes from the IR-owned System V classification
  (`ir_type_abi_value`), aggregate values own frame slots, parameters and
  results transfer per part through LOAD_FRAME/STORE_FRAME64 with byte
  offsets into slots, and three chunk-copy opcodes
  (frame↔frame/ptr↔frame, chunked 8/4/2/1 like the canonical copy loops)
  carry aggregate loads and stores. The encoder's capacity estimate became
  a per-row worst-case walk because switches and copies expand with their
  side data.
- **Coverage:** fallbacks 2267 → 1730 — **1187 of 2917 functions (40.7%)
  machine-compiled**, mir-built stage 2 still byte-identical; the
  String8-by-value hypothesis from `2026-08-09n` confirmed (aggregates
  nearly doubled coverage; variadic support alone had moved nothing).
  `machine_tests` 367/367 including Span (String8-shaped) pass/return/
  round-trip differentials. Fixed point `bytes=27013000`. Remaining
  fallback mass: float scalars, >6-register/stack arguments, atomics,
  va_start machinery, inline assembly, over-aligned locals, and
  machine-path debug locations.

`2026-08-09n` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3,
subset growth — shifts, the divide family, and direct System V calls in the
machine path.)

- **What was built.** Shifts (count in CL), signed/unsigned divide and
  remainder (dividend/result in RAX, RDX clobbered) mirror the canonical
  sequences and are the subset's first fixed-register constraints. Direct
  calls lower to explicit fixed-register argument copies plus a
  MACHINE_X64_CALL_DIRECT row whose payload indexes a call-target side
  table; the encoder returns call sites and the canonical-module wrapper
  converts them to ordinary symbol relocations. The placement rule that
  makes argument sequences safe: a copy into a fixed physical register
  reloads its source directly into that register, so no staging scratch
  can clobber an already-placed argument. FUNCTION values are legal only
  as direct-call callees; any other use is an explicit unsupported result.
- **Two wiring bugs the differentials caught:** the call-relocation offset
  was computed with the post-copy formula before the buffer advanced
  (calls landed on garbage displacements), and the module-level test had
  to resolve internal call relocations linker-style before taking the
  executable copy.
- **Gates:** `machine_tests` 271/271 (machine-to-machine calls execute in
  the module differential); Release `test_all` all passing; self-host
  token (`1399519`) and executable (`bytes=26936064`) fixed points hold;
  unity soak now **314 of 2917 functions (10.8%)** machine-compiled with
  byte-identical mir-built stage 2 (was 130 at the first wiring, 159 with
  shifts/divide). Stage-1 instructions per token `3712.397`, flat.
- Reference points: stage 1 `1399519` tokens / `3712.397` per token,
  fixed point `bytes=26936064`, soak 314/2917 byte-identical. Remaining
  fallback mass: float/aggregate signatures, GLOBAL/INDEX/FIELD places,
  SWITCH, variadics, >6 arguments, and inline assembly — the next
  stage-3 increments in fallback-frequency order.
- **Same-day addendum — the address increment:** GLOBAL (non-TLS, via
  `lea rip+rel32` symbol sites sharing the call-site relocation stream),
  INDEX (base address plus sign-extended scaled index composed from
  existing rows through selection-synthesized temporaries), FIELD (base
  plus immediate offset), ADDRESS_OF, and the widened indirect place set
  for loads/stores. Address-producing values hold an 8-byte address in
  their vreg regardless of declared type, exactly like canonical slots.
  `machine_tests` 317/317 (global-touching functions are
  selection/encode-verified only — raw code copies cannot resolve data
  relocations; the linked soak is their execution proof). Unity soak:
  **608 of 2917 functions (20.8%)** machine-compiled, byte-identical;
  fixed point `bytes=26957032`; stage-1 per token `3712.501`, flat.

`2026-08-09m` (Linux x86_64, Zen 4 7940HS; register-allocator stage 3, first
increment — MIR_STACK wired into `codegen_generate_canonical_module` with
per-function counted fallback, soaked on the full unity self-compile.)

- **What was built.** Under `-fregister-allocator=mir-stack` on x86-64
  System V, each lowered function attempts machine selection → structural
  verification → stack placement → machine encoding into the module buffer;
  the machine prologue byte-matches the canonical plain prologue so the
  descriptor's PUSH/SET_FRAME_POINTER/ALLOCATE_STACK unwind actions keep
  their exact meaning, and every ineligible function falls back to the
  canonical path and is counted (functions targeted by label-address
  relocations are conservatively excluded). Machine-path functions carry a
  function-start line row but no per-instruction lines or debug locations
  yet — recorded as an open stage-3 item, acceptable only because MIR_STACK
  is an internal verification mode. `machine_tests` grew to 213 assertions
  with a module-level differential (NONE module vs MIR_STACK module,
  fallback accounting, descriptor/unwind shape, execution equality
  including the fallback divide).
- **The soak and the bug it caught.** Full unity compile with mir-stack:
  130 of 2917 functions (4.5%) take the machine path; the mir-built
  compiler then recompiles ide.c under NONE and the output is
  **byte-identical** to the canonical stage 2. Getting there required one
  real fix a small differential corpus could not see: sized direct-slot
  frame stores (`mov [rbp+slot], ecx`) left stale upper bytes that the
  64-bit slot loads and `test rax, rax` short-circuit branches consumed —
  harmless on fresh zero stack pages, wrong on the deep dirty compiler
  stack (first symptom: the stage-2 parse failing at an unrelated line).
  Direct-slot stores now always write the full 8-byte slot, exactly like
  the canonical `C_X64_STORE_RESULT`. The debugging lever worth keeping in
  mind: a temporary BUSTER_MACHINE_LIMIT env bisection over machine
  functions, deleted before commit, and cross-checking any byte-diff
  criterion against a *current-tree* reference — a stale stage-2 reference
  produced a phantom culprit first.
- **Gates:** Release `test_all` 19773/19773 across 30/30 modules;
  `test_self_host` token (`1396539`) and executable (`bytes=26910088`)
  fixed points hold; the mir soak above. Stage-1 cost: `5.1716 G` to
  `5.1865 G` (`+14.9 M`, `+0.29%`) at only `+328` tokens — instructions
  per token `3704.027` to `3713.821` (`+0.26%`), the first per-token
  regression of the project; the suspects are the wiring block enlarging
  the already-huge canonical generator function. Flagged for the stage-6
  audit: if the per-token cost keeps climbing with wiring code, move the
  machine path out of the canonical function body.
- Reference points for the next audit: stage 1 `5.1865 G` / `1396539`
  tokens / `3713.821` per token, stage 2 `69.1864 G` / `49541.307` per
  token, fixed point `bytes=26910088`, mir soak 130/2917 machine functions
  byte-identical.

`2026-08-09l` (Linux x86_64, Zen 4 7940HS; register-allocator stage 2 — the
x86-64 scalar selector, MIR_STACK placement, and encoder over the compact
machine IR, differentially executed against the canonical NONE path.)

- **What was built.** `machine_select_canonical_function` selects scalar
  integer functions (≤6 System V register arguments, scalar/void returns,
  no calls/divides/aggregates — everything else is an explicit
  `failed_opcode` result) into the 24-byte machine rows: entry argument
  capture, constants via an immediate pool, integer casts/unary/binary and
  comparisons through flags metadata, direct locals as selector stack
  slots, pointer loads/stores, stack save/restore as RSP copies, branches
  and returns. `machine_stack_placement_build` gives every vreg an 8-byte
  frame slot and derives sorted reload/spill `MachineEdit` rows plus fixed
  per-slot scratch registers; `machine_encode_x86_64` merges edits and rows
  into bytes with branch fixups and an RBP frame. `machine_tests` grew to
  183 assertions: 13 C functions selected, verified, encoded, and executed
  against the canonical module over an argument grid, plus explicit
  unsupported-fallback and placement-bound checks.
- **The one real bug the differential caught:** the C frontend materializes
  `ARGUMENT` lazily at first use, so a body row's RCX operand scratch
  clobbered argument 3 before its capture; `six(a,…,f)` returned `c` in
  `d`'s place. Fix: all incoming argument registers are captured at entry
  and the typed `ARGUMENT` instructions become row-free. Recorded here
  because every future allocator stage inherits this invariant: fixed
  incoming registers must be captured before any scratch can touch them.
- **Gates:** Release `test_all` 19743/19743 across 30/30 modules;
  `test_self_host` token (`1396211`) and executable (`bytes=26897064`)
  fixed points hold. Source growth: `+12031` tokens (`+0.87%`), stage 1
  `5.1288 G` to `5.1716 G` (`+0.83%`), instructions per token flat
  (`3705.305` to `3704.027`); stage2/stage1 ratio `13.369` (NONE emission
  untouched).
- Reference points for the next audit (stage 3: full canonical coverage
  plus MIR_STACK wiring into `codegen_generate_canonical_module` with
  counted fallback): stage 1 `5.1716 G` / `1396211` tokens / `3704.027`
  per token, stage 2 `69.1392 G` / `49519.146` per token, fixed point
  `bytes=26897064`.

`2026-08-09k` (Linux x86_64, Zen 4 7940HS; register-allocator project stage
0+1 — the current-HEAD rebaseline under the new token metrics, plus the
allocator-mode option and the compact machine-IR skeleton with **no
production-path change**. Baseline measured on the clean `41814eaa` tree,
patched numbers on the same host, clang-built, quiet machine.)

- **Stage 0 baseline at `41814eaa` (pre-patch), the first reference numbers
  recorded under the `1d5e6c5`/`41814ea` token gates:** stage 1 `5.1202 G`
  instructions / `1378839` tokens / `3713.440` instructions per token;
  stage 2 `68.4355 G` / `49632.695` instructions per token; stage2/stage1
  per-token ratio `13.365`; fixed point deterministic
  (`SELF_HOST deterministic bytes=26726416`, token and spelling-byte streams
  equal); stage-2 benchmark `8.379 G`. Against the `2026-08-09g` reference
  (stage 1 `5.0824 G`, taken at `380f8c4`) the `+37.9 M` (`+0.75%`) is the
  four commits landed since — the always-on source/preprocessed measurement
  and the build-side token gates — not this patch.
- **What was built.** `CodegenRegisterAllocatorMode`
  (`none|mir-stack|fast|quality`) stored in `CodegenModuleOptions`' reserved
  bytes (the record stays 8 bytes), parsed as `-fregister-allocator=` beside
  `-fsource-metrics=` and threaded through all three codegen call sites;
  until the machine selector exists every function under a non-NONE mode
  takes the existing path and is counted in the new
  `CodegenStatistics.fallback_function_count`, printed as append-only
  `allocator=`/`fallback_functions=` keys on the `CODEGEN` line. New
  `compiler_machine` module (`machine.{c,h}`, included by `machine.c`'s
  future backend files, registered in CMake/unity): `MachineRef` (3-bit
  kind + 29-bit payload), four-phase `MachinePoint`, and the nine
  `BUSTER_CT_CHECK`ed hot records (`MachineInstruction` 24 B,
  `MachineVirtualRegister` 16 B, `MachineBlock` 32 B, `MachineEdge`/
  `MachineAddress`/`MachineEdit`/`MachineLocationSegment` 16 B,
  `MachineSegment`/`MachineUse` 8 B), static opcode-metadata interface with
  skeleton opcodes, the chunk-and-flatten builder (16 KiB chunks), a
  structural verifier (block partition, opcode validity, terminator
  placement, operand references, unused-slot emptiness, definition points,
  point capacity), and versioned test-only replay serialization.
  `machine_tests` adds 48 assertions including a compile-time size census of
  the hot IR/codegen records (`IrInstruction` 112 B — the stale 216-to-152
  comment in `ir.h` is corrected to point here). Verified end to end: the
  probe executable built with `-fregister-allocator=fast` is byte-identical
  to the default build and reports `allocator=fast fallback_functions=2`;
  `bogus` values get a driver diagnostic.
- **Patched-tree gates, all passing:** Release `test_all` 19608/19608 unit
  tests across 30/30 modules (`machine_tests` 48/48 in 38 µs);
  `test_self_host` token and executable fixed points hold
  (`SELF_HOST deterministic bytes=26743112`, tokens `1384180` in both
  stages). Source-growth cost of the skeleton: `+5341` tokens (`+0.39%`),
  stage 1 `5.1202 G` to `5.1288 G` (`+8.6 M`, `+0.167%`), stage-1
  instructions per token `3713.440` to `3705.305` (the added rows/tests
  compile cheaper per token than the tree average), stage2/stage1 ratio
  `13.365` to `13.366` (generated-code quality untouched, as required),
  stage-2 benchmark `8.379 G` unchanged.
- Reference points for the next audit (stage 2 of the allocator plan: x86
  scalar selector/encoder subset under `MIR_STACK`), all clang-built on this
  host: stage 1 `5.1288 G` instructions / `1384180` tokens / `3705.305`
  instructions per token, stage 2 `68.5511 G` / `49524.678` per token /
  ratio `13.366` (validation only), `ide bench` reference remains `343.2 M`
  from `2026-08-09g` (parser untouched), fixed point
  `SELF_HOST deterministic bytes=26743112`.

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
