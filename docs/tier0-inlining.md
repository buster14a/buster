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

## The splice

The splice reads a caller's stream, `F`, and the expanded streams of its sites'
callees (see "Callees that call callees"), and writes a new stream, `G`. `F` is
left as it was: `--tier1` lowers its IR from `F`, and #1282's inliner does its
own inlining there.

### Binding the arguments

At a `CALL` the arguments are on the stacks, the first pushed first, and the
callee's parameters are its locals `0` to `parameter_count - 1`, in order
(`Tier0Function`: "the locals (parameters first, in order)"). The site's copy of
the callee therefore starts with one binding per parameter, the last first, each
taking the value on top:

| parameter | the argument on the stack | the binding |
| --- | --- | --- |
| a value of a class (`argument_sizes[a] == 0`) | the value | `SET_LOCAL p` of the class |
| an object of 4, 8, 16, 32 or 64 bytes of a vector, mask, F80 or I128/U128 local class | its address (U64) | `LOAD` of the class and size, then `SET_LOCAL p` |
| any other object (a struct or union by value) | its address (U64) | `SET_LOCAL t` (U64), `LOCAL_ADDRESS p`, `LOCAL t`, `COPY size` |
| an object of size 0 | its address (U64) | `DISCARD` |

`t` is one U64 local per caller for every copy's struct bindings. A struct of
more than `BUSTER_TIER0_MAX_COPY_BYTES` (256) keeps its call, so a binding never
needs the runtime's `memmove`. The table follows the parameters' own entries:
the `LOAD` and `SET_LOCAL` of a vector of an object of the vector's size are
exactly what the contract defines ("Vectors ... LOCAL, STORE_LOCAL and SET_LOCAL
(of an object of the vector's size, all of whose bytes they read or write)").

When the callee's first `n` entries are `LOCAL 0` to `LOCAL n - 1`, its
parameters' only reads, of value classes, the argument values are already where
those reads would push them. The bindings and the reads are both dropped
(**forwarding**). That is the shape of a wrapper of scalars and pointers,
`_mm_loadu_si128(p)`'s for one. A vector parameter's argument is an address,
not a value, so forwarding a vector argument needs the caller's side changed
too: see "Later steps".

### The result

The callee's last entry is its only `RETURN` (or, for a void function, its
stream ends without one, and its body is all its entries). The copy drops it, and
the call's result is bound as the call's row says:

| the callee's result | the binding |
| --- | --- |
| `VOID` | nothing |
| a value of a class | nothing: the `RETURN`'s operand is on the stack, where the `CALL` would have pushed its result |
| an aggregate in registers (`result_size > 0`, the `RETURN`'s operand its bytes' address, U64), at a call whose `call_result_objects[c]` is `1 + R` | of a vector local class's size: `LOAD` of it, `SET_LOCAL R`; else `SET_LOCAL t`, `LOCAL_ADDRESS R`, `LOCAL t`, `COPY result_size` |
| an aggregate in memory (`AbiReturn` `MEMORY`, a hidden pointer) | not a callee: it keeps its call (first version) |

A vector result of 16, 32 or 64 bytes returns in a register under the targets
that have that register. Under `-mno-avx` a `__m256` returns in memory, and its
wrappers keep their calls there, as do structs of more than 16 bytes.

### Rebasing the callee's entries

Each copied entry's `value` means one of a few things, by its kind and, for
`CONST`, its class (`tier0.c`, "The kinds"). The copy adds one base per meaning,
from a table:

| meaning | kinds | the base |
| --- | --- | --- |
| a local | `LOCAL`, `LOCAL_ADDRESS`, `STORE_LOCAL`, `SET_LOCAL` | the callee's locals' base in `G` |
| a local in the high bits | `ATOMIC_CAS` (from `BUSTER_TYPED_CAS_LOCAL_SHIFT`) | the same, shifted |
| an entry | `BRANCH_FALSE`, `BRANCH_TRUE`, `JUMP`, `JUMP_VALUE` | the copy's first body entry in `G` |
| a call row | `CALL` | the site's call rows' base |
| an operation | `X86` | the callee's operations' base |
| a constant | `CONST` of F80 (an `x87_constants` index), `CONST` of VECTOR (a `vector_constants` index) | the callee's constants' bases |
| none | every other kind and class (a symbol's id is the unit's, the same in both streams) | 0 |

A target equal to the callee's entry count, its end, becomes the entry after
the copy. Everything else in the callee's tables is appended to `G`'s:

- **once per caller and callee:** its locals (`local_classes`, `local_flags`,
  `local_sizes`, `local_alignments`; a parameter becomes a plain local), its
  operations (`Tier0Operation` rows) and its x87 and vector constants;
- **once per site:** its call rows (`call_targets`, `call_first`,
  `call_argument_counts`, `call_variadic`, `calls`, `call_result_objects`,
  `call_tails`) and their arguments' rows (`argument_locations`,
  `argument_classes`, `argument_sizes`). The contract doesn't promise that a
  row has only one `CALL`, and a copy per site doesn't need it to.

