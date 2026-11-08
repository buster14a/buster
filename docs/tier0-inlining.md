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

#1398's kernels run 4.8 to 29 times clang -O0 at Tier 0 on the 9700X. Tier 1,
which inlines these wrappers since #1282, runs three of them within 1.1 to 1.5
times clang -O2. The fourth, a masked count, is 5.1 times, which the issue puts
down to #1374's `popcount`.
Tier 0 is what `-c` and the direct path use by default, and what the self-host
qualification (`tools/ci/selfhost.c`, #1111) builds the compiler with: a
compiler built by itself runs its own vector lexer, refinement and checker
kernels through these calls.

A Tier 0 `-c -march=znver5` of #1398's `k_adds`
(`_mm256_storeu_si256(d, _mm256_adds_epu8(loadu(a), loadu(b)))`), by a build of
`77fb9128` with #1754 (whose `-H` doesn't touch code generation), is 45
instructions with four calls in the caller. `_mm256_adds_epu8`'s own copy is 21
instructions, its `ret` among them: a realigned frame, two parameter stores, two copies into
the vector stack's slots, the `vpaddusb`, and three moves of the result through
its slot and its result object into `ymm0`.

## Goals and non-goals

Goals:

1. Tier 0 targets calls of the unit's internal `always_inline` functions,
   the same candidate set as Tier 1 (`c_check_tier1_inline.c`), subject to the
   first version's eligibility, cycle and depth limits below. The x86
   intrinsics' wrappers are the target; measure their coverage explicitly.
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
- **Recursion.** The first version keeps calls on recursive cycles, even where
  clang may inline some of them. This conservative rule makes every expansion
  independent of which caller or lane first requests it.
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

Before accepting a site, also prove that the call row and callee definition
have compatible argument counts, value classes and widths, aggregate sizes
and layouts, and result conventions. Use the checked source signature and ABI
records to account for promotions and hidden parameters; if that information
does not establish the bindings, keep the call. A matching symbol alone is not
enough, and `tier0_validate` cannot prove that a well-formed binding preserves
the original call semantics. The existing Tier 1 path checks its own signatures
(`ir_inline_signature`); the Tier 0 check must cover its aggregate bindings too.

A callee whose stream fails a callee-only rule is never inlined, and its calls
stay calls. That decision is made once per callee before any caller is spliced;
site-specific signature and target checks remain per call.

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

A vector result of 16, 32 or 64 bytes uses the register placement in the
call row: a wide vector can be split across XMM or YMM registers when the
target lacks a register of its full width (`Tier0Function.result_size`). A
result actually classified `MEMORY` keeps its call in the first version; do
not infer that classification from vector width alone.

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
  operations (`Tier0Operation` rows), its x87 constants and its vector
  constants together with their `vector_constant_bytes` width column;
- **once per site:** its call rows (`call_targets`, `call_first`,
  `call_argument_counts`, `call_variadic`, `calls`, `call_result_objects`,
  `call_tails`) and their arguments' rows (`argument_locations`,
  `argument_classes`, `argument_sizes`). The contract doesn't promise that a
  row has only one `CALL`, and a copy per site doesn't need it to.

Optional per-local and per-call columns need a full output span if any input
uses them. For a missing `local_sizes`, `local_alignments`, `argument_sizes`,
`call_result_objects` or `call_tails` column, materialize zeros for that input
segment rather than omitting the other segment or reading a null pointer.
Preserve column-level constraints too: if the resulting optional-column shape
is invalid for the caller, validation keeps the original call.

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

- **Indirect control flow.** The caller may have labels and jump tables even
  though a copied callee may not. Rebase every `label_entries`, `table_jumps`
  and optional `table_addresses` entry through `start`, including an
  end-of-function label; preserve a null `table_addresses` column. Retain
  `label_tables`, whose values are table identities rather than entry indices.
  A valid stale index can silently select the wrong entry, so validation alone
  cannot establish this mapping is correct.
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
refuses `G` or the deterministic per-caller expansion budget is exceeded, the
caller compiles `F`, every site of it a call, which the reference count of
"The callee's own code" then sees. Scratch is reserved before code emission;
reservation failure disables this optimization for the whole unit.

## Callees that call callees

`<immintrin.h>`'s masked forms call unmasked ones:
`_mm256_mask_add_epi32(W, U, A, B)` is
`__builtin_ia32_selectd_256(U, _mm256_add_epi32(A, B), W)`. A caller's site
therefore splices the callee's **expanded** stream: the callee's own stream with
its sites already spliced, so that one level of splicing at the caller takes
everything.

