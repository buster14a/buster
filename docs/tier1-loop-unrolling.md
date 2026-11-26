# Tier 1 loop unrolling (#2755)

Status: implemented as an option, `--tier1-passes=+unroll`, off by default.
`src/buster/compiler/ir_optimize_unroll.c` is the pass; `test-ir-unroll`
([the IR optimization guide](commands/ir-optimization.md)) is its test. The
measurements are the 9700X's, recorded in buster-performance (MEASUREMENTS
below). The issue is [#2755](https://github.com/buster14a/buster/issues/2755);
it follows [#2743](https://github.com/buster14a/buster/issues/2743)'s
measurement and [the loop vectorization design](tier1-loop-vectorization.md).

**In one paragraph.** On the 9700X at `-march=x86-64`, clang -O2's scalar loops
over the 19 array loops of #2743's suite are 1.41 times faster with its
unroller than without it, separately from vectorization; Tier 1 had no
unroller. This pass gives each counted innermost loop a main loop of up to
eight copies of its body, entered and continued only while that many whole
iterations remain, and keeps the original loop as it was, as the remainder of
fewer iterations and as the whole loop wherever the main loop can't run. The
main loop's bound is computed once before it, after a two-instruction test that
sends a loop of no iteration straight to the original; inside it, each copy's own exit test is known
to pass and becomes a jump, copy k's induction is the main induction plus k
steps (folded into its addresses' displacements), and an exact integer
reduction is split into accumulators that the main loop's exit combines. Every
step that looks at many instructions is data-parallel (#2924 deleted its
scalar twins). On the 9700X at `-march=x86-64`,
#2743's 19 vectorizable loops run 1.41 times as fast, clang's whole unrolling
component, and its six controls 1.05 times, at each of four placements, with no
kernel slower anywhere. The pass is off by default (`--tier1-passes=+unroll`)
until the owner decides on this record.

## What it unrolls

The pass looks at the natural loops of [the dominator analysis](dominators.md)
and admits a loop only when all of these hold. A loop that fails is left as it
is and counted under its first failing rule, which `--tier1-report` prints
(`IrUnrollRefusal`, below).

| Rule | Refusal |
| --- | --- |
| It contains no other loop. | `outer` |
| It is reducible. | `irreducible` |
| Its header has one reachable predecessor outside it, which ends in `br` to the header (its preheader). | `preheader` |
| Its header has one predecessor inside it (its latch). | `latch` |
| It has no call (of any kind: a row call or an asm body is one too). | `call` |
| It has no switch, indirect branch or labeled block. | `control` |
| It has no `alloca`, `stack_save` or `stack_restore`. | `stack` |
| It leaves by exactly one edge, from its header or its latch, and has no return or `unreachable`. | `exit` |
| That edge's block ends in a conditional branch on an integer `icmp`. | `test` |
| One of the compared values is defined outside the loop, or is a constant (wherever the lowering put it). | `bound` |
| A count-up continues on `ult`, `ule`, `slt` or `sle`. A countdown's header, distinct from its latch, continues while its i32 phi differs from constant zero. Either operand order and either edge are accepted. | `predicate` |
| A count-up compares an integer header phi (8 to 64 bits) or the phi plus a constant from 0 to 65,536, and its latch adds a positive constant step of at most 65,536. A countdown's latch subtracts one from the phi or adds the i32 bit pattern for minus one. Constants may come through copies, extensions and truncations. | `induction` |
| Two copies' estimated cost fits the budget (below), and the loop has at most 1,024 instructions. | `size` |
| The function's growth stays within its budget. | `growth` |

These are the forms C's counted `for` loops take after promotion, threading and
if-conversion: the test at the top on the induction phi (`for`), at the top on
the increment, or at the bottom on either (a one-block loop, `do`/`while`). A
body may branch and join inside itself, and its phis are copied with it.

For `for (uint32_t i = n; i != 0; --i)`, the unsigned value of `i` is the
number of iterations left. The entry and back edge both test `i >u f - 1`
for a chunk of `f` iterations. Its copies therefore see positive values
`i, i - 1, ..., i - (f - 1)`, and the next induction `i - f` is representable.
This also proves that the zero-extended index of `a[i - 1]` in copy `k`
equals the first copy's index minus `k`. The loads keep their order and
the offsets fit the same addressing mode. A short or empty input goes
straight to the original header. After the last chunk that header receives
the exact remainder.

## The rewrite

Write the continuing test as `v P n`, where P is `<` or `<=` (signed or
unsigned), `v` is the induction phi `i` plus a constant c >= 0 (0 for the phi,
the step `s` for its increment), and `n` the bound. Iteration k of the loop
returns to the header exactly when `i + ks + c P n`. With the induction
increasing and no wrap, `f` whole iterations from `i` all return exactly when
the last one does: `i + (f - 1)s + c P n`. So, with K' = (f - 1)s + c for a
strict test and one less for a non-strict one, the main loop may run its `f`
copies exactly when `i + K' < n` without wrapping, in the test's signedness.
Two tests decide that, the first one cheap:

```text
P:   ...
     cbr (start < n), MP, H            ; no iteration at all: the original loop, at once
MP:  diff = n - start                  ; with start < n, the true difference
     bound = n - K'                    ; can't wrap once diff > K'
     cbr (diff >u K'), MH, MX
MH:  the main phis: the header's phis' copies, a split reduction's accumulators
     copy 1 of the body, ..., copy f
     next = i + f*s
     cbr (next < bound), MH, MX
MX:  phis of what MP and the main loop pass on; split reductions combined
     br H
H:   the original loop, its phis entered from P as before and from MX
```

Given `start < n`, `n - start` is the true difference as an unsigned value, so
`diff >u K'` is exactly `start + K' < n`, and `n - K'` is then above `start` and
wraps in neither signedness. A loop of no iteration pays the first test alone,
two instructions; an earlier version computed a clamped bound with a `select`
for every entry, and Lua's one-iteration loops paid seven (below). Every
`i + ks` with k < f is below `n` without wrapping, in the test's signedness,
which is what the copies rely on; `next` may wrap past the type's limit as the
original's induction would, and the main test reads it as the original's test
would. The original loop is the remainder (fewer than `f` iterations left) and
the fallback (either test fails) at once, so there is one copy of its
legality, as [the vectorizer's design](tier1-loop-vectorization.md) asks of its
own remainder.

Within the main loop:

- **Copies.** Copy 1's header block is MH, after the main phis. Each copy's
  header copy reads the header phis as the previous copy's latch passes them
  on. The exit test's branch becomes a jump into the body, and the latch's back
  edge a jump to the next copy's header; the last copy's latch tests `next`. A
  copy keeps every instruction, in order, with the same effects: no access,
  trap or floating operation is reordered or removed; the optimizer's dead-code
  removal takes the copies' dead tests afterwards.
- **The induction.** Copy k (from 0) reads `i + k*s` from the main phi, not a
  chain of increments, so the copies don't wait on one another.
- **Addresses.** An `address base + i * scale + d` in copy k becomes
  `base + i_main * scale + (d + k*s*scale)`, a new address table entry: exact,
  since `i_main + ks` doesn't wrap. The same holds for `sext i` under a signed
  test and `zext i` under an unsigned one, where that extension's block
  dominates the latch (it runs in every iteration, so copy 1's version reaches
  every copy). Without it, Tier 1 materialized `i + k` in a register wherever
  a load and a store shared it (`scale_u8`: an `lea` per copy).
- **Integer reductions.** A header phi `r` whose latch value is `r op x`
  (`add`, `sub` with `r` first, `mul`, `and`, `or`, `xor`), `select c, r op x,
  r` (either arm order), or a minimum or maximum `select (r P x), ...` of `r`
  and `x` (signed or unsigned, either operand order), and which the loop reads
  nowhere else, takes up to four accumulators: the first starts at `r`'s entry
  value, the others at the operation's identity, and copy k updates
  accumulator k mod A. MX combines them in order. Wrapping integer arithmetic
  is associative and commutative and each update is either `op x` or the
  identity, so the result is exact. Floating values, and integers the loop reads
  elsewhere, are carried through the copies as they are.

The pass returns the input itself when it admits nothing. Original blocks keep
their identities and the new blocks follow them, so labels and debug regions
stay valid; copies keep their instructions' origins, and new instructions have
none.

## Budgets

- **Factor.** The largest power of two up to 8 whose copies' estimated cost
  fits 128. A copy's cost is its live instructions less the free ones: a
  liveness walk from the copy's effects, traps, internal branches and the next
  copy's phi inputs (not its exit test, which becomes a jump), not counting
  constants, copies, pointer casts, addresses of slots and symbols, phis and
  jumps. The lowering leaves dead truth-value chains and `sext (const 1)`
  steps in the loop until the optimizer runs, which a raw instruction count
  would charge for. Two copies over the budget refuse the loop (`size`).
- **Accumulators.** Up to 4, halved while the main loop's general values
  (its integer and pointer phis, the outside values it reads, and its bound)
  would pass 7. Tier 1 gives a value that crosses no call the caller-saved
  registers alone ([the allocation guide](tier1-allocation.md), #2819), nine
  of them, and a copy's own temporaries take two. Before this rule, `count_eq_u8`'s four
  accumulators spilled the bound and two of them and ran 0.72 times as fast
  locally as without unrolling.
- **Growth.** At most 4,096 instructions added to a function; a loop that
  would pass it is refused (`growth`). Each admitted loop adds about `f` times
  its instructions, its main and exit phis, the preheader's one or two and
  MP's five or more.

`IrUnrollOptions` (`factor`, `budget`, `accumulators`, `growth`) holds them;
`ir_unroll_options` gives the defaults above, and the tests run others.

## With vectorization and LICM

The pass runs after if-conversion, `+licm` and `+vectorize`, before the operand
sink and the optimizer, whose SCCP, dead-code removal and merges clean the copies up: the
copies' dead tests and increments go, and their blocks merge into one where the
body was one. LICM first moves invariant computations into the preheader once,
not into every copy; the unroller doesn't move anything itself.

The unit's inlining (#2541) builds a summary of each candidate by the same
passes, and its callers expand the summary and run the passes again. Summaries
skip the unroller, so an expanded loop is unrolled once, in its caller, and a
summary's size, which the expansion decisions read, is the loop's own. A
candidate whose own code comes from its summary takes the unroller there
instead, on the summary's optimized function (which it doesn't change), and
then the sink and the optimizer again where a loop unrolled
(`check_tier1_finish_own`, which also applies late local-copy pairing). The first version compiled such candidates by their
whole pipeline again, which made `-c` of `sqlite3.c` about 23% slower locally;
this way the unroller's share is the cost below.

The loop vectorizer (#2743, [its design](tier1-loop-vectorization.md),
`--tier1-passes=+vectorize`) runs before this pass: it writes `steps[10]` and
this pass `steps[11]`. With both on, #2743's kernels at znver5 show how they
meet: the vectorizer takes 12 of the 27 loops, and this pass refuses all 24
loops it made (the 12 vector loops and their 12 scalar remainders) by its
preheader rule, which wants one block entering the loop by an unconditional
branch; the vectorizer's loops have none. The pass unrolls 12 of the loops the
vectorizer refused, as it does alone, and every kernel's digest is the default
build's. So the remainders, which run fewer than `w` iterations and would only
grow, are never unrolled. Unrolling a vector main loop (more vector
accumulators) is the next step: it needs the preheader rule to take the
vectorizer's entry and a vector reduction split (the reduction rules take
integer scalars), each with its own tests, and the remainders refused by a mark
rather than by their shape.

## Data-parallel parts

- **Classes.** One 64-byte masked load of the opcodes and one byte permute of a
  128-entry class table per 64 instructions (`ir_unroll_classify`), which also
  answers whether the function has a phi at all: a function without one has no
  induction, and nothing else is built for it.
- **A loop's classes.** The union of its blocks' classes, 64 bytes at a time,
  or-reduced (`ir_unroll_block_classes`).
- **The copies' operands.** Every copied range's fixed operands, 16 at a time:
  the operand tables' masks (`ir_lookup_bytes`, as the use lists' builder
  does) and three masked gathers from the old-to-new map
  (`ir_unroll_remap`).

Each is checked against its definition on random columns (#2924, which
deleted their scalar twins); the remap of a function past `INT32_MAX`
instructions, whose indices a signed gather can't take, goes one at a time.
The rest is per loop and small: its blocks, its header's phis, its test and its liveness walk, bounded by
1,024 instructions, and the CFG and phi construction of its copies. The
vectorized parts are the ones that run over every instruction of a function
(the classes) or of every copy (the remap).

## Tests

- **`test-ir-unroll`** builds loops in the IR's text form from a builder that
  names values, parses them, and runs each: its function, outcomes and counts
  folded into a digest frozen where the twins agreed (with the default seed and
  shapes), scratch returned. An unrolled function must verify with
  dominance, pass Tier 1's optimizer, and execute as the original did on the
  shared independent executor (return, status and every store), on 98
  parameters: 0 to 25, for zero, partial and whole main iterations, and windows
  of 24 at each type's unsigned maximum and on both sides of its signed limit,
  where the bound's clamp decides. At least the 26 small runs must return, so
  that no fixture passes on two equal faults. The shapes are every form (the
  test on the phi or the increment, at the top or the bottom, a one-block loop,
  a diamond inside), every predicate in either operand order and on either
  edge, every reduction kind and two that only look like one, the induction's
  address and its extensions', each width from 8 to 64 bits, then 400 random
  shapes (`--shapes N`, `--seed N`) over steps 1, 2, 3 and 7. Every refusal has
  a loop; a nested pair checks the outer refusal beside the inner unroll. The
  budgets (factors 1, 2 and 64, a budget of 1, no growth, one accumulator) and
  every short output arena (each fails with nothing left in it or succeeds)
  have their own checks.
- **Mutations.** An off-by-one in the unsafe direction of the offset, a
  missing c, a wrong `and` identity, a displacement without its scale, `>=` for
  MP's `>`, an unsigned first test of a signed loop, and no first test each fail
  it. Testing `<=` as `<` passes, as it should: the
  main loop then leaves one iteration early, to the remainder.
- **`test-ir-optimize`** runs `tests/ir/optimize/unroll-loops-*.ir`: a store
  loop and a sum, the reviewed after text byte for byte and the executor.
- **`test-ir-source --passes`** runs the pass on every function of
  c-testsuite's subset, the frozen `sqlite3.c` and musl, as Tier 1 gives them
  to the pass (the optimizer's form with truth values simplified): every
  unrolled function verifies with dominance; 5, 26 and 45 functions unroll.
- **`test-checker-driver`'s passes fixture** builds with `+unroll`,
  `--verify`, and prints clang's output; `+all` unrolls too and the default
  doesn't.
- **Programs.** `test-c-testsuite --tier1` with `+unroll` (through
  `--compiler`, a wrapper that adds it) passes all 220 programs, Tier 1
  compiling their 430 functions; `measure-tier1-programs --variant
  --tier1-passes=+unroll` gives speedtest1's verification hash and every Lua
  script's output as clang -O0's; #2743's 25 kernels give every build's
  digest. The baseline host build (`BUSTER_HOST_ARCH=x86-64`, the scalar
  paths) passes `test-ir-unroll`, and so does an AddressSanitizer build, which
  also compiles the kernels and `sqlite3.c` with `+unroll` without a report.

## Measurements

The record is buster-performance's
[`tier1-unroll-2755-2026-11-15`](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-unroll-2755-2026-11-15):
its scripts, every raw line, the counters, the tables, the hosts and holds. It
reuses #2743's kernels, harness and method; Tier 1 at main `1935a91f` (the base)
and at this branch's `7ad1353e` with `+unroll`, Release, on the 9700X under its
lock, each run on CPU 2. Each lock hold timed a pair of builds at one
placement, alternating kernel by kernel.

### #2743's kernels at -march=x86-64

Every build gives every kernel the same digest, and the head's default object is
the base's byte for byte. Geometric means of time ratios (larger is slower):

| Ratio | 19 vectorized, placement 0 | 19 vectorized, best placement of each | 6 controls, placement 0 | 6 controls, best placement of each |
| --- | ---: | ---: | ---: | ---: |
| Tier 1 / Tier 1 `+unroll` | 1.41 | 1.36 | 1.05 | 1.05 |
| clang -O2, no vectorizer or unroller / clang -O2, no vectorizer | 1.41 | 1.45 | 1.05 | 1.05 |
| Tier 1 `+unroll` / clang -O2, no vectorizer | 1.12 | 1.15 | 1.03 | 1.03 |
| Tier 1 / clang -O2, no vectorizer or unroller | 1.12 | 1.08 | 1.03 | 1.03 |
| Tier 1 `+unroll` / clang -O2 | 2.45 | 2.41 | 1.03 | 1.03 |

At the four placements, Tier 1 / Tier 1 `+unroll` is 1.41, 1.41, 1.40 and 1.39
over the 19 loops and 1.05 at each over the controls; their cycles per element
give 1.42, 1.41, 1.41 and 1.39. The unroller recovers clang's whole unrolling
component, the issue's 1.41, and leaves Tier 1 exactly as far from clang
without its vectorizer as it was from clang without either (1.12). No kernel
runs slower at any placement: the least ratio is 0.999 (`find_u8`, which isn't
unrolled), and every unrolled kernel gains at least 1.6% (`i32_to_f32`).

| Kernel | Tier 1 | `+unroll` | clang no vectorizer or unroller | clang no vectorizer | gain |
| --- | ---: | ---: | ---: | ---: | ---: |
| sum_i32 | 0.191 | 0.095 | 0.186 | 0.102 | 2.02 |
| sum_u8 | 0.181 | 0.091 | 0.183 | 0.091 | 2.00 |
| copy_u8 | 0.181 | 0.091 | 0.183 | 0.093 | 1.99 |
| fill_u32 | 0.184 | 0.094 | 0.191 | 0.094 | 1.96 |
| max_i32 | 0.209 | 0.131 | 0.540 | 0.151 | 1.60 |
| scale_u8 | 0.247 | 0.162 | 0.226 | 0.118 | 1.52 |
| dot_i16 | 0.293 | 0.209 | 0.294 | 0.222 | 1.40 |
| add_i32 | 0.194 | 0.140 | 0.192 | 0.142 | 1.39 |
| count_eq_u8 | 0.250 | 0.181 | 0.210 | 0.181 | 1.38 |
| cond_sum_i32 | 0.334 | 0.252 | 0.306 | 0.242 | 1.32 |
| histogram_u8 (control) | 0.226 | 0.178 | 0.190 | 0.151 | 1.27 |
| rgb_to_gray | 0.759 | 0.743 | 0.536 | 0.478 | 1.02 |

ns per element at placement 0; the record has every kernel at every placement,
with cycles and instructions. The integer reductions gain most where two
accumulators break the add chain (`sum_i32` runs faster than clang's unrolled
code, whose reassociated tree keeps a longer chain); `rgb_to_gray` (a large
body, two copies) and `popcount_u64` (a software population count at x86-64,
four) gain least. Tier 1's remaining distance from clang without its vectorizer
is the back end's, as it was before unrolling: `matmul_f32`, `rgb_to_gray`,
`aos_sum`, `scale_u8`, `popcount_u64`, `i32_to_f32` and `clamp_i32`.

The base's placement outliers, which #2743's record couldn't explain, are gone:
`matmul_f32` took 0.731 ns at placement 0 and 0.372 to 0.385 elsewhere,
`saxpy_f32` 0.364 at placement 16, `clamp_i32` 0.725 at 32 and 48 and
`blend_u8` 0.542 at 32; with `+unroll` each varies by at most 6% over the four
placements. Those were one-iteration-per-cycle loops of a few instructions,
where a taken branch's placement decides; a main loop of eight copies isn't.

The kernels' text grows from 1,935 to 7,497 bytes (clang without its
vectorizer: 1,957 to 4,461 with its unroller).

### #2743's kernels at -march=znver5

At placement 0, with every digest matching and the default objects identical,
Tier 1 / Tier 1 `+unroll` is 1.40 over the 19 loops and 1.05 over the controls
(cycles 1.39 and 1.05). clang's unroller is worth 1.65 there, so Tier 1 `+unroll`
stays 1.29 from clang without its vectorizer, against 1.09 without either. The
difference is the back end's, and two kernels show it:

- **`i32_to_f32`** gains 1.02 (1.009 cycles per element; clang's unrolled loop
  0.582). Tier 1 converts from a register (`mov; xorps; cvtsi2ss r32`), clang
  from memory. On the 9700X `cvtsi2ss` from a register runs one per cycle and
  from memory two (the record's `convert.c`), so the register form caps the loop
  whether it is unrolled or not.
- **`saxpy_f32`** runs 0.1944 ns with `+unroll` at every placement and the base
  0.1840 to 0.1927 (1.0 to 5.7% faster). In the record's `saxpy.c`, Tier 1's
  eight-copy body runs 1.070 cycles per element and its rolled loop 1.056; with
  the `x` load folded into `vfmadd231ss` the body runs 1.008, as clang's does
  (1.009), while VEX moves or the other load order change nothing. Tier 1 folds
  no load into floating-point arithmetic or conversions:
  [#2829](https://github.com/buster14a/buster/issues/2829).

The same program found that Tier 1's legacy SSE moves (`movss`, `movd`,
`cvtsi2ss`), which it writes beside VEX FMAs at znver5, run 6.0 cycles per
element after a dirty upper state, and that Tier 1 returns from 512-bit code
without `vzeroupper`: [#2830](https://github.com/buster14a/buster/issues/2830).

The kernels' text grows from 1,807 to 6,969 bytes (clang without its
vectorizer: 1,853 to 6,661).

### Real programs

Under callgrind (deterministic instruction counts, Lua's hash seed fixed),
`+unroll` against the default pipeline at the same head: speedtest1 −0.17%
(`balance_nonroot` −10.9%), Lua's five scripts −0.00%, +0.73%, +0.67%,
−0.06% and +0.06% (arithmetic, calls, closures, strings, tables). Lua's costs
are its one-to-three-iteration loops, which pay the first test and MP's on
every entry: `luaD_precall`'s missing-argument loop (+3.8 M of 125.9 M in
`calls.lua`), `luaD_poscall`, `luaH_resize`. The guard was cut down for them
(above); a loop whose trip count is usually below the factor gains nothing from
the main loop, and the pass can't know that.

On the 9700X (`measure-tier1-programs --runs 5 --variant`, medians), every
build's speedtest1 hash and Lua output match clang -O0's, and `+unroll` against
`--tier1` costs speedtest1 +1.40% user cycles with 0.17% fewer instructions,
and Lua's five scripts +0.35% (closures +2.02%, arithmetic −1.07%). Each is one
code placement, and growing some functions moves every one after them: relinked
at four placements (speedtest1's object after 0, 528, 1,568 and 2,608 bytes of
`.text`) and six link orders (Lua's objects), seven rounds each, speedtest1
averages +0.51% cycles (from −0.12% to +1.26%) and Lua's five scripts −0.48%
(from −2.34% to +0.95%; closures' +2.0% was its placement). Both are within
their placements' spread; Lua's calls (+0.5%) follow their guard's +0.73%
instructions.

### Compile cost and code growth

On the 9700X (`measure-compile --runs 11 --variant --tier1-passes=+unroll`, one
lane, the record's `9700x/compile/`), both inputs' default objects are the base's
byte for byte, and `+unroll` costs:

| Input | Loops | Unrolled | Default | `+unroll` | Wall | Instructions | Text |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `sqlite3.c` | 2,748 | 53 | 636.2 ms | 650.2 ms | +2.20% | +1.95% | +0.77% |
| the compiler's `main.c` | 2,448 | 297 | 1,270.1 ms | 1,327.9 ms | +4.54% | +4.55% | +3.20% |

The largest refusals on `sqlite3.c` are other exits (834), calls (635), outer
loops (526), predicates (302) and preheaders (222).

Under callgrind (a baseline-host build, `sqlite3.c`), Tier 1 executes 237.5 M
more instructions. The pass is 109 M of them: the dominators of the 1,018
functions with a phi 38 M, its scan and rewrite about 25 M, their CFGs 13 M, and
zeroing its arrays. The sink and optimizer that follow take 69 M more and the
back end 44 M more, for the bigger functions. Skipping the CFG and dominators of
a function with no branch backward in block order (a test with twins) took off
3–4 M, since 729 of the 1,018 have a loop and carry the analysis' cost, so it
wasn't kept; reusing a loop analysis another pass made would need its
invalidation tracked across if-conversion and LICM.

## What is left

- **Default.** The pass stays off until the owner decides on the record above.
- **Forms.** Other countdown widths, steps and bounds, other `!=` tests,
  pointer inductions and early exits each need their own proof.
- **Registers.** The pressure rule exists because Tier 1's leaf code takes no
  callee-saved register; giving a busy leaf loop those registers is the
  allocator's change ([#2819](https://github.com/buster14a/buster/issues/2819)),
  and would let more accumulators pay.
- **The vectorizer's main loops**, as above.
- **The back end's floating-point code**: folded loads
  ([#2829](https://github.com/buster14a/buster/issues/2829)), which would let
  `i32_to_f32` and `saxpy_f32` gain as clang's unrolled loops do, and VEX forms
  and `vzeroupper` ([#2830](https://github.com/buster14a/buster/issues/2830)).

The implementation is original. LLVM's [loop terminology](https://llvm.org/docs/LoopTerminology.html)
was consulted for the preheader, latch and exiting-block definitions; no
implementation source was read or imported.
