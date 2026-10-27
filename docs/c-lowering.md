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
a block-scope static, `.compoundliteral` for a file-scope compound literal, and
`.N` for a name that another symbol has already, made before it or given by the
source, an identifier or an asm label, as LLVM makes a name unique in its
module), so that tools and the comparisons below read them alike.

## Against clang -O0's objects

clang emits no unreferenced static or inline function, even at `-O0`, nor an
object that only such a function refers to, and every static local of a
function it emits: `clang -O0 -c`'s object is #129's oracle. `c_lowering_symbols.c`
reads an object with the import path's reader and lists the functions, objects,
thread-locals and IFUNCs it defines, with their bindings, but not the
assembler's local labels (`.L` names: string literals, which clang names
`.L.str`). Two objects' lists compare as multisets.

Names compare exactly but for the uniquing suffix of the ones a compiler makes.
LLVM takes a repeated name's `.N` from one counter for the module, which string
literals' names advance too, and the emitter from its own, so their numbers
differ. The checked source gives the comparison the bases of its made names, a
block-scope static's `function.name` and `.compoundliteral`, and its asm labels
(`lowering_names_of_input`): a local named by a base, `.` and N compares as the
base and the fact that it had a suffix, and every other name exactly, an asm
label above all, which is the user's name even where it ends as a suffix does
(`tag.1`, or `f.x.1` beside `f`'s statics). An identifier has no dot, so only a
label could spell a made name. `test-c-testsuite` compares every name exactly:
none of its subset's programs makes a name twice in clang's objects.

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
  emitted and unreferenced definitions; and `used` functions and objects. And
  the comparison's own check: asm labels `tag.1` and `tested.x.1` beside two
  statics `x` of `tested`, whose object must agree with clang's (`tested.x` and
  `tested.x.2` in both) and differ from it once a name in its string table is
  replaced: `tag.2` for the label, `tested.x` for the made `tested.x.2` (the
  suffix lost) and `tested.x.3` for the label `tested.x.1`. Constant `if`s'
  fixtures select their dead arms' callees, as Tier 0 lowers them, and their
  objects are compared under clang's model (`folded_ifs`, below) instead, and
  run: a chain of `sizeof` conditions, dead arms with a label, with an outer
  `case` and with their own `switch`, a true condition with an `else`,
  floating, pointer and side-effecting conditions, and an `else if` chain. And
  the reasons below on one fixture of each (`c_check_corpus_symbols_test.c`),
  through `check_corpus_measure` as `--symbols` runs it, must all be
  explained, and a function clang alone sees renamed must not be.
- **c-testsuite** (`test-c-testsuite --cross-check`): each program's `-c`
  object against `clang -O0 -c`'s, functions and objects. All 220 programs
  agree; the backend compiles every one since it lowers `long double`.
