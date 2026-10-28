# C semantic checking and typed expressions

`c_checker.c` implements the expression graph and two schedules for
[#133](https://github.com/buster14a/buster/issues/133). `c_check_actions.c`
supplies their shared C semantic actions; `c_check_input.c` is the current
parser and declaration adapter. The default C command and explicit checker share
the semantic driver:

```sh
./build/compiler check [--scalar] [--dump-typed] [--dump-static] [preprocessor options] file.c
./build/compiler [--scalar] -fsyntax-only [preprocessor options] file.c
./build/compiler [--scalar] [--lower-all] -c [-o file.o] [preprocessor options] file.c
./build/compiler [--scalar] [--verify] [--lower-all] [-o a.out] [preprocessor options] file.c
```

`-c` and the direct path lower only the definitions that the unit's roots reach,
as clang does ([#129](https://github.com/buster14a/buster/issues/129),
`check_program_select`, [the lowering guide](c-lowering.md)): an unreferenced
static or inline function gets no code, and an object only such a function
refers to no data. `--lower-all` lowers every definition that may be emitted,
referenced or not, a test option for measuring what that saves.

The last form compiles and links an executable by the direct path (#145): the
checked program lowered to Tier 0, bound against the C runtime's snapshot
(`runtime_open`, `bind_begin`/`bind_end`), and written as a dynamic PIE
(`executable_write`). A binding that finds the snapshot damaged discards it and
links once more from a fresh import. `--verify` also checks every function's
records with `x86_validate`, as the c-testsuite harness asks
([the c-testsuite guide](c-testsuite.md)), with `-c` too. Without it, the
emission checks only each function's branch columns, which branch sizing reads
and writes by (`branch_sizing_validate_branches`): Tier 0's records are valid by
construction, and `x86_validate` of them all made branch sizing take 14 times
the encoder's time on `sqlite3.c`, against 1.33 times without it
([#532](https://github.com/buster14a/buster/issues/532),
[the branch sizing guide's contract](branch-sizing.md#contract)).

`-lNAME` (or `-l NAME`) links a shared library besides the C runtime's (the
value of an option that takes the next argument stays that option's, as
`pp_option_takes_value` lists them: `-o -lresult` writes `-lresult`), found as
GNU ld finds it: `libNAME.so`, or else `libNAME.a`, in the first of the `-LDIR`
directories, then glibc's library directories, that has one; a linker script
(glibc's `libm.so`) stands for the libraries it names. Binding takes a name
from them before `libc.so.6`, where GCC's driver puts them on the line, and the
executable needs one only for a strong reference that binds to it, the
program's or a needed library's own that its `DT_NEEDED` doesn't cover and no
definition the executable exports satisfies, as GNU ld links them
`--as-needed` (the default of Debian's and Ubuntu's GCC);
`DT_NEEDED` lists them before `libc.so.6` (`runtime_libraries_import`, `bind.c`
and `dynamic.c`). A needed library's strong reference that nothing defines is
refused, naming the library, as GNU ld and lld refuse it; one that requires a
symbol version binds only to an export at that version, as both linkers bind it,
and version needs that don't read are refused. Each loaded library's version
needs must be versions that the library each need names defines (a named
library, `libc.so.6` or the loader), as glibc's loader checks them at startup;
neither linker checks them (`bind_library_versions`). An empty archive (glibc's
`libpthread.a`, `libdl.a` and `librt.a`) adds nothing; one with members is
refused by name, since the direct path doesn't link archives yet. A call of a
function that a system header declares and no linked library has gets a named
error that points at `-l` (`-lm` for libm's). A library's data, the C library's
or a named library's, is reached through its GOT slot (the access column,
below).

It preprocesses, parses, resolves supported declarations, checks expressions,
and reports errors with the preprocessor's source locations. A successful check
returns zero. `--scalar` selects both the scalar parser and the postorder checker
schedule and the independent scalar binding-event resolver. `--dump-typed`
prints the node types, categories, constants, faults and operand/store
conversion metadata. `--dump-static` materializes supported static
definitions as real `Link` atoms and prints their bytes and symbolic fixups.
Preprocessor options include the
existing language, include-path and macro-definition options.

Every body is parsed and checked by default. `-flazy-system-headers` optionally
skips semantic checking of provably unreferenced static and nonemitting inline
bodies defined in system headers, identified by `-isystem` or system-header
pragmas. Declarations, ordinary-source bodies, referenced bodies and all syntax
are still checked. This mode intentionally accepts a system header whose only
semantic error is in a deferred body; it departs from standard C diagnostics
and from Clang's default behavior, as authorized in
[#155](https://github.com/buster14a/buster/issues/155).
`-fno-lazy-system-headers` restores the default, and the last option wins.
Both modes use the same checked-reference selection for object publication;
tests require equal objects for valid inputs. Performance comparisons use the
default mode, with any lazy-mode measurements reported separately.

Unsupported source constructs produce an explicit diagnostic. `-c` also
requires the checked program to be representable by the current backend; a
successful syntax check alone does not establish executable support for every
type or operation. The explicit `parse` command retains the parser-only path.
The combined `-c --dump-static` command validates the object and renders the
same resolved `Link` before publishing the object file. Its data relocations
therefore share the actual emitted functions' symbols.

Semantic checking currently uses one C LP64 target model: 32-bit `wchar_t`,
80-bit x87 `long double` stored in 16 bytes, and floating arithmetic evaluated
in its source format, with plain `char` signed (x86-64's) or unsigned, as
`-funsigned-char` and its spellings say. Options that advertise a different
model are rejected before declaration elaboration. This includes short
`wchar_t`, 64/128-bit `long double`, double/extended floating evaluation, and
default extended evaluation when SSE is disabled. Explicit source-format
evaluation is accepted. Assembly is outside this adapter, and the C language
option parser does not admit C++ modes.

Plain `char`'s signedness is the type table's (`TypeTable.char_unsigned`), which
`check_input_run` sets from the unit's options, the deferred reader on the
caller's table too, and `type_copy` keeps. Every phase reads it from there:
`type_integer_properties` answers it, so the folding, conversions and
comparisons, the initializers, a `mode` attribute's base (which keeps its
signedness, an unsigned one's too) and the lowering's classes (U8 where it was
I8) follow it, as does the convention's extension of a char argument
(`abi_extension`, zero for unsigned). An ordinary character constant of one
character is its char's value (`'\xff'` is 255 where char is unsigned, -1
where it's signed); multicharacter, `u8`, `u`, `U` and `L` constants don't
depend on it. `test-checker`'s fixtures check each against clang with
`-funsigned-char`, and its program fixtures build clang's oracle with the
fixture's own signedness (`-fsigned-char` but for those that ask otherwise),
one of them from the same source both ways.

## Implementation boundary

The source adapter handles nested pointer, array and function declarators;
prototype scopes and parameter adjustments; struct, union and enum definitions
in a declarator, in a parameter's specifiers or an array bound; typedefs and
local scopes;
arithmetic, pointers, calls and casts; records/unions, anonymous members,
bit-fields (with GNU attributes after a width, the member's, which the parser
hangs on the member's end, outside the width's tree, so that the width is
evaluated as any constant expression is) and flexible arrays (a struct whose only named
member is one too, a GNU extension clang takes, with clang's layout);
enumerations, C23's with a fixed underlying type among them, and source-point
completion.
It retains initializer lists, designators and ranges, strings and predefined
function-name arrays, compound literals, GNU statement expressions, generic
selection, `typeof`, and runtime array bounds. Increment/decrement and compound
assignments retain one destination evaluation and an explicit store conversion.
`if`, loops, `switch`/cases, labels/goto, break/continue, returns and static
assertions receive semantic checks. Assembly operands/constraints and source
attribute applicability have separate bounded consumers.

An enum with a fixed underlying type (`enum E : T`, C23's, which clang takes in
every mode) is complete at its base, so that its own list may take `sizeof` of
it. Its enumerators have the base's type in its list, and after it the enum's
in C23 but still the base's before it, as clang 23 types them (two such enums'
enumerators of one base are of one type before C23, and of two after it). Each
value is representable in the base as clang's `isRepresentableIntegerValue` has
it (a negative one by its significant bits, any other by its active bits, less a
signed base's sign bit) and converted to it; the base drops `const` and `volatile` (`restrict`, which
needs a pointer, is refused), and an integer type of any width but `_BitInt`
takes it. GNU attributes among the base's specifiers are the base type's, as
clang has them: ignored, but one that would make it a vector's (`vector_size`,
`ext_vector_type`), refused. Its redeclarations in its scope keep the base, a
declaration of it alone (`enum E;`) too; a reference to it (`enum E x;`) needs
none. A declaration of it with a declarator (`enum E : int x;`) is refused at
the `:`, and an enum without a base can't be given one, nor the other way round,
each where clang reports it. An `aligned` attribute after its body, which clang
applies after the base completed it, is refused as unsupported. (Attributes
after a tag's name, before a `;`, make any tag's declaration a reference to the
visible tag, as clang has it, not a declaration of it alone.)

What this compiler's own `main.c` needed (#129) the checker takes as clang 23
does, each rule checked against clang's answers on probes of it:

- **Loop hints** (`c_check_loop_input.c`): `#pragma clang loop` with its
  eleven options, and `#pragma unroll`, `nounroll`, `unroll_and_jam`,
  `nounroll_and_jam` and `GCC unroll`, as hints of the same categories, read in
  clang's three steps. Its pragma handler lexes the options' names and their
  arguments in parentheses: an unknown name, a missing `(` and an unclosed
  argument are errors, and a token after the options that isn't another one's
  name drops the pragma, with a warning (`unroll_count(4))`), so that nothing
  in it is placed or checked. Then each argument: a state takes its own words
  (`licm` only `disable`, `vectorize_predicate` `assume_safety` too), and a
  count must be a positive integer constant that fits an `int`
  (`vectorize_width` also `fixed` or `scalable`, alone or after the count).
  What follows the word or the constant expression clang reads is ignored,
  with a warning (`vectorize(enable x)`, `vectorize_width(scalable, 4)`); an
  empty argument is an error, and so is one no expression starts with
  (`unroll_count(;)`, `unroll return`). Last, no hint may come twice in one
  category, across the pragmas before one loop too, nor a count beside a state
  that rules it out (`unroll`'s every state, the others' `disable`), and a
  `for`, `while` or `do`-`while` loop must follow, in a function. Errors are
  reported on the pragma's line, but a misplaced one's at the statement after
  it, as clang reports them. `GCC unroll` is `unroll` (#480's review), whose
  count may be left out, in parentheses or the rest of the line, as clang's
  `ParseLoopHintValue` reads it; a pragma clang drops with a warning
  (`nounroll 4`, `unroll (4) x`) is no hint. A count here is an integer
  literal, alone or before a token no expression goes on with (`unroll 4)`,
  `unroll_count(4, x)`, as clang reads them): another constant expression
  (`4 + 4`, `-1`, an enumerator) is refused as unsupported, not guessed. In a
  function, clang checks a count before it reads the next option or pragma,
  and places the hints and checks them against each other only after the
  statement they precede, so the first error is the count's when it isn't a
  valid one (`int N`, `4 / 0`, an enumerator of 0), and a later pragma's or
  the place's only when it is (#480's fourth review): such a count leaves the
  loop's pragmas unsupported, including when a state or count of its category
  shares its pragma. At file scope, where clang reads no count, the pragma is
  an error. Placement, duplicate and incompatible hints wait for the whole
  annotated statement (#503): its exclusive end is the reader's barrier and
  the ordering position of its report, so the diagnostic graph includes the
  statement's type errors too. The printed source is separate: conflicts at
  the later pragma's `loop` or hint name, placement at the statement's first
  token, with clang's directive names and messages. Nested statements finishing
  together report the inner hints first. The existing postorder subtree ends
  supply the boundary; the pragma reader follows parent links to the statement
  root without scanning its body. Only a rejected unit retains one cold
  `CheckInput.loop_diagnostic` record; successful loops retain no hint metadata.
  Dropped pragmas create neither a placement error nor a barrier. Counts not
  evaluated here remain unsupported rather than guessing which later error
  clang would report. At the same completion position, an error from the next
  pragma's handler precedes the current hints (the parser lexes that lookahead
  before Sema); an error parsing that next pragma's argument follows them.
  The rejected prefix still runs the scalar/vector control walk: duplicate
  labels and completed switch constraints inside the statement precede the
  hint. It collects the function-scoped names of forward labels from the tree,
  while deferring undefined-label and jump-scope obligations until function
  completion. Unfinished label references have canonical diagnostic-only
  identities for typing `&&name`; these can never be published or emitted.
  The ordinary control walk is specialized without the prefix checks.
  Tier 0 has no loop transformation for the hints to steer.
- **`#pragma STDC FP_CONTRACT`**, with `FENV_ACCESS`'s placement: at file scope
  or a compound statement's start. Its state changes nothing, since nothing
  here contracts `a * b + c` into a fused operation.
- **`no_builtin`**, on a function's definition only: on another declaration of
  a function it is an error, and on anything else it is ignored, as clang has
  it.
- **`unsigned _BitInt(1)`:** the declaration's specifiers read a `_BitInt`'s
  width alone, so that their own `unsigned` decides its signedness. A typedef
  of an unsigned `_BitInt` stays unsigned where it is used, which main didn't
  have (`U8` of `typedef unsigned _BitInt(8) U8` was signed).
- **An array of unknown size with struct elements:** the reader infers its size
  before expressions have types, so whether an element initializes a whole
  struct, or starts brace elision, comes from the tree: the element's root's
  type, from an identifier's declaration, a compound literal's type, a `?:`'s
  third operand, a parenthesized expression or a call of a function by its name,
  and a subscript's, a member's and an indirection's of these (`x[1]`,
  `bind->runtime->libc.soname`, `*p`), each step applied to its base's type from
  the innermost out. It came from the last typed token in the element, and so a
  compound literal with a member's value, `(struct L){ .target = n }`, read as
  `n`'s type, and `{ x[1], x[2] }` of structs as a struct's first two members:
  one element where clang has two.
- **Compound literals in a static object's initializer**, in a function too:
  clang folds a block-scope compound literal's value, an automatic object's, as
  GNU C does, into a `static` array's element or a `static` struct, where its
  body's parts are constant (`(S8){ "a", 1 }`, not `(S8){ "a", n }`). The
  initializer copies the literal's parts, as it did only for a file-scope
  literal's. A scalar compound literal (`(int){ 1 }`) stays refused there, at
  file scope too.

Declaration specifiers and comma-separated declarators are elaborated in source
order, including the scope of a name in its own initializer. The reader
records declare/use/restore events as it elaborates, and each use's event
retains the declaration it sees; `c_check_binding.c` publishes those answers
directly, and `--scalar` replays the events independently
([the binding handoff](c-check-bindings.md), #336). The events are also the
history for source-dependent semantic environments. Every ordinary use retains
its exact declaration identity. It also retains the
composite declaration types, canonical object identities, linkage, storage
duration, actual and tentative definitions, and each selected initializer node.
Linked declarations also retain translation-unit identity across closed blocks.
An out-of-scope prototype constrains later declarations of the same entity but
does not silently change the type visible to an unprototyped call. A
declarator's parameter list is a scope of its own, a level deeper than the one
around it (C11 6.2.1p4): its parameters are declared in it, and so are the tags
and enumeration constants it declares, those of a struct, union or enum defined
in a parameter's specifiers or in an array bound among them, each read where it
is, in source order. A prototype's end at its `)`. A definition's are declared
again at its body's opening, as clang has them, since the body's scope is
theirs too: in `int f(struct T *p) { struct T *q = p; ... }` the body's
`struct T` is the parameter's (#435), and in
`int f(enum E { K = 7 } e) { return K; }` its `K` the list's (#438). A list's
second definition of a tag only warns in clang, which makes it a tag of its own
that its name doesn't name, and the checker does the same. A tag that a
declarator's array bound names or defines outside a parameter list is the
declarator's scope's: `int a[sizeof(struct V { int x; })]; struct V v;` is
valid. A prototype in a constant expression's type name (a typeof's, a
builtin's operand, a bound's `sizeof`) is a scope a level deeper as well, which
ends its tags and enumeration constants at its `)`, in the evaluator's binding
walks (`c_check_constant.c`): `typeof(int (*)(struct Z *))`'s `struct Z` isn't
a later one's (#437), and in `int f(int K, int a[sizeof(void (*)(enum E { K }))])`
the inner `K` is that prototype's alone. A bound's definitions are read beside
its statement expressions, before or after them; one inside a statement
expression is refused with its bound, as a statement expression that holds a
declaration is ("Variable-length arrays" below). A definition in a `typeof` is
still refused, there as anywhere.

Type-name prototypes check duplicate parameter names with a separate digital
search tree per prototype (`check_constant_parameter_unique`, #604). A root
and two child indices per existing parameter record consume interned 32-bit
names directly, low bit first. Each unequal record fixes another bit, bounding
an insertion by 32 unequal comparisons, independent of the parameter count.
Zero names are skipped. Nested and sibling prototypes have independent roots;
the tree is private to one `check_constant_type` scratch lifetime. It adds no
allocation or identifier-table initialization. Parameter records grow from
12 to 20 bytes; the scratch reservation grows by eight bytes per token of the
bounded type slice. Reordering the part record keeps it at 40 bytes.

The check stays after parameter type adjustment and before reading the next
parameter, preserving the existing failure order and binding activation.
The source-order insertions and their dependent bit decisions use scalar
loads; the full scalar/vector source readers exercise the same membership
rule. The affected path is declaration-time evaluation, for example
`enum { N=sizeof(int (*)(int p0,int p1)) };`; a body-level `return sizeof(...)`
uses the ordinary declarator reader and is a separate, already-linear control.

On the locked 9700X against main `92c025e7`, a 4,096-name prototype takes
11.27–11.41 ms as a whole `-fsyntax-only` process before this change and
2.01–2.09 ms afterward (5.46–5.61 times faster); following 100 ms of scalar
work it improves 5.15–5.33 times. Instructions fall 77.94%. SQLite's timing
stays within the identical copies' variation; a possible few-percent cost on
many small prototypes is unresolved. The 4,096-unnamed-parameter control
uses about 2 MiB more peak RSS, while the named control stays near 31 MiB.
[The pinned qualification and measurements](https://github.com/buster14a/buster-performance/tree/3e6cd7e2a88ae93b11797050f9fb8806be18ecc9/docs/measurements/prototype-parameter-names-604-2026-10-27)
include all named/unnamed sizes, source/binary identities, memory costs,
the passing target suites and the excluded setup run. These are measurements
of this reader change and its recorded controls, not whole-compiler acceptance.

Overloadable declarations retain source-position candidate sets. A call selects
its candidate from the checked argument types; a function address selects from
the destination function-pointer type. Initializers, casts, assignments,
callbacks and returns retain that selected object and conversion on the use
edge, without rewriting a shared identifier leaf. Candidate availability is
checked at the use's source position. Function calling conventions participate
in canonical type identity and compatibility; checking their types does not
claim backend support for emitting each convention.

Source-point type history prevents a later tag definition or declaration from
retroactively changing an earlier expression. Runtime VLA bounds have stable
extent identities and explicit captures; an unevaluated operand suppresses
runtime work except where the language requires VLA evaluation. Builtin calls
retain their exact formal conversions, evaluated-operand masks and runtime
requirements. An unsupported declaration or unresolved identifier never
acquires a guessed `int` type. Full-corpus and target qualification are tracked
separately from the existence of these interfaces.

Direct semantic actions keep strict integer folding unless the caller selects
`BUSTER_CHECK_EXTENSION_GNU_CONSTANT`. Source enables that explicit policy in
every C mode, as clang folds GNU's way in `-std=c99` and `-std=c17` too (only
`-pedantic` warns): wrapped overflow and Clang-compatible shift extensions
retain a warning witness and clear strict ICE eligibility. The other GNU
extensions clang accepts in every C mode (zero-length arrays, `sizeof` and
`__alignof__` of `void` and of functions, arithmetic on `void *` and function
pointers, a conditional with one void side, `return` of a void expression from
a void function, a cast of a record to its own type, `?:` without its middle
operand, designator ranges, static initialization of flexible array members,
`__extension__`) are accepted in every mode as well. Lexical ICE-form provenance remains distinct
from an arbitrary folded value, so `__builtin_choose_expr` does not accidentally
admit comma expressions or computed floating casts. Declaration-time warnings
are retained by token and merged with graph warnings before rendering. Warning
identity and default severity remain separate: an enum's GNU folding extension
is normally quiet, while the corresponding folded file-scope array bound warns.
Immutable source-position diagnostic pragmas apply push/pop and exact warning
group policy to these witnesses. Promoted warnings participate in first-error
ordering and prevent object publication; `-w` suppresses them as Clang does.

The action layer is broader than that adapter. It can check enum promotions,
bit-field restrictions, compatible record assignment, nested const members,
pointer qualification, function calls and their parameter conversions.
Anonymous member lookup and aggregate
modifiability use bounded iterative walks, including inherited qualification
on anonymous members.

Before record layout, `check_input_tag_definition` packs that record's direct
members for `check_input_record_names` (`c_check_record_input.c`). Direct names
have already passed the reader's generation-stamped duplicate check. While
packing, the reader records whether an unnamed non-bit-field member exists and
skips namespace allocation/validation entirely when none does. An anonymous
nonrecord still enters the defensive helper and fails as before. Named fields
introduce only their own name, even when their type contains another record's
definition. The temporary hash table starts from the direct member count and
grows geometrically at half occupancy as anonymous fields expose more names.
Its clearing and rebuilding therefore follow the visited namespace, rather
than the entire nested token span (#614). The explicit expansion stack starts
with one local frame and grows only with anonymous depth; the token span still
bounds that depth. Both scratch structures belong to this validation call and
restore its arena mark on success, duplicate or allocation failure. Duplicate
diagnostics retain the later containing direct member; layout receives the
same packed members. `test-checker` exercises colliding identifier IDs, repeated
table/stack growth, every scratch byte limit, named versus anonymous nesting,
and Clang-checked source layouts and first-error lines on both parser paths.

`CheckStream` holds expression results. `CheckProgram` adds checked control
bracketing, body references and shared object/function identities without
rechecking expressions or rebinding identifiers. The source body-extraction
benchmark verifies that an extracted body's stream equals its original slice
before comparing scalar and vector schedules. The corpora and their semantic
mutants are compared with clang in "Corpus comparison and semantic mutations"
below; 9700X measurements at the qualified revision are still required, and
historical evidence below applies only to its named checkpoints.

## Entry points and storage ownership

The implementation is included in the compiler's existing C translation units.
Its private entry points are:

```c
bool check_input(CheckInput* input, const Parser* parser, Preprocessor* pp,
                 TypeTable* types, Arena* output, Arena* scratch);

bool check_graph_initialize(CheckGraph* graph, Arena* arena,
                            uint32_t capacity, uint32_t argument_capacity);
uint32_t check_graph_add(CheckGraph* graph, CheckOp op, uint32_t token,
                         uint32_t operand0, uint32_t operand1, uint32_t operand2);
bool check_graph_run(TypeTable* types, const CheckGraph* graph,
                     Arena* output, Arena* scratch,
                     const CheckOptions* options, CheckStream* stream);

void check_action_scalar(TypeTable* types, const CheckAction* action,
                          CheckActionResult* result);
void check_actions(TypeTable* types, const CheckAction* actions,
                    CheckActionResult* results, uint32_t count);
```

The parser must retain its complete tree and postorder; `tree_only` mode is not
enough. Parser tokens and the preprocessor's interned identifiers remain alive
while adapting and diagnosing that source. The adapter puts its graph,
declaration identities and reports in the supplied output arena, and releases
its temporary scratch allocations before returning. `CheckInput.resource`
distinguishes capacity failure from the adapter's ordinary diagnostics. A false
adapter result is not a usable, successfully checked graph.

`CheckInput.binding_event_count` and `binding_statistics` describe the
reader's events in every binding mode; only the batch sort counts walks. Its declaration, type, object, linkage, definition, tentative
definition and initializer-node columns all have `token_count` entries. Only
declaration tokens (`declarations[token] == token`) define objects; uses map to
those tokens. An object identity is nonzero and stable across compatible
redeclarations. A definition flag belongs to that particular declaration;
seeing an earlier definition does not turn a later `extern` into a definition.
Event count includes restores, and identifier/declaration statistics count
events, including parameter reactivation, rather than distinct source tokens.
The independent scalar resolver has zero candidate-walk/fallback counters.

`check_graph_initialize` allocates all standard graph columns from an arena and
sets the logical count to zero. `check_graph_add` appends one node if capacity,
operation and earlier-operand checks permit it; otherwise it returns
`BUSTER_CHECK_NONE` without incrementing the count. Initialization rolls back
its allocations if it fails. Both node and argument capacities are bounded by
`1 << 30`; allocation limits normally intervene much earlier.

Graph storage, checker output and scratch storage belong to the caller. They
must be disjoint live regions; in particular `output` and `scratch` must be
different arenas. Type-table storage must also stay alive and writable. The
checker may intern derived types into that table, so concurrent invocations
must not mutate the same table without an external ownership arrangement.

Every column is a conventional C array with its element type's natural
alignment and `sizeof(element)` stride. There is no extra SIMD alignment,
sentinel or readable-padding requirement. Logical count, allocated capacity and
arena capacity are distinct: spare capacity does not authorize reading past a
logical tail. Graph inputs remain immutable during a run. Direct action inputs,
results and argument-conversion outputs must occupy separate live storage;
their arrays need no padding and are accessed only within their counts.

### Input graph

| Column | Logical extent and meaning |
| --- | --- |
| `ops` | `count` `CheckOp` entries; one operation per stable node ID. |
| `operand0`, `operand1`, `operand2` | `count` 32-bit entries each. Used operands are earlier node IDs; absent operands are `BUSTER_CHECK_NONE`. Arity is checked. |
| `tokens` | Optional `count` 32-bit source-token identities. If absent, diagnostics use node IDs. |
| `leaves`, `leaf_slots` | `leaf_count` `CheckValue` records, one per `VALUE` node in node order, and `count` 32-bit slots: a `VALUE` node's is its record's index, the next after the previous `VALUE` node's, and every other node's is `BUSTER_CHECK_NONE` (`check_graph_leaf_of`). Only `VALUE` reads a record, so other nodes take none of its 96 bytes (#572). |
| `types`, `members` | Optional `count` columns containing a requested destination/query `TypeId` or member identifier. A missing column supplies zero. |
| `contexts` | Optional `count` bytes. The documented bits request an ICE, a true static assertion, or a static initializer. Unknown bits are invalid. |
| `argument_offsets`, `argument_counts` | Optional `count` 32-bit columns. A call owns a bounded slice of `argument_nodes`; its callee is `operand0`. |
| `argument_nodes` | `argument_count` 32-bit earlier node IDs, within `argument_capacity`. Nonempty slices belong only to calls. |

Nonempty call-argument slices are append-only in postorder, disjoint and
collectively cover `argument_count` entries. Each such call starts at the end
of the previous nonempty slice. This gives every argument-conversion record a
single owner. The source adapter produces exactly that layout. Arguments are
already checked expressions, not syntax that the call action must reparse.
`check_graph_run` validates topology, arity, slice bounds, operation values and
context bits before reading child results. Empty graphs need no node columns;
an empty action batch may use null input and output pointers.

Counted auxiliary records retain builtin calls, overload candidates/selections
and statement-expression proof regions. A region owns a contiguous bounded
slice of `EVAL`, `BRANCH`, `CASE`, `JUMP` and `END` steps over earlier typed
nodes. The source adapter builds these steps from the existing parser tree and
checked expression roots. A `CASE` retains both the switch expression and case
operand so comparison uses the promoted switch type. Validation checks node
order, ownership, slice coverage and branch targets before interpreting a
region. Body extraction includes these dependencies and remaps every retained
node reference while preserving branch targets within each compacted slice.

Overload declaration lookup uses transient signature buckets keyed separately
by name and lexical scope, and by name and translation-unit linkage. Each
fingerprint preserves canonical parameter identity, including nominal tags and
nested function ABIs, while ignoring alignment sugar and the outer function's
return type and ABI. Append-only type-DAG fingerprints are computed once per
type. Eight contiguous bucket keys are compared together, with an exact scalar
twin; collisions still undergo the original signature and compatibility checks.

Validated redeclarations carry accumulated attributes, definition state and
canonical object identity. Identical type representatives can then be replaced
in the transient index. Ordinary declarations recorded before a name becomes
overloaded must first have their complete history checked. Merely compatible
return types cannot end that history walk: compatibility of old-style function
pointers is not transitive. The immutable source candidate chains and captured
call heads remain intact, so declarations in later or sibling scopes cannot
change an earlier call. Declaration-only scaling is measured separately from
call-site overload resolution (#612).

### Typed output

`CheckStream.count` matches the input graph's count. Operators, source tokens
and operand edges stay in `CheckGraph`; the stream adds node-indexed semantic
columns without renumbering or expanding the graph:

- `values` stores each node's `TypeId`, value category, constant representation,
  flags, optional object/addend, bit width and deferred evaluation fault.
- `conversions` contains three operand records and a separate store record per
  node. Each records the
  destination type and the applicable lvalue, array/function decay, promotion,
  arithmetic, assignment, explicit-cast or null-pointer conversion flags.
- `argument_conversions` contains one record for each call-argument slot,
  including prototype conversions and default argument promotions.
- `member_offsets` stores a `MEMBER` or `ARROW` node's member's offset in bits
  from its record's start, through anonymous members, as the action's lookup
  found it (`docs/c-types.md`, "Member lookup"), and 0 for every other node:
  the lowering takes its member accesses' places from it rather than looking
  the members up again (#591).
- `diagnostics` stores the node's constraint diagnostic. `reports` is the
  compact diagnostic list, sorted stably by source token; `report_count` is its
  logical length and never exceeds the node count.

`CheckCategory` distinguishes rvalues, lvalues and function designators.
`TypeId` retains qualifiers and full semantic type identity. Conversion records
describe the conversion sequence and destination; they are not backend machine
instructions. The action result also carries a diagnostic operand index for
direct callers. The stream currently reports the expression node as its
diagnostic source location.

For compound assignment and increment/decrement, `operand0` is the sole graph
edge to the destination. Its conversion describes the read/promotion needed for
the calculation; `store` describes conversion back to the destination type.
The node remains a side effect and is not an ICE. This metadata does not by
itself implement runtime ordering or the old/new value choice in a backend.

`check_graph_run` returning true means that checking completed. It can still
produce semantic errors, so callers must inspect `report_count`.
`CHECK_STATUS_INVALID_GRAPH` means the producer violated the graph contract;
`CHECK_STATUS_RESOURCE` means output, scratch or type capacity was exhausted.
On either API failure the output arena is restored and the stream's pointers
and logical count are cleared; `status` and `failed_node` remain available.
Scratch is restored on every return. Type interning is append-only and is not
rolled back with the output arena: a failed run may leave valid, unused derived
types in the caller's table.

## Dependency scheduling and vector actions

Parser depths describe brackets, not expression readiness. In `a + b + c`,
the second addition depends on the first even when both tokens have the same
bracket depth. The checker computes heights from actual operand edges. A node
is ready only after all its operand values and types have been produced.

The scalar schedule processes graph nodes in postorder. The vector schedule
finds contiguous regions whose type behavior is known to stay within builtin
arithmetic types, computes their relative dependency heights, and stably groups
ready nodes at each height. Operations that can create a pointer, decay an
array or function, or otherwise intern a derived type are postorder barriers.
This conservative rule preserves newly allocated `TypeId` numbers across fresh
scalar and vector type tables. Comparing both schedules in a table already
populated by one schedule would miss that requirement.

`CheckOptions.batch_size` is a logical batching limit: zero selects 16 and
values 1 through 16 are valid. Every dependency level and region end flushes
its partial batch. The vector arithmetic kernel uses eight 64-bit lanes, with
per-lane 32-bit or 64-bit C arithmetic semantics; the logical batch size is
independent of that lane count. Eligible addition/subtraction, unary integer
operations, shifts, comparisons and bitwise operations use that kernel.
Irregular types and operations, integer multiplication/division/remainder,
128-bit arithmetic, pointer semantics, member lookup, calls and floating
folding use the scalar semantic action. Singleton frontiers execute directly
without packing a vector batch.

The vector path completes each leaf without an overload set, a `VALUE` that
reads no operand and interns no type (about 45% of `sqlite3.c`'s body nodes),
without running its action (`check_graph_leaf`, #530). The record is the one
the action gives: the leaf with its type as operand 0's conversion, or for an
invalid type, that type's diagnostic and a zero value. It writes the records in
node order: the barrier's leaves in the walk, a region's in its level pass,
which then schedules only the rest. A separate pass of leaves wrote each
output cache line twice. The vector path also validates the graph 16 nodes at a
time (`check_graph_validate_vector`): the operators' classes come from one
`vpermi2b` table built from the scalar rules, and each argument slice's offset
from a prefix sum. A block with an invalid node, a `VARARGS` or x86 builtin
or a sideband entry goes to `check_graph_validate_node`, the scalar
validator's step. Most of that validation's time is the leaves' records it
reads: two cache lines of each 96-byte record, which since #572 lie one after
another, as only `VALUE` nodes have one. `test-checker` compares the two
validators on random graphs whole, with each rule broken, and with their
columns ending at `PROT_NONE` pages.

Both schedules call the same semantic-action contract. Scalar fixtures with
independently stated expected results and the external source oracle therefore
remain necessary: scalar/vector equality alone cannot validate shared C rules.
`CheckStatistics` reports nodes, levels, batches, actual scalar/vector node
counts, and arena bytes. The node counts describe the path executed, not merely
which operations could theoretically use SIMD: `vector_nodes` counts the vector
path's leaves and its batches' vector-eligible arithmetic. The schedule is currently
single-threaded; it does not establish useful multicore scaling.
`query_proof_nodes` counts visits across the three constant-query proof lanes;
`query_replay_nodes` counts the subsequent graph replay. These are additional
work, separate from the initial scalar/vector scheduling counts. Scratch bytes
are the peak live allocation above the caller's initial arena position,
including temporary overload, region and query allocations that are released
before return.

## Constant evaluation

Integer constants contain target-width bits in `unsigned __int128`. Promotions,
usual arithmetic conversions, narrowing, sign extension and overflow checks
use the x86-64 target type rules. Host signed overflow does not implement target
overflow. Integer widths through 128 bits have explicit arithmetic handling;
unsupported widths produce a defined failure rather than truncation.

`float`, `double` and x86 `long double` folding uses those formats, including
rounding arithmetic results to their destination precision. Converting a
128-bit integer to `float` or `double` converts directly to that format: an
intermediate `long double` could round twice around a midpoint. Complex
constants retain real and imaginary components in their source format; GNU
integer-complex values retain separately masked components. A failed component
conversion clears the entire constant payload while preserving its fault.
Exotic floating formats retain explicit unsupported folding requirements.

Constraint diagnostics and evaluation faults are separate. An invalid operand
type is an error even in an unselected expression. A possible divide by zero,
overflow, invalid shift or out-of-range floating conversion travels with its
value and source node until an enclosing context requires that evaluation.
`&&`, `||` and `?:` discard faults in unselected operands; `sizeof` does not
evaluate its ordinary operand. Thus these two cases have different outcomes:

```c
_Static_assert(1 || (1 / 0), "not evaluated"); /* accepted */
_Static_assert(1 || (1.0 % 2), "invalid type"); /* constraint error */
```

ICE eligibility is tracked separately from whether a value happened to fold.
For example, an immediate floating literal cast to integer can be an ICE;
an ordinary floating arithmetic expression is not made an ICE merely by
folding. Source contexts explicitly request integer constant expressions,
static assertions or constant static initializers.

Constant-query answers keep source proof, speculative proof and runtime effects
separate. A source constant answer does not erase a floating-point exception or
rounding dependency in an evaluated operand. Statement-expression proofs follow
one known control path and refuse a revisited step or unsupported transfer;
runtime effect collection visits reachable steps once and combines unknown
branches. Both consume the existing checked values rather than interpreting a
second variable environment. The ordinary program body remains responsible for
runtime execution.

`__builtin_bit_cast` retains its destination type and raw source operand before
array decay or ordinary argument promotion. Type constraints, representation
availability and a usable constant payload are separate results. A valid cast
with unavailable representation data remains semantically valid and carries an
explicit downstream requirement; it does not acquire invented bytes or a
guessed result type. Floating representation transfer does not read a poisoned
or unavailable host floating payload.

Address constants retain an object identity and signed byte addend. The source
adapter marks static-storage objects and functions. Taking the address of an
automatic object retains identity for checking but does not create a static
initializer constant. Same-object pointer operations can fold when their
element size and addends permit it; absolute integer-to-pointer casts do not
invent same-object provenance for relational comparison or subtraction.

## Static-data handoff

`c_check_data_input.c` consumes the retained source declaration columns. It
selects one real or tentative definition per canonical object, keeps the latest
composite type, creates distinct local/global symbols and passes selected
`CheckDataObject` records to `check_data_emit` in `c_check_data.c`. It never
reparses declarations or binds identifiers again. An actual initializer wins
over later tentative or external declarations. Automatic objects and
unreferenced function bodies produce no data atom.

The emitter consumes checked aggregate initializer plans, including brace
elision, designated/range stores, bit-fields, compatible string copies and
selected union members. It handles integer and bit-precise storage,
binary32/binary64 and complex components, x87 components with deterministic
padding, and pointers. Objects use RODATA, DATA_RELRO, DATA, BSS, TDATA or TBSS
as appropriate; retained string candidates use STRINGS. Symbolic pointers emit
ABS64 fixups with signed byte addends; absolute pointer values remain bytes.
External targets may remain unresolved. A referenced internal function needs
its real code atom: standalone data inspection reports that limitation, while
combined object/data output uses the program's actual symbol space.
All-zero writes and large clear ranges remain symbolic. For ordinary nonzero
objects, a bounded proof inspects at most eight final writes for a nonzero
integer representation untouched by later writes. When the retained byte suffix
already fits, validation and serialization then run together in source order,
without a scalar cache or replay. Capacity failures still take the validating
symbolic path, so an earlier invalid write retains diagnostic precedence.
Other aggregates validate and encode their scalar writes once before replay. When
overwrites can remove earlier nonzero bytes, a backwards last-writer walk over
sorted bit-range boundaries determines the final state; each interval is visited
once, with scratch proportional to checked writes rather than declared storage.
Only a surviving nonzero byte or relocation requires an object-sized payload,
built directly in the unpublished retained suffix. Encoded scalar slots use the widest actual scalar representation in
that object rather than an unconditional 32-byte slot; CLEAR/BYTES-only objects
allocate none; mixed plans may retain stride-sized positions for other part kinds.
Replay releases its cache before the next object is planned,
retaining only payload and fixups. Zero plans release their unused per-write
caches as well. Const zero objects keep RODATA and are filled directly during publication;
negative floating zero is classified by its encoded bytes. The byte-range scan
has bounded AVX-512 loads and an exact scalar twin, including partial-byte tails;
both stop after the first nonzero block/byte. Overlap boundaries reuse the existing
stable scalar/vector radix kernel, sorting both 32-bit halves and skipping
constant digits on the vector path, with guarded independent ordering tests.
Initializer plans retain the exact selected overload candidate and object.
The standalone source bridge refuses a referenced overloadable symbol until
its ABI symbol name is available, before publishing any Link state. An unused
overloadable declaration needs no symbol; the lower-level emitter can consume
an explicitly supplied object-to-symbol map.

The source initializer adapter matches the selected language's warning policy
for excess elements and compatible overlong string copies. Ignored tails are
still expression-checked, but produce no stores, runtime evaluations or live
data references. The direct initializer API retains its strict rejection
contract. Diagnostic-only scans borrow complete checked roots, restore both
arenas and never publish an initializer or a supposedly clean stream.

String initializer lookup uses `CheckInput.string_ids`, the reader's token-to-literal
index retained in the output arena. Every adjacent literal part names the same
record. An array-valued expression also resolves its ordinary literal through
its object identity (`token + 1`), preserving parenthesized string copies;
predefined function-name arrays remain excluded from literal initialization.
The planner checks the indexed record's bounds and identity instead of scanning
all literals for every initializer, including nonstring elements of a record.
Deferred bodies remap this existing column together with their merged literal
pool, and corpus comparisons include the retained indices.

Static-assert messages contribute no expression value. The reader's existing
constant-context pass marks each message span once, after identifying its
assertion's top-level comma. String operands in the condition remain checked.
This removes a parser-ancestor walk per string, whose repeated traversal of a
long initializer's separator chain was quadratic (#593). Both changes consume
existing reader facts; they add no search pass or per-initializer allocation.
The parser rejects nonstring message operands before this span is ignored;
regressions also check a C23 assertion with no message. The retained scaling
campaign measures the default production path. Scalar/vector semantic checks
establish equality, while the scalar reference's separate value-chain walks
remain outside this string-specific scaling claim.

The source bridge owns symbols, names and Link tables in `output`; `bytes`
owns initialized contents and `scratch` is temporary. The three arenas must be
distinct. The lower-level emitter accepts an existing Link and object-to-symbol
map and plans validation/capacity before publishing atoms or fixups. Failures
leave its existing Link, bytes and scratch unchanged. The source bridge rolls
back all its output on failure and validates the completed Link on success.
Tests pass these atoms through the production ELF writer and independently
compare bytes, symbol targets and relocation addends with Clang's object files.

Static bit-field writes (`check_data_bit_field_value`, #601) merge only the
partial boundary bytes and replace each interior byte once. They consume the
validated integer's low bits, including signed and 128-bit values; an unaligned
128-bit field can occupy 17 destination bytes. Clears mask the boundaries and
zero the interior with `memset`, including ranges wider than a scalar. Neither
helper reads or writes beyond the checked extent, requires padding, or changes
the source order of overlapping designated writes and union replacements.
`c_check_data_bit_field_test.c` checks every scalar width and byte offset against
an independent bitwise reference at guarded allocation edges, larger clears,
ordered overlapping plans and adjacent relocations. Source fixtures also pass
packed 128-bit fields and replacements through the ELF writer and Clang.

The locked [9700X comparison for #601](https://github.com/buster14a/buster-performance/tree/960c8cd2fd82c707f89296ef386352e7b28edee5/docs/measurements/static-bitfields-601-2026-10-27)
compares `92c025e7` with `268c6a85`, Clang 23.1.1, `-O3 -march=native`,
without debug information, on CPU 2 with one lane. Each whole-process case has
24 samples per variant, warmed case-specific output files, two fresh tmpfs
executable copies and a second baseline as its A/A control. Eleven paired
stage-clock samples measure publication independently of parsing and checking.

For 65,536 records with two 31-bit fields, whole compilation takes
47.473 → 42.322 ms (10.85% less time), with 13.38% fewer user instructions;
publication takes 8.535 → 3.423 ms (59.90% less time). For 16,384 packed
128-bit records, whole time falls 13.37% and publication time 72.19%.
Peak RSS is essentially unchanged. SQLite and the configured Lua sources'
whole-time changes lie within their A/A copy ranges; this is no claim of an
ordinary-corpus, cold-cache or multicore speedup. The record retains all
identities, commands, raw reports, guarded tests and disassembly, and separates
an excluded pilot whose shared output file biased some whole-process timings.
Materialized payloads are constructed directly in an unpublished suffix of
`bytes`, with no second payload copy when atoms are published. A zero-fill
candidate releases its suffix; a failed batch restores the original length
before returning. When the byte arena cannot fit a candidate, temporary scratch
storage permits its zero-fill classification and exact capacity demand to
complete; a nonzero payload in that storage can never be published. Scratch
otherwise holds metadata proportional to objects and initializer references,
not a second image of every payload. Live input prefixes in either arena remain
valid throughout planning. Literal pools, non-TLS readonly objects and objects
with live relocations already determine their section without scanning bytes.
Other materialized objects use an early-exit 64-byte zero test with bounded
masked tails. Avoiding materialization of explicit-zero aggregates is separate
work in [#596](https://github.com/buster14a/buster/issues/596).

For [#600](https://github.com/buster14a/buster/issues/600), the locked 9700X
comparison of `92c025e7` and `4e21ef63` measured complete single-lane `-c`
processes, with fixed output files, two executable copies and both build
orders (36 processes per build/input). The
[qualification record](https://github.com/buster14a/buster-performance/tree/b9552b4f387d9673dd9e6e55dd4d04d16e51a491/docs/measurements/static-data-600-2026-10-27)
retains all sizes, counters, arena probes, assembly and correctness evidence.

| 64 MiB object | Before, ms | After, ms | Change |
| --- | ---: | ---: | ---: |
| writable array | 27.412 | 22.894 | -16.48% |
| readonly array | 27.426 | 22.858 | -16.66% |
| array with one relocation | 27.354 | 22.912 | -16.24% |

A 64 MiB array's emitter scratch peak fell from 64 MiB + 48 bytes to 48
bytes; readonly/literal/relocation-bearing objects scanned no payload bytes
for classification. Whole `-c` peak RSS stayed about 149 MiB for the arrays:
the ELF writer later constructs its complete output image in scratch while
retained bytes remain live. SQLite's 147.103 to 146.697 ms was within the
observed spread. The 1 MiB writable median rose by 0.054 ms (+2.46%) with
overlapping ranges; the explicit-zero control retained its materialization
and used about 2 MiB more peak RSS. Neither a general small-input speedup nor
an improvement to #596 is claimed. All nineteen source cases matched the
baseline's entire object and Clang's data oracle, and the checker, driver,
Link, object-writer and call-graph qualification groups passed.

## Backend handoff

`c_check_program.c` consumes the typed stream, source control records, asm
constraints and initializer references. `c_check_program_lower.c` translates
supported operations into `Tier0Function` records with explicit conversions,
loads/stores, scaled pointer operations, control flow, locals, calls and ABI
classes. `c_check_program_emit.c` compiles those functions, resolves shared
symbols and publishes code/data together; the functions' lowering, Tier 0
code and encoding run on lanes, into one lane's link
([the lanes guide](function-lanes.md), #151). Backend machine classes never
replace the full semantic `TypeId` in the checked stream. It also gives Tier 0
the unit's access column (`Tier0Function.symbol_access`, docs/tier0.md): a
symbol the unit doesn't define is reached through its GOT slot, since it may be
a shared library's and the direct path makes no copy relocations, and a
thread-local by local-exec where the unit defines it and by initial-exec where
it doesn't.

What the lowering covers past scalars (#145's programs):

- **Local objects.** Local arrays, structs and unions are Tier 0 frame objects
  of their own size and alignment (up to 1 GB each), reached through
  `LOCAL_ADDRESS`. An aggregate's value in the stream is its object's address.
  Past 16 bytes' alignment, Tier 0 realigns the function's frame
  ([the Tier 0 guide](tier0.md#realigned-frames-136)).
- **Values aligned past 8 bytes.** A scalar local or parameter whose alignment
  (its type's, an aligned typedef's, or its declaration's `_Alignas`) passes 8
  bytes is a frame object of its size and alignment, as a long double's is,
  which `LOCAL`, `STORE_LOCAL` and `SET_LOCAL` of its class read and write
  (Tier 0's `tier0_value_object`), so that the rest of the lowering reads and
  writes it as it would a value's local. A parameter's arrives from its
  register as a struct of it would, or from its 8-byte stack slot, which
  Tier 0's prologue copies into the object (an aligned typedef passes as its
  base, so the slot is aligned to 8 alone).
- **Copies.** Struct and union assignment and initialization up to 256 bytes
  use one bounded `COPY`. Larger nonvolatile copies use one `memmove` runtime call (#597),
  through the same symbol publication, ABI assignment and live-value preservation
  as other runtime calls. The pointers and `size_t` have unsigned long's U64
  INTEGER ABI on the generated SysV x86-64 target; no new type is created after
  shared type classification. The call returns the destination when the
  assignment's value is used, and its return is discarded otherwise. `memmove`
  permits self-assignment. The number of typed entries and machine records is
  bounded independently of the object size, so syntax-sized lowering capacities
  cover this producer's actual demand. Small kept assignments retain the
  destination in a temporary (#440), because `COPY` itself has class `VOID`.
  Zero-size copies still evaluate their operands without touching memory.
  Volatile copies retain the existing inline pieces, including self-assignment's
  accesses; a library routine may skip copying when its pointers are equal.
  The producer follows the copied value through comma, conditional, selected
  and statement expressions, whose lvalue conversions can erase a source's
  volatile qualifier. Its bounded private worklist borrows unused task storage.
  A shared type-facts bit also retains volatile subobjects through by-value
  records, arrays and aligned/atomic wrappers, stopping at pointer pointees.
  Its memoized nonrecursive preparation visits each type and member edge once;
  a bounded scalar/vector member scan skips that preparation when no member
  is volatile. Initializer copies retain the declared destination's qualifiers
  even when a plan's part type is unqualified. Shape and native regressions
  cover destination-only volatility, nested volatile arrays, self-copy,
  initialization/return and nonvolatile pointers to volatile storage.
  The [9700X comparison](https://github.com/buster14a/buster-performance/blob/3d131b17c8b08bc2688da9c929e26ffea01055d2/docs/measurements/aggregate-copies-597-2026-10-27/README.md)
  at compiler `03d79f50` records an 8192-byte discarded assignment shrinking
  from 223 typed entries, 1152 machine records and 6197 text bytes to 6, 11
  and 43, with hot-buffer runtime 52.659 to 23.647 ns per call. Copies through
  262144 bytes compile where the baseline exhausts code capacity from 16384;
  all 18 tested objects through 256 bytes remain byte-identical. Five release
  test groups pass. There is no demonstrated compile-time gain; 257-byte calls
  cost more than the old expansion, and the single-placement SQLite control's
  median increases 0.79% with overlapping ranges. The report preserves these
  limits and the full evidence.
- **Initializers.** A local object's plan is applied in order: the object is
  cleared first (stores of 8 down to 1 bytes, or a loop of 8-byte stores past
  128 bytes) unless its first part clears it whole, then each part's zeros,
  bytes or value is stored, an aggregate's copied.
  Above 128 bytes of explicit constant payload, dense nonvolatile byte-aligned numeric
  plans use a readonly byte image and one call to the target C library's
  `memcpy` (#598). The image ends at the last explicit value; omitted trailing
  storage is cleared separately. Images with internal holes larger than their
  payload keep the existing writes, avoiding large readonly zero gaps for sparse
  initializers. This preserves checked string truncation,
  embedded NULs, wide characters, designated overrides and local mutability.
  Pool preparation reads the same immutable by-value volatile-containment facts
  as aggregate-copy lowering. The unit prepares those facts once before pool
  planning; each initializer uses one bounded lookup, without a type-sized
  visited table or graph walk. The many-record regression retains complete
  prefixes using only 256 bytes of plan scratch despite hundreds of unused
  types in the unit.
  A mixed runtime plan retains its ordered parts, pooling an individual string
  part above the same threshold. Small payloads, volatile parts, bit-fields and
  values needing address relocations retain their existing lowering. Pools are
  immutable before function lanes start, have deterministic local symbols, and
  require no generated ISA beyond the ordinary C runtime call ABI.
  The [locked 9700X record](https://github.com/buster14a/buster-performance/tree/a1cfed5a9927bcbf4cca3130943efdf3fcd1af7f/docs/measurements/local-initializers-598-2026-10-27)
  compares `92c025e7` with `b5b679ad`: signed `int[40000]` compiles in
  18.676 → 15.428 ms without stage clocks (−17.4%), emits 680,026 → 96 bytes
  of text plus 160,000 bytes of readonly data, and takes 11,929 → 1,275 ns per
  warm repeated call. Strings above the threshold stay at 100 text bytes,
  14 typed entries and 24 machine records through 32,769 total bytes.
  Small objects are byte-identical and their timing differences are below 1%;
  tiny-input compiler peak RSS rises about 2 MiB, and the frozen SQLite
  whole-object control is 0.88% slower (same-binary A/A 0.20%). Large baseline
  string compiles that exhaust code capacity remain failure observations,
  with no baseline runtime speedup claimed for them. The record includes raw
  samples, identities, native/Clang scalar/vector checks and source-corpus
  receipts, including the successful pinned c-testsuite rerun.
- **Calls.** A call through a pointer is indirect (callee 0, the address pushed
  before the arguments), its callee evaluated first. A struct or union argument
  is its address, its size in `argument_sizes`, and Tier 0 passes its bytes
  where `abi_assign` puts them; a struct or union result lands in a frame object
  of the call site's own (`call_result_objects`), whose address is the call's
  value. That object is aligned to the result's canonical type at least, an
  aligned typedef's smaller alignment left out: a callee takes a hidden result
  pointer as so aligned (clang's stores through it may be `movaps`).
- **Structs and unions by value (#137).** A parameter of one is a frame object
  of its size and alignment, which Tier 0's prologue fills. A function that
  returns one in registers returns its bytes' address (`result_size`); one that
  returns it through a hidden pointer takes the pointer as parameter 0, copies
  the bytes there and returns it, and one returned in x87 registers (a struct
  of a `long double`, in st0) returns its bytes' address too. Those larger than
  64 KB, or aligned past 1 GB, are refused. One aligned past 16 bytes passes on a
  stack aligned to it at the call, which realigns the caller's frame.
- **Variadic definitions (#137).** `va_start`, `va_arg`, `va_copy` and `va_end`
  expand to `c_builtin_varargs_lower.c`'s recipes over the function's incoming
  frame ([the Tier 0 guide](tier0.md#incoming-variadic-frames)): at the first
  `va_start`, two anchors give the register save area's and the overflow area's
  addresses, and `va_start`'s offsets follow the named parameters (8 bytes per
  general register, 48 plus 16 per vector register, from the function's own
  signature through `abi_assign`). `va_arg` of a struct or a union copies its
  eightbytes from the save area into a frame object of its own, or finds them
  in the overflow area, as `abi_va_arg`'s plan says; its value is their address.
  `va_arg` of a `long double` reads its 16 bytes in the overflow area. Named
  arguments aligned past 16 bytes align the incoming area to them, which
  Tier 0's incoming frame takes.
- **Long double (#136's item 8).** Its class is Tier 0's F80, whose values are
  in memory ([the Tier 0 guide](tier0.md#long-double-136s-item-8)): a local or a
  parameter of it is a frame object of 16 bytes aligned to 16, which `LOCAL`,
  `STORE_LOCAL` and `SET_LOCAL` of the class read and write, and a global of it
  is read with `LOAD` from its address and written with `STORE` or `SET`
  through it, the address evaluated first; so are other objects' (`*p`,
  `a[i]`, `s.m`). `x op= v`, `++` and `--` on a global or through a pointer go
  through its address as on memory. Its constants are each function's table
  (`x87_constants`), +0.0 at place 0, which truth tests and conversions to
  `_Bool` compare with. A function with an entry of the class or a long double
  result says so (`Tier0Function.x87`), which the lowering notes as it writes
  each entry. A temporary of the class (a statement expression's value or the
  operands live around it, `?:` without its middle operand, a postfix one's old
  value) is a frame object of its own (`check_lower_temporary`).
  The floating classification builtins on it (`isnan`, `isinf`, `isinf_sign`,
  `isfinite`, `signbit` and `fpclassify`) and `fabsl` are `c_builtin_float_lower.c`'s
  recipes over its argument's frame object, with clang 23's -O0 result for every
  x87 encoding ([the integration guide](c-builtins-integration.md#floating-checking)).
- **The 128-bit integers.** `__int128` and `unsigned __int128` are Tier 0's
  I128 and U128 ([the Tier 0 guide](tier0.md#128-bit-integers-136)), memory
  classes as long double's is (`check_lower_memory_class`): their locals,
  parameters and temporaries are frame objects, and their globals are read and
  written through their addresses. A constant is one `CONST` where its high
  half is its low half's sign or zero extension, else its high half shifted by
  64 and ORed with its low half. The convention passes one as two INTEGER
  eightbytes, which Tier 0 takes as a 16-byte object: an argument's value is set
  into a frame object of its own and passed by its address (`argument_sizes`
  16), a call's result comes back into its result object, which `LOCAL` reads,
  and a function returns one from a frame object of its own by its address
  (`result_size` 16). `va_arg` of one takes the path of a struct of two
  eightbytes, its value loaded from their address. Its division and remainder
  (`__divti3`, `__udivti3`, `__modti3`, `__umodti3`) and its conversions to and
  from `float`, `double` and `long double` (`__floattisf` to `__fixunsxfti`) are
  calls of libgcc's, as clang -O0 makes them: their operands set into frame
  objects of their own and passed by their addresses. Those functions' symbols,
  and `__errno_location`'s, are the runtime's (`CheckProgramLowered.runtime_symbols`):
  the unit's own declaration's where its program has one with linkage, else
  one of the lowering's own after the helpers' (`check_lower_runtime`). The
  direct path's executables take them from `libgcc.a`'s members, which their
  runtime joins as GCC's driver links them (`runtime.c`, `bind.c`). A shift
  by a 128-bit count shifts by its low 64 bits. A bit-field of the type is read
  and written as any other (below), its bits within 8 bytes. A `_BitInt(128)`
  is refused by name. The overflow builtins take 128-bit operands and
  destinations (`c_builtin_lower.c`'s `BUSTER_CBUILTIN_LOWER_WIDE`), each operand
  in a frame object of its own.
- **Complex values (#133).** `_Complex float`, `double` and `long double` are
  aggregates of their two elements, the real part then the imaginary one
  (`check_lower_complex`): their locals, parameters and temporaries are frame
  objects, the stream carries a value by its object's address, and copies,
  calls, returns, `va_arg` and static data take it as the convention classifies
  it (`float`'s and `double`'s in SSE eightbytes, `long double`'s in memory
  and returned in st0 and st1). An operation computes into a frame object of
  its own from its operands' parts (`check_lower_complex_compute`), each
  operand set into a temporary as soon as it is evaluated, since Tier 0 reads
  an entry's last operand as the class of the entry before it. A real operand
  keeps its type domain, as clang computes it: no imaginary part joins a sum, a
  difference or a product with it, so that signed zeros and infinities come
  out as clang's (`x - z`'s imaginary part is `-z`'s). A product or a quotient
  of two complex values, and a real dividend's over a complex divisor, call
  libgcc's `__mulsc3`, `__muldc3`, `__mulxc3`, `__divsc3`, `__divdc3` and
  `__divxc3`, runtime functions as the 128-bit integers' are, which give the
  values clang -O0's inline product and its NaN recovery give. `==` and `!=`
  compare both parts; a value's truth is either part's; a conversion converts
  each part (to a real type the real part, to `_Bool` the truth); a compound
  assignment reads its destination in the calculation's type through its
  address, computes and stores the result back converted; `__real__` and
  `__imag__` read an lvalue's part or an rvalue's object's; and a constant's
  parts are a floating constant's (a `long double`'s in the function's table).
  The direct path's executables take the libgcc functions from `libgcc.a`'s
  members (#483): `test-checker-driver --executable` links products and
  quotients of each type by both paths into the same bytes, which need
  `libc.so.6` alone and print every part's bits as clang's code computes them.
  The complex types it doesn't take are refused by what they need: GNU's
  complex integer types ("complex integer types") and the complex types of
  the other floating types, `_Complex _Float16` and `_Complex __float128`
  ("complex types other than _Complex float, double and long double"). A
  local of a type that no class holds is refused by what its type needs, as
  its values are (`check_lower_type_feature`), rather than as "values of this
  type".
- **Bit-fields.** A bit-field is read and written through the bytes that hold
  its bits and no other member's (C11 makes those other memory locations), as
  pieces of 8, 4, 2 and 1 bytes that form one little-endian unit: a read shifts
  its bits to the top and back, sign- or zero-extending them as its type says;
  a store clears its bits and puts the value's in, and its value is the field's
  as it now reads. `++`, `--` and compound assignment read, operate and store
  the same way, and so do initializer parts with a bit width. A field whose bits
  are spread over more than 8 bytes (a packed one) is refused.
- **Variable-length arrays.** Their objects live on the stack, as clang's do.
  The source reader's declaration handoff retains the half-open append range
  of VLA records introduced by its specifiers and declarator. Only that range
  is associated and given the declaration's scope; reusing a typedef creates
  no new bound or capture. A suspended `typeof` specifier saves the range's
  beginning across its source walk. A definition retains its header's range
  until parameter activation, and deferred-body cuts save it for each lane,
  whose private record table also contains other headers. The original
  declaration-token/body filters still exclude nested prototypes. These ranges
  use scalar indices, require no allocation or padded access, and are empty
  for declarations without new bounds. One-bound declarations visit one record
  each instead of all earlier records (#611); the small, ordered span updates
  retain the existing scalar record layout and stable bound identities.
  Empty ranges still validate variably modified declarations and retain their
  jump-scope metadata. In eleven paired fresh-process runs on the locked
  9700X (CPU 2, one lane), 32,000 one-bound typedef declarations compile in
  34.744 ms median instead of 249.305 ms, with 507,587,203 user instructions
  instead of 6,650,980,156. From 16k to 32k, head instructions grow 1.940 times.
  The SQLite control adds 0.0112% instructions and shows mixed, small timing
  changes within process/copy variability. The
  [frozen measurements and target checks](https://github.com/buster14a/buster-performance/blob/b5a850b6171da718b16feb4db341109476e3db19/docs/measurements/vla-record-spans-611-2026-10-27/README.md)
  record exact sources and limits, five byte-identical baseline/head objects
  and their stage receipts ([performance PR #27](https://github.com/buster14a/buster-performance/pull/27)).
  The control stream puts a `STACK_SAVE` before each block's first array, an
  `ALLOCATE` at each array's declaration (after its bounds) and a
  `STACK_RESTORE` at the block's end and before every `break`, `continue` or
  `goto` that leaves the arrays' scope, restoring the outermost block it
  leaves; a `return` needs none. The lowering keeps the saved position and each
  array's address in locals of their own (Tier 0's `STACK_SAVE`, `ALLOCA` and
  `STACK_RESTORE`). An array's size is its extents' locals, which its bounds
  set once where they were declared, times its constant dimensions and its
  element's size, and the same extents give `sizeof` and the strides of
  pointer arithmetic over a pointer to a variable-length array. One aligned past
  16 bytes (its element's alignment, or its declaration's `_Alignas`) takes its
  alignment less 16 bytes more, and its address rounds up to its alignment,
  `(address + alignment - 1) & -alignment`, where clang's code rounds rsp down:
  `ALLOCA`'s address is a multiple of 16. An array declared in a `for`
  statement's clause or directly in a statement expression's block, and one
  initialized (C23's `= {}`), are refused by name. The source reader doesn't yet take a bound whose
  statement expression holds more than expression statements (a declaration,
  an `if` or a jump: `invalid or unsupported declaration`); the control stream
  already puts such a jump out of a block's first array's bounds before the
  block's `STACK_SAVE`, where it needs no restore.
- **Labels as values** (GNU C's, which Lua's interpreter dispatches with).
  `&&label` is a `void *` address constant of the label's own object (its
  token plus one, which no declaration has); a label its function doesn't
  have, or `&&` outside a function's body, is an undeclared name at the
  label's, where clang puts its errors. In `goto *expression` the `*` is
  the goto's delimiter (the parser's child of the goto, with the whole
  expression after it as its own child, a comma's or an assignment's too:
  `goto *p ? &&a : &&b` goes to the conditional's value, as clang's does), and
  the expression converts to `const void *` as an assignment would. Clang's
  jump-scope rules for it: a function with a computed goto and no address of a
  label refuses its first one, and every computed goto must be inside each
  variable-length array's scope and each statement expression that holds a
  label whose address its function takes, since it may go to any of them.
  Those scopes nest, so the labels' innermost ones intersect in one interval,
  which each goto must be in: a pass over the function, not one per pair. The
  control stream marks each such label's place (`CHECK_CONTROL_LABEL`) and a
  computed goto is `CHECK_CONTROL_JUMP_INDIRECT`, with no `STACK_RESTORE`: as
  in clang's code, leaving an array's block that way doesn't release its
  storage, which the function's return does. An address of a label is a
  reference to its function in the selection. The lowering gives Tier 0 each
  function's labels (`Tier0Function.label_entries`), and the emission defines
  a local symbol at each one's first record after branch sizing
  (`.Lbuster_label_N`, of no type), which `GLOBAL_ADDRESS` reaches and a static
  table's pointers point at. Not yet: the difference of two labels' addresses
  as a static initializer's constant (GNU's relative jump tables), which clang
  folds and the checker refuses as not a constant.
  The two control walks filter the parser's immutable kind/role columns in
  bounded 64-byte blocks and visit exact events in source order: function
  bodies, labels, cases, gotos and label addresses in the first walk; gotos,
  label addresses and semicolons in the second. Masked tails need no padding;
  the scalar option retains the original full traversals. Deferred masking,
  branch priorities, body expiry and diagnostic order are unchanged.
  The control reader counts resolved `&&label` expressions while finding their
  targets (`CheckInput.label_address_count`) and reuses the loaded kind/role
  facts in their consumers. A unit without one has no
  addressed-label table or scan in `check_program`. A unit with one filters
  64 kind/role bytes at a time, then marks only the selected targets, with an
  exact scalar twin and a scalar tail requiring no input padding. The table
  belongs to the program pass's scratch arena; an absent table means no label
  needs a `CHECK_CONTROL_LABEL` entry.
  In five paired rounds on the exclusively locked 9700X, exact source `f7af14f`
  against `9de3d30` reduces frozen SQLite user instructions from 1,718,752,973
  to 1,688,577,437 (−1.7557%), wall time from 147.964 to 146.825 ms and peak
  RSS from 598,300 to 598,208 KiB. Required Lua and computed-goto dispatch
  inputs show no consistent wall/RSS regression; dispatch cycles vary in sign.
  The saving includes removing ordinary control-loop work, so it is not an
  exact attribution of #431's older 13.3-million-instruction delta. The
  [paired measurements, causal diagnostic, commands and validation](https://github.com/buster14a/buster-performance/blob/e46965d5ca1a609cec54345ac5ea1f38902560aa/docs/measurements/label-address-cost-449-2026-10-26/README.md)
  retain the limits and raw evidence for #449.
- **Switches (#136's item 7).** A switch's value goes in a temporary, and each
  case compares it, as clang -O0's code does for a sparse switch, unless clang
  would build a jump table: then the switch dispatches through one
  (`check_lower_switch_table`), decided before any body is lowered
  (`check_lower_switch_tables`) for each switch of four cases or more, which
  the checked program lists as it writes their controls
  (`CheckProgram.switches`), and found by its control when its body is
  lowered (`check_lower_table_of`). LLVM's rule at -O0
  (`SwitchLowering::findJumpTables`) takes a switch's whole range or nothing:
  four clusters or more (runs of consecutive values that go to one place;
  `case 1: case 2:` with nothing between them is one, as it is LLVM's one
  block) whose cases hold at least a tenth of the range's values (its
  jump-table density). The class must be one of 32 or 64 bits, and the range
  at most 65,536 values (`BUSTER_CHECK_LOWER_MAX_TABLE`; LLVM's limit is
  UINT_MAX). The dispatch, at the switch's first case: a value whose
  difference from the lowest case's, unsigned, is past the last entry goes to
  the default or the switch's end, and any other to the table's address plus
  the entry's 32-bit offset of its label from it (`LOAD` of `I32`, `CONVERT` to
  `I64`, `PTR_ADD`, `JUMP_INDIRECT`), as clang's code goes. The table is a
  `.rodata` atom (`.Lbuster_switch_N`) of a PC32 fixup per value, to its
  label's symbol (`.Lbuster_case_N`, which the emission defines at the label's
  record as it does `&&label`'s) with the entry's offset as its addend, as
  clang's `.long .LBB - .LJTI` entries are. The tables and their labels are
  numbered in source order when they are decided, so that whatever lane lowers
  a body (#151), they come out as one lane's. Their symbols are the lowering's
  own, after the runtime functions', which `check_lower_finish` numbers once
  they are all known: while the bodies compile, a table's address is a
  provisional symbol after the runtime functions' (`provisional_tables`, which
  the access column has), whose references the emission's merge replaces after
  the runtime functions', in the functions that have tables: a table's final
  symbol falls among the runtime functions' provisional numbers, while theirs
  are below the tables' provisional ones, so that each reference is rewritten
  once. An empty GNU range
  (`case 5 ... 3`) counts for nothing, as clang's Sema drops it. In `-c`'s
  object the tables' and the labels' symbols are temporaries
  (`ObjectOptions.temporary_symbols`), as an
  assembler takes clang's `.LJTI` and `.LBB` labels: left out of its symbol
  table, where a label inside a function would split the function for a
  symbolizer, with the relocations against their sections' symbols. Deciding
  takes a merge sort of the switch's cases, scalar: a pass over a few cases
  per listed switch. On `sqlite3.c`, whose 53 tables hold 2,776 entries, the
  tables added about 431,000 instructions (0.019%) to `-c` before the function
  lanes, and their temporaries 298,000, with no cycles beyond the runs' spread
  (one executable with the tables on and off, on the 9700X), and its shell runs
  13.8% faster
  ([the record](https://github.com/buster14a/buster-performance/blob/6328c2664043f54076850f6c03c5a580ab5bfeb2/docs/measurements/switch-tables-136-2026-10-26/README.md)).
  With the lanes, on one lane, the tables and their temporaries add about
  900,000 instructions (0.04%; one executable with the tables on and off, on a
  Zen 4 laptop's build).
- **Truth tests** of 8- and 16-bit values widen them first, since Tier 0 has
  no compares of those classes.

- **Names.** A block-scope static is named as clang names it,
  `function.name` (its function's asm label, if it has one, for the
  function's name; an asm label of the static's own replaces both), and a
  file-scope compound literal is `.compoundliteral`. A made name that a symbol
  of the unit has already, made before it or given by the source (an
  identifier or an asm label), gets `.N`, as LLVM makes one unique in its module
  (`check_program_emit_made_name`).

Everything else the lowering can't express is refused with
`error: unsupported: FEATURE`, naming the feature: complex integer types,
`_BitInt`, inline assembly and so on. The c-testsuite harness counts such a
test as unsupported, not failed.

Builtin module results retain exact argument conversions, runtime operation
identities and effect requirements. Native helpers are ordinary calls with real
symbols and fixups. Missing backend representations or runtime requirements
produce an explicit emission diagnostic. Independently compiled native fixtures
check the supported bridge; source acceptance is a separate gate.

`FENV_ACCESS` preserves source constant-expression proof separately from runtime
dependence on rounding and exceptions. Runtime operations carrying that dependence
cannot be folded away or discarded solely because their source operands are
constant. The fourteen F32/F64 math helpers exchange raw floating-point bits,
including NaN payload and sign. Square root calls the target's real
`int *__errno_location(void)` accessor when errno storage is needed; an existing
compatible declaration is reused and a conflicting declaration is diagnosed.

Diagnostics distinguish a source location from when a complete constraint is
available. Duplicate switch/default checks run after the original switch has
closed, in Clang's constraint order. Earlier complete initializer/asm/control
errors can be reported even when a later declaration or parser error prevents
full checking. Recovery only supplies diagnostic fragments; the original
rejection remains final and cannot emit an object.

The source driver caps its published errors as Clang 23 does (#669): nineteen
ordinary errors by default, followed, only if another error occurs, by the fatal
`too many errors emitted` diagnostic. Explicit `-ferror-limit=N` permits N
ordinary errors before that fatal diagnostic, and zero leaves them unlimited.
Warnings do not consume the error budget; each accepted diagnostic keeps its
macro notes, while the cutoff has neither a location nor notes. Suppressed
warnings (`-w`) do not enter the counts. The final line gives the published
warning and error totals with Clang's singular/plural spelling; clean units print
no summary, and explicit `-fno-caret-diagnostics` suppresses the summary as it
does in Clang. Both output paths share the publication gate, including deferred
preprocessor-history replay and the standalone `-E` command. This bounds reporting rather than the semantic
graph's construction.

The source driver collects preprocessing, parser and checker groups before
publication (#698). Their order follows expansion-output boundaries and proven
semantic completion points, independently of the diagnostic's located token.
For example, an earlier missing name precedes a later `#error`; runtime shift
warnings complete after their function's closing brace and its lookahead, so
an EOF preprocessing diagnostic can precede that warning. Required constant
evaluation retains its own expression-completion boundary in a sparse warning
fact. Include stacks and macro/explicit notes are rendered with their primary
after the final order is known. The shared gate then counts only published
groups and applies the error budget once. Standalone preprocessing retains
immediate publication.

A nonfatal preprocessing error leaves the source stream available for
diagnostics. A real preprocessing fatal leaves only its produced prefix;
existing complete-declaration/statement recovery can diagnose safe original
fragments there. Either outcome remains a rejected unit and cannot replace an
object file, even if the final budget hides a later error. Diagnostic capture
exhaustion also rejects the unit and publishes one fatal rather than accepting
an incompletely diagnosed warning-only source. Failed history replays keep
queued groups' backing alive or replace a silent failure safely.

After the whole declaration/body resolver has completed, ordinary missing-name
binding reports can retain independent later declarations (#699). Two bounded
forest passes give each rejected top-level declaration its earlier safe graph
prefix; independent declarations keep their full graphs. Missing operands
acquire no invented value or type. Initializer, assembly and control scans use
the same local completion cuts, including whole-switch completion barriers,
and independent tentative-definition constraints still run at translation-unit
end. Type-query/declarator failures, unsupported pragmas, resource failures and
unfinished resolver states retain the conservative prefix. Repaired/deferred
views with skipped bodies do not enable this new binding recovery. A successful
deferred merge has the same complete unit as source order and uses the same cuts.
All these views remain
diagnostic-only and preserve the original source rejection.

### What the lowering costs (#531)

On the 9700X, the lowering (`check_lower_prepare`, then the bodies'
`check_lower_part`) took 25.7 ms of `-c`'s 190 ms on the frozen `sqlite3.c`,
five times Tier 0's compile of the same entries (#520's stage clock, with its
step's options). Laps of its own phases on a Zen 4 laptop's build of main put
69% of it in the tasks' loop, 12% in Tier 0's check of each lowered function
(`tier0_validate`), 8% in each body's setup (half of it the first touches of the
writer's columns), 7% in the program-wide preparation and 4% in each body's end
(its branch targets and its columns' copy). On the laptop's copy of the file it
ran about 740 instructions per typed entry, as many in a large body as in a
small one.

- **Every type's class, once.** The tasks' loop asked each node's types their
  class, and whether they were aggregates, complex, pointers or `_Bool`, each
  time by walking the type table (the aligned typedef's base, the enum's
  underlying type, the integer's width and signedness): about a quarter of the
  stage. `check_lower_classify_types` makes every type's class and facts
  (`BUSTER_CHECK_LOWER_TYPE_*`) before the bodies, 64 types at a time: each
  type's kind looks both up in 64-byte tables with `vpermb`, and the few whose
  kind doesn't settle them (an aligned typedef's, an enum's, a `_BitInt`'s, a
  complex type's, and the builtins) take the rules, `check_lower_type_class`
  and `check_lower_type_facts`, which its twin,
  `check_lower_classify_types_scalar`, applies to every type. The bodies read
  the table (`check_lower_class`, `check_lower_facts`), which has a row per type
  of the unit's table, as the lowering makes none: 4,740 for `sqlite3.c`.
- **The checked value in place.** Each expansion copied its node's 96-byte
  `CheckValue`; it reads it where it is.
- **No task for an empty conversion.** A conversion that writes no entry and
  can't fail (an array's decay, a pointer's to another, a change of qualifiers:
  `check_lower_conversion_empty`) isn't pushed.
- **An entry's append.** In a function that doesn't track its stack
  (`CheckLowerWriter.track`: one without a statement expression, in a program
  without a terminal helper), `check_lower_emit` appends without the operand
  class's bookkeeping, and the function's memory classes (`Tier0Function.x87`
  and `int128`) come from its entries' classes once it is lowered
  (`check_lower_entry_classes`, 64 at a time, and its twin).
- **Lane 0's writer's columns in base pages** ([the lanes
  guide](function-lanes.md#memory)): in the caller's scratch, each cleared a
  huge page.
- **Each statement's nodes prefetched a statement ahead.** The tasks' loop
  waited on its first touch of each node's checked value and conversions, two
  arrays too large for the caches. A statement's nodes come before its root in
  the graph, so before each statement the lowering prefetches the next one's,
  up to its root (`check_lower_prefetch`, its last 64 nodes at most).

The objects are the same bytes. On the 9700X, against main at `ad408cf2`
(medians: the whole compile's of 10 rounds of `perf stat -r 3`, the two in
turn, each after a warm-up of its executable; the stage's of 11 rounds of
`measure-compile` with #520's clock, each build the other's reference; the
emission's of `benchmark-check-lanes --lanes 1`'s 11 passes, twice each):

| `sqlite3.c` | main | #531 |
| --- | ---: | ---: |
| `lower` stage | 25.6 ms | 17.5 ms |
| its system time and peak RSS added | 2.9 ms, 56,044 KB | 0.0 ms, 9,824 KB |
| `-c`, task-clock | 190.5 ms | 181.8 ms |
| user cycles and instructions | 902M, 2,227M | 864M, 2,133M |
| kernel cycles | 155M | 145M |
| peak RSS | 674,216 KB | 627,200 KB |
| the emission on one lane, warm memory | 37.1 ms | 30.0 ms |

What is left is the tasks' loop's dispatch on tasks and operators, and the
first touches the prefetch doesn't hide; `tier0_validate`'s check of every
lowered function, a scalar walk of the stack with a mispredicted `switch` per
entry, 16% of the stage (#569); and the preparation's passes over every token
(#517).

## Correctness tests

```sh
./build/compiler test-checker --seed 133 --clang clang
./build/compiler test-checker --seed 1 --clang clang
./build/compiler test-checker-driver --clang clang --executable ./build/compiler
./build/compiler test-checker-asm --clang clang
./build.sh check-call-graph
```

`--clang` selects the independent compiler executable; it defaults to `clang`.
The test command fails if the oracle cannot run. It covers:

- Independent action results for integer widths, promotions, overflow, division
  and shifts; floating precision and 128-bit conversion midpoints; qualifiers,
  arrays, pointers, members, calls and modifiability; and the distinction between
  evaluation faults and constraints in unevaluated operands.
- Random action batches and legal partitions, poisoned result storage, and
  readable arrays ending immediately before protected pages, including empty
  inputs and lengths around vector boundaries.
- True dependency chains and wide graphs, random DAGs, a 65,537-node chain,
  every legal scheduler batch size, fresh type-table identity comparisons,
  source-ordered diagnostics, invalid topology and call slices, and arena
  exhaustion/rollback. Guarded graph columns test all logical lengths 0–33.
- Real preprocessing and both parser schedules, separately initialized type
  tables, source adaptation and both checker schedules. Small source fixtures
  compare acceptance and the first error line with Clang in GNU C17 mode.
  Generated unsigned arithmetic assertions are checked independently; mutations
  of their expected values must be rejected. Explicitly unsupported valid C
  fixtures require an unsupported report rather than claimed Clang parity.
  GNU folding has separate strict-action and source-extension fixtures,
  including warning provenance and forms that remain ineligible for an ICE.
- Independent declaration identities across nested/prototype scopes, source
  event batch/scalar equality, fallback and exhaustion, complex declarators,
  record/enum constraints, loop controls and update/store metadata.
- Static source definitions through `Link` and the ELF writer, independent
  Clang object bytes and relocations, guarded batches and transaction rollback,
  signed bit-precise storage and deterministic x87 padding. Character UCN tests
  distinguish GNU17 constraints from GNU23 acceptance.
- Named UCNs (`\N{...}`), which clang reads as its extension in C, are refused
  alike in identifiers and `#if`'s character constants (the preprocessor's
  errors) and in literals (`invalid or unsupported literal`), at their places
  (#452; `test-checker-driver`'s `check_test_driver_named_ucns`).
- Executable programs (`c_check_program_test.c`): each source's checked
  object, from both paths, linked with clang's build of the same source (its
  oracle, each global renamed `oracle_NAME`) and a harness that runs both on
  the same inputs. The harness is compiled once per run and each oracle once
  per distinct text, with `-c` and the flags of the one-step build they
  replaced, and a path whose checked object and oracle are byte for byte the
  last linked program's takes that program's run: the same files would link
  into the same executable (#465). A program that fails is linked and run, and
  its directory kept with `driver.c` and `oracle.c`.
- The passes after checking that visit a unit's few records rather than every
  token (#592), on every program fixture by both paths
  (`check_test_program_sparse`): `check_data_prepare` against the dense pass it
  replaced, kept as the test's oracle, record for record, with each object's
  last declaration (`check_data_latest`) and the static declarations' keys;
  `check_program_token_body` against the token-sized column of every body's
  tokens; and the objects `check_program_object_access` finds the unit defines
  against a walk of every token. `check_init_declarations` runs at every count
  to 200 and larger ones, ending at `PROT_NONE` pages, beside #442's kernels.
- The diagnostics' report (`check_driver_reports`, #592) against the walk of
  every token it replaced, kept as the test's oracle
  (`check_test_driver_reports` in `c_checker_driver_test.c`): units with
  warnings of each kind, an error and a fatal error, reported as checked and
  with their sparse records rewritten at random (constants' and nodes'
  warnings, floating-point pragmas and packed members' addresses in token order
  and out of it, the original pragmas apart, reports, the driver's own
  diagnostics and excess elements, quiet and not). The text, the counts and the
  fatal flag must be the oracle's by both paths.

The fixtures that ask clang for acceptance and first errors go to it many at a
time (`parse_test_clang`), split in order among up to eight processes that run
at once, each process's output read for its own files: clang checks each file
on its own, so the results are those of one process (#465).

Where clang accepts a constraint violation with a warning, the checker accepts
it with clang's warning, as the semantic mutants require: an assignment that
discards qualifiers, nested pointer qualifiers (`int **` to `const int **`),
pointees that differ only in signedness, `void *` with a function pointer, a
comparison of distinct pointers or of a pointer with an integer, and a
conditional of either. The warnings clang makes errors by default
(`-Wint-conversion`, `-Wincompatible-pointer-types` and its function-pointer
group, `-Wreturn-mismatch`) are errors, but in a system header, where clang
suppresses them and converts as it would have. A token is in a system header
when its expansion location is (`check_input_system_tokens`): the preprocessor
judges every output token from runs its main loop keeps (`pp_system_levels`),
or with `--scalar` and for a unit whose runs can't say, by the expansion
history (`pp_system_levels_history`); `docs/c-preprocessor.md`'s "System
levels" has both.
Floating results are compared field by field, never by `long double` padding.

Packed-member addresses warn by default (`-Waddress-of-packed-member`, #507).
`c_check_packed_input.c` retains sparse source witnesses even in `sizeof` and
`typeof`: a direct chain of members must start at a declaration, contain a
packed record or field, and have either an insufficient enclosing alignment
or an offset incompatible with the final member's type. An explicitly aligned
object can strengthen a direct `.` access. `#pragma pack` alone does not trigger
this warning, nor do array decay, an address of an array element, or the complex
bases Clang currently excludes (a call, subscript or explicit dereference).
Anonymous members participate in both layout and the choice of packed culprit.
The reader counts actual layout witnesses before reserving their retained
storage. It bounds chain scratch by the largest candidate expression, and
builds conversion context only when a witness exists. An unused packed type
plus ordinary local addresses needs no diagnostic records or scratch.
The first anonymous diagnostic builds a temporary tag-to-source/context map;
later addresses reuse it, so formatting many anonymous locals stays linear in
the source size plus the emitted names.

`c_check_packed_diagnostic.c` cancels a pending warning when a source conversion
permits the culprit's reduced alignment, including integer and incomplete-pointer
casts. It follows conditional arms and a comma expression's right operand in
their implicit consuming context. Generic/choose selections transparently use
the chosen operand. A bounded source interval scan preserves pending warnings
inside unevaluated and unchosen operands, where Clang does not perform its
implicit-conversion analysis. An explicit cast cancels only a directly
addressed member; its target does not flow into conditional/comma operands.
A cast to `void` does not cancel it. Builtin lowering
conversions are distinguished from source conversions: `__builtin_assume_aligned`
retains its warning while the ordinary pointer parameters of `__builtin_memcpy`
can cancel it. The existing diagnostic pragma history controls suppression and
promotion, and `-w` silences the diagnostic. Source records remain ordered and
the driver merges them with its sparse warning and report keys.

The rules were checked against LLVM's `SemaChecking.cpp` and `SemaExpr.cpp` at
`llvmorg-23.1.3`, then probed with external Clang 23.1.4. No LLVM implementation
or fixtures were imported. `test-checker-driver` runs the original probes through
both checker schedules and, with `--executable`, both production command routes;
it compares warning decisions, complete messages and presumed line/column pairs
with Clang, and covers guarded classifier/interval tails and allocation failure.

## Corpus comparison and semantic mutations

```sh
./build/compiler test-checker-corpus --clang clang [--report NEW.tsv] [--files-from LIST --root DIR] [--semantic-mutations N [--minimize N]] [--seed N] [--keep] [preprocessor options] file-or-directory...
./build/compiler test-checker-corpus --clang clang --minimize-mutant FILE... [preprocessor options]
```

`test-checker-corpus` checks each file (or every `.c` file under a
directory) as the driver does and compares it with `clang -fsyntax-only` on
the same options, #125's configuration: acceptance, the first error's presumed
file and line, and the scalar and vector paths, column by column. It exits with
status 1 on any difference.

`--semantic-mutations N` then checks N semantic mutants, #133's last "Done
when" item (`c_check_mutation.c`). Each unit that the comparison found no
difference in and that clang accepts is printed as `-E` text with line markers,
its base, which is compared as a file first, so that any later difference
belongs to a mutation. The scalar parser's tree of the base gives the sites,
and each mutant is one edit of the base text:

| Family | Kinds |
| --- | --- |
| A type changed | a pointer added or removed, signedness or width changed, `const` added or removed, a struct for the type (or `int` for a struct) |
| A declaration removed | a local, a parameter, a file-scope declaration, a struct member |
| Operands swapped | a binary operator's operands, or two arguments of a call |

The families take turns, three mutants in four edit the unit's own file and
the fourth a file it includes, and `--seed N` (133) orders the units and picks
the sites. A mutant is checked as a file is, with clang reading the same text
as `-x cpp-output`. The first `--minimize N` (4) differences are minimized
(top-level declarations, statements, then tokens blanked while both results
hold, then the line markers dropped) and saved as `checker_mutation_N.c` in the
current directory; `--minimize-mutant FILE...` minimizes saved mutants again.
Each difference that was fixed is a fixture in `c_check_mutation_test.c`, which
`test-checker` checks against clang, which must still give the result recorded
with it, and through both paths. A difference the checker means to keep would
go in `check_mutation_intended`, with its reason; there are none. The command
prints, for each kind, the sites, the mutants, clang's rejections and the
differences.

### The campaigns

On a cloud VM, with clang 23.1.5, its glibc 2.39 headers and
`--no-default-config --target=x86_64-linux-gnu -march=znver5`, seeds 201 to
204. Their first runs found the last two of the differences below (main's
signature and initialized objects of incomplete type); these are their runs
after the fixes:

| Corpus | Options | Files compared | Mutants (clang rejects) | Differences |
| --- | --- | --- | --- | --- |
| SQLite 3.53.2's `sqlite3.c` | `-std=gnu17` | 322 | 320 (174) | none |
| musl 1.2.5 (Zig 0.14.1's tree): its 1,587 sources under `src` and `compat` | `-std=c99 -ffreestanding -nostdinc -D_XOPEN_SOURCE=700 -Os` with the oracle's include path | 4,725 | 1,600 (774) | none |
| Lua 5.4.8's 34 files in `src` | `-std=gnu99 -O2 -DLUA_COMPAT_5_3 -DLUA_USE_LINUX` | 867 | 800 (469) | `lvm.c`'s base |
| The libc corpus (Zig 0.14.1's `lib/libc`): the 3,832 of its 8,983 C and header files that clang accepts here | `-std=gnu17` | 6,552 | 1,600 (302) | `fmaxl.c`'s and `fminl.c`'s bases |

Files compared counts each corpus's files, the mutated units' bases and the
mutants. The scalar and vector paths gave identical columns on every one. The
differences left are in bases, and mutants aren't made of a unit whose base
differs. Lua's `lvm.c`, GNU's labels as values (`&&L_OP_MOVE`, `goto *`), was
one: since the checker takes them (#133), Lua's 34 files agree with clang (the
corpus comparison, without mutants, on a laptop's clang 23). Two are left:

- the libc corpus's `mingw/math/fmaxl.c` and `fminl.c`:
  `__builtin_isgreaterequal` and `__builtin_islessequal` on `long double`,
  floating comparison builtins that [#148](https://github.com/buster14a/buster/issues/148)'s
  families don't have yet.

What the mutants and the bases found, each fixed with its fixture:

- pointer conversions as clang's Sema has them in every mode (qualifiers
  discarded, nested qualifiers, signedness, `void *` and function pointers),
  comparisons of distinct pointers and of a pointer with an integer,
  conditionals of either (to `void *`), and a call's argument errors at the
  argument and too many arguments at the first extra one;
- GNU's extensions in every C mode (above);
- clang's default-error warnings suppressed in system headers;
- `restrict` on a non-pointer at the qualifier, attribute items kept out of a
  failed declaration's graph, `const void` results, main's signature, and
  initialized objects of incomplete type;
- in the libc corpus's bases: `f16` literals and `align_value`, which clang's
  `<immintrin.h>` uses.

The 9700X job's test step checks 8 mutants of the frozen SQLite source and 96
of the oracle's four musl sources with seed 133, on the frozen headers.

## The source reader's passes over the tokens

`check_input` reads the parser's tree in several passes over every token: the
counts that size its tables, the walk in source order
(`check_input_resolve_range`), the expression graph's walk in postorder
(`check_input_expressions`), and the passes after them. #529 cut the work
they do per token where little of it was per declaration or expression:

- **Attribute arguments (#605).** `check_input_attribute_validate` counts the
  arguments in one forward delimiter walk, then fills a compact table of the
  separating commas as it checks their shapes in a second forward walk.
  Both skip complete parenthesis, bracket and brace groups through the
  parser's matching-parent links. A validated list of A arguments retains
  exactly A - 1 `uint32_t` token indices when A > 1, owned by the output
  arena and shared immutably by attribute copies and merges. Empty and
  single-argument attributes allocate no boundary table. The existing outer
  bounds plus those indices give `check_input_attribute_argument` each
  half-open argument range in constant time, without reading source tokens.
  Shape validation, the declaration reader, and the constant reader's
  preparation and evaluation reuse those ranges; numeric operands still
  evaluate once, in source order. Malformed delimiters fail before a table
  is published, and allocation failure reports the attribute's original
  range as a resource error. No token-sized sparse column is added.
- **Value chains.** The graph's walk gives each token the chain of its
  values, its children's chains joined, and counted the chain by walking each
  child's again. An initializer's separators are each a child of the next, so
  a list of n elements took about n²/2 steps: on the frozen `sqlite3.c`, 4.05%
  of the whole `-c`'s cycles. Each token now keeps its chain's length beside
  its first and last values (`CheckInputReader.value_counts`), and a parent
  adds its children's. The scalar path, `--scalar`, walks the chains: the
  tests that compare the two paths compare the counts with the walks.
- **One pass before reading.** `check_input_count_tokens` makes every count
  `check_input_run` sizes its tables by (`[`, `asm`, `sizeof`, member
  operators, operands and calls, and the names of the builtin constants, the
  varargs, `transparent_union`, `overloadable`, `_Generic` and
  `__builtin_choose_expr`) in one pass, 64 tokens' kinds and roles and 16
  tokens' names at a time. The same pass writes each token's syntax bits
  (`CheckInputReader.syntax`, `BUSTER_CHECK_SYNTAX_*`): one of the names whose
  builtins the walk prepares, an attribute's start, and a string or
  `__func__`. The walk in source order tests a bit where it compared the
  token's name and kind with each name and kind: on `sqlite3.c`, 50.8 million
  instructions fewer, about 70 a token. The scalar path computes each bit with
  the walk's own tests (`check_input_syntax_of`).
- **Scans for few tokens.** The passes that act on few tokens test 64 of them
  at once (16 or 8, by the column's width) and visit only those
  (`check_input_scan_*`): the constant contexts (static objects, static
  assertions, case labels), the attribute items, the complete uses (tentative
  definitions), the asm labels (tokens with attributes, then with
  declarations) and the intrinsics' operands. Each scalar path visits every
  token through the same body. The selections are skipped in a unit without
  a `_Generic` or `__builtin_choose_expr` token, and the VLA finish's scans in
  one without a variably modified declaration's lifetime.
- **Builtins' names.** `cbuiltin_resolve`, which the reader asks of each
  distinct identifier, refuses a name that doesn't start with `__` before it
  scans its catalog, whose names all do (`test-preprocessor` checks it).

Both ordinary and declaration-time `_Generic` readers use the same association
index (`c_check_generic.c`). Pointers, arrays and atomic wrappers ending at a
nominal struct or union use that tag as a necessary compatibility key. Other
types share an ambiguous key: enums and integers, function prototypes,
transparent-union parameters and the two vector kinds still receive exact
source-position-aware compatibility checks. Qualifiers and alignment never
replace those checks. The control's key also filters provably incompatible
control matches.

The producer appends associations in source order into scratch-owned key and
type columns. Each hash probe compares sixteen owned keys at once; the scalar
twin computes the same candidate and empty masks. Equal-key candidates stay in
insertion order through collisions and wraparound, so the first failing
association and expression/warning handling keep their existing order. Up to
eight associations use local storage; larger lists reserve a bounded,
at-most-half-full table from the token span. Allocation and sizing failures
propagate as resource failures, and each reader restores its scratch mark.
The index records exact duplicate and control compatibility-query counts.

For A distinct nominal tags, including pointers to forward-declared tags,
duplicate validation makes zero compatibility queries; a control with one of
those keys needs one exact query. Hash probing and input reading remain. This
is a bounded improvement for the nominal families in #602; a large family
sharing an ambiguous key can still require quadratic compatibility work.
`test-checker` compares the index with independent pairwise semantic queries,
checks source-time transparent unions, guarded groups, collisions, exhaustion
and the query-count bound, and checks both source readers against Clang.

On the locked 9700X (CPU 2, one lane, six repeated fresh-process samples),
10,000 pointer associations went from 738.960 to 12.474 ms in an ordinary
expression and from 785.990 to 58.097 ms in a declaration-time constant.
Duplicate compatibility queries fell from 49,995,000 to zero in both readers,
and control queries from 10,000 to one. The complete constant-expression
compile still grows faster than linearly; this removes its quadratic
association-compatibility work, without claiming every other operation is
linear. SQLite's control instructions were effectively unchanged. The
[source pins, full scaling/RSS/instruction table, exact-query logs and validation](https://github.com/buster14a/buster-performance/blob/b6424ad152a7cdc49ee9d3ca89e66ac850c90dbb/docs/measurements/generic-association-index-602-2026-10-27/README.md)
retain both record and pointer families, scalar/vector/native equivalence and
the experiment's limits.

`test-checker` checks every scan, count and syntax bit, by both paths,
against its own reading of columns of every length to 200 that end at a
`PROT_NONE` page (`c_check_input_scan_test.c`).

On the 9700X (CPU 2, `-c` of the frozen `sqlite3.c` with #520's options, eight
rounds of `perf stat -r 3` with main's and this change's executables in
alternating order), the whole compile went from main's 903.3 million user
cycles and 2,227.2 million user instructions to 820.9 million and 2,014.0
million (−9.1% and −9.6%), and its task clock from 190.9 ms to 176.1 ms; every
round's cycles were apart. #520's stage clock (`measure-compile`, 11 runs of
each in rotating order, both executables with the clock merged in) puts the
reader's stage, `check-input`, at 64.8 ms (64.3 to 65.3) against main's 80.3 ms
(79.7 to 80.8), and the whole compile at 175.5 ms against 190.2 ms. The reader
alone (`benchmark-checker-bindings --only direct`, 11 passes on warm pages)
went from 67.3 ms to 54.4 ms (−19%). The object is main's, byte for byte, by
both paths. The scratch the reader writes grew by 3.5 MB (`value_counts` and
`syntax`), and the compile's peak RSS by 4.3 MB. [The measurement
record](https://github.com/buster14a/buster-performance/tree/6929480812d31868d77a515f88ddca3bad408429/docs/measurements/check-input-529-2026-10-27)
has the scripts, the raw outputs and the trees' manifests.

### The walks' visits

The two walks still made their tests at every token: the walk in source
order, some fifteen columns' at each, though it acts at about a quarter of
`sqlite3.c`'s tokens, and the expressions' walk its chain of tests, though at
about a quarter it only writes the token's first value. Each now visits the
tokens it may act at, and tests there only what may make it act. On SQLite
3.53.4's `sqlite3.c` (716,380 tokens), the walk in source order visits
263,473 of them and the expressions' walk 525,518
(`benchmark-checker-bindings` prints both):

- **The walk in source order** visits the tokens marked in a byte column,
  `CheckInputReader.visits`, the next marked one found 64 bytes at a time
  (`check_input_resolve_next`). Two kinds of writes mark them, each with a
  reason (`BUSTER_CHECK_RESOLVE_*`):
  - `check_input_count_tokens`' pass marks the tokens whose syntax the walk
    tests: their roles and kinds, and their successors', looked up 64 at a
    time in tables of the walk's tests (`check_input_role_tests`,
    `check_input_kind_tests`, with `vpermi2b`), and the syntax bits;
  - each write ahead of the walk to a column it tests marks its token
    (`check_input_visit`): an activation, a scope's end, an initializer's
    finish, a declarator's or an initializer's start, or a type query's, a
    designator's or a builtin's operand.

  A visit tests only the columns its marks give reasons for, each read when
  the walk tests it, after its earlier steps at the token may have marked it.
- **The expressions' walk** takes each token's classes from a pass before it
  (`check_input_expression_classes`): whether the walk acts at the token (it
  has children, its role or kind is one the walk's tests take, by the same
  tables, or one of seven sparse columns has an entry for it, 16 tokens' at a
  time), and whether a sparse column does. It skips the tokens it doesn't act
  at, whose first values, `NONE`, the pass writes, and tests the sparse
  columns only of the tokens that have an entry.
- **Deferred bodies** (#113) need no marks of their own: each starts at an
  `OPEN_BODY`, which the walk tests, and the walk jumps over the body from
  there. Lanes share the column, each writing only its own body's tokens.
- **The twins.** The scalar path, `--scalar`, still visits every token and
  makes every test. At each token it also checks what the vector path would
  have done: each test that could act there, without the mark or the class
  that would make the vector path make it, counts in `CheckInput.visit_misses`,
  which `test-checker`'s fixtures and `test-checker-corpus` require to be 0. A
  mutant without the activations' mark failed 3,243 of `test-checker`'s checks.
  `test-checker` also requires the vector path's walks to visit fewer tokens
  than the scalar path's (`resolve_visits`, `expression_visits`), which a mark
  on every token, passing every comparison, would not. The scans' test checks
  the marks and the classes by both paths against its own reading, with every
  column ending at a `PROT_NONE` page.

On the locked 9700X, CPU 2, main `92c025e7` against `60732582`, the completed
final comparison gives `check-input` 63.682 → 58.927 ms, the direct reader
53.912 → 49.375 ms, and whole `-c` perf task clock 146.340 → 141.725 ms.
User cycles fall 3.77% and instructions 5.21%; all observations, unchanged
scalar/one/eight-lane objects and the 16 target checker-suite results are
in the [record](https://github.com/buster14a/buster-performance/tree/703e034725e79fed5574e42b0be96b3af4cae9bf/docs/measurements/check-input-529-walks-2026-10-27).
This is the sparse-walk snapshot, before the later combined storage/traversal
integration, not a new measurement of that combination.

What is left of the reader's per-token work is the walks' work where they
act, the expression-comma propagation, graph construction and the remaining
output fills. Since #572 the graph stores `CheckValue` only for VALUE leaves;
since #186 the parser and reader share one retained traversal. The sparse
walk consumes that reader-owned postorder, and the continuation's dynamic
visit mark is retained even when zero-valued sparse stores are skipped.

## Benchmark and validation limits

```sh
./build/compiler benchmark-checker --nodes 65535 --shape wide
./build/compiler benchmark-checker --nodes 4096 --shape chain
./build/compiler benchmark-checker --only scalar
./build/compiler benchmark-checker --only vector
./build/compiler benchmark-checker --only vector --after-scalar 200
```

The benchmark calls the production `check_graph_run` API. The timed interval
includes graph validation, dependency analysis and scheduling, action
preparation, output allocation, semantic actions and diagnostic collection.
Graph construction and the scalar reference run are outside timing; each
measured output is checked against that reference afterward. The wide shape
contains independent balanced arithmetic trees; the chain exposes the cost of
frontiers with only one ready node. The input is synthetic and contains builtin
unsigned arithmetic, so it does not represent complete source adaptation or a
real compiler corpus.

One warmup precedes 21 recorded passes. The default alternates scalar/vector
order; `--only` isolates one measured path, and `--after-scalar` conditions the
vector path with repeated scalar graph work before each timed sample. Output
reports median nanoseconds per node, useful node throughput, cycles and
instructions when available, and schedule/memory statistics. A denied or
multiplexed performance counter is printed as unavailable, never as zero.
The reported output/scratch bytes exclude the source graph and type table;
representative end-to-end measurements must include those producers too.

Cloud checks use Clang 19 on an AVX-512-capable EPYC host, not the project's
9700X. The unmodified native full build on that cloud host encounters an
existing `lane_wait` compile error for `_mm_monitorx`/`_mm_mwaitx`, because the
VM does not advertise MWAITX. Focused checker compilation avoids that unrelated
command. A temporary external compiler wrapper adding `-mmwaitx` can check full
compilation while tests avoid executing `lane_wait`. The cloud integration
build also substitutes `-O1` for the production `-O3` in that external wrapper;
checker benchmarks compile directly with Clang 19 at `-O3`. This does not establish
that the unmodified production build passes on the cloud VM, nor authorize a
production ISA fallback. The protected target build and measurements remain
separate required validation. No Zen 5 throughput improvement is claimed here.

### Original submission validation, 2026-10-23

The following historical results describe `d9b6144f23d0c03a84324e704742e9f101522c9d`
on the AVX-512 cloud EPYC host. They do not qualify subsequent takeover changes.
Production source and build flags were not changed to accommodate that host.

| Check | Configuration | Result |
| --- | --- | --- |
| Original checker suite | Clang 19, `-O1 -march=native`, repository arithmetic/aliasing flags; independent source oracle `/usr/bin/clang-19` | 40,743 checks, zero failures for each of seeds 133 and 1. |
| Full compiler integration | Clang 19 through the temporary external `-O1 -mmwaitx` wrapper; executable linked in `/tmp`, then installed as `build/compiler` | Build succeeded; integrated `test-checker` passed 40,743 checks with zero failures. No lane-wait command was executed. |
| Call-graph gate | `./build.sh check-call-graph`, original submission | 31 translation units, 16,451 functions, 90,840 calls; zero disallowed cycles and three existing allowed cycles. |
| Source driver target-model checks | Original integrated executable, default/supported options and eight unsupported-model cases | Supported defaults passed; all eight unsupported configurations were rejected. |
| AddressSanitizer and UndefinedBehaviorSanitizer | Clang 18, `-O1 -fsanitize=address,undefined -fno-omit-frame-pointer`, seed 133, Clang 19 source oracle | 40,743 checks passed with zero failures and no sanitizer findings. This includes target-option rejection and type-capacity failure/rollback. |
| Production graph benchmark smoke | Direct Clang 19 `-O3 -march=native`; wide 65,535 nodes, chain 4,096 nodes and one-node isolated vector mode | Scalar and vector outputs matched. Singleton frontiers reported scalar execution. Hardware cycles and instructions were unavailable. |
| Unmodified native full build on this VM | Production `-O3 -march=native` | Blocked by the existing MWAITX intrinsic/host-feature mismatch described above. |
| Protected target and corpus acceptance | 9700X timing/scaling and SQLite semantic corpus | Not run; still pending. |

Clang 19's sanitizer runtime libraries were absent, so the instrumented build
used the installed Clang 18 runtime. That temporary build suppressed
`-Wbitwise-instead-of-logical` for two existing lexer expressions that Clang 18
warns about. LeakSanitizer could not read `/proc` in this environment; the
successful run used `ASAN_OPTIONS=detect_leaks=0` and
`UBSAN_OPTIONS=halt_on_error=1`. Leak detection is therefore not a validated
gate here. Benchmark runs overlapped compilation on a shared VM, so their
unstable wall-clock rates are deliberately omitted from this record.

### Takeover validation, 2026-10-23

At checkpoint `411e413`, after integrating main through `c62cd84`, the expanded checker passed **67,677
checks, zero failures** with local Clang 23.1.3, seed 133, ASan, UBSan and leak
detection enabled. The source adapter's SHA-256 for this run was
`96b93a0fcbc23dbc16b504a4ac4e4acff8c2ef3b21d12a058f39daaf81f8831c`;
the source/test files stayed unchanged across compilation and execution.
This Ryzen 9 diagnostic run does not substitute for protected 9700X validation.
Independent source review also compared 86 original cases with Clang on both
schedules, finding no remaining Clang-rejected inputs accepted in that matrix.

The subsequent review fix counts named record members separately when checking
flexible-array eligibility. Its five new fixtures reject unnamed-only
predecessors, a union member and a non-final member, and accept a named
predecessor followed by unnamed padding. The full checker passed **67,719 checks,
zero failures** normally and under ASan, UBSan and leak detection, with seed 133
and the Clang oracle. The source adapter's SHA-256 was
`81a4050407102bf4f5d7ff2482a0e8c7ad3ef63484eca3180a29e06bc9e43b94`;
the source fixture file's was
`22338c9a6a73d7adf434f187a206db0888ae0cdac7d8d869269ae19b90ac90f6`.
These are also Ryzen 9 diagnostic results. Clang accepts some rejected
flexible-array forms as GNU extensions; the fixtures record those intentional
acceptance differences explicitly.

The source producer/binding/checker measurement harness, frozen input hashes,
and reproduction commands are retained in
[the measurement directory](https://github.com/buster14a/buster-performance/blob/a00d16e47b6f1090b8a0424ab0dd8cb4e6016713/docs/measurements/c-checker-2026-10-23/README.md).
Whole-corpus semantics, multicore scaling and the runtime backend consumer
remain acceptance work even when these bounded fixtures pass.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md). Actual sources:
[reader](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_input.c#L4),
[graph/stream](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_checker.c#L4),
[static selection](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_data_input.c#L4),
[static emission](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/c_check_data.c#L4).

| Producer → consumer | Status and boundary |
| --- | --- |
| Full parser tree/postorder and PP spelling → `check_input` | Implemented supported-source reader; declarations/types are elaborated in source order, expressions in postorder. Parser/PP source IDs are retained. |
| Declaration elaboration → binding event resolver → expression graph | Implemented checker-side events and exact declaration/type columns. Since [#336](https://github.com/buster14a/buster/issues/336), each use's retained answer is published directly, without the parser's second grammar replay ([the binding handoff](c-check-bindings.md)). |
| Literal spelling → `c_check_literals.c` → leaf `CheckValue` | Counted physical/logical spelling is decoded once for its graph value; invalid/unsupported literals are errors. Macro occurrence remains the diagnostic identity. |
| `CheckGraph` → dependency scheduler → `check_actions` / scalar actions | Implemented earlier-node operands, call argument slices, source tokens and contexts; actual operand heights define readiness. Type-interning actions form ordered barriers. |
| Actions/constants/conversions → `CheckStream` → typed dump/diagnostics/static selection | Implemented node-indexed values, explicit operand/store/argument conversions and stable source-ordered reports. Faults in unevaluated branches remain conditional as described above. |
| Source declaration/object columns + stream → `check_data_input` → `check_data_emit` → Link | Implemented scalar static-data subset. Canonical object IDs and latest composite types select one definition, without reparsing or name lookup in the emitter. |
| Graph/stream → executable typed functions / SSA | Missing at this pin. Graph nodes plus conversions are not an evaluation-order function stream or CFG; [#133](https://github.com/buster14a/buster/issues/133)/[#136](https://github.com/buster14a/buster/issues/136)/[#131](https://github.com/buster14a/buster/issues/131)/[#134](https://github.com/buster14a/buster/issues/134)/[#145](https://github.com/buster14a/buster/issues/145) own the bridge. |

The detailed storage contract above applies: natural alignment, explicit
node/argument count and capacity, no readable padding, disjoint live graph,
output and scratch regions. Optional columns remain optional only as documented.
Node identity survives grouping. Every partial ready-level batch flushes at its
region/level end; logical size 1..16 is independent of the eight arithmetic
lanes. Singleton and exceptional actions run exactly; changing legal batch
partitions must preserve fresh-table TypeIds, values, conversions and reports.
A successful run can contain semantic reports; consumers check both status
and report count. Output rollback does not roll back valid appended types.

Static data reads immutable counted `CheckDataObject` records (four u32 fields)
and an object-to-symbol map. It plans counts/bytes before publication, rolls
back failed emission, and owns emitted bytes in a distinct arena that outlives
object writing/linking. Scratch is restored. Forward-reference definitions
must be in the same batch or already published; arbitrary independent batches
are not valid. Aggregate/string/TLS emission remains outside this implementation.

Observed extra work: source reader's event construction/replay, operand-height
construction/grouping, and scans over declaration columns to select objects.
[#336](https://github.com/buster14a/buster/issues/336) removed the replay: the
reader's events are the one producer, and their retained answers the column. Heights are semantic dependencies, so parser depth cannot
replace them. Retaining heights/selected-definition lists is a candidate only
if producer maintenance and downstream savings beat the existing complete
path; future selective emission must include real body references ([#129](https://github.com/buster14a/buster/issues/129)).
Builtin folding ([#148](https://github.com/buster14a/buster/issues/148)), source-point environments ([#113](https://github.com/buster14a/buster/issues/113)) and executable
function lowering are explicit integration obligations, not hidden consumers.

Disposition: retain the checked graph and transactional scalar-data path.
`test-checker` covers independent actions, guarded graphs, partition sizes,
source/Clang diagnostics, source binding identity and data-to-object execution
fixtures. Existing synthetic checker rates exclude source adaptation and are
not end-to-end evidence. [#133](https://github.com/buster14a/buster/issues/133) owns supported-source/corpus completion and the
whole source/check/data costs, allocations, memory and occupancy; [#145](https://github.com/buster14a/buster/issues/145)/[#138](https://github.com/buster14a/buster/issues/138)
own the eventual complete compiler/program-quality evidence.

## Nested selections and warning ranges (#603)

`check_input_range_root` consumes the parser's postorder/subtree sizes and the
reader's existing inverse postorder and subtree ends. A complete expression
occupies one contiguous run in both orders. The first token's own subtree
begins that run; its postorder position minus its subtree size, plus the
expression length, locates the root in constant work. The root's size and last
token validate the requested slice. Both source selections and declaration-time
constant evaluation use this lookup, with no additional syntax-tree column.
Raw attribute expressions retain their separate explicit-stack representation.

`c_check_warning_input.c` applies clearing and suppression immediately in their
original order. Its interval index has a two-byte summary per node and one leaf
per 64 tokens, rounded to a power of two: less than one eighth of a byte per
token for large inputs. The low byte summarizes clearable warnings; the high
byte summarizes missing suppression bits. Completed intervals skip repeated
updates, while a later warning write reopens its leaf's ancestor path. Range
queries cost logarithmic index work plus the blocks that still need changes;
repeated nested ranges no longer rescan their full token spans. Short ranges
before index initialization use direct updates. The index borrows reader scratch
and its first long range initializes conservative summaries, without reading
the whole unit's warning columns.

Scalar and masked AVX-512 block updates preserve independently evaluated bound
contexts, both warning columns and the graph's suppression facts. Loads and
stores stay within the requested range and logical token count. Boundary blocks
retain conservative summaries for their untouched bytes. Deferred workers own
separate indexes, so neither interval ancestors nor adjacent bodies' bytes are
shared mutable summaries; joining workers invalidates the parent reader's old
summaries. Replacing constant contexts also invalidates the index. Diagnostics
retain their existing source-order consumer.

`test-checker` checks every slice of two independent parser trees against a
parent-scan root oracle, and ordered warning mutations against an immediate
byte-update oracle with protected tails at every length through 257. Source
fixtures cover nested selected/discarded arms, vector results, lvalues, bound
warnings and rejected discarded expressions. `test-checker-deferred` covers
warning-bearing selections in several bodies. The scaling campaign's original
C generator and work-count instrumentation live in buster-performance under
`docs/measurements/nested-selections-603-2026-10-27`.

The [locked 9700X record](https://github.com/buster14a/buster-performance/tree/6ab3a0e96500957ce30a05f974806eb153f482ac/docs/measurements/nested-selections-603-2026-10-27)
compares main `92c025e7` with the implementation at `1014c296`. At depth
2048, the eight default-driver cases improve from 8.42–30.57 ms to
2.04–3.91 ms (4.1–8.4 times faster). Runtime discarded generics' root and
warning scans fall from 23,065,600 and 46,123,008 token visits to 8,192 bounded
root queries, 154,432 warning-byte visits and 107,908 index-node visits.
All five depths and both schedules are retained. Scalar and AVX-512 paths
pass the independent byte oracle; the available whole driver and the reader's
initialization/traversal/selection path are measured together with their own
costs, rather than claiming an isolated block-update rate.

With the shared traversal from #640, complete-root queries consume
`CheckInputReader.subtree_sizes` by postorder position. The reader borrows the
parser's optional size column when available; tree-only, prefix and repair
inputs derive one reader-owned column alongside their existing traversal.
This actual consumer requires four retained bytes per token plus a sentinel.
The first-child start column is temporary and released immediately after
derivation. The default vector path publishes sizes in the same traversal
pass; scalar/prefix/repair views derive them from their already retained
postorder, positions, subtree ends and first children. The parent-scan root
oracle explicitly requests the parser's optional size column. Warning tests
also cover independent-to-ordinary context transitions and late writes after
worker-column replacement against the immediate-byte oracle.

Frozen SQLite has a small measured cost in this paired build: syntax checking
+0.16% and object compilation +0.36%, with about 0.1 MiB more peak RSS and
byte-identical objects. The record separates the deep Clang enum oracle's
limit, focused sanitizer evidence, full checker/driver/deferred suites and
whole-process measurements. It makes no whole-compiler sanitizer/corpus claim.
