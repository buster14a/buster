# C semantic checking and typed expressions

`c_checker.c` implements the expression graph and two schedules for
[#133](https://github.com/buster14a/buster/issues/133). `c_check_actions.c`
supplies their shared C semantic actions; `c_check_input.c` is the current
parser and declaration adapter. The explicit command is:

```sh
./build/compiler check [--scalar] [--dump-typed] [--dump-static] [preprocessor options] file.c
```

It preprocesses, parses, resolves supported declarations, checks expressions,
and reports errors with the preprocessor's source locations. A successful check
returns zero. `--scalar` selects both the scalar parser and the postorder checker
schedule and the independent scalar binding-event resolver. `--dump-typed`
prints the node types, categories, constants, faults and operand/store
conversion metadata. `--dump-static` materializes the supported static scalar
definitions as real `Link` atoms and prints their bytes and symbolic fixups.
Preprocessor options include the
existing language, include-path and macro-definition options.

This is a bounded source path, not completion of #133. Unsupported syntax is an
error. The normal `compiler file.c` syntax driver remains the parser's driver
until semantic checking has the necessary language and corpus coverage. No
machine code or object file is emitted by this command; `--dump-static` exposes
the data/relocation handoff without claiming a completed code generator.

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

The source adapter handles builtin scalar types; nested pointer, array and
function declarators; prototype scopes and parameter adjustments; typedefs and
local scopes; scalar initializers, calls, casts, arithmetic/pointer expressions
and `sizeof`; named records/unions, member access, bit-fields and flexible-array
constraints; and ordinary int-valued enumerations. Declaration bounds support
integer/enum arithmetic and logical operators. Increment/decrement and all
compound assignments retain the single destination expression and an explicit
store conversion. `if`, `while`, `do`, `for`, `break`, `continue`, returns and
static assertions receive semantic checks.

Declaration specifiers and comma-separated declarators are elaborated in source
order, including the scope of a name in its own initializer. The adapter emits
declare/use/restore events for the production batch resolver in
`c_check_binding.c`; `--scalar` uses the independent scalar event oracle. Every
ordinary use retains its exact declaration identity. It also retains the
composite declaration types, canonical object identities, linkage, storage
duration, actual and tentative definitions, and each selected initializer node.

String objects, aggregate initializer lists, compound literals, `switch`/`case`,
labels and `goto`, inline assembly and attributes still need source elaboration.
VLA evaluation, `sizeof`/casts/conditional expressions inside declaration ICEs,
anonymous members, parameter-scope tag definitions, wide or fixed-base enums,
generic selections, atomic semantics, extension-specific builtins and the full
C23 surface also require integration. Retained pragmas are rejected. Expressions
that use an incomplete tag before its later completion are conservatively
unsupported, avoiding retroactive completion of an earlier semantic context.
An unsupported declaration or unresolved identifier never acquires a guessed
`int` type.

The checker currently keeps strict integer-constant-expression rules even in
GNU language modes. GNU folded-constant extensions such as
`_Static_assert(1 || f(), "unevaluated call")` remain rejected although Clang
can accept them with a warning. Full dialect parity, including extension
handling for returns from `void` functions, remains part of the source
integration work; this implementation does not establish #125 mode parity.
Discarded indirection through a `void *` is another explicitly tested GNU
extension rejection. The standard `&*p` cancellation is supported, including
`void *` and absolute-address constants, because it does not access a void
object's value.

The action layer is broader than that adapter. It can check enum promotions,
bit-field restrictions, compatible record assignment, nested const members,
pointer qualification, function calls and their parameter conversions.
Anonymous member lookup and aggregate
modifiability use bounded iterative walks, including inherited qualification
on anonymous members.

The source binding handoff is one bounded integration slice of
[#128](https://github.com/buster14a/buster/issues/128); declaration-time typedef
resolution still has ordered work. `CheckStream` is expression output, not a
typed entry for every parser node: executable control-flow bracketing and
per-body references from [#129](https://github.com/buster14a/buster/issues/129)
are still missing. The expression scheduler's vector path does not establish
completion of either surrounding stage.

The remaining acceptance work is substantive:

- Complete declaration, statement and initializer elaboration and language-mode
  handling, then run the required valid and invalid source corpora through the
  production driver and an independent compiler oracle, including the SQLite
  corpus. No SQLite semantic-corpus acceptance is claimed here.
- Complete literal/aggregate objects and their data initialization. The scalar
  static-data path now reaches `Link` and is checked through the actual ELF
  writer, including symbolic address addends; it does not yet cover strings,
  aggregate initializers or TLS.
- Connect typed expressions and explicit conversions to a reviewed backend
  lowering boundary, with generated-program tests for the accepted language.
- Measure producer, handoff and consumer together on representative small and
  large inputs on the protected 9700X, including single-core and multicore
  behavior and the two vector power states. Synthetic cloud timings do not
  satisfy that requirement. The 9700X cycles-per-node acceptance gate is pending.

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

## Constant evaluation

Integer constants contain target-width bits in `unsigned __int128`. Promotions,
usual arithmetic conversions, narrowing, sign extension and overflow checks
use the x86-64 target type rules. Host signed overflow does not implement target
overflow. Integer widths through 128 bits have explicit arithmetic handling;
unsupported widths produce a defined failure rather than truncation.

`float`, `double` and x86 `long double` folding uses those formats, including
rounding arithmetic results to their destination precision. Converting a
128-bit integer to `float` or `double` converts directly to that format: an
intermediate `long double` could round twice around a midpoint. Exotic floating
formats and complex constant folding report unsupported evaluation. This is
not an implementation of every C floating-environment mode.

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

The emitter handles integer and bit-precise integer storage, binary32/binary64,
x87 fields with deterministic zero padding, and scalar pointers. Objects use
RODATA, DATA_RELRO, DATA or BSS as appropriate. Symbolic pointers emit ABS64
fixups with signed byte addends; absolute pointer values remain bytes. External
function/object targets may remain unresolved. A referenced internal function
requires the backend's real code atom, so the source-only dump rejects that
case rather than inventing an external definition. Strings, aggregates and TLS
return an explicit unsupported result.

The source bridge owns symbols, names and Link tables in `output`; `bytes`
owns initialized contents and `scratch` is temporary. The three arenas must be
distinct. The lower-level emitter accepts an existing Link and object-to-symbol
map and plans validation/capacity before publishing atoms or fixups. Failures
leave its existing Link, bytes and scratch unchanged. The source bridge rolls
back all its output on failure and validates the completed Link on success.
Tests pass these atoms through the production ELF writer and independently
compare bytes, symbol targets and relocation addends with Clang's object files.

## Backend handoff

The typed stream is not yet connected to `Tier0Function`. The contract merged
in [PR #303](https://github.com/buster14a/buster/pull/303) uses lowered operation
kinds, machine classes and values, plus control-flow, local, call and ABI data.
Its scalar integer and floating classes cannot replace full `TypeId` semantics.
The checker has not yet produced that backend's required function records.

A real bridge must lower conversions, lvalue reads and stores, pointer element
scales, control flow, calls and storage consistently with the backend's reviewed
contract. It must also decide how unsupported backend types are diagnosed and
how static object identities become data/relocation references. These are
observable compilation semantics, not a cast between similarly named streams.

## Correctness tests

```sh
./build/compiler test-checker --seed 133 --clang clang
./build/compiler test-checker --seed 1 --clang clang
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
  Known GNU folded-constant and void-indirection extensions have separate
  explicit rejection fixtures, with Clang's differing acceptance recorded
  deliberately.
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
