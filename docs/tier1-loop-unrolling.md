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
main loop's bound is computed once in the preheader, with its wrap at the
induction type's limit ruled out; inside it, each copy's own exit test is known
to pass and becomes a jump, copy k's induction is the main induction plus k
steps (folded into its addresses' displacements), and an exact integer
reduction is split into accumulators that the main loop's exit combines. Every
step that looks at many instructions has a vector path and an exact scalar
twin, and the two give identical functions. On the 9700X, RESULTS.

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
| The loop continues while the other is `ult`, `ule`, `slt` or `sle` than it, in either operand order and on either edge. | `predicate` |
| The other is a header phi of an integer type (8 to 64 bits), or its latch value, which adds it a positive constant step of at most 65,536 (read through copies, extensions and truncations, as the lowering's `sext (const 1)`). | `induction` |
| Two copies' estimated cost fits the budget (below), and the loop has at most 1,024 instructions. | `size` |
| The function's growth stays within its budget. | `growth` |

These are the forms C's counted `for` loops take after promotion, threading and
if-conversion: the test at the top on the induction phi (`for`), at the top on
the increment, or at the bottom on either (a one-block loop, `do`/`while`). A
body may branch and join inside itself; its phis are copied with it. A loop that
counts down, steps by a variable, tests `!=` or exits early (#2743's `find_u8`)
is refused today; each is a later rule with its own proof, not a weakening of
these.

## The rewrite

Write the continuing test as `v P n`, where P is `<` or `<=` (signed or
unsigned), `v` is the induction phi `i` (d = 0) or its increment `i + s`
(d = 1), and `n` the bound. Iteration k of the loop returns to the header
exactly when `i + (k + d)s P n`. With the induction increasing and no wrap,
`f` whole iterations from `i` all return exactly when the last one does:
`i + (f - 1 + d)s P n`. So, with K' = (f - 1 + d)s for a strict test and one less
for a non-strict one, the main loop may run its `f` copies exactly when
`i < n - K'` in the test's signedness. The preheader computes that bound once:

```text
P:   ...
     low   = n < (least + K')          ; n - K' would wrap
     bound = select low, least, n - K'  ; least is 0, or the signed minimum
     guard = start < bound
     cbr guard, MH, MX
MH:  the main phis: the header's phis' copies, a split reduction's accumulators
     copy 1 of the body, ..., copy f
     next = i + f*s
     cbr (next < bound), MH, MX
MX:  phis of what P and the main loop pass on; split reductions combined
     br H
H:   the original loop, its phis taking MX's values where they took P's
```

Where the subtraction would wrap, no induction value is below the least value,
so the guard fails and the original loop runs alone. Every `i + ks` with k < f
is then below `n` without wrapping, in the test's signedness, which is what the
copies rely on. The original loop is the remainder (fewer than `f` iterations
left) and the fallback (the guard fails, or a zero trip count) at once, so there
is one copy of its legality, as [the vectorizer's design](tier1-loop-vectorization.md)
asks of its own remainder.

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
  registers alone ([the allocation guide](tier1-allocation.md)), nine of them,
  and a copy's own temporaries take two. Before this rule, `count_eq_u8`'s four
  accumulators spilled the bound and two of them and ran 0.72 times as fast
  locally as without unrolling.
- **Growth.** At most 4,096 instructions added to a function; a loop that
  would pass it is refused (`growth`). Each admitted loop adds about `f` times
  its instructions, its main and exit phis, and the preheader's seven or eight.

`IrUnrollOptions` (`factor`, `budget`, `accumulators`, `growth`) holds them;
`ir_unroll_options` gives the defaults above, and the tests run others.

## With vectorization and LICM

The pass runs after if-conversion and `+licm`, before the operand sink and the
optimizer, whose SCCP, dead-code removal and merges clean the copies up: the
copies' dead tests and increments go, and their blocks merge into one where the
body was one. LICM first moves invariant computations into the preheader once,
not into every copy; the unroller doesn't move anything itself.

[The loop vectorizer's design](tier1-loop-vectorization.md) puts that pass
before this one. Its vector main loop is then this pass's input: a counted
loop whose step is the vector's lane count and whose bound its preheader
computes, which this pass admits like any other, its vector reductions carried
(the reduction rules take integer scalars; a vector accumulator split is the
same rule over vector `add`, and is that pass's to add with its own tests). Its
scalar remainder runs fewer than `w` iterations, and unrolling it can only add
code: the vectorizer is to mark its remainders' headers, which this pass will
refuse by that mark. Neither pass's legality rules depend on the other's.

## Data-parallel parts and their twins

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

Each has a scalar twin, the reference the tests compare against, and a host
without the whole AVX-512 set takes the scalars. The rest is per loop and
small: its blocks, its header's phis, its test and its liveness walk, bounded by
1,024 instructions, and the CFG and phi construction of its copies. The
vectorized parts are the ones that run over every instruction of a function
(the classes) or of every copy (the remap).

## Tests

- **`test-ir-unroll`** builds loops in the IR's text form from a builder that
  names values, parses them, and runs each by both twins: the same function,
  outcomes and counts, scratch returned. An unrolled function must verify with
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
- **Mutations.** An off-by-one in the unsafe direction of the offset, the
  missing `d`, a wrong `and` identity, a displacement without its scale and an
  unclamped bound each fail it. Testing `<=` as `<` passes, as it should: the
  main loop then leaves one iteration early, to the remainder.
- **`test-ir-optimize`** runs `tests/ir/optimize/unroll-loops-*.ir`: a store
  loop and a sum, the reviewed after text byte for byte, both twins and the
  executor.
- **`test-ir-source --passes`** runs both twins on every function of
  c-testsuite's subset, the frozen `sqlite3.c` and musl, as Tier 1 gives them
  to the pass (the optimizer's form with truth values simplified): all agree;
  5, 26 and 45 functions unroll.
- **`test-checker-driver`'s passes fixture** builds with `+unroll`, both paths,
  `--verify`, and prints clang's output; `+all` unrolls too and the default
  doesn't.

## Measurements

MEASUREMENTS

## What is left

- **Default.** The pass stays off until the owner decides on the record above.
- **Forms.** Count-down loops, `!=` tests, pointer inductions and early exits,
  each with its own proof.
- **Registers.** The pressure rule exists because Tier 1's leaf code takes no
  callee-saved register; giving a busy leaf loop those registers is the
  allocator's change, and would let more accumulators pay.
- **The vectorizer's loops**, as above, once that pass exists.

The implementation is original. LLVM's [loop terminology](https://llvm.org/docs/LoopTerminology.html)
was consulted for the preheader, latch and exiting-block definitions; no
implementation source was read or imported.