Sharing the locals between a caller's sites of one callee is sound because no two
of them are ever live at once. A site's locals live from its first binding to
its last entry. Its arguments, any other site among them included, are
evaluated before its first binding. A callee doesn't reach itself (the cycle
rule below). So the frame grows by each distinct callee's frame once, as a
call's would, rather than by each site's. The locals' alignment carries over: a
`__m256` parameter is an object aligned to 32, which makes a caller that had
none realign its frame (`Tier0Frame.alignment_log2`), as clang -O0's does.

### The caller's own entries

Every entry of `F` that isn't a site is copied as it is, but for the targets:
where `start[i]` is the first entry of `G` that entry `i` of `F` became, a
target `k` becomes `start[k]`, and the end, `count`, becomes `G`'s count. A
site's first `G` entry is its first binding, so a target at a call
(`f();`'s, whose `CALL` is the statement's first entry) reaches the copy's
start. The function's flags take the callee's: `vectors`, `masks`, `x87` and
`int128` are each the OR of the caller's and its callees'. `vector_bytes` is
the caller's: its target includes the callee's, so its vector transport
covers the callee's entries.

The other columns:

- **Lifetimes** (`lifetime_entries`, read only by `ir_lower`) are the caller's
  own, rebased by `start`. `G` reaches only Tier 0, which ignores them, but they
  keep `G` a valid stream.
- **Source tokens** for `-g` (`CheckProgramLowered.entry_tokens`): a copied
  entry keeps its token, and every entry of a site's copy takes the `CALL`'s.
  `c_check_debug.c` keeps rows only in the function's own file, so an inlined
  wrapper's lines from a header would have been dropped anyway. A step onto
  the call's line is what gdb shows of an intrinsic now too, with no
  `DW_TAG_inlined_subroutine` (#1114's).

Then `G` goes through `tier0_validate`, as the lowering's own streams do. If it
refuses `G`, or anything above runs out of room, the caller compiles `F`, every
site of it a call, and the callees' kept-call counters (below) count each one.

## Callees that call callees

`<immintrin.h>`'s masked forms call unmasked ones:
`_mm256_mask_add_epi32(W, U, A, B)` is
`__builtin_ia32_selectd_256(U, _mm256_add_epi32(A, B), W)`. A caller's site
therefore splices the callee's **expanded** stream: the callee's own stream with
its sites already spliced, so that one level of splicing at the caller takes
everything.

Each lane keeps a cache of expanded callees, as #1282's keeps prepared ones
(`CheckTier1Lane.inline_cache`): an arena, and a state per callee body
(unprepared, in progress, ready, refused) for the emission's generation. A
lane expands a callee when a caller's site first needs it, by a walk with an
explicit stack (the coding style's rule against recursion) over the callees
its sites reach:

- a callee whose sites are all ready is spliced into its expanded stream and
  marked ready;
- a site whose callee is in progress closes a cycle, and stays a call;
- a callee that fails eligibility, or whose expansion `tier0_validate` refuses,
  is refused, and its sites stay calls.

The depth is bounded by `BUSTER_TIER0_INLINE_DEPTH` (16). A site past it
stays a call; `<immintrin.h>`'s deepest chain is four. Each lane expands a
callee at most once an emission, and the expansion depends only on the callee
and the bodies it reaches, so every lane's expansion of it is the same bytes.
