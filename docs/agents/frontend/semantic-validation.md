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

## Lowering diagnostic inventory

The inventory covers source-dependent rejection sites in `c_gen.c`, including
`failure_message` assignments and direct `c_parse_diagnostic` calls. Resource
limits and failures to construct or verify canonical objects remain lowering
failures. The following groups have semantic owners; the original lowering
checks remain defensive checks for direct lowering callers.

| Lowering responsibility | Semantic owner / shared policy |
| --- | --- |
| Invalid type/specifier combinations, incomplete and void objects, parameter/return layouts | Existing declarator analysis, `c_parse_validate_signature`, `c_parse_validate_vla_declarations` |
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
