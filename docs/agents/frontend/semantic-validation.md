# Semantic validation without canonical IR

[Frontend index](../frontend.md) · [Issue #292](https://github.com/buster14a/buster/issues/292)

`c_analyze_semantics_only` completes the semantic model and the source constraints
that previously depended on lowering. The driver uses its diagnostics and
`analysis_complete` for `-fsyntax-only`; success does not require an `IrProgram`.
`c_analyze_with_options` runs the same validation before canonical lowering, so
syntax-only and ordinary compilation share diagnostic ownership. The older
`c_parse` / `c_analyze_semantics` entry points retain their model-building contract
for callers that explicitly pair them with `c_lower_to_ir`.

The semantic model remains `CAnalysisResult` (`CParseResult` is its compatibility
name). Validation uses its types, entities, declaration ranges, identifier
bindings, delimiter index and scope index. It does not construct a second IR.
Expression/type queries and initializer walks use explicit work stacks. Constant
values contain scalar bits and a C type, including the target's integer width
and floating representation; they are not canonical values or instructions.

## Declaration constraints

Declaration-specifier parsing rejects repeated or conflicting storage classes
before publishing an entity. `c_parse_storage_classes_valid` normalizes GNU
thread-local aliases and retains C23's permitted `auto`, `constexpr`, and
thread-local combinations. A typed `auto` declaration at file scope or with
another storage class still requires type inference under C23 6.7.1p4.

`c_parse_parameter_list_names_validate` checks a completed parameter list before
its names can overwrite function parameter bindings. A scratch hash table belongs
to one published list and reports its first repeated name at the later parameter,
including the earlier declaration's line and column. Unnamed parameters are
skipped, and separate prototype scopes, nested function-pointer lists and C23
unnamed definitions retain their existing rules. Direct, parenthesized and block
local declarators use the same check. The local suffix reader reserves its outer
parameter range before parsing nested declarators, keeping child names outside
that list. `c_test_duplicate_parameter_names` checks syntax-only/lowering
parity, both frontend forms and symbol/spelling lookup, and inspects the outer
parameter names of nested block-local prototypes.

Windows target predefines in `c_source.c` normalize `__inline` and `__forceinline`
to the function specifier `inline`, without injecting a storage class. UCRT-style
`static __inline` and `extern __inline` declarations retain their source storage;
explicit duplicate and conflicting classes remain rejected. The regression checks
canonical symbol linkage for static, extern, and bare aliases using the existing
inline lowering policy; it does not add Microsoft COMDAT emission semantics.

`c_type_parse_root_finish` validates restrict applicability on the type rows a
query appended, after parenthesized function-pointer declarators settle. Direct
pointer construction and GNU `__auto_type` inference use the same object-pointer
predicate. Scalar, void, and function-pointer restrict qualifiers fail; object
pointers, typedef-mediated pointers, and arrays of object pointers remain valid.

`c_parse_type_is_variably_modified` follows array and pointer derivations, so
file-scope identifiers and block-scope identifiers with linkage cannot acquire
variably modified types through typedefs or pointer-to-VLA declarations. The
separate array-only duration check still permits a static pointer to a VLA while
rejecting a VLA object with static duration. Function prototype parameter types
retain their existing rules; the VM walk stops at function types.

Constant negative array bounds are rejected at the original bound expression
by `c_parse_validate_array_bound_values`, after expression/type-name validation
has populated the bound table. A temporary source-token bitset also admits
newly read bound rows while querying each original expression only once.
The existing typed integer query supplies the
target signed-magnitude fact, including narrow casts and 128-bit operands;
masked low-limb values do not decide the sign. Parameter `static`, qualifiers
and nullability words are skipped by the same prefix reader as the syntax check.
Runtime VLA bounds, inferred bounds, flexible arrays and GNU zero-length arrays
keep their existing policies. `c_test_negative_array_bounds` covers five negative
spellings across eleven declaration/type-name contexts in GNU17/C17/C23, with exact
source locations and matching semantic-only/full-compilation refusal. Both
frontend forms validate positive neighbors in those dialects, including
enum/typedef shadowing, and GNU-zero neighbors independently.

Structure and union member validation walks array, pointer and function-return derivations,
including those inherited through typedefs, and rejects variably modified
types at the member's original source site. Each bound is queried in its
declaring scope using the existing typed constant predicate. The check runs
after expression/type-name validation and also visits members appended by its
own bound queries. Completed aggregate definitions retain their source identity,
so deeply nested `sizeof` type names are covered without declaring an object.
Flexible arrays, constant expression bounds and function-pointer prototype
parameters keep their existing rules. `c_test_variable_member_types` checks
rejected file/block/type-name/nested/typedef forms, exact member locations and
valid neighbors through semantic-only analysis and both canonical frontend
forms. This enforces C11/C17 6.7.6.2p2 and 6.7.2.1p9; see
[WG14 N1570](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf).

The bound check uses the semantic typed constant folder. NORMAL-mode sizeof
type operands use the complete abstract-declarator reader, so parenthesized
pointers to arrays and functions retain their pointer size. Its explicit task
stack retains GNU's selected omitted-middle conditional value and its common
branch type, and resolves null-derived member addresses from target layout offsets, evaluating each
array index as a typed child so truncating casts and faults retain C semantics. Numeric pointer
subtraction uses pointee units, so musl's portable null-based offsetof array
bounds remain constant. Runtime pointer bases and indexes still produce VM
types; a folder's unsupported shape is never accepted as a constant. The
protected TYPE-mode and enum constant queries retain their separate refusal
rules; only NORMAL-mode conversions admit numeric pointers at the target width.

`c_parse_tag_lookup` chooses the nearest visible spelling across the existing
struct, union, and enum indexes. A conflicting kind reports `wrong kind of tag`;
an inner tag body or standalone tag declaration can introduce a new identity.
Ordinary identifiers and typedef names retain their separate namespace.

`c_test_declaration_constraints` pins accepted and rejected neighbors for
issues #1854, #1855, #1856, and #1859 through semantics-only analysis and both
canonical lowering forms. Rejected inputs must have source diagnostics and may
not become successful partial programs.

## Lowering diagnostic inventory

The inventory covers source-dependent rejection sites in `c_gen.c`, including
`failure_message` assignments and direct `c_parse_diagnostic` calls. Resource
limits and failures to construct or verify canonical objects remain lowering
failures. The following groups have semantic owners; the original lowering
checks remain defensive checks for direct lowering callers.

| Lowering responsibility | Semantic owner / shared policy |
| --- | --- |
| Invalid type/specifier combinations, incomplete and void objects, parameter/return layouts | Existing declarator analysis, `c_parse_validate_signature`, `c_parse_validate_vla_declarations` |
| Repeated names within one parameter list | `c_parse_parameter_list_names_validate`; completed direct, parenthesized and local declarator ranges |
| Integer literals, typed constant expressions, static assertions, `sizeof`/alignment, enum and designator values | `c_parse_typed_constant`, `c_parse_validate_deferred_assertions`, `c_parse_validate_sizeof_operands`; shared literal selection and floating-point bit helpers |
| Zero-width named bit-fields, explicit alignment, array element stride, alignment redeclarations | `c_parse_validate_bit_field_widths`, `c_parse_validate_alignment_range`, `c_parse_validate_array_strides`, `c_parse_validate_alignment_redeclarations` |
| Initializer shape, promoted members, separators, string width/bounds, automatic range designators, VLA initialization/storage | `c_parse_validate_initializer_shape`, `c_parse_infer_initializer_array_count_core`, `c_parse_validate_vla_declarations` |
| Static scalar folding, calls, address constants, thread-local addresses, compound literal storage, constexpr restrictions | `c_parse_validate_static_initializers`, `c_parse_validate_static_scalar`, `c_parse_validate_compound_literals`; shared literal decoding and extended-float folding |
| Places, qualifiers, updates, indirection, members, indexing, scalar/aggregate/function-pointer conversions | `c_parse_validate_const_assignments`, checked expression/type machine, `c_parse_incompatible_aggregate_value`, `c_parse_incompatible_function_initializer` |
| Arithmetic and conditional operands, standalone expressions, conditions, returns, nested statement expressions | `c_parse_checked_expression_type`, `c_parse_validate_statement_expressions`, `c_parse_validate_return_statements` |
| Named and computed calls, arity, argument conversions, discarded generic associations | `c_parse_validate_named_call_arities`, `c_parse_validate_const_assignments`, `c_parse_validate_generic_duplicates`; `c_semantic_call_accepts_arity` and shared message formatting |
| `_Generic` shape, complete object associations, duplicate compatible types, selected association | `c_parse_validate_generic_duplicates` and the shared semantic expression/type queries |
| Switch braced-body support, controlling types, typed cases/ranges, overlap, nested-case restrictions; break/continue/goto/labels | `c_parse_validate_switch_duplicates`, `c_parse_validate_control_statements`, `c_parse_validate_labels` |
| Label-address conversion, storage, escape, computed target and dynamic aggregate indexing restrictions | `c_parse_validate_label_values` and compact entity provenance facts |
| Builtin arity/types/immediates, variadic access, frame address, complex construction, atomic widths, SIMD shapes | `c_parse_validate_builtin_calls`, `c_parse_validate_atomic_accesses`; shared atomic spelling/arity, SIMD metadata and target layout facts |
| Inline asm constraints, matching operands, register bindings, clobbers, names/labels and x87 stack positions | `c_parse_validate_assembly`; shared scalar-class, bound-register, clobber-conflict, fixed-operand and x87 policy helpers |
| Alias target declaration/definition and competing definitions | `c_parse_validate_alias_targets` |

Canonical allocation capacities, value/block/instruction construction, SSA
resolution, relocation emission, ABI instruction emission, backend instruction
selection and IR verification still belong to lowering and code generation.
Syntax-only is not an object-emission or linker-success guarantee. Target source
constraints are checked using the selected target, without allocating canonical
type tables merely to inspect scalar layout facts.

Validation reuses successful expression types within one function, keyed by
source token identity, token range, scope and checking mode. Checked facts can
answer a type-only query, while an unchecked fact cannot suppress validation.
Failed queries, speculative machine frames and copied semantic models do not
publish cache entries. Immutable scalar query types are created before query
checkpoints; declarator and qualified types remain independent. All borrowed
cache pointers are cleared before the semantic model is returned.

`CParseResult.type_identity_queries` retains successful `_Generic` selections
and `__builtin_types_compatible_p` answers through lowering. These compact rows
contain original token ranges or an integer answer; no frontend identity enters
canonical IR. The shared semantic resolver uses `CType` compatibility, applies
lvalue/array/function conversion only to generic controllers, and preserves
qualifiers below the outermost level. The GNU builtin ignores only outermost
qualifiers. Associations use full abstract declarators, including function
pointers and pointers to arrays. A generic expression retains its selected
expression's type, including when inspected by `typeof` or `sizeof`.

The existing token position index records identity-query candidates once.
Nested queries settle in reverse token order on an explicit work walk; the
expression task stack follows the selected range without evaluating the
controller or discarded values. Append-only answers participate in semantic
result checkpoints. Lowering consumes retained answers; model-building-only
callers resolve missing answers through the same semantic helper.

`c_test_type_identity_authority` inspects the independent expected return
constants in raw canonical IR for both frontend forms on six native layouts.
The `fixtures/type_identity.c` fixture beside the frontend tests repeats qualifier, decay, function
pointer, conditional-pointer and GNU-compatibility answers across enumerators,
static assertions, static initializers, array bounds, case labels and runtime
values. Driver coverage runs it with strict codegen verification under both
frontend forms and all four native allocators. The equivalence table accepts
qualifier-distinguished associations and rejects a missing compatible arm.

The conversion and compatibility rules follow C17 6.3.2.1, 6.5.1.1 and 6.7.3;
WG14 [DR 481](https://www.open-std.org/jtc1/sc22/wg14/issues/c11c17/issue0481.html)
records the historical generic-controller conversion question. The builtin's
outermost-qualifier rule and constant-expression contract follow the
[GCC documentation](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html).

Every type-machine push copies a whole `CTypeParseFrame`, so a frame holds
neither a parse-result snapshot nor the token stream. The frames of one run
share the root query's `CPreprocessResult` by pointer; the root outlives the
run, which pops its frames before returning or failing. The few frame kinds
that can abandon a partial parse (aggregate segments, typeof and `_Atomic`
operands) snapshot into the machine's `frame_checkpoints` row for their own
slot, and root snapshots and rollbacks are passed by pointer. A rollback that
does not continue into a successful parse is masked by its root's rollback, so
`c_test_type_parse_snapshot_rows` checks row independence directly.

A modification destination is typed from its whole operand.
`c_parse_assignment_identifier_is_operand` is the one rule both assignment
scans in `c_parse_validate_const_assignments` use for when the identifier in
front of `=` is that whole operand: a member access, indirection, address-of
or prefix update in front of it names another object, so `*--p = c` converts
`c` to the pointee's type, not to `p`'s. A parameter entity keeps its declared
array spelling, so `c_parse_update_operand_modifiable` and the read-only check
apply C17 6.7.6.3p7 themselves: `argv++` on `char *argv[]` updates the
adjusted pointer, a local or member array stays unmodifiable, and a `const`
inside the brackets (`CArrayBound.is_const`, `int a[const 2]`) makes that
pointer read-only here and in `c_ir_mark_local_read_only`. A qualified
typedef'd array parameter (`const L2 v`) is still treated as a const pointer
on both paths; C17 6.7.3p10 qualifies its element instead.
The embedded modification-destination sources in `compiler_driver_tests`
run these shapes under every allocator and both frontend forms; the
equivalence table holds their rejected neighbours.

`c_parse_validate_label_values` walks a body's assignment, return and call
values -- one scope-chain entity lookup per identifier -- only when
`c_parse_label_values_needed` proves the body takes a label address or
branches through a computed goto. The proof is the walk's own test:
`c_parse_label_address_prefix_proven`, or `c_parse_label_address_cast_at` for
the `(type)&&label` form, whose type name may be a typedef resolved in the
token's scope. Every provenance fact the walk records descends from such a
root, so a body it skips could have recorded or diagnosed nothing. The
conjunction operator shares the spelling; a body with a goto label and
`a && b` is not walked, and a parenthesized sizeof/alignof operand before
`&&` is that operator's operand, not a cast. `c_test_label_values_gate` pins
the gate through the private seam beside the unchanged diagnostics.

## Regression contract

The registered `c_test_integer_semantics_agreement` matrix pins #1577's
nearest-object lookup for `sizeof(x)`, `sizeof x` and `_Alignof(x)` when a
local character array shadows an outer pointer typedef. Literal operands select
the immediate assertion route (historically the legacy route); enumerator-dependent bounds and answers use the
deferred typed route. Both routes require the independent array size 256 and
alignment 1 across six native target layouts, semantic-only/canonical analysis
and both frontend SSA forms. False-value neighbors must report a static
assertion diagnostic and publish no program. GNU17 is explicit because
expression operands to `_Alignof` are a GNU extension. Both routes now prefer the shared typed constant answer when available.
The production shadow guard is the previously landed #1529 repair; this matrix supplies the remaining
explicit route/spelling coverage without changing it.

Lowering checks the query machine's five arrays, missing scope-child indexes,
per-function builder arrays and body tasks before carving them. Ordinary queries
reuse thread scratch. When the finalized scope/query plan exceeds its remaining
reservation, lowering creates a private arena for that checked plan plus the
ordinary arena's remaining workspace (normally 256 MiB), rounded by doubling.
The private arena is destroyed on both successful and failed lowering; no result
may retain its pointers. This grows virtual address reservation, not populated
query depth or committed rows.

Each function preflights its builder arrays and a conservative token-derived
body-task, retained statement-state and SSA workspace plan. The function arena
grows before the first builder allocation and reuses the largest mapping until
the translation unit finishes. Body tasks reserve at most five rows per token
plus four; geometric SSA event/read/local growth includes old arrays, bounded
by four times the canonical row capacity. SSA slots and finish work have their
own allowances in the same plan. These allowances do not claim every possible
SSA graph has linear storage: existing dynamic checks remain authoritative.
Reservations double from the existing size and are checked against
`ARENA_MAX_RESERVATION` (2^48 bytes); unavailable mappings become structured
source diagnostics. Neither the driver's translation-unit reservation nor the
ordinary thread scratch reservation changes.

SSA event/read growth and retained assignment
states check the remaining capacity at each growth or allocation. A refused
query publishes a source diagnostic before creating an `IrProgram`; a refused
function retains its rejected state and cannot certify partial canonical IR.
Private test preflight limits preserve their refusal path and disable reservation
growth.

`c_test_ir_lower_scratch_capacity` exercises both frontend SSA forms, accepted
small source, query and function refusal, a synthetic million-token query plan
under an explicit fixed budget, small and large function reservation plans,
and isolated body-task/SSA-growth exhaustion in a small arena. Arithmetic
controls pin exact fit, alignment, zero count and multiplication overflow.
The private smaller-budget wrapper avoids allocating huge source/IR fixtures in
every test configuration; it does not change driver options. These are resource
limit regressions for #1330 and the query prerequisite of #1412, not a promise
that every allocation in lowering has a recoverable failure path.

`c_test_ir_query_scratch_growth` generates one valid 400,000-element unsigned-byte
initializer. An explicit 256 MiB query budget must refuse that source before
program publication; ordinary lowering must grow, certify and validate canonical
IR, infer the array extent and preserve every independently expected payload byte.
The fixture checks results after the private query arena has been destroyed.

`compiler_driver_test_syntax_diagnostic_equivalence` contains frozen acceptance
expectations, valid neighbors and rejected cases from the migration. Each source
runs through syntax-only and object actions in both frontend SSA forms. The test
compares success/failure, diagnostic and warning counts, structured fields and
rendered diagnostics. Frozen expectations are essential: agreement between two
paths sharing validation cannot alone prove that neither changed acceptance.
An explicit invalid const-pointer assignment under an unbraced control statement
is now rejected; main previously missed that qualifier check. The neighboring
mutable assignments, including a directory-macro-shaped statement expression,
remain accepted. This diagnostic correction is not counted as baseline parity.

With `BUSTER_BENCH_ALLOCATIONS=ON`, the same cases require every canonical
construction counter to remain unchanged across syntax-only. The counters cover
program initialization, type/symbol/global creation, blocks, values and
instructions. Valid object compilations are positive controls requiring program
and type construction. Keep timing builds uninstrumented.

For further validation, compare the same source/flags/target against an immutable
main compiler, including the tracked C fixtures, standalone frontend test inputs,
large translation units and the compiler unity source. Then run the complete
suite, sanitized suite and byte-identical self-host fixed point. Record compiler
identities, input identities, actual results and limits in a new performance
audit; do not substitute cross-path agreement for a baseline comparison.

The final member check defers failed bound classifications until its live
member walk has materialized nested aggregate definitions. It retries only failed
candidates and visits physical members appended by retries, until neither member
rows nor unique completed source-backed aggregate definitions grow. A lazy scratch
bitset keyed by definition tokens includes GNU empty records; qualified copies
and temporary type-only derivations do not count as progress. It then reports
VM members, so a valid deeply nested sizeof
bound does not become a runtime bound merely because its first layout query
could not yet resolve a copied type name. Pending rows are sparse scratch data,
released after validation; completed definitions reuse their source identity.
The multidimensional constant/runtime pair pins a later array derivation that
first materializes a member during retry, including its exact source diagnostic.
A GNU17-only empty-record dimension pins completion without member-row growth.
