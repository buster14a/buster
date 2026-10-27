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
([the c-testsuite guide](c-testsuite.md)).

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
| `leaves` | `count` `CheckValue` records when value leaves are present. Only `VALUE` reads its leaf record. |
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

Both schedules call the same semantic-action contract. Scalar fixtures with
independently stated expected results and the external source oracle therefore
remain necessary: scalar/vector equality alone cannot validate shared C rules.
`CheckStatistics` reports nodes, levels, batches, actual scalar/vector node
counts, and arena bytes. The node counts describe the path executed, not merely
which operations could theoretically use SIMD. The schedule is currently
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

The source bridge owns symbols, names and Link tables in `output`; `bytes`
owns initialized contents and `scratch` is temporary. The three arenas must be
distinct. The lower-level emitter accepts an existing Link and object-to-symbol
map and plans validation/capacity before publishing atoms or fixups. Failures
leave its existing Link, bytes and scratch unchanged. The source bridge rolls
back all its output on failure and validates the completed Link on success.
Tests pass these atoms through the production ELF writer and independently
compare bytes, symbol targets and relocation addends with Clang's object files.

## Backend handoff

`c_check_program.c` consumes the typed stream, source control records, asm
constraints and initializer references. `c_check_program_lower.c` translates
supported operations into `Tier0Function` records with explicit conversions,
loads/stores, scaled pointer operations, control flow, locals, calls and ABI
classes. `c_check_program_emit.c` compiles those functions, resolves shared
symbols and publishes code/data together. Backend machine classes never
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
- **Copies.** Struct and union assignment and initialization are `COPY`s of
  at most 256 bytes each, the source's address kept in a temporary between
  them; a statement's assignment leaves no address to discard, and neither
  does a discarded comma's right operand (`k = 1, a = b;`). An assignment whose
  value is used keeps its destination's address in a temporary too, pushed
  again after the last `COPY`: whatever reads it takes its class from the entry
  before it, which a `COPY`, of class `VOID`, can't give (#440).
- **Initializers.** A local object's plan is applied in order: the object is
  cleared first (stores of 8 down to 1 bytes, or a loop of 8-byte stores past
  128 bytes) unless its first part clears it whole, then each part's zeros,
  bytes or value is stored, an aggregate's copied.
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
  The builtins of its operands and the floating classification builtins on it
  are refused by name.
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
  and a builtin of 128-bit operands are refused by name.
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
the driver consumes them sparsely without widening its dense warning columns.

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
