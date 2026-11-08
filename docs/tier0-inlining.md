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