Build the eligible direct-call graph once in deterministic body and site order.
Find its strongly connected components; a self-edge and each edge whose ends
are in the same nontrivial component remain calls. Other edges form a DAG.
This conservative policy removes a choice that otherwise depends on the
active walk: in an `A -> B -> A` cycle, expanding `A` first and expanding `B`
first would leave different closing calls. Retaining all edges within that
cycle is deterministic and preserves the original calls.

Each lane keeps a cache of expanded callees, as #1282's keeps prepared ones
(`CheckTier1Lane.inline_cache`), but keys each entry by both callee body and
remaining inline depth. A caller starts with `BUSTER_TIER0_INLINE_DEPTH` (16)
levels; expanding a site consumes one. At zero remaining levels its calls
stay calls. A cached stream is computed solely from its body, the immutable
cycle-edge policy and its remaining depth, never from the requesting lane,
caller or previously prepared entries. An explicit-stack walk over the DAG
prepares dependencies before their users (the coding style rules out recursion).
A failed eligibility check or validation keeps the affected site as a call.
Cached failures have the same key and must not depend on transient scratch
exhaustion.

A body can therefore have up to 16 cached variants per lane. Bound expansion
growth by a deterministic per-caller budget, computed the same way on cache
hits and misses; a caller over budget keeps its original stream and references.
The cache is an optional bounded acceleration: eviction or a cold cache must
not change that decision. Reserve the required lane scratch before starting the
code epoch; if that reservation fails, disable this optimization for the whole
unit before any body is compiled, rather than making a lane-dependent choice.
The tests compare exact output across lane counts, caller orders and cold and
warm caches, including recursive pairs and a callee reached at different
depths. `<immintrin.h>`'s chains are a few
calls long (a masked form, its unmasked form, a `_mm*_set*` under that); the
implementation counts them on clang 23's headers.

## Data-parallel: the splice's vector path and its twin

Sites are rare among a caller's entries. The work is copying `F`'s entries between them and the callees' entries
at them, and rebasing values, all of which is wide and branch-free. In
`tier0_inline.c`, beside `tier0.c`:

