# C semantic checking and typed expressions

`c_checker.c` implements the expression graph and two schedules for
[#133](https://github.com/buster14a/buster/issues/133). `c_check_actions.c`
supplies their shared C semantic actions; `c_check_input.c` is the current
parser and declaration adapter. The default C command and explicit checker share
the semantic driver:

```sh
./build/compiler check [--scalar] [--dump-typed] [--dump-static] [preprocessor options] file.c
./build/compiler [--scalar] -fsyntax-only [preprocessor options] file.c
./build/compiler [--scalar] -c [-o file.o] [preprocessor options] file.c
./build/compiler [--scalar] [--verify] [-o a.out] [preprocessor options] file.c
```

The last form compiles and links an executable by the direct path (#145): the
checked program lowered to Tier 0, bound against the C runtime's snapshot
(`runtime_open`, `bind_begin`/`bind_end`), and written as a dynamic PIE
(`executable_write`). A binding that finds the snapshot damaged discards it and
links once more from a fresh import. `--verify` also checks every function's
records with `x86_validate`, as the c-testsuite harness asks
([the c-testsuite guide](c-testsuite.md)). The direct path links `libc.so.6`
and its loader alone: a call of a function that a system header declares and
the C library lacks (libm's) and a read or write of the C library's data (which
needs the GOT) are refused by name; `-c` and a system linker take both.

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

Semantic checking currently uses one C LP64 target model: signed plain `char`,
32-bit `wchar_t`, 80-bit x87 `long double` stored in 16 bytes, and floating
arithmetic evaluated in its source format. Options that advertise a different
model are rejected before declaration elaboration. This includes unsigned
plain `char`, short `wchar_t`, 64/128-bit `long double`, double/extended floating
evaluation, and default extended evaluation when SSE is disabled. Explicit
source-format evaluation is accepted. Assembly is outside this adapter, and
the C language option parser does not admit C++ modes. The host compiler's
`-funsigned-char` implementation flag does not select the input program's
plain-char semantics.

## Implementation boundary

The source adapter handles nested pointer, array and function declarators;
prototype scopes and parameter adjustments; typedefs and local scopes;
arithmetic, pointers, calls and casts; records/unions, anonymous members,
bit-fields and flexible arrays; enumerations and source-point completion.
It retains initializer lists, designators and ranges, strings and predefined
function-name arrays, compound literals, GNU statement expressions, generic
selection, `typeof`, and runtime array bounds. Increment/decrement and compound
assignments retain one destination evaluation and an explicit store conversion.
`if`, loops, `switch`/cases, labels/goto, break/continue, returns and static
assertions receive semantic checks. Assembly operands/constraints and source
attribute applicability have separate bounded consumers.

Declaration specifiers and comma-separated declarators are elaborated in source
order, including the scope of a name in its own initializer. The adapter consumes
#128's parser declaration-resolution column and retains declare/use/restore
history for source-dependent semantic environments. `c_check_binding.c`
validates this handoff; `--scalar` uses its independent scalar oracle. Every
ordinary use retains its exact declaration identity. It also retains the
composite declaration types, canonical object identities, linkage, storage
duration, actual and tentative definitions, and each selected initializer node.
Linked declarations also retain translation-unit identity across closed blocks.
An out-of-scope prototype constrains later declarations of the same entity but
does not silently change the type visible to an unprototyped call.

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
`BUSTER_CHECK_EXTENSION_GNU_CONSTANT`. GNU source enables that explicit policy:
wrapped overflow and Clang-compatible shift extensions retain a warning witness
and clear strict ICE eligibility. Lexical ICE-form provenance remains distinct
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
before comparing scalar and vector schedules. Acceptance still requires the
frozen #125 corpora, semantic mutants and 9700X measurements at the qualified
revision; historical evidence below applies only to its named checkpoints.

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

`CheckInput.binding_event_count` and `binding_statistics` describe the actual
resolver path. Its declaration, type, object, linkage, definition, tentative
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
  of their own size and alignment (up to 16 bytes, 1 GB), reached through
  `LOCAL_ADDRESS`. An aggregate's value in the stream is its object's address.
- **Copies.** Struct and union assignment and initialization are `COPY`s of
  at most 256 bytes each, the source's address kept in a temporary between
  them; a statement's assignment leaves no address to discard.
- **Initializers.** A local object's plan is applied in order: the object is
  cleared first (stores of 8 down to 1 bytes, or a loop of 8-byte stores past
  128 bytes) unless its first part clears it whole, then each part's zeros,
  bytes or value is stored, an aggregate's copied.
- **Calls.** A call through a pointer is indirect (callee 0, the address pushed
  before the arguments), its callee evaluated first.
- **Bit-fields.** A bit-field is read and written through the bytes that hold
  its bits and no other member's (C11 makes those other memory locations), as
  pieces of 8, 4, 2 and 1 bytes that form one little-endian unit: a read shifts
  its bits to the top and back, sign- or zero-extending them as its type says;
  a store clears its bits and puts the value's in, and its value is the field's
  as it now reads. `++`, `--` and compound assignment read, operate and store
  the same way, and so do initializer parts with a bit width. A field whose bits
  are spread over more than 8 bytes (a packed one) is refused.
- **Truth tests** of 8- and 16-bit values widen them first, since Tier 0 has
  no compares of those classes.

Everything else the lowering can't express is refused with
`error: unsupported: FEATURE`, naming the feature: aggregates passed or
returned by value, variadic definitions, variable-length arrays, `long double`,
`_Complex`, thread-locals, inline assembly and so on. The c-testsuite harness
counts such a test as unsupported, not failed.

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

The source comparisons deliberately use inputs whose errors Clang treats as
errors under the selected flags. They are not a universal equivalence claim
for extensions or constraint violations that Clang accepts with a warning.
The direct semantic fixtures cover rules such as rejecting `int **` to
`const int **` assignment even when an oracle configuration only warns.
Floating results are compared field by field, never by `long double` padding.

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
| Declaration elaboration → binding event resolver → expression graph | Implemented checker-side events and exact declaration/type columns; [#336](https://github.com/buster14a/buster/issues/336) tracks reuse of pending parser metadata without replacing typed elaboration. |
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
The first has [#336](https://github.com/buster14a/buster/issues/336). Heights are semantic dependencies, so parser depth cannot
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
