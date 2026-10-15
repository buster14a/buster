# Semantic validation without canonical IR

[Frontend index](../frontend.md) · [Issue #292](https://github.com/buster14a/buster/issues/292)

`c_analyze_semantics_only` completes the semantic model and the source constraints
that previously depended on lowering. The driver uses its diagnostics and
`analysis_complete` for `-fsyntax-only`; success does not require an `IrProgram`.
`c_analyze_with_options` runs the same validation before canonical lowering, so
syntax-only and ordinary compilation share diagnostic ownership. The older
`c_parse` / `c_analyze_semantics` entry points retain their model-building contract
for callers that inspect the semantic model.
The model-only `c_parse` result cannot now be lowered directly: callers that
construct canonical IR use `c_parse_for_lowering`, which completes the same
source constraints once and retains the required syntax and semantic outputs.
`c_lower_to_ir` inherits these outputs from the analyzed model and never
constructs a tree or runs another semantic pass. Explicit historical parser
controls use `c_parse_ast_reference`; ordinary `c_parse_ast` builds the tree
once and reads its declaration split. The caller's type-interning policy is
unchanged.
The model-only `c_parse` endpoint uses its original token declaration split and
tree-free semantic query method and type inventory. Independent model
tests keep that output for inspection and prepare a separate full-authority
result when they also exercise canonical lowering.

At the end of lowering-constraint validation, file-scope tentative object
definitions are checked against their final translation-unit types. An object
whose type is still incomplete is rejected before semantic-only success. An
external-linkage tentative array with a complete element type is completed to
one element on a per-object type copy under C17 6.9.2p2; a later tag or array
completion and an extern-only declaration remain valid. C17 6.9.2p3 also
requires every internal-linkage tentative definition to have a complete type
at that declaration, using the compatible composite type visible at that
point. A prior known array bound, or a bound inferred from an earlier initialized
unsized array, can therefore complete a later `[]` tentative declaration, but a
later bound cannot rescue an earlier `[]` declaration; raw declaration and
typedef types retain their source-spelled bounds.
End-of-unit completion does not make an earlier `sizeof` of an incomplete
array valid. The registered
compiler_driver_test_tentative_object_completeness fixture checks
syntax-only/object diagnostic parity, declaration completion, exact final
array symbol sizes, the internal-linkage declaration-point constraint, and
point-of-use refusals in C17, GNU17, C23 and GNU23 across both canonical
frontend forms and the AST pilot.


The semantic model remains `CAnalysisResult` (`CParseResult` is its compatibility
name). Validation uses its types, entities, declaration ranges, identifier
bindings, delimiter index and scope index. It does not construct a second IR.
Size and alignment operator recognition in semantic analysis and lowering uses
the existing well-known symbol IDs. `C_ALIGNOF_WORDS` preserves `_Alignof`,
`__alignof` and `__alignof__`; `C_SIZEOF_ALIGNOF_WORDS` additionally accepts
`sizeof`. Readers that accept plain C23 `alignof` retain their separate rule.
Uninterned tokens retain spelling lookup. `c_test_word_class_token_kinds`
checks the spelling and ID paths, including tokens with no accessible spelling.
Expression/type queries and initializer walks use explicit work stacks. Constant
values contain scalar bits and a C type, including the target's integer width
and floating representation; they are not canonical values or instructions.

`c_parse_validate_offsetof_operands` checks fully constant `__builtin_offsetof`
designators in static initializers and function bodies with the existing typed
constant/member walk. Negative and noninteger indices, malformed member paths,
and target-width offset overflow therefore fail before canonical construction,
including in unevaluated operands. Nonconstant indices remain owned by runtime
lowering. `c_test_offsetof_typed_refusals` pins semantic refusal in all four
expression contexts; the driver diagnostic-equivalence matrix covers those
boundaries alongside cast-truncated indices, scoped enumerators and valid
unevaluated operands. This addresses the constant-designator validation residual
of [#1570](https://github.com/buster14a/buster/issues/1570); shared parser/lowering
member authority and nonconstant-index validation remain separate work.

The iterative `c_parse_expression_place_range` helper follows only the
retained selected-expression range for `_Generic`; it never infers a place
from the controller or discarded associations. Place shape, modifiable-update
checks, address eligibility and register-address validation consume that same
selected range. Selected ordinary objects and members remain addressable, and
selected function designators remain valid address operands. Enumerators and
C23's `true`, `false`, and `nullptr` scalar constants remain non-places;
before C23, `true` and `false` may still name ordinary objects. The bit-field
validator continues to reject selected bit-fields before lowering (#3758).
Register-address validation uses the shared unary-address classification and
full prefix-operand extent, then normalizes through the retained selected
range. Under [WG14 DR 017.06](https://open-std.org/JTC1/SC22/WG14/issues/c90/issue0017.06.html),
member access and array subscripting retain the register root, so `&(v.i)`
and an array member's element remain non-addressable. Arrow access, pointer
subscripts and unary dereference reach another object and retain their valid
pointee behavior. Prototype-style parameter records retain their register
storage on the parameter entity through array adjustment; prior compatible
prototypes do not supply storage for a later definition. Parenthesized selections and a surrounding `(void)` cast
stay under the register-address constraint without misclassifying binary `&`.
`c_parse_validate_bit_field_addresses` checks unary address operands against
semantic member widths before constant folding, including parenthesized,
indirect and promoted anonymous members. GNU `__extension__` prefixes before
`&` and around its operand retain the same constraint. Unevaluated and
unselected operands retain this constraint; binary `&` remains a value
operation. The registered `compiler_driver_test_bit_field_addresses` matrix
covers selected ordinary object/member positives beside selected-bit-field
negatives, selected-enumerator and C23 keyword-constant address/update
refusals, direct, selected, and Generic-postfix register-member and array-subobject
refusals, adjusted register array-parameter address controls, a wide promoted
anonymous-member lookup followed by cache reuse, and
direct/parenthesized/cast selected-register address refusals, C17 objects named
`true`/`false`, and valid pointer-member, arrow/dereference, selected
update/function-address, binary and pointee controls. It checks syntax-only/object diagnostic parity and no
object on refusal across four C dialects, three frontend routes and three
target layouts (#3743, #3758).

For a direct prefix or postfix update of a selected `_Generic` place, C lowering
defers the selection to its iterative place frames. That path lowers only the
retained association as a place, forcing its call-preparation pass even inside
an enclosing call argument; selected-arm calls therefore run once. Generic
update ownership and the selected arm use retained syntax nodes, preserving
the topology-based place and call walks without reintroducing token walkers. Ordinary
Generic values and Generic-selected call callees retain the expression path.

## Block-scope static initializers

In GNU dialects, a block-scope scalar static initializer whose complete
expression is a statement expression with one expression statement can be
folded when the existing typed scalar evaluator proves that tail constant.
Aggregate destinations and tails that read mutable globals, update values or
call functions retain the statement-expression refusal. The parser and folder
use the same bounded tail range. Bodies with declarations, multiple statements
or an evaluated nested statement expression remain nonconstant. File-scope
statement expressions remain rejected by the separate file-scope constraint.
That constraint excludes opaque unknown-namespace attribute argument clauses:
`[[vendor::tag({gnu::error("m")})]]` is a balanced token sequence, with no
expression or GNU error binding inside it. The retained ATTRIBUTE_SCOPED
namespace/name nodes and the semantic candidate index bound that context;
the existing token proof still checks real GNU expression operands, including
known attribute arguments and definition parameters outside the body.
The registered driver regression checks the constant-only runtime result
alongside aggregate and nonconstant-tail refusals.

## Declaration constraints

A block declaration with a parenthesized declarator must name an identifier at
the declarator position. A later identifier in a malformed group cannot supply
the name: `T(1, x)` remains invalid even when `x` is in scope. A size or alignment
type operand whose specifiers leave a parenthesized abstract declarator is read
through `c_parse_identity_type_name`, including its parameter declarations and
array suffixes, before semantic-only success. Numeric parameters such as
`sizeof(T(1, 2))` and `sizeof(int (*)(int, 2))` therefore receive structured
diagnostics before lowering. Valid grouped declarations, abstract function
pointer/array types, `_Generic` association types and value names shadowing
typedefs retain their existing paths. The registered
`compiler_driver_test_numeric_declarators` matrix checks syntax-only/object
diagnostic parity and output absence across four dialects, both SSA forms and
all three AST/query routes (#4301).

Before C23, a `for` initializer declaration may introduce only automatic or
register objects (C17 6.8.5p3 and WG14 DR277). The local declaration binder
uses its for-initializer context to reject typedefs, static/extern/thread-local
objects, function declarations, and tags or enumerators introduced by direct
aggregate specifiers. References to existing tags and typedefs remain valid,
as do automatic function pointers. C23 removes this contextual restriction.
Wrapped `_Atomic` and GNU `typeof` specifiers also check their direct tag tokens
against the existing tag index, so newly introduced incomplete tags cannot
escape the rule merely because the specifier returns a pointer type.
The diagnostic belongs to the declaration's first specifier; binding continues
so uses of the rejected declaration do not create secondary name diagnostics.
Initializer-nested expression declarations retain their separate context.
`c_test_for_declaration_constraints` freezes rejection/acceptance on both sides
of the C17/GNU17 versus C23/GNU23 boundary, through semantics-only analysis and
both canonical frontend forms on three target layouts.

Before resolving a block-scope `typeof` declaration, the iterative block binder
schedules each GNU statement-expression body in its type operand, then resumes
the declaration at its source point. The trailing expression can resolve body
locals, while the names remain private to that operand. Object and typedef
declarations, sibling operands, for-header declarations, deep nesting,
syntax-only checks, code generation and name privacy are covered by
`c_test_typeof_statement_expression_declarations` (#1254).

The binder and canonical body lowerer find `for` header separators only outside
parentheses, brackets and braces. Member-declaration semicolons in a direct
aggregate definition or initializer compound literal therefore stay inside the
first clause. Anonymous aggregate objects remain valid before C23; a named
aggregate definition still follows the contextual tag rule above. The same
regression pins acceptance and canonical validity of these clauses through
both frontend forms.

Declaration-specifier parsing rejects repeated or conflicting storage classes
before publishing an entity. `c_parse_storage_classes_valid` normalizes GNU
thread-local aliases and retains C23's permitted `auto`, `constexpr`, and
thread-local combinations. A typed `auto` declaration at file scope or with
another storage class still requires type inference under C23 6.7.1p4.

Storage-class context validation uses the same specifier reader: parameters
permit only `register`, and resolved function declarations cannot carry thread
storage (`_Thread_local`, C23 `thread_local`, or GNU `__thread`). Direct and
nested parameter readers validate the specifier prefix before array adjustment,
so `int a[static 4]` remains valid. File and block function declarations are
checked after type resolution, including function typedefs and prototypes.
`c_test_storage_class_contexts` covers four dialects and three target layouts;
the driver diagnostic-equivalence matrix compares syntax-only and object paths.

Windows target predefines in `c_source.c` preserve the `__inline` spelling and
map `__forceinline` to it, without injecting a storage class. The existing needed
function dependency walk decides which header bodies are reachable; the two late
body decisions retain those Windows definitions, including transitive UCRT option
helpers. The existing entity-definition map shares that decision across every
redeclaration, including later prototypes. Unused header bodies stay omitted.
Ordinary `inline`, GNU `__inline__`,
explicit GNU-inline attributes and non-Windows targets retain their rules.
`static __inline` and `extern __inline` retain source storage; duplicate and
conflicting classes remain rejected. This bounded compatibility policy does not
provide full MSVC mixed-spelling synonyms or multi-TU COMDAT coalescing.

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

File-scope object and function, block-scope and enumerator (file and block scope)
redefinitions, and conflicting declarations (including the static/non-static
linkage pair), name the entity and print the line and column of the earlier
declaration, in the `(previous declaration at L:C)` form parameters use:
`redefinition of 'x' (previous declaration at 1:5)`,
`redefinition of enumerator 'RED' (previous declaration at 1:10)`. The site is the
entity's first declaration (a prototype before the first definition, not the
definition) mapped through the preprocessor source map, so `#line` and macro
expansion sites read as the diagnostic's own do. When the earlier site is in
another file than the diagnostic (an included header, or a `#line` that renamed
the file) it is prefixed with that file's path (`previous declaration at
header.h:1:5`). The diagnostic always sits at the later of the two declarations:
file-scope enumerators are published after every object and function, so an
enumerator declared first is compared by final token order with the entity found
and, when that entity follows it, the error is `redefinition of 'A'` at the entity
naming the enumerator as the previous site. The text is formatted only when the
diagnostic is emitted. `c_test_redefinition_names_previous_site` checks the
spellings and both orders; `compiler_driver_test_record_diagnostic_equivalence`
pins them for syntax-only and object output, including a header-first case.
Residue: the previous site is not yet the first definition, `duplicate member`
and tag redefinitions carry no site, and a header included twice (or a macro
expanded twice) maps both declarations to one source position, so the previous
site equals the diagnostic's own (#1432).

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

Array declarators record function and incomplete element types at construction,
including typedef, parameter and abstract declarators. Aggregate definition
extents distinguish a completed nested definition from the enclosing record or
a tag completed later. The bound-constraint walk reports each invalid source
bracket once before canonical lowering; later tag completion cannot erase the
refusal. Incomplete outer bounds, flexible members, GNU zero-length arrays,
VLAs and pointers to incomplete types retain their separate rules.
`c_test_array_element_constraints` covers rejected and accepted neighbors on
three target layouts, four dialects and both canonical frontend forms; the
driver equivalence fixture pins syntax-only/object parity (#3744).

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

The typed-constant precedence scan reuses a completed no-operator verdict
only for unchanged cast and unary suffixes. Each type-name probe must preserve
semantic inventories and mutation records; only identifier-only primitive
scalar type-name runs can certify reuse. Constructor, `typeof`, attribute,
array and function forms retain their existing scan order. Exposing a
parenthesized group or a selected generic/choose arm starts a fresh scan;
binary, builtin and array-dependency children also begin independently.
`c_test_constant_prefix_scan_work` counts actual visits at geometric sizes and
retains the original scan as a quadratic negative control.
`c_test_constant_prefix_scan_boundaries` compares model inventories, protected
query rollback, structured diagnostics and global images against that control.
The original deep wide-float initializer population is unchanged.

The typed-constant precedence scan keeps `sizeof` and the alignment operators
as unary prefixes while it finds binary splits. A parenthesized type operand
belongs to that operator, rather than being mistaken for a cast, and GNU
`__extension__` remains a prefix through the full unary operand. The existing
expression-type and layout queries still validate the complete inner operand
before folding, so an incomplete type inside nested `sizeof` remains an error.
The registered `compiler_driver_test_local_sizeof_static_asserts` fixture checks
nested unary, dereference, alignment, extension, parenthesized and type-name
forms, GNU statement-expression neighbors, incomplete-inner refusal, syntax-only
and object parity, and execution in both canonical frontend forms (#3566).

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

## Constant array object sizes

`c_parse_validate_array_object_sizes` diagnoses a nonzero-element array whose
constant byte product exceeds the target size_t width or 61-bit byte limit,
before static-initializer validation and again for newly materialized body
query types. This follows Clang 18's
[`ConstantArrayType::getMaxSizeBits`](https://github.com/llvm/llvm-project/blob/llvmorg-18.1.3/clang/lib/AST/Type.cpp),
which caps size_t at 61 bits so bit sizes fit u64; it is not a PTRDIFF_MAX rule.
The inspected upstream source is Apache-2.0 WITH LLVM-exception; no code is
imported. Buster's first-party license remains unselected under #621.

Admitted cached layouts need no new constant evaluation. Simple runtime
identifier bounds remain VLAs. An unresolved/oversized legacy layout, or a
bound with wide-integer provenance, uses the isolated TYPE query before a new
source diagnostic is issued; narrowing casts are not rejected merely because
the legacy layout retokenizer erased a cast. The ordinary type-layout evaluator
and declaration-point authority are unchanged. The protected query copies all
existing parameter, alignment and diagnostic rows, but reserves private append
space from its expression extent instead of copying the unit's unused table
capacities. The isolation fixture repeats accepted and refused queries with
synthetic UINT32_MAX spare capacities and checks unchanged shared rows and exact
integer facts; it never allocates those synthetic tails. Canonical array construction
checks division before multiplication, and direct lowering retains a source
report rather than silently losing an oversized global. High integer limbs
cannot become a small direct-lowering array count.

`c_test_array_object_size_limits` covers independent literal boundaries,
product overflow, nested/member/local/prototype/pointer/typedef contexts,
wide and typedef-mediated bounds, syntax-only/full/direct-API refusal, narrowing
casts, zero bounds, VLAs and flexible arrays across six native layouts plus
Wasm32/Wasm64 and both frontend forms. Types at the accepted limit have no
backing object in the regression. GNU arrays of zero-sized elements retain
zero-byte products; existing static range-designator tests pin that extension.
Record member sums and final alignment rounding use the shared saturating
record cursor (#3278). Semantic-only validation also covers complete records
whose layout no expression has queried: one uncached array or complete record
triggers a complete-pending ordered solve, and the existing cache scan reports
oversized records at their definition. The solve commits every settled pending
row once; validation does not add a solve per record.

A static assertion that measured an oversized type folds a value from its
saturated size, so the size error stands for it. The decision is local to the
assertion: `c_parse_type_layout_core` counts answers past the limit in
`CObjectSizeFacts`, and `c_parse_static_assert_check` and
`c_parse_validate_deferred_assertions` suppress only an assertion whose own
fold moved that count. Every other failing assertion is reported. An immediate
failure gates the lowering constraints, so it requests the size validation and
`c_analyze_semantics_core` runs it once at the end: no assertion validates or
scans diagnostics itself. `c_test_array_object_size_limits` pins unrelated
assertions before and after an oversized type in immediate, function-scope and
deferred (enumerator, `_Generic`) forms, and
`c_test_static_assert_object_size_scaling` bounds layout queries for 2,000
failing assertions over 2,000 array types.

The validator must not add one whole-table solve per array or record. A
pass solve copies and seeds the whole type table; inferred bounds are checked
from their element layout and known count, while one uncached array or complete
record triggers the ordered complete-pending solve that warms every settled
layout for the existing cache scan. This keeps the new record coverage to
one solve per validation call rather than one per record; special unresolved
bounds retain the per-item exception documented below. Asking each array in
table order previously cost one solve per array: self-hosting `ide.c` went from
about 160 to about 34,000 solves (#2406's merge).
`c_type_layout_test_array_validation_solves` and the unqueried record row in
`c_type_layout_test_validation_scaling` require pass solves to stay constant
and layout work to grow linearly as their item counts increase.

## Whole-unit pass scaling

`c_parse_validate_lowering_constraints` runs passes over every type, member,
declaration, entity or bound. Any per-item query that can do whole-table work
makes such a pass quadratic in the translation unit. Three kinds of query can:

- **A layout miss.** `c_parse_type_layout` with the analysis machine is a
  whole-table pass solve whenever the type is not yet committed to the
  machine's layout cache.
- **A protected TYPE query.** `c_parse_type_integer_constant_query_core` copies
  the parameter, alignment and diagnostic rows, and solves layouts on a
  private machine with no cache.
- **An `offsetof` miss.** An idle machine replays committed member layouts;
  an uncommitted or provisional layout still needs the whole-table pass.
  Queries inside a live type machine retain ordered solves until #1247 makes
  their evaluator reentries side-effect-free (#1297).

Member-offset padding fixtures vary unrelated arrays, not aligned typedefs.
The sparse type-alignment lookup still scans the alignment rows per query;
that separate scaling axis remains outside the counted layout work (#1297).

Scope lookups (`c_parse_scope_for_token`, the scope cursor) cost the scope
depth, not the table. No pass scans all diagnostics, types or declarations per
valid item.

A layout that depends on an initializer-inferred bound is provisional while
inference can still rewrite that bound. Once the validation's own inference
loop finishes, the machine sets `inferred_bounds_final`. From then on, later
writes only give a count to a bound that had none, so those layouts commit
like any other. Before that, every `sizeof table` of an inferred array paid a
whole-table solve at each use (#3096).

| Pass | Per-item work | Whole-table work per item |
|---|---|---|
| `c_parse_validate_array_bound_syntax` | constant-expression syntax walk per bound | none; token walk |
| `c_parse_infer_file_array_bounds`, local inference loop | initializer walk per unsized array | layouts and typed constants per element; run before `inferred_bounds_final` |
| type-alignment entries, declaration alignment, `c_parse_validate_alignment_redeclarations` | `c_parse_validate_alignment_range` per specifier | a layout miss; a protected query for `_Alignof(x.m)` operands |
| `c_parse_validate_array_object_sizes` | one layout per bound, one complete-pending warm-up per call for arrays and unqueried complete records | a bound the layout pass cannot fold (`offsetof`, `_Alignof(x.m)`, `sizeof ident`) never commits, so each such array misses (#3113) |
| `c_parse_validate_members`, `c_parse_validate_member_types` | alignment range per member; bound and unresolved width typed constants | a layout miss; uncommitted `offsetof` in a member bound (#1297) |
| `c_parse_validate_deferred_assertions` | typed constant per deferred assertion | uncommitted `offsetof` (#1297); a protected query for `_Alignof(x.m)` |
| `c_parse_validate_static_initializers` | expression queries and typed constants per initializer element | uncommitted `offsetof` (#1297); a protected query for `_Alignof(x.m)` |
| variably modified objects | typed constant per array bound in an object's type | uncommitted `offsetof` in a bound (#1297) |
| `c_parse_validate_array_strides` | element layout per array of an aligned type | a layout miss |
| `c_parse_validate_array_bound_values` | typed constant per distinct bound | uncommitted `offsetof` in a bound (#1297) |
| `c_parse_validate_alias_targets` | declaration-binding scan | none from the table; see #3114 for split declarator lists |

The census found further costs that no layout counter sees; #3114 records them:

- split declarator lists rescan the declaration per declarator;
- protected queries copy rows that grow with the unit;
- array-bound folding scans every enumerator for each bound identifier that
  is not a constant entity.

The duplicate-diagnostic scan in `c_parse_validate_array_object_sizes` costs
O(diagnostics) per oversized array. That makes it quadratic only in the number
of oversized arrays, and it runs only on the error path.

`c_type_layout_test_scales` is the shared fixture helper. It compiles `count`
copies of an item through `c_analyze_semantics_only` at 16 and 256 copies and
requires:

- pass solves to stay constant;
- every other layout-work counter to grow at most linearly.

`c_type_layout_test_validation_scaling` has one row per pass above that can
reach a layout per item. Each row reaches its pass through an inferred-array
`sizeof`, and those rows fail with `inferred_bounds_final` disabled.
A new pass over every type, member or declaration needs a row in that fixture,
or its own scaling fixture, in addition to single-item correctness cases.

## Typedef names in expressions

Checked expression leaves reject a typedef binding as a value, including a
name followed by a call argument list. An unsuccessful complete named-call leaf must
publish the same constraint as a lone identifier rather than returning an
unknown type without a diagnostic. Type-name operands in casts and sizeof
remain owned by their type frames; successful value and builtin leaves keep
their existing path. The registered `compiler_driver_test_typedef_expressions`
fixture checks syntax-only/object diagnostic identity and absent failed output
for direct and nested callees, conditions, casts and ordinary typedef-name
values in C17/GNU17/C23/GNU23, both SSA forms and AST/query/explicit-layout routes.
Grouped declarations, type operands, indirect calls and values shadowing typedefs
remain accepted (#4299).

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
| Initializer shape, promoted members, separators, string width/bounds, VLA initialization/storage | `c_parse_validate_initializer_shape`, `c_parse_infer_initializer_array_count_core`, `c_parse_validate_vla_declarations` |
| Automatic GNU range designators: lowered by `c_ir_lower_nested_compound_literal_step`, which stores element `first` once and copies it over the range (`c_ir_nested_initializer_copy_range`); a range followed by or inside a designator chain, or whose value brace-elides an aggregate element, is rejected there | Lowering only; static initializers accept every form |
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

`CParseResult.type_identity_queries` retains successful `_Generic` selections,
`__builtin_choose_expr` selections and `__builtin_types_compatible_p` answers
through lowering. These compact rows
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
controller or discarded values. A `__builtin_choose_expr` row
(`c_parse_choose_expr_builtin`) selects the arm its integer constant condition
picks, so the semantic typer and constant evaluator see that arm's unconverted
type and value; a choice in the condition starts later and is already settled
when the walk evaluates it. A condition that is not an integer constant
records nothing. `__builtin_constant_p(x)` is typed as `int` by the
expression task stack without a row; its value belongs to lowering. Each query
re-walks its range, so a site whose row is retained is recognized from its
slot's hint without a search.

Lowering follows the same `__builtin_choose_expr` row through
`c_ir_selection_answer`, as it does for `_Generic` (#3860). Prepared-call
lowering, `c_ir_constant_evaluate` (static-storage array bounds) and the strict
sizeof walk take the retained arm, so a choice whose condition is another
choice needs no folding in lowering. They fold the condition themselves only
when the parser recorded no row, which happens for a `__builtin_constant_p`
condition. A deferred `_Static_assert` does not reach lowering's evaluator at
all: lowering consumes the stored verdict (see the static-assertion entry in
[foundations](foundations.md)). `c_test_builtin_operand_types` lowers and
runs those forms in both frontend forms. Append-only answers participate in semantic
result checkpoints. Lowering consumes retained answers; model-building-only
callers resolve missing answers through the same semantic helper.

### Sizeof handoff comparison (#2531)

Lowering does not yet read the semantic type of a `sizeof` expression
operand; it derives the operand's size itself. A default-off, test-only
comparison measures whether the two agree before any lowering consumer is
made to depend on semantic answers. `c_test_sizeof_handoff_set` turns it on
for the calling thread. While it is on:

- Semantic analysis keeps a 16-byte `CSizeofOperandType` row in
  `CParseResult.sizeof_operand_types` for each `sizeof` expression operand it
  types as a scalar integer. The row holds the keyword token, the scope the
  producer typed it in, the unqualified kind (an enum's underlying kind) and
  the size. It holds no `CTypeId`, so a rollback cannot leave it naming a
  truncated row. The table is append-only and part of result checkpoints, and
  a protected constant query copies it on first append.
- Rows have two producers. `c_parse_typed_constant` contributes the operands
  it already types in constant contexts in the normal mode. The
  `c_parse_validate_sizeof_operands` walk asks for the operand type of
  runtime `sizeof`s for this comparison alone, because ordinary validation
  types nothing inside a `sizeof`.
- Rows of the two producers for one keyword are also compared with each
  other: a different kind or size is counted as a conflict. Each producer
  keeps its own row and counts.
- Lowering reports every size the `C_IR_QUERY_FRAME_SIZEOF` resolver settles,
  and the body's prediction fallback, to `c_sizeof_handoff_compare`. Each path
  of `c_ir_sizeof_expression_attempt` records the operand type it sized; when
  that is an integer type, its signedness is compared as well as its size.
  The outcomes are agree, size mismatch, signedness mismatch, declined, or
  unmatched (no row for that keyword). Rows whose producer scope differs from
  the keyword's innermost scope are counted separately.

The fields exist only in builds with tests; there they add 16 bytes to
`CParseResult`, which the default `ide` build also carries. When the
comparison is off, the only work is a flag test at each recording and
comparison site. When it is on, the emitted program does not change: the
fixture compares it with the program lowered with the comparison off.

`c_test_sizeof_semantic_handoff` covers the following in both frontend
forms:
- the 64-operator cast operand;
- qualified, promoted, enum, bit-field-promoted, conditional, comma,
  `_Generic` and cast-to-`typeof` operands;
- a shadowing block;
- static assertions.

It runs each case for Linux x86-64, Windows x86-64 and Linux AArch64 layouts.
It checks:
- each row's kind;
- that the syntax-only and `-c` analyses keep identical rows;
- that a protected query leaves the rows unchanged;
- that signedness was compared and agreed.

As a negative control, `c_test_sizeof_handoff_perturb_set` flips the
validation rows' signedness while keeping their size. The comparison must then
report signedness mismatches, no size mismatch, and a conflict for each static
assertion operand that both producers keep. The fixture also runs the
`c_test_tall_expression_types` heights (1, 2, 63, 64, 65, 4096 and 10000) with
the comparison on. That is the sampled sweep the regression already uses; a
dense 1 to 10,000 sweep would lower about 50 million operands.
`c_ast_test_conditional_publication_work` separately counts ordinary semantic
work and the guarded publication summary at geometric sizes for both the
`sizeof` conditional and `_Generic` controller. Its forced-original completion
control retains the repeated suffix work. The paired authority fixture checks
complete model inventories, diagnostics and exact conditional facts, including
constructor, member-width, loose-operator, projection and refusal neighbors.
`c_ast_test_sizeof_handoff` runs the comparison over the `c_ast` corpus and
the hosted frontend sources and enforces floors on both. At `4ed35b59` on
Linux x86-64 it keeps about 7,230 rows (17 from constant contexts). Of those
operands, 7,165 are settled by the resolver with 0 mismatches. Another 7,515
resolver answers have no row; they are operands outside the scalar-integer
population. The prediction path is not reached. The validation producer made
about 14,750 operand queries that ordinary validation does not make.

Prepared `_Generic` lowering consumes the selected token range and lets its
ordinary child expression produce the value and canonical type. It does not
predict a selected type that the caller discards. Association duplicate checks
keep up to sixteen type IDs locally; larger lists grow on accepted typed arms,
independently of their expression token spans, and rewind at query completion.

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

Production type-machine frames live in lazily allocated, stable blocks
owned by the analysis machine arena. A frame keeps a pointer to its predecessor,
while fixed-array query/test machines retain their bounded array storage. The
production stack limit is derived with checked arithmetic from the input token
count plus delimiter-based grammar allowance; it does not impose a fixed cast
depth. The root query's `CPreprocessResult` is shared by pointer and outlives
the run, which pops its frames before returning or failing.

Rollback snapshots use a separate reusable pool rather than one
`CParseResult` row per frame. Aggregate-segment frames (owned by the aggregate
brace) and SCALAR frames that parse `typeof`/`_Atomic` operands (owned by
their opening parenthesis) save snapshots; flat cast chains do not. Their live
snapshot count is bounded by the delimiter census and checked separately. On
failure the machine walks frames top-down, releases each frame's accounting,
and recycles snapshot slots and emptied storage blocks. It then restores the
oldest discarded `sizeof` task and scratch-arena marks.
`c_test_type_parse_snapshot_rows` continues to check row independence in the
fixed-array test machine.

GNU `__attribute__((fallthrough));` and its `__fallthrough__` alias
are null statements, including in C99/GNU11/C17. Leading attribute lists
are skipped by the lowering body walker; `c_parse_validate_gnu_fallthrough`
therefore checks the empty statement and zero-argument constraint (allowing
an empty parenthesized parameter list) before lowering can erase the attribute prefix. Other attributes retain their own
handling. Embedded driver regressions cover both spellings, dialects, both
frontend forms and both native allocators, with syntax/object diagnostic
equivalence for a missing semicolon or attribute arguments.

A modification destination is typed from its whole operand.
Function-typed destinations are refused before assignment-source conversions,
including direct, parenthesized and dereferenced designators in unevaluated
operands. Function pointer objects retain their pointer type and remain valid
assignment destinations. The driver diagnostic-equivalence corpus covers these
negative and positive neighbors in C17/C23 and both frontend SSA forms (#3746).
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

The indexing scan distinguishes an array declarator from a subscript by its
parsed `CArrayBound` bracket identity. One scratch bit per source token records
the real opening brackets before body validation; synthetic inferred bounds
have no bracket identity, and cloned bounds retain the original one. Expression
record prebinding also recognizes leading type qualifiers before `struct` or
`union`, so their member bounds exist before this role snapshot. The mask
outlives each body's scratch checkpoint and skips only the declarator opener,
so `int c; struct T { char c[8]; };` respects the separate member namespace
(C17 6.2.3p1). Expression subscripts also bypass the broader local-declarator
mask, so invalid subscripts inside a bound remain checked even when body
binding recorded a declaration inside an expression record's brace scope.
`c_test_member_array_declarators` covers tag-only and object declarations,
anonymous records inside local array bounds (including C11 static assertions),
unions, shadowing, macros, derived members and expression neighbours through
semantics-only analysis and both canonical frontend forms. Its runtime source
checks member storage under all four native allocators and both forms.

`c_parse_validate_label_values` walks a body's assignment, return and call
values -- one scope-chain entity lookup per identifier -- only when
`c_parse_label_values_needed` proves the body takes a label address or
branches through a computed goto. The proof is the walk's own test:
`c_parse_label_address_prefix_proven`, or `c_parse_label_address_cast_at` for
the `(type)&&label` form, whose type name may be a typedef resolved in the
token's scope. The scoped prefix proof treats a balanced `typeof (...)`
specifier as one type-name component; its operand remains owned by expression
or type-name semantics. Operand matching uses the position index when present
and the scalar delimiter reference when absent; the registered fixture checks
both proof paths, including an ordinary conjunction inside the typeof operand. The machineless cast reader falls back to that proof
for decorated types it cannot resolve. A preceding sizeof/alignof group is
still an operand, and ordinary conjunctions still bind ordinary identifiers.
`c_test_typeof_label_address` covers GNU17/GNU23, all six native target
layouts, both canonical forms, and default/pilot verified execution under
FAST/QUALITY, with undeclared conjunction/sizeof/alignof/typeof neighbors.
A complete root equality/inequality is a provenance sink: its result is the
ordinary integer produced by canonical pointer equality. Logical `&&` and
`||` compositions preserve that ordinary boolean result only when every
operand's label provenance is consumed by a comparison. An explicit guarded
worklist splits top-level logical operands once and uses indexed delimiter
jumps for nested groups; it never recurses. Direct label-pointer truth
conversion and comma and cast transports retain the conservative policy.
A conditional transports its result arms after the condition's provenance
has been consumed by a comparison or valid logical boolean composition.
Direct label-pointer truth retains the existing conservative escaped-taint
policy for returned values, static storage, call arguments and later uses of
local assignments. Discarded expressions, control positions and unused local
assignments retain their existing acceptance; a complete equality remains a
sink even around a conditional boolean. The GNU omitted true arm still
transports the condition's value, checked once. The
same guarded worklist uses the semantic conditional/delimiter index to route
these ranges without recursion or an AST requirement; reference-token and
retained-tree semantic diagnostics must agree. Pointer result arms,
return/call/static-storage and invalid computed targets remain rejected.
`c_test_label_comparison_logicals` covers the frozen typeof witness,
a plain reduction, nesting, both logical operators, short circuits and
exactly-once effects, with independent hosted GCC/Clang expected-value runs and
reference-token/retained-tree semantic parity, debug/no-debug, SSA/memory and
FAST/QUALITY controls. The computed-goto machine fixture additionally retains
its original ten functions, including the comparison-conditioned ordinary
integer return, through both native backend selectors and allocators.
Conditional result ranges also carry their owner's proven delimiter depth.
At the same depth the owner's root proof excludes lower-precedence operators,
so an indexed question inside the child proves a conditional root directly.
Stripping a group changes depth and requires a fresh root proof. The registered
128/256/512-term comparison-boolean and mixed logical witnesses, together with
bounded direct-truth refusal witnesses, bound
actual label root/value/split/index work in both semantic paths; this prevents
right-nested conditional suffixes from being rescanned at every result arm.
Every provenance fact the walk records descends from such a
root, so a body it skips could have recorded or diagnosed nothing. The
conjunction operator shares the spelling; a body with a goto label and
`a && b` is not walked, and a parenthesized sizeof/alignof operand before
`&&` is that operator's operand, not a cast. `c_test_label_values_gate` pins
the gate through the private seam beside the unchanged diagnostics.

A GNU label difference `&&b - &&a`, each operand optionally cast to an integer
or byte-pointer type, is an integer: `c_parse_label_difference_end` stops the
walk from marking its value or storage as label-carrying, and
`c_parse_validate_static_scalar` leaves a proven `&&label` to IR lowering as it
does `&object`. In a static object of the defining function,
`c_ir_label_difference_expression` folds it into an `IrGlobalLabelDifference`
beside the zero byte image (`c_ir_label_difference_record`), and native code
generation writes the block distance once the function is placed
(`codegen_resolve_label_differences`). LLVM bitcode, WebAssembly and eBPF
refuse such a global; automatic objects, range designators, bit-fields and
widths other than 1, 2, 4 or 8 bytes stay diagnostics.
`c_test_static_label_differences` checks every stored value against the
labels' code offsets on both native targets and every allocator.

In a function body, `&&b - &&a` between two bare label values and
`&&base +/- n` use the numeric view of a label
(`c_ir_emit_label_numeric_view`): a pointer-to-integer CAST of a pure label
value, the one label conversion `ir_label_cast_is_numeric_view` lets validate,
whose integer result carries no label metadata. A difference is then an
ordinary integer subtraction, and `c_ir_emit_label_offset` yields a plain void
pointer marked by `c_ir_mark_label_offset_value`. Only a computed goto accepts
that mark: it cannot name its successors from metadata, so it dispatches
through the `POINTER_EQUAL` ladder of
`c_ir_emit_computed_goto_cleanup_dispatch` over every label the body takes the
address of (`c_ir_address_taken_label_blocks`). An offset stored anywhere else
loses the mark and is refused as a goto target. Both forms need the label
addresses of the native backends, so LLVM bitcode, WebAssembly and eBPF refuse
them through LABEL_ADDRESS. `c_test_automatic_label_differences` runs both
shapes on every allocator and frontend form and runs them on the host; AArch64
is validated and selected without running.

## Reservation failure contract

Required preprocessing spelling/token/shape/phase and semantic machine/phase
reservations produce an error row in the caller's result arena, with the phase
and requested size in its message. Parsing has no private arena reservation;
it forwards failed preprocessing and diagnoses absent or oversized token input.
Semantic analysis stays incomplete on a reservation failure. Lowering refuses
an incomplete semantic model and never publishes its partially constructed
program after a required arena reservation fails, including scratch growth.
The driver preserves these diagnostics for syntax-only and object actions and
has a nonempty fallback for an incomplete producer result.

`c_test_frontend_reservation_failures` and
`compiler_driver_test_frontend_reservation_failures` select each mandatory
phase-local creation through a calling-thread private seam, which arms the
existing `arena_test_fail_next_reserve` immediately at that creation and
disables pool reuse for that attempt. Both SSA forms cover all four
preprocessing, two analysis and one ordinary lowering reservations, failed
result publication and successful next-call recovery. Driver coverage includes
syntax-only for phases it actually executes. Oversized query/function growth
uses the same reservation boundary and reports the requested bytes. A private
tests-only initial function budget forces real growth with a small valid input;
a synthetic query extent refuses its mapping before any oversized source walk.
The attribute-role regression also forces its deep semantic spill reservation
and checks recovery. The frontend reservation fixture releases each complete
preprocessed unit with `c_preprocess_release` before rewinding scratch, so the
test-unit registry is unregistered and cannot retain destroyed arena pointers. Existing scratch-limit regressions cover checked plan refusal.
The lexer diagnostic arena remains an optional optimization with a tested
result-arena fallback; failure there retains the original lexical diagnostics.

### Per-body validation scratch (#1256)

`c_parse_validate_lowering_constraints` validates each function body in the
thread's scratch arena between a body mark and its rewind. Those arrays are
sized by the input (the body's tokens, scopes, labels, cases and asm operands,
and the expressions and type table that constant, layout and typing queries
reach), so one large body can need more than the arena's whole reservation.
The contract is recoverable, not a preflight estimate:

- While a body's guard is open (`c_parse_body_scratch_guard_begin` to
  `c_parse_body_scratch_guard_end`), every allocation from the guarded arena
  is either made by `c_parse_body_scratch_allocate`
  (`C_PARSE_BODY_SCRATCH_ARRAY`) or preceded by `c_parse_body_scratch_fits`
  for the bytes the caller then carves. Both are an O(1) overflow-checked test
  of size plus alignment padding against the body's limit, which is the
  arena's whole reservation; no headroom is held back for unchecked sites,
  because there are none. Other arenas, and the guarded arena outside a body,
  allocate exactly as `arena_allocate` does after one thread-local compare.
- The first refusal marks the body exhausted and every later request of that
  body is refused. Each family treats a zero pointer as "skip this work"
  (an unanswered constant, layout or member query, an unbuilt cache table, a
  failed evaluation). The caller drops the body's partial diagnostics back to
  its mark, reports one `C function body is too large for semantic
  validation` diagnostic naming the function, rewinds the scratch mark and
  continues with the next body. Validation of later bodies, and their own
  findings, is unchanged.
- Sites that reach the guarded arena through generic `Arena*` parameters are
  routed too: the body-scope, statement-end, label, switch, const-assignment
  and asm tables; `c_space_local`, the integer evaluator
  (`c_integer_expression_evaluate` and its `_with_features` core, whose
  parse-side form links its token list over one guarded array instead of
  running macro expansion without macros), `c_conditional_feature_operators`
  and `c_include_name`; string-literal decoding and imaginary spellings;
  `c_parse_constant_expression_evaluate`, `c_parse_typed_constant` and the
  type-constant query machine; the layout attempts and ordered passes (the
  agenda solver, which grows its tables as it discovers the closure, does not
  run in the guarded arena); the member-type, member-offset and initializer
  slot-cache searches; `c_parse_alignof_object_alignment` and the bit-field
  width evaluation (whose two-entry diagnostic buffer is constant-size but is
  routed so nothing bypasses the limit after a refusal); and parse-result
  growth through `c_parse_arena_can_allocate`, which asks the guard before its
  own reservation check so the refusal is the body's. Decimal spellings that
  `string_format` used to put in scratch are formed on the stack
  (`c_parse_space_decimal`), the type-constant query's call-arity probe forms
  no message, and the private type model of
  `c_parse_type_integer_constant_query_core` is guarded as described below.
  The lazy position index is built before the first guard opens.
- The type-constant query's private model (`CParseResult query`) lives in the
  other scratch arena, or a private arena when the caller's arena is that one,
  never the body's. While a body's guard is open,
  `c_parse_body_scratch_model_begin` guards that model arena on the same body
  under its own limit (its whole reservation, measured from its position when
  the query opens it) and `c_parse_body_scratch_model_end` restores an
  enclosing query's. The guard is keyed on both arenas and shares one
  exhausted flag, so a model request past its limit exhausts the body. A query
  that saw the body exhausted leaves its constant or member alignment
  unanswered and forwards no width diagnostic. Each model-arena allocation is
  checked by construction:
  - the copy-on-first-append tables (types, array bounds, type alignments,
    noreturn function types and deferred bit-field widths) and the aggregate
    and definition slot tables grow only through
    `c_parse_arena_can_allocate`, which asks the guard first;
  - the type-identity answers (`c_parse_type_identity_record`) and builtin
    call positions (`c_parse_position_index_append`; the query seals its
    bfloat16 list at its count) use the guarded array and drop the row on a
    refusal; string decoding and both evaluators already did;
  - the member name index is not offered: the query clears `member_lookup`,
    whose entries are shared with the caller, so an index built in the model
    arena can no longer be left in the caller's lookup after the model is
    rewound; member lookups in a query scan the record's members;
  - every message `c_parse.c` forms goes through `c_parse_message`, which on
    the model arena forms a fixed text and allocates nothing, and
    `c_parse_assignment_conversion_type_name` forms no name there. No message
    leaves the query except bit-field width texts: their copies use
    `c_parse_message_copy` (checked for the copied length) and
    `c_parse_bit_field_width_message` checks `2 * name + 256` bytes first,
    which bounds its at most two copies of the name, two decimal magnitudes
    and fixed text;
  - the layout cache is unreachable: the query's machines carry none, and
    its layout solves run with no machine.
  The fixed parameter, alignment-specifier and diagnostic buffers stay in
  their own arena sized exactly before the query starts, and the parse
  machine's arrays are guarded on the body's arena.
- Completeness is established by enumeration, not by a margin: the allocation
  sites reachable from per-body validation were audited, and a local
  instrumentation build that reported every allocation on the guarded arena
  not made or pre-checked by the guard found none across `c_frontend_tests`,
  `test_all`, the self-host stages and the large-body `_Generic` reproducer.
  The same instrumentation on the query model arena reported the member index
  and conversion type names, both removed above, and nothing else across the
  unit tests and a corpus of 73 member-alignment query shapes in bodies
  (casts, compound literals, string and wide literals, nested queries, vector
  and bit-field operands and invalid operands). The unit tests themselves
  make no type-constant query inside a body; the model fixture below is the
  first. This is coverage evidence for an audited list, not a static proof
  over every input: the model arena has no numeric maximum-live bound, only
  the guard's checked limit, so a new site that allocates in a scratch arena
  or a query model reachable from body validation must use the guard and
  tolerate a zero result, and the families or model fixture is where its
  exact-bound refusal belongs.

`c_test_body_validation_scratch_exhaustion` and
`c_test_body_validation_scratch_families` use the test seams
(`c_test_body_validation_scratch_limit` and `c_test_body_validation_scratch_trace`)
to record each checked request, cap the body exactly at the furthest end
reached before a named site, and assert that site is the first refused and the
function is reported once: the flat body's per-token arrays (and its one-byte
boundary), the scope-pending stack's initial size and its geometric growth, the
asm constraint, name and clobber tables, the statement-end table of the loop
and jump checks, the untyped evaluator's token copy and linked token list, the
typed evaluator of a long case label, and the inferred initializer's slot
tables. Recovery fixtures refuse an earlier body (flat and evaluator) and check
that a later invalid body still reports its own finding.
`c_test_body_validation_type_query_model` caps the query model through
`c_test_body_validation_model_limit` and reads the trace's `model` rows: a
member-alignment case label in one body (`_Alignof(((const struct S*)&s)->m19)`)
validates and lowers at its model peak and one byte past it, one byte short
refuses a model row and reports that function once, the later bodies are
still validated, and a later invalid body reports its own finding.

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

### Promoted-member workspace

Initializer member paths keep the existing breadth-first search and generation-stamped
visited table. The semantic machine starts with token-derived workspace capacity,
but expression queries can append type rows beyond that estimate. A search grows
its checked private workspace to cover the live type table before indexing it;
scratch and phase rewinds cannot release those buffers. The next growth releases
the previous private reservation, and semantic teardown releases the last one.
Reservation failure produces a structured diagnostic rather than treating an intact
record as if it lacked the requested member. The depth-16/32 later-assertion regression
in `c_test_nested_designator_static_assert` covers this interaction in syntax-only
validation, both frontend forms and the AST pilot; runtime controls check inferred
array length and member values.

If an omitted outer array bound remains uninferred after a failed initializer
walk, shape validation still traverses the list through the existing unbounded-root
mode. It publishes no synthetic bound and diagnoses unknown outer/inner member
names instead of silently skipping the list (#3672). The same registered fixture
checks unsized/sized invalid neighbors through direct semantics and the driver,
with independent Linux GCC/Clang rejection controls. Workspace ownership has its
own registered fault/recovery control: a failed replacement preserves the old
reservation and generation, a later search recovers, and production teardown
returns the private reservation count to its starting value.

## Block-scope function prototypes

When registering a local function declaration against an existing file-scope
function, check its type with `c_parse_types_compatible` before merging inline
attributes. The first local declaration needs this check even when no name
has yet been declared in that block. A conflict produces
`C_DIAGNOSTIC_CONFLICTING_DECLARATION` with both declaration sites; compatible
repeats and inherited internal linkage retain their existing behavior.
The registered local-linkage controls include FFmpeg's variadic ioctl-probe
shape without platform headers, return/arity/variadic conflicts, a nested
extern declaration, typedef prototypes and compatible static declarations.