1. **Sites.** 64 entries at a time, `vpcmpeqb` of the kinds against `CALL`
   gives a mask. Each set bit (`tzcnt`) reads its row's target and the caller's
   callee table (by `SymbolId`, as `check_tier1_inline_table`'s), and appends
   the entry to the site list if the callee is ready. The site list, in entry
   order, has each site's growth: its bindings, body and result binding, less
   the `CALL`.
2. **Starts.** `start[i] = i + growth(sites before i)` is a step function. It is
   written one segment between two sites at a time, by `iota + delta` stores of
   16 lanes. The scalar twin writes it entry by entry.
3. **The caller's segments.** `kinds` and `classes` are copied 64 bytes at a
   time (masked at the tail), and `values` 8 at a time. The lanes whose kind is
   a branch (a `vpermb` of the kind through a 64-byte table of meanings) gather
   `start[value]` (`vpgatherqq`, masked: a stream's branches are a small share
   of its entries).
4. **The sites.** The bindings are a few scalar writes. The callee's expanded
   body is copied as a segment is, and its values rebased: a `vpermb` of the
   kind through the meanings table gives each lane's meaning, `CONST`'s turned
   into the x87 or vector constants' meaning by a compare of the class; a
   `vpermq` of the copy's bases by meaning gives each lane's base, and a masked
   add applies it. A meaning of none adds 0.
5. **Tables.** Appended with `memcpy`, then rebased in place the same way:
   `call_first` by the site's argument rows' base, and `call_result_objects` by
   the locals' base where it isn't 0.

The scalar twin (`tier0_inline_scalar`) does each step an entry at a time, with
a `switch` over the kind for the meaning. Both give identical bytes in every
column, which the tests compare. The tests' reference rebases from the
contract's text, written in the test, and doesn't use either path's table.
`--scalar` runs the twin, so `test-c-testsuite`'s comparison of the two paths'
executables compares them too.

Every load is masked to its array or reads within its padding, and the
tests put every column's end at a `PROT_NONE` page, as `test-tier0` does for
the validator (#569).

## Where it runs in the emission

`check_program_emit_lanes` lowers, compiles, sizes and encodes each function as
lane items (#151). #1282 already splits that into two epochs when Tier 1 inlines
(`CheckEmitRun.tier1_inline`, `CHECK_EMIT_HALF_LOWER` then
`CHECK_EMIT_HALF_CODE`), so that every callee's typed stream exists before any
caller's code. Tier 0's inlining takes the same schedule:

- **The table.** `check_tier1_inline_table` becomes the shared
  `check_inline_table`, built once a unit for either tier. A unit with no
  always_inline function of internal linkage gets none, and runs one epoch as
  now: no barrier, cache or allocation, and no new code on its path.
- **Lowering epoch.** Every part is lowered, as for `--tier1`.
- **Code epoch.** For each body of its part, a lane expands the callees the
  body's sites need (its cache), splices the body into `G` in the lane's
  scratch, and compiles `G` by Tier 0. A body without a site compiles `F`
  directly, without a copy.
- **Merge.** The calling thread adds the atoms in source order, as now, but
  skips each callee that nothing still needs (below).

`--tier1` compiles a body by Tier 1 first, from `F`, with #1282's inliner. Only
a body Tier 1 refuses comes to Tier 0, and the splice then gives it the same
calls inlined. `--tier0-no-inline` (the control, as `--tier1-no-inline` is)
compiles every `F` as it is, every site a call.

Holding every part's lowered stream until the code epoch costs memory, which
#1282 pays already under `--tier1`. A cheaper schedule lowers only the callees
before the parts, and lowers each other body in its part's code epoch. The
first version takes #1282's schedule, which exists and is tested, and the
measurements decide whether the other pays.

## The callee's own code

A callee is emitted only if something still references it after the splices,
as clang emits none of an always_inline function it inlined everywhere. Before
the merge adds the atoms, one pass counts the references to each callee's
symbol:

- the `LinkReference`s of every compiled body's code but the callee's own: a
  kept call (`CALL`, a site that stayed a call), an address (`GLOBAL_ADDRESS`),
  and those that inlined copies brought in;
- the data atoms' fixups (a function pointer in a static initializer);
- the symbols that the inline assembly's templates name (`check_program_emit_asm_symbols`).

A callee with no reference, that isn't one of the selection's roots
(`check_program_select`: `used`, `retain`, a constructor or destructor, an
alias's target), isn't added: no atom, symbol, `-g` rows or unwind table
entry. The count is a vector scan of the reference columns into a bitset by
symbol, deterministic whatever the lanes' order. A callee's code is still
compiled in the code epoch, since it's a part's body like any other. Dropping
it at the merge wastes that small compile in exchange for no third epoch.
`--lower-all` (#129's test option) keeps every callee.

The `--symbols` comparison with clang -O0's objects (`c_lowering_symbols.c`)
documents a local always_inline function as a difference (`inlined`). Once
callees are dropped, its Tier 0 objects define what clang's do. The reason stays
for `--tier0-no-inline`, and for the callees that keep a call, which clang's
code would also call.

## What the first version leaves, and later steps

What each `k_adds` intrinsic costs after the first version, counted from the
current code's patterns:

- **The call is gone:** the call and return, the callee's prologue, frame
  realignment and epilogue, the ABI's moves into `rdi`, `ymm0` and `ymm1`, and
  the saves of the caller's values around the call (`(%rsp)` in `k_adds`).
- **The objects are not:**
  - the caller's argument temporaries;
  - each parameter's binding (a `LOAD` of the argument's 32 bytes into the
    vector stack's slot, a `SET_LOCAL` into the parameter's object), and its
    read back;
  - the callee's result object, and the binding's copy of it into the caller's
    result object.

That is roughly half of today's instructions per intrinsic, against clang
-O0's 11 or 12 (its `k_adds` is 46 for four). The measurements below decide
it. Three later steps take the objects out, each its own pull request:

1. **Argument objects forwarded.** A vector argument's entries are the
   lowering's temporary `t` (`SET_LOCAL t` of the value, then `LOCAL_ADDRESS t`
   as the argument). Where the callee only reads its parameter `p` whole
   (`LOCAL p` of its class: never written, its address never taken), the
   binding is dropped, and the copy's `LOCAL p` reads `t` instead, through a
   per-local remap rather than one base. Finding each argument's root entry
   takes the stream's depths, which Tier 0 already works out 64 entries at a
   time (`tier0_compile`'s effects and depths), so the splice can share that
   pass.
2. **Result objects forwarded.** The callee's `RETURN` operand is
   `LOCAL_ADDRESS r` of the temporary its last statement filled. The copy
   writes the caller's `R` in place of `r`, through the same remap, and the
   result binding goes.
3. **More callees:** multiple `RETURN`s (each a binding and a `JUMP` to the
   copy's end, which `tier0_validate` checks at the join), results in memory
   (the caller's `R`'s address for the hidden pointer), and structs of more than
   256 bytes (a `COPY` in pieces).

With the first two, an intrinsic's copy is about clang -O0's: its operands'
loads, the operation and a store, each through Tier 0's vector slots. Keeping
Tier 0's vector values in registers, rather than in a slot per depth (#780),
would be the next cut. That is a Tier 0 change for every vector program, not
this one's, and needs its own issue.

## Tests

- **`test-tier0-inline`** (new, `tier0_inline_test.c`):
  - callers and callees from `tier0_generate.c`'s programs, with some of their
    callees marked inlinable: both paths' streams byte for byte, `tier0_validate`
    of each, and each program run natively with and without the splice on the
    generator's eight inputs, which must give the same results and globals;
  - hand-written fixtures for every row of the binding and result tables,
    mixed-width vector constants and their width column, scalar callers with
    aggregate callees and the reverse (including null optional columns),
    forwarding and its near misses, nested callees, recursive cycles entered
    from different callers and orders, the same callee at different remaining
    depths, cold and warm caches, the depth bound, a
    callee of every refused kind (each keeping its calls), compatible and
    refused call-signature shapes including promoted arguments and aggregate
    results, a target the caller lacks, and a callee shared by sites inside a
    loop and inside another site's arguments;
  - callers with a splice before a computed-goto label and a jump-table
    dispatch, including an end label and a non-null `table_addresses` column;
    verify all mapped metadata and native results against the original stream,
    including stale indices that would validate, and preserve a null column;
  - every column's end at a `PROT_NONE` page, and a lane short of scratch: each
    failure must keep the caller's `F` and count its kept calls.
- **`test-checker-driver`:** #1282's inlining fixture (intrinsics, a struct
  helper and a `noinline` function) at Tier 0 too, by each path, with and
  without `--tier0-no-inline`, against clang -O0's run. A fixture with
  `-march` variants checks that a callee of a feature the caller lacks keeps
  its call, and gives clang's error where clang does.
- **`test-check-lanes`**, **`--id-order`** and **`test-c-testsuite`:** the
  outputs are one lane's, any identifier order's and the scalar path's, as now.
- **`--symbols`:** a corpus's local always_inline functions match clang -O0's
  objects (no `inlined` difference) but under `--tier0-no-inline`.
- **`test-x86-intrinsics`:** unchanged. Its cases take each intrinsic's address,
  which keeps the wrapper.
- **`test-check-debug --cross-check`:** a program of intrinsics by `-g`: gdb's
  steps and backtraces, and `llvm-dwarfdump --verify`.

## Measurements

On the 9700X, under the lock, CPU 2, a `znver5` build, before and after:

- #1398's four kernels at Tier 0 against clang -O0 and -O2: each step's effect,
  with and without `--tier0-no-inline`.
- Compile time, by `measure-compile`, the `--time-stages` laps and a new
  inlining lap: `sqlite3.c` (no callee; must not move) and the compiler's
  `main.c` at `-march=znver5`, whose sources spell 12,104 intrinsic calls of 858
different intrinsics (a `grep` of `src/buster/compiler/*.c`). The splice and the second
  validation against the Tier 0 work that the calls' code no longer needs.
- `main.c`'s object: its size, and the callees it no longer defines.
- The self-built compiler (`tools/ci/selfhost.c`'s build): `benchmark-lexer`,
  `benchmark-refinement` and `benchmark-files` through it against the
  clang-built one, as the issue suggests. This is the number the issue is
  about.
- The splice's own kernels: `benchmark-tier0-inline [--only vector|scalar]
  [--after-scalar MS]`, ns and cycles per copied entry, on `main.c`'s callers.

The record goes to buster-performance, as AGENTS.md asks.

## Plan

1. **The splice** (`tier0_inline.c`, its twin, `test-tier0-inline` and its
   benchmark): standalone, used by no driver yet.
2. **The emission:** the shared inline table, cycle policy and depth-keyed
   per-lane cache, the code epoch's splice, `--tier0-no-inline`, the driver
   fixtures, and the measurements above.
3. **Dropping callees**, with the `--symbols` changes.
4. **Forwarding** argument and result objects (later steps 1 and 2), each
   measured.
5. **More callees** (later step 3), as the corpora need them.

## Open questions

- **Tier 1 for such bodies instead?** The issue offers routing a body with
  always_inline sites to Tier 1, which inlines them already. Tier 0 is the
  default because it compiles fast, and a body of intrinsics is common in the
  repository's own code. Tier 1 compiles it several times slower and refuses
  more. This design keeps such bodies in Tier 0. The measurements give the
  numbers to revisit that.
- **External always_inline functions.** Clang inlines those too and still
  emits them. #1282 takes only internal ones. Taking external ones changes
  nothing in the splice, but should change both tiers together.
- **One schedule or two.** #1282's schedule holds every part's stream until the
  code epoch. If that shows in `main.c`'s peak resident set, a callee-only
  lowering round is the alternative (see "Where it runs in the emission").