- **Corpora** (`test-checker-corpus --symbols`): each file the checker accepts,
  its object built as `-c` builds it, against `clang -O0 -c`'s of the file with
  the corpus's options. A file the backend refuses is counted with its feature,
  and `--symbols-minimum N` fails the run when fewer than N files' objects were
  compared. The 9700X job compares eight of the frozen musl `src` directories
  (`math`, `string`, `stdlib`, `fenv`, `legacy`, `process`, `signal` and
  `multibyte`: 354 files the backend lowers, 351 before it lowered inline
  assembly (#147), and every explained difference of musl's) with
  `--symbols-minimum 354`; `sqlite3.c`, which the backend refuses, compares
  none.

On corpora, clang -O0 decides three things in its code otherwise than the
selection. The harness explains a difference with one of them only from the
checked program, never from a name, and fails on any other:

| Reason | Which side | What clang does |
| --- | --- | --- |
| folded | ours only: an object that a selected body reads, a const and not volatile scalar with an initializer | Reads its value as a constant (`CodeGenFunction::tryEmitAsConstant`), so nothing needs the object. musl's `math` constants (`pS0`, `pio2_hi`) and Lua's `HOOKKEY` and `CLIBS`. |
| unselected | clang only: a definition the selection reaches when it also counts the operands that a constant condition never evaluates (`CheckProgramOptions.unselected_operands`) | Emits a `?:` in a branch's condition as branches without folding a constant condition (`CodeGenFunction::EmitBranchOnBoolExpr`), so the dead arm's calls stay. musl's `isnan` and `signbit` call `__FLOAT_BITS` in the dead arm for a `double`. |
| dropped | ours only: a definition that the selection of clang's model doesn't reach, the program with unselected operands counted and a constant `if`'s dead arm left out (`CheckProgramOptions.folded_ifs`) | Doesn't emit the arm that an integer constant condition without side effects never runs, unless a label or a `case` of a `switch` outside it enters it (`CodeGenFunction::EmitIfStmt` and `ContainsLabel`), and so not what only that arm calls. musl's `FORCE_EVAL` calls `fp_force_evalf` and `fp_force_evall` in the arms a `double` never runs. Tier 0 lowers both arms, and the selection counts them. |

The model doesn't name string literals, which have no symbols, and a constant
`if`'s dead arm drops the ones only it refers to too (c-testsuite's 00212, 00214
and 00215): the corpora compare no strings.

Results on the Zen 4 laptop, with clang 23.1.3 (`--target`'s configuration of
each corpus as `test-checker-corpus` runs it):

| Corpus | Accepted | Compared | Explained differences (folded, unselected, dropped) | Unexplained | Refused by the backend |
| --- | --- | --- | --- | --- | --- |
| c-testsuite (220) | 220 | 220 | 0 | 0 | 0 |
| musl 1.2.5 `src` (Zig 0.14.1's copy), 1,524 files, `-std=c99 -ffreestanding` | 1,484 | 546 | 383 (210, 59, 114) | 0 | 937, and 1 the backend fails on |
| Lua 5.4.8 `src`, `-std=gnu99 -O2` | 33 | 13 | 2 (2, 0, 0) | 0 | 20 (`visibility`) |
| `sqlite3.c` | accepted | 0 | | | 1 (an `__uint128_t` local: values aligned past 8 bytes) |

The backend's refusals are its features, not the selection's: most of musl's
are attributes (aliases, weak symbols, visibility, 480), inline assembly (384)
and `_Complex` (64). Since Tier 0 lowers `long double` (#423), musl's files
that only refused it are compared, and its `FORCE_EVAL`'s dead arms are the
dropped differences. The backend fails on `stdio/vfwprintf.c`: an internal
error lowering it (a last operand of another class than the entry before it),
which main has too, outside the selection.

On the 9700X, with its frozen corpus (musl 1.2.5 with the headers its Makefile
generates, clang 23.1.1, `-march=znver5`, one run over `src` at `ddf3537c`,
merged with main's `4f4a0bfa`), 1,484 of the 1,524 files are accepted, as clang
accepts them; 544 are compared, with no unexplained difference, 939 refused by
the backend, and 1 the backend fails on, `stdio/vfwprintf.c`, as on the laptop.
The explained differences are all in the 9700X job's eight directories, which
give the same counts alone: 210 folded, 59 unselected and 114 dropped. Those
directories, `math` and seven small ones with many files the backend lowers,
compare 348 files.

## What the selection saves

```sh
./build/compiler benchmark-lowering-source [--passes N] [preprocessor options] file...
```

checks each file as the driver does, outside the time, then times in turns the
two selections and everything `-c` does after one (lowering, Tier 0, branch
sizing, encoding, the static data's publication and the object's bytes),
`--passes` (21) medians. It prints each file's functions and objects defined,
lowered and skipped, both medians and the time saved, then the totals over the
files that lower both ways. A file the checker rejects is left out of the
totals, and so is one refused by name, with the reason the report names: a
statement the checked program's preparation marks unsupported
(`CHECK_PROGRAM_UNSUPPORTED`), or a feature that either way's emission or
publication of the static data marks so (`CHECK_PROGRAM_UNSUPPORTED`,
`CHECK_DATA_UNSUPPORTED`). Lowering everything also lowers what nothing calls,
which the backend may refuse so: then the file is reported with both outcomes.
Any other failure leaves its file out and makes the command exit with status 1:
a file that doesn't preprocess or that exhausts the checker's resources, a
preparation or a warm-up that fails otherwise (the backend's, a resource's, an
invalid input's), and a timed run that fails or lowers other than its warm-up
did (the same counts and object bytes). So does a run that measures no file.
`test-checker` checks each outcome, on fixtures of its own and on failures
injected where the real ones arise, with the status each would have
(`c_lowering_source_benchmark_test.c`).

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

On the 9700X (the 9700X job's run of `benchmark-lowering-source` on
c-testsuite's programs, 21 passes, run 40179359967), the 219 programs that lower
both ways define 341 functions and 682 objects; the selection lowers 322 and 645
and skips 19 and 37. The skipped functions are glibc's static inline helpers
that `<stdlib.h>` defines (`__bswap_16`, `__bswap_32`, `__bswap_64` and the three
`__uint*_identity`), which lowering only what is referenced saves where a program
includes it:

| Program | Functions (defined, lowered) | Referenced only (µs) | Everything (µs) | Saved |
| --- | --- | --- | --- | --- |
| 00040 | 9, 3 | 26.5 | 31.8 | 16.5% |
| 00181 | 10, 4 | 63.4 | 72.4 | 12.4% |
| 00182 | 11, 5 | 43.2 | 48.6 | 11.2% |
| all 219 | 341, 322 | 1,676 | 1,696 | 1.2% |

A program that skips nothing costs the same either way, so the selection's own
work is in the noise. These are small units: the measurement that matters,
`main.c`'s, waits on the two items above.

That comparison is within one tree. Against main, on the same work, a harness of
`benchmark-lowering-source` reduced to the driver's selection
(`check_program_select`, which both trees have) was compiled into main at
`5863eeda` and into this work, and timed c-testsuite's 219 programs in
alternating rounds on the 9700X, each run under the lock and pinned to CPU 2
([the record](https://github.com/buster14a/buster-performance/tree/34d55c24d9d74f452c51c9919bca6d48c0316220/docs/measurements/lowering-emit-418-2026-10-25)).
The first version of this work took 4.1 to 5.1% longer than main, round by
round: its emitter built a table of every symbol's name, and added the source's
names in a pass of their own, in every unit, so that no name it makes is one of
them. Most units make none, and now only a unit that makes a name builds the
table; that took 0.8 to 1.9% longer than main (a median of 1,673,043 ns over the
219 programs against main's 1,653,603), with the same objects as before. The
rest is in this work's other changes on that path, which the runs don't tell
apart: the edges from a body to its block-scope statics, and the scan for a made
name.

[The planner's qualification record](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-lowering-2026-10-23/README.md)
retains the planner's original synthetic measurements.

### Whole-unit passes after checking (#442)

[#442](https://github.com/buster14a/buster/issues/442) attributed that rest, on
the same harness split by stage, with profiles and ablations of #418
([the record](https://github.com/buster14a/buster-performance/tree/3918dc06a3ea00780da7942e2aca615fb48c7c7a/docs/measurements/lowering-scans-442-2026-10-26)).
**It was measured in a cloud VM on an Intel Xeon, not the 9700X**; its scripts
make the 9700X's record, which is still to be made. The path's time after
checking went mostly to passes over every token of the unit, most of them its
headers', which predate #418: the selection's loop for declarations and its
numbering of the needed objects over every object, the emission's three loops,
the lowering's loop, and `abi_initialize` clearing the ABI table's columns for
the type table's whole room, which the checker sizes at four times the unit's
tokens. #418's excess grew with the tokens there: its selection's isn't its new
edges (an ablation without them is no faster), but the per-token loop's code,
and about two thirds of its emission's is `c_check_program_emit.c`'s (an
ablation with main's file takes it back).

So the selection, the emission and the lowering visit the tokens that declare
(and for the lowering, that start an initializer), which
`check_program_declaring` gives 16 at a time with a register compress; the
selection counts and numbers its needed objects 64 at a time
(`check_program_needed_count`, `check_program_needed_number`); each has an exact
scalar twin, which `test-checker` compares at `PROT_NONE` pages. The lowering's
ABI table has rows for its type table's types alone (`abi_initialize_types`):
the lowering makes no type, and classifies only the types it has rows for. The
emission builds the declaring tokens before each place that reads them (its
`defined` pass before the bodies compile on lanes, its attributes' passes
after) and gives them back after it, never holding them through Tier 0. Of the
names a unit gives, only asm labels with a `.` go in the made names' table,
since every made name has one and no identifier does.
Every c-testsuite object, by both paths, is byte for byte what main wrote before.

On that host, c-testsuite's 220 programs, each file's least run minimum over 6
rounds of 101 passes, summed:

| | Main before #418 | #418's merge | Main at `7e3782a` | #442 |
| --- | ---: | ---: | ---: | ---: |
| All 220 (ns) | 4,310,181 | +4.9% | +4.9% | −25.5% |
| The 216 whose output #418 kept | 3,977,392 | +4.5% | +4.1% | −25.2% |
| The 4 whose output #418 changed | 332,789 | +9.4% | +14.2% | −28.8% |
| Under 300 tokens (154) | 907,551 | +4.3% | +6.4% | +4.0% |
| 3,000 tokens or more (46) | 2,843,942 | +5.2% | +5.0% | −32.7% |

A second host repeated it (−24.8% in all, +1.7% under 300 tokens). The smallest
units, which have few tokens to skip, keep a fixed cost: chiefly the ABI
classification of the 64 builtin types on every lowering, whose rows are the
same for every unit of a configuration but would need state that `main.c`'s
units share to cache. These are the post-checking path's times, not the whole
compiler's, and not what the selection saves against `--lower-all`.
