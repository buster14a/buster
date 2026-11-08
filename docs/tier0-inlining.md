# Tier 0 inlining of always_inline functions (#1398): design

Status: design only. Nothing here is implemented yet; the issue's claim and this
document's revisions are on [#1398](https://github.com/buster14a/buster/issues/1398).
The names of existing code below are main's at `8a29955f`.

## The problem

Clang inlines every call of an `always_inline` function whose body it has, at
`-O0` too (LLVM's `AlwaysInliner`). It then emits none of an internal function
whose calls it all inlined and whose address nothing took. Tier 0 inlines
nothing: it calls a local copy of the function, which the object defines
(`docs/c-lowering.md`, "Against clang -O0's objects", the `inlined` row).

For most code that costs a call. For the x86 intrinsics it is nearly the whole
cost. Every `_mm*_*` function of `<immintrin.h>` is a
`static __inline__ __attribute__((__always_inline__, __nodebug__, __target__(...)))`
wrapper around a builtin, so every intrinsic is a call, and every vector argument
and result goes through the call's objects:

- the caller copies the argument into a temporary object and passes its address;
- the callee's prologue stores the register into its parameter object;
- the callee stores the result into a result object, loads it into `ymm0`;
- and the caller stores `ymm0` into its own result object and copies that again.

#1398's kernels run 4.8 to 29 times clang -O0 at Tier 0 on the 9700X; Tier 1,
which inlines these wrappers since #1282, is within 1.1 to 1.5 times clang -O2.
Tier 0 is what `-c` and the direct path use by default, and what the self-host
qualification (`tools/ci/selfhost.c`, #1111) builds the compiler with: a
compiler built by itself runs its own vector lexer, refinement and checker
kernels through these calls.

A Tier 0 `-c -march=znver5` of #1398's `k_adds`
(`_mm256_storeu_si256(d, _mm256_adds_epu8(loadu(a), loadu(b)))`) at `77fb9128`
is 45 instructions with four calls in the caller. `_mm256_adds_epu8`'s own copy is
20 instructions: a realigned frame, two parameter stores, two copies into
the vector stack's slots, the `vpaddusb`, and three moves of the result through
its slot and its result object into `ymm0`.

## Goals and non-goals

Goals:

1. Tier 0 inlines a call of a unit's `always_inline` function of internal
   linkage wherever Tier 1's inliner would (`c_check_tier1_inline.c`), so the
   two tiers agree on which calls are calls. The x86 intrinsics' wrappers are
   the target, and every one of `<immintrin.h>`'s must qualify.
2. A function whose every call was inlined, and whose address nothing takes, is
   not emitted, as clang emits none.
3. The output stays deterministic: one lane's bytes on any number of lanes, the
   vector path's bytes on the scalar path (`--scalar`), and any identifier order
   (#153).
4. Units without a candidate pay nothing: no new pass, barrier or allocation.
   `sqlite3.c` is such a unit.
5. Inlining is an optimization, never a refusal: anything it can't do keeps the
   call, as Tier 1's does.

Non-goals, each its own follow-up:

- **Keeping vector values in registers at Tier 0.** A Tier 0 vector value lives in
  a 64-byte slot of the frame by its depth (#780), so an inlined wrapper still
  stores and loads each value. Inlining removes the calls, the frames and the ABI
  moves around them, not the vector stack's slots.
- **Functions that aren't always_inline.** Clang -O0 inlines none of them either.
- **Debug information for inlined code** (`DW_TAG_inlined_subroutine`): #1114's.
- **Tier 1.** Its inliner works on the IR, after the lowering, and is unchanged.

## What clang does, which this follows

- **Which calls.** A direct call of a function that has `always_inline` and a
  body in the unit. A `noinline` beside it wins: clang inlines neither way, and
  the checker records a function with both, or neither, as no callee
  (`CheckProgram.object_inline`, `BUSTER_CHECK_PROGRAM_INLINE_ALWAYS`). Calls
  through a pointer are calls. A variadic callee is a call: LLVM can't inline a
  function that calls `va_start`.
- **Recursion.** An always_inline function that reaches itself is inlined until
  the cycle closes, and the call that would close it stays a call.
- **Targets.** A callee whose target has a feature its caller's lacks is
  clang's error ("always_inline function '...' requires target feature '...',
  but would be inlined into function '...'"), which the checker's driver already
  gives (`c_checker_driver.c`). Inlining needs the caller's features to include
  the callee's, as Tier 1's does (`check_tier1_inline_target`). In a unit
  without that error, it always does.
- **The callee's own code.** It is emitted only where something still needs it:
  its address is taken or a call of it stays a call. A function of external
  linkage is always emitted, and #1282's Tier 1 takes only internal ones: Tier 0
  takes the same set.
- **Debug information.** Clang gives an inlined call's instructions the
  callee's lines in the header, under a `DW_TAG_inlined_subroutine`. That is
  #1114's.

## Where: on the typed stream, after the lowering

The lowering turns each function's checked graph into a typed stream
(`Tier0Function`: the entries' kinds, classes and values, and the locals', calls'
and operations' tables). Tier 0 compiles that stream, and Tier 1 lowers it into
the IR. There are three places an inliner could go.

1. **The checked graph, before the lowering.** Every node of the callee would be
   copied into the caller's graph with its types and bindings remapped, which
   the checker's columns aren't built for. Every later stage would see the
   larger graph, and the lowering would lower each copy again.
2. **The lowering, which would lower the callee's body inline at the call.** The
   lowering is a task interpreter over one body at a time
   (`c_check_program_lower.c`), and #1048 is replacing it with node templates.
   Inlining there would tie this work to that rewrite, and it would lower every
   call site's copy again.
3. **The typed stream, between the lowering and Tier 0.** Each callee is lowered
   once, as now. A caller's stream is rewritten by splicing in the callee's
   entries for each call, with the callee's locals, calls, operations and
   constants appended to the caller's tables, and its indices rebased.

This design takes the third. The typed stream's contract
(`tier0.c`, "The kinds") is small and fixed. Every entry's `value` column has one
meaning per kind and class, so the splice needs no knowledge of C.
`tier0_validate` checks the result exactly as it checks the lowering's output, so
a splice that would break the contract is caught and the call kept. It is also
where Tier 1 reads its callees: #1282's lowers every body before any body's
code (`CheckEmitRun.tier1_inline`), and its lanes read the callees' typed
streams.

## Eligibility

A **callee** is a body whose function is `BUSTER_CHECK_PROGRAM_INLINE_ALWAYS` and
of internal linkage, as `check_tier1_inline_table` finds them, and whose lowered
stream (`CheckProgramLowered.functions[b]`) has none of:

- `incoming_variadic` (a variadic definition), `returns_twice`, `eh_return`,
  `unwind_init`, `interrupt`, `preserve_general` or `preserve_vectors`,
  `realign_stack`, or a naked body;
- labels or jump tables (`label_count`, `table_count`), `ALLOCA`, `STACK_SAVE`,
  `STACK_RESTORE`, `FRAME_ADDRESS`, `TAIL_CALL` or `JUMP_INDIRECT`. These are
  about the callee's own frame or code addresses, which an inlined copy doesn't
  have;
- more than one `RETURN`, or a `RETURN` that isn't its last entry. This is the
  first version's rule, and every `<immintrin.h>` wrapper keeps it. The
  "Later steps" section lifts it.

A **call site** is a `CALL` entry of the caller whose call table row names a
callee (`call_targets[c]` a callee's symbol), with `call_variadic[c]` clear and
the caller's target including the callee's. Every other call stays a call.

A callee whose stream fails any rule is never inlined, and its calls stay calls.
That is a property of the callee alone, so it is decided once per callee, before
any caller is spliced (below).
