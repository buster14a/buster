# Selecting definitions for lowering

`c_lowering.c` supplies the emission planner for #129, part of parser tracker
#126. It consumes resolved definitions and references, finds the definitions
reachable from the unit's roots, and returns them in source order. It has a
vector path and an independent scalar worklist. Neither path recurses.

This is a graph consumer. On the source path, `check_program_select`
(`c_check_program_select.c`) gives it the checked program's definitions, roots
and evaluated references, and `-c` and the direct path lower what it selects
("The source path" below). `c_lowering_symbols.c` compares what they emit with
`clang -O0 -c`'s objects on fixtures and corpora ("Against clang -O0's
objects"), and `benchmark-lowering-source` measures what it saves ("What the
selection saves").

## Producer and consumer

The semantic producer numbers definitions in source order after resolving
redeclarations and tentative definitions. One definition identity represents
one possible emitted function body or object, even when several declarations
refer to it. Its flags describe the already resolved emission policy; this
module does not reinterpret storage-class tokens or look up identifier names.

Roots include externally emitted definitions and definitions retained by
`used`, `constructor`, `destructor`, or an alias. An alias-target root means
the target body or object; publishing the alias symbol remains the link
producer's responsibility. Plain C99 inline-only definitions have no local
out-of-line emission. Their calls and addresses refer to the external symbol,
and their bodies do not add local dependencies merely by existing. The
producer must account for all declarations of a function and GNU inline
semantics before setting the flags. Clang documents the distinction in its
[C99 inline compatibility guide](https://clang.llvm.org/compatibility.html#inline)
and [language-mode guide](https://clang.llvm.org/docs/UsersManual.html#differences-between-various-standard-modes).

References record dependencies of emitted bodies and initializers. Local
evaluated references can reach another definition; unevaluated references and
references resolved to an external symbol do not. A blanket rule that drops
everything written inside `sizeof` would be wrong: evaluated variable-length
array bounds still contribute dependencies. The semantic producer must select
`_Generic` associations, handle constant-folded expressions, and resolve
calls and addresses before producing these columns. Static objects reachable
only from an unreferenced function remain unselected.

The output is a reached bitset and a compact array of selected definition
IDs in source order. A downstream function producer maps those IDs to checked
bodies for Tier 0 or IR lowering; an object producer maps them to checked
initializers and link definitions. Discovery order never becomes emission
order. Diagnostic checking is independent: integration must preserve the
requirement to check every body by default, as #155 requires.

Checked inline-only bodies must also remain available to a future Tier 1
inliner. Attribute any emission dependencies introduced by inlining to the
emitted caller, then prepare and compute reachability before emission. A changed
inline plan requires recomputing that closure; edges whose source is an
inline-only body are deliberately discarded during preparation. This out-of-line
selection is not a list of checked bodies that may be discarded.

## Graph preparation and traversal

`c_lowering_prepare` validates the bounded input columns and builds a stable
counting-sort adjacency index. Preparation belongs to the cost of using the
planner. The caller owns the arena and the prepared workspace; selections
reuse that workspace. No input column needs readable padding. Empty inputs
and partial vector batches are valid, and allocation failure is explicit.

`CLoweringInput` contains `definition_count` flags and `reference_count`
entries in each of the source, target and kind columns. All four columns use
`uint32_t` elements with their natural alignment. Source and local target IDs
are below `definition_count`; an external or unevaluated target may instead be
`BUSTER_C_LOWERING_NONE`. Neither count may be `UINT32_MAX`. Zero-length
columns may be null. Preparation copies the retained information, so the
producer can release its input columns after success. Failure preserves both
the arena length and the previous result.

For a nonempty input, the prepared `CLowering` owns a flags column,
`definition_count + 1` adjacency offsets, retained targets, a queue, a scalar
visited column, the reached bitset
and selected IDs. An empty plan has null buffers, including adjacency offsets.
Each solver reuses these buffers without allocating. The bitset has
`ceil(definition_count / 64)` words, with high unused bits zero;
only the first `selected_count` output IDs are valid. A workspace belongs to
one worker and must outlive every consumer of its selected IDs. The reported
`workspace_bytes` includes both paths' reusable storage and alignment;
`scratch_bytes` identifies its mutable traversal/output portion. These are
allocated capacities, not a count of bytes touched by every invocation.

`c_lowering_vector` keeps a compact frontier and a visited bitset. It batches
target membership checks and compacts the final selection in source order.
Sparse bitset writes serialize conflicts so duplicate targets cannot lose a
bit. A long dependency chain does not cause a full-edge scan at every round.
`c_lowering_scalar` uses an independent byte-visited worklist and supplies the
exact reference result. A select-all diagnostic path includes every eligible
local definition; it still excludes inline-only definitions.

## Validation and measurement

`./build/compiler test-lowering --clang` checks graph selection against an
independent oracle and checks manually modeled C emission fixtures against
Clang's object symbols. It exercises source ordering, cycles, duplicate
edges, empty inputs, vector tails, invalid contracts and resource limits.
The 9700X CI test list runs the Clang comparison.

`./build/compiler benchmark-lowering` measures deterministic synthetic graphs,
including an intrinsic-shaped small live set, broad fanout, chains and cycles.
Run each build, test and measurement through the shared 9700X lock; pin timings
to CPU 2. Selection-only measurements reuse the prepared graph; the complete
planner measurement additionally charges graph preparation and first touches.
Conditioned runs use a separate prepared plan immediately before a vector
sample, either running the scalar path or warming the vector path explicitly.

Options are `--shape all|intrinsic|fanout|chain|cycle|duplicates|large`,
`--definitions N`, `--only vector|scalar|prepare` and either `--before-vector MS`
or `--after-scalar MS`.
Each path has one warm-up and 21 samples. `prepare` means arena reservation,
first touches, validation, copying, adjacency preparation and vector selection;
result verification and arena teardown are excluded. Nanoseconds time the
operation, while the perf cycles/instructions also include the two clock reads.
Unavailable or multiplexed counters are reported as unavailable. Every measured
result is compared to a separate oracle outside the timed interval.
The default samples are unconditioned single selections; the checks between
samples do not establish sustained vector execution. Use separate processes
with `--before-vector 100` and `--after-scalar 100` for the two conditioned
states. There is no prepare-plus-scalar timing path, so the prepare result does
not establish a whole-planner improvement over scalar selection.

These measurements describe the planner, not backend lowering or a complete
compiler. The required SQLite/libc/c-testsuite emission comparison and
`main.c` counts and time saved require the source producer and downstream
lowering path described above.

[The qualification record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/README.md)
retains the exact compiler revision, host and binary identities, raw test
outputs and measurement conditions.

## The source path

`check_program_select` (`c_check_program_select.c`) runs after checking, on the
checked program (`check_program`, every body's control stream and references)
and the static data (`check_data_prepare`). Its definitions are the bodies and
the data objects, string literals and compound literals among them, numbered in
source order: a canonical object joins its redeclarations, and a prototype is no
definition. Its flags and edges:

- **Roots:** a definition with external linkage, unless it is inline-only; and
  one marked `used`, `retain`, `constructor` or `destructor`. A C99 inline
  definition without an `extern` declaration in the unit is inline-only, and so
  is a GNU `extern inline` one (with `gnu_inline`): its calls and addresses go to
  the external symbol.
- **Edges:** each body's evaluated references (calls, addresses and reads of
  static objects, from the checked expression walk: not `sizeof`, `alignof` or a
  `_Generic` association it doesn't select, and not the operands of `&&`, `||`
  and `?:` that a constant condition never evaluates), and each initializer's
  (its address constants, strings and compound literals). A body also selects
  its own block-scope statics, referenced or not, as clang emits every static
  local of a function it emits.

The selected bodies, data and the symbols they need become the emission's view,
in source order whatever order the traversal found them in. `--lower-all`
(`check_program_select_mode` with `C_LOWERING_ALL`) selects every definition that
may be emitted instead, for measurement: an inline-only definition is never one.
Every body is checked either way; only lowering depends on the selection (#155).

The emitter names the local symbols it makes as clang does (`function.name` for
a block-scope static, `.compoundliteral` for a file-scope compound literal, `.N`
for a repeated name), so that tools and the comparisons below read them alike.

## Against clang -O0's objects

clang emits no unreferenced static or inline function, even at `-O0`, nor an
object that only such a function refers to, and every static local of a
function it emits: `clang -O0 -c`'s object is #129's oracle. `c_lowering_symbols.c`
reads an object with the import path's reader and lists the functions, objects,
thread-locals and IFUNCs it defines, with their bindings: not the assembler's
local labels (`.L` names: string literals, which clang names `.L.str`), and a
local's name without a final `.N`, the suffix LLVM makes a repeated local name
unique with, by one counter for the module that string literals' names share,
which no other compiler reproduces. Two objects' lists compare as multisets.

- **Fixtures** (`test-checker`, in `c_check_program_select_test.c`): every
  emitted fixture's object is compared with `clang -std=gnu17 -O0 -c`'s,
  functions, objects and string literals (the strings of mergeable string
  sections). Besides the existing selection fixtures, one for each kind of
  definition: static functions referenced, unreferenced and referenced only from
  an unreferenced one; C99 inline-only, inline made external by an `extern`
  declaration, `extern inline` and static inline ones, used and unused; an
  inline-only definition called through its external symbol; GNU inline in both
  spellings; addresses taken in a body, in a global's initializer, a referenced
  static's and an unreferenced static's; objects referenced, unreferenced,
  tentative and reached through a chain of initializers; block-scope statics of
  emitted and unreferenced functions, two of them of one name; string literals of
  emitted and unreferenced definitions; and `used` functions and objects.
- **c-testsuite** (`test-c-testsuite --cross-check`): each program's `-c`
  object against `clang -O0 -c`'s, functions and objects. All 219 programs the
  backend compiles agree.
- **Corpora** (`test-checker-corpus --symbols`): each file the checker accepts,
  its object built as `-c` builds it, against `clang -O0 -c`'s of the file with
  the corpus's options. A file the backend refuses is counted with its feature.

On corpora, clang -O0 decides two things in its code otherwise than the
selection. The harness explains a difference with one of them only from the
checked program, never from a name, and fails on any other:

| Reason | Which side | What clang does |
| --- | --- | --- |
| folded | ours only: an object that a selected body reads, a const and not volatile scalar with an initializer | Reads its value as a constant (`CodeGenFunction::tryEmitAsConstant`), so nothing needs the object. musl's `math` constants (`pS0`, `pio2_hi`) and Lua's `HOOKKEY` and `CLIBS`. |
| unselected | clang only: a definition the selection reaches when it also counts the operands that a constant condition never evaluates (`CheckProgramOptions.unselected_operands`) | Emits a `?:` in a branch's condition as branches without folding a constant condition (`CodeGenFunction::EmitBranchOnBoolExpr`), so the dead arm's calls stay. musl's `isnan` and `signbit` call `__FLOAT_BITS` in the dead arm for a `double`. |

clang -O0 also drops a constant `if`'s dead arm and the string literals only it
refers to (c-testsuite's 00212, 00214 and 00215), which is why the corpora
compare no strings.

Results on the Zen 4 laptop, with clang 23.1.3 (`--target`'s configuration of
each corpus as `test-checker-corpus` runs it):

| Corpus | Accepted | Compared | Agreeing | Explained (folded, unselected) | Unexplained | Refused by the backend |
| --- | --- | --- | --- | --- | --- | --- |
| c-testsuite (220) | 220 | 219 | 219 | 0 | 0 | 1 (`long double`) |
| musl 1.2.5 `src` (Zig 0.14.1's copy), 1,524 files, `-std=c99 -ffreestanding` | 1,484 | 455 | 410 | 45 files | 0 | 1,029 |
| Lua 5.4.8 `src`, `-std=gnu99 -O2` | 33 | 13 | 11 | 2 files (2 folded) | 0 | 20 (`visibility`) |
| `sqlite3.c` | accepted | 0 | | | | 1 (`__int128`, then conversions between unsigned 64-bit integers and floating types) |

The backend's refusals are its features, not the selection's: most of musl's
are attributes (aliases, weak symbols, visibility), `long double`, parameters
aligned past 8 bytes and inline assembly.

## What the selection saves

```sh
./build/compiler benchmark-lowering-source [--passes N] [preprocessor options] file...
```

checks each file as the driver does, outside the time, then times in turns the
two selections and everything `-c` does after one (lowering, Tier 0, branch
sizing, encoding, the static data's publication and the object's bytes),
`--passes` (21) medians. It prints each file's functions and objects defined,
lowered and skipped, both medians and the time saved, then the totals over the
files that lower both ways. Lowering everything also lowers what nothing calls,
which the backend may refuse: then the file is reported with both outcomes.

The issue's case is `main.c`'s unit, whose intrinsic headers define about 5,200
inline functions of which the compiler uses about 50. It can't be measured yet:

- the checker refuses four of its expressions, three AVX-512 builtins outside
  [#148](https://github.com/buster14a/buster/issues/148)'s frozen own-main scope
  (`__builtin_ia32_prold512`, `__builtin_ia32_gathersiv16si`,
  `__builtin_ia32_vec_ext_v16qi`) and `#pragma clang loop`;
- Tier 0 lowers no vector types, so none of the compiler's own functions, nor
  the intrinsics they call, can be lowered either way.

Where it works today, selection lets units compile that lowering everything
can't: of musl's math files, the referenced definitions lower and all of them
don't, refused by `long double` helpers in `libm.h` that nothing calls.

[The planner's qualification record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/README.md)
retains the planner's original measurements; the source measurements are below.
