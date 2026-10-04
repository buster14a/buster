# Declarators, typeof, calls, and driver boundaries

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

## Transparent-union pointer arguments

A pointer argument matches a GNU transparent-union member through the ordinary
`c_parse_assignment_conversion_message` rule. Compare unqualified pointee
identity, then require the destination pointee to retain the source's const,
volatile and restrict qualifiers. Adding qualifiers is permitted;
dropping them or adding a qualifier at a deeper pointer level is not. The same
atomic, object/void-pointer and null-pointer policy applies as in an ordinary call.

`c_test_transparent_union_qualifiers` checks accepted additions, unchanged
qualified pointers, incompatible tags, nested-pointer mismatches and each
qualifier-loss control through semantic-only validation and both canonical
frontend forms on six target layouts. Its runtime companion checks member
transport, reads/writes and exactly-once argument evaluation in all four native
allocator modes and both frontend forms.

- **A top-level `(` right after an identifier** is the parameter list of a
  function that identifier names in `T f(int)`, and a parenthesized declarator
  in `T (*p)[2]`, whose `T` is the last word of the declaration specifiers.
  The syntax scan that finds a declaration's name has no typedef table to tell
  those two identifiers apart, and does not need one: a parameter is a
  declaration, so a parameter list can never begin with a `*`, and a group that
  does is a declarator group whatever precedes it. Without that,
  `T (*p)[2]` and `struct s (*p)[2]` were read as functions named `T` and `s`
  and the declaration each really makes was dropped whole -- no definition
  emitted, no diagnostic, and a `sizeof(*p)` that folded nothing. The redundant
  `T (p);` is the one shape still left: it needs the typedef table, since
  `int f(x);` with `x` a typedef name is a real function declaration.
- `__typeof` is accepted alongside `__typeof__` and `typeof`, because musl's
  `weak_alias` macro is written with it. **A function declared through a type
  name rather than a parameter-list declarator** -- `extern __typeof(f) g;`,
  or the same through a typedef -- has no function-name token, and that token
  is what the declaration kind is otherwise decided by. `c_analyze_semantics`
  in `c_parse.c` therefore reclassifies on the resolved type: a declarator
  whose type is a function is filed `C_DECLARATION_FUNCTION` with the
  parameter range taken from the `CType`, because the declarator has no
  parameter list and only the type knows the parameters. Everything
  downstream reads the declaration -- `c_ir_build_function_name_index` indexes
  it, `c_ir_function_signature` gives it an arity, and the entity is a
  `C_ENTITY_FUNCTION` -- so the name is callable and not merely addressable
  (issue #641). The same handoff from type to declaration is what the
  parenthesized declarator path and the block-scope function declarator in
  `c_parse_declaration_type` already do.
  `c_parse_entity_kind_redeclares` in `c_parse.c` is what keeps this spelling
  and an ordinary prototype one entity, which in musl every published name
  has.
- Compatible repeated block-scope `extern` object declarations and function
  declarations keep separate declaration-local rows for token-range ownership;
  `c_parse_local_declarations` permits the repeated binding only when both
  declarations have linkage. Type and thread-storage conflicts are diagnosed
  as conflicting declarations; explicit nonzero alignment requests must agree.
  An omitted request or `_Alignas(0)` keeps an earlier request, and an incomplete
  array or unprototyped function spelling keeps an earlier complete type.
  Function alignment specifiers remain invalid even when their request is zero.
  Lowering resolves linked local function uses through the function-name index
  when building definition dependencies, retaining called static functions
  without rooting unused function bodies or their dependencies.
  No-linkage object duplicates remain redefinitions. Registered
  `c_test_local_linkage_redeclarations` checks semantic-only analysis and both
  canonical-IR frontend forms. Its runtime companion exercises syntax-only,
  object output and linked executables in C17/GNU17 and all native allocator
  modes, including visible internal-linkage objects/functions.
- **Usual integer arithmetic conversions choose rank before representation.**
  The parse-side expression typer and canonical lowering both call
  `c_semantic_integer_arithmetic_kind` after their context's integer
  promotions. Equal signedness selects the higher rank; mixed signedness
  selects the unsigned operand when its rank is at least the signed rank,
  otherwise it selects the signed type only when the target's width can
  represent every unsigned value, or the signed type's unsigned counterpart.
  Thus LP64 `long + long long` is `long long` in either order, LP64
  `long long + unsigned long` is `unsigned long long`, and LLP64
  `long + unsigned int` is `unsigned long` (C17 6.3.1.1 and 6.3.1.8,
  issue #1246). Canonical C scalar types carry an optional numeric
  `integer_conversion_rank`; whole-record qualified and aligned copies
  preserve it without frontend type IDs. Synthetic IR integer carriers
  without source rank retain their representation rule. The registered
  `c_test_integer_conversion_rank` checks semantic typedef identities and
  raw nonconstant CALL operand types on Linux x86-64/AArch64 and Windows
  LLP64 in both frontend modes, plus inline native GNU17/GNU23 fixtures over
  all allocators and O0/O2. Enumerator, static initializer, array-bound and
  runtime `_Generic` answers agree, and `typeof`/`__auto_type` pointer
  witnesses preserve the resulting identity.
- **A `typeof` operand is typed twice, by two engines, and both have to
  answer.** `c_ir_sizeof_operand_type_attempt` in `c_gen.c` types the operand
  of a `typeof` written in an *expression* -- a cast, a compound literal --
  and `c_type_parse_sizeof_step` in `c_parse.c` types the one written as a
  *declaration's specifier*, because the parse is what decides a declaration
  exists at all. A specifier that resolves to nothing declares no name, so the
  reported error is "use of undeclared identifier" at the first *use*, naming
  neither the `typeof` nor its operand; a `typeof` bug reads as a missing
  declaration (issue #760). The parse-side walk carried two gaps that the
  gen-side one did not: no unary `*` or `&` at all -- so `__typeof__(*(double
  *)0)` resolved to nothing, prefixes over a plain identifier chain being the
  only ones `c_parse_direct_expression_type` handles -- and a conditional
  merge that compared type ids, so two pointer arms of different types
  resolved to nothing either. `c_parse_conditional_pointer_type` now applies
  C11 6.5.15p6 in its own order, and both engines agree: a *null pointer
  constant* on either side yields the other operand's type, and only if
  neither is one does a `void *` operand pull an object pointer to `void *`.
  Two object pointers with incompatible elements are what clang reports as
  `-Wpointer-type-mismatch` and still types as `void *`, so
  `*(0 ? (double *)0 : (char *)0)` is `void` and the declaration on it is
  refused for an incomplete type -- clang's own diagnostic -- rather than
  never being seen. `c_parse_range_is_null_pointer_constant` answers the
  spelling half: it gates on the arm's type (integer, or pointer to `void` --
  the constant walk strips casts, so without the gate `(double *)0` would
  answer yes), strips leading pointer casts by shape, since a parenthesized
  group whose last token is `*` is never a parenthesized expression, and folds
  the rest through `c_parse_integer_constant_range` *machineless*, the caller
  already being inside a type-parse frame. That composition is exactly musl's
  `__type1(c,t)`/`__type2(c,t1,t2)`, whose outer cast is
  `(__typeof__(...) *)` and whose machineless base-type reader cannot resolve
  it -- hence the by-shape strip. `tests/basic_c_typeof_conditional.c` runs
  both macros under all four allocators and
  `c_test_typeof_conditional_type` pins the resolved types themselves.
- **Every conditional converts to its own common type before its consumer.**
  The selection worklist types immediate children in postorder, retaining only
  the type at each question token. Flattened control flow shares a result place
  only when the types agree; a differing nested type gets its own place and its
  merge continuation converts that value into the parent place. This preserves
  unsigned widening and rounding through float before conversion to double.
  Void arms retain effects without result storage. The arithmetic-plus special
  path lowers its nested conditional as an independent value.
  `c_test_nested_conditional_conversions` checks six native target layouts,
  both frontend forms, static/enum/array/block constant controls, preprocessing
  intmax arithmetic, pointer/void/aggregate neighbors and selected-arm effects.
  Its desktop runtime matrix covers all four allocators and O0/O2, with
  independent GCC/Clang execution on hosted Linux x86-64 (GitHub #2523).
- **A comma expression can be any value operand, including when its right side
  calls a function.** The lowering expression machine sequences the complete
  left operand before it prepares the right operand's calls, then yields only
  the converted right value to a conditional arm, binary operator, cast,
  initializer, comparison, argument or enclosing comma. The unselected
  conditional arm is never evaluated, and the result is not an lvalue.
  `c_test_comma_value_operands` checks these contexts on all six native target
  layouts in both frontend forms and executes the exact-once ordering controls
  in every native allocator/frontend combination (GitHub #1421).
- `c_parse_direct_expression_type_core` resolves nested comma/prefix bases
  with explicit continuations. Each frame keeps its prefix slice and postfix
  range; all frames share one query-sized scratch allocation, released on
  success and failure. Constructed types remain in the translation-unit arena.
  Both this walk and `c_type_parse_sizeof_step` use the existing matching-
  delimiter index to skip nested ranges. Consecutive unary tasks inherit a
  completed operator scan until removing a parenthesis exposes a new range.
  Comma results undergo value conversion (including array/function decay and
  top-level unqualification); bare `typeof` operands retain their original type.
  `c_test_typeof_expression_frames` checks geometric depths and scratch lifetime,
  and `basic_c_typeof_expression_frames.c` checks observable type behavior under
  all native allocators (GitHub #255).
- **A failed call blames the call, not the declaration.** A direct call's
  callee token is not an identifier *use*: it resolves through the
  call-target index, so the parser records no binding for it and
  `c_ir_identifier_entity` misses. The lowering failure in
  `c_ir_lower_expression_core_step` looks the name up before reporting, and
  keeps the identifier's own token index rather than the one the call arm
  advanced to the closing parenthesis, so an object called as a function is
  reported as one instead of as an unbound identifier. A name nothing
  declares still reports as unbound, which is what a SIMD builtin missing
  from `c_symbol_predefined` looks like.
- **A call to a function declared `()` supplies the parameters itself.**
  Before C23 an empty parameter list is no prototype at all, so the arguments
  take the default argument promotions and the *call site* is the signature.
  `IrType.is_unprototyped` marks such a declaration's type, and
  `c_ir_unprototyped_call_type` in `c_gen.c` gives the `IR_OPCODE_FUNCTION`
  reference the call's own parameter types plus a trailing `...` -- Clang's
  model, which declares `void die()` as `void (...)` and types each call site
  `void (i32, ...)`. The `...` reaches only System V x86-64, where it sets the
  AL vector count that a callee which really is variadic reads;
  `int printf(); printf("%f", x);` is that program. Every argument stays
  *named*, so Darwin AArch64 passes them in registers rather than on its
  variadic stack, and a definition is untouched: `int main()` is still an
  ordinary zero-parameter body with no register save area.
  `ir_validate_instruction` therefore lets a function reference disagree with
  its symbol's type when that type is the unprototyped one and the return
  types agree, and Wasm64 refuses such a call, because it types every call by
  the callee's declared signature. C23 made `()` mean `(void)`, so the marker
  is never set in that dialect and the call is refused as an arity error --
  which every dialect now reports by naming the callee and its parameter
  count rather than as "could not prepare C calls" (issue #666).
- **A pre-C23 identifier-list definition keeps declared objects and promoted
  callable values separate.** In `int f(c, x) char c; float x; { ... }`, the
  body observes `char` and `float` parameter objects, while callers and the
  callee ABI exchange the default-promoted `int` and `double` values. The
  syntax pass records the identifier list and its following declaration list
  as distinct token ranges; semantic binding reuses ordinary local-declarator
  parsing, supplies `int` for an omitted declaration and preserves the
  unprototyped call rule. C23 and GNU23 diagnose the removed definition form
  while retaining declaration/body resynchronization. The registered
  identifier-list frontend fixture checks declared object types, promoted
  canonical IR signatures, call-site signatures, omitted declarations,
  earlier `()` declarations and the declaration immediately following the
  body. Its native runtime companion covers C17/GNU17, both frontend forms
  and all four allocator modes with zero machine fallback (GitHub #1263).
- **A callable parameter type is separate from its local object's type.**
  `c_ir_parameter_value_type` strips only top-level `volatile` from fixed
  parameter values in declaration and expression-built function types. It
  retains the existing atomic ABI shape and never strips pointee or member
  qualifiers. `c_ir_emit_parameter` receives both types: its `ARGUMENT` matches
  the callable signature exactly, while its local copy keeps the definition's
  qualifiers, alignment and volatile accesses. Array/function/va_list
  adjustments remain intact. The strict call-signature validator is unchanged;
  `c_test_qualified_parameter_values` checks the producer, parameter object and
  a deliberately mismatched call before shared promotion. The minimal fixture
  and the existing native differential corpus cover execution and compatible
  function pointers (GitHub #361).
  Compound assignments likewise compute with the unqualified value type,
  including when a narrower right operand needs promotion. Their original
  place retains volatile load/store effects and the separate atomic update
  path; `c_test_qualified_compound_values` checks both frontend forms.
- **A redeclaration's return type is the one of the function it joins.** Each
  declarator builds its own C function types, and each maps to its own IR
  function type (they are not interned by structure), so `int (*f(int))(int)`
  spelled on a prototype and again on the definition names two distinct
  pointer types. The `IrFunction` keeps the type of the declaration that
  registered it, and the validator checks RETURN rows and direct call results
  against that type. The registration loop in `c_lower_to_ir_with_options`
  therefore gives every later declaration that joins the function the
  registered return type, much as `c_ir_function_signature` already copies
  the canonical parameter types. Without it `-fverify-codegen` rejected the
  unit and the default path silently declined FAST for all of SQLite, through
  `sqlite3OsDlSym` (#1601, #1602); a typedef'd return type is one C type in
  every declaration and never diverged.
  `compiler_driver_test_function_pointer_return_redeclarations` covers plain,
  static, qualified and two-level returns. An unprototyped declaration
  followed by its prototyped definition with a function-pointer return still
  fails `-fverify-codegen` at its call sites (#1327).
- A by-value parameter's local copy retains its type's natural alignment.
  `c_ir_emit_parameter` must pass the resolved layout alignment to
  `c_ir_emit_local`, just as an ordinary declaration does. Rounding a slot's
  frame-relative offset alone cannot honor alignment greater than the frame
  pointer guarantee; MIR stack placement and native encoding use the place's
  alignment to reserve and materialize dynamically aligned storage. The parameter
  alignment tests inspect IR on all six native targets and use an opaque,
  separately host-compiled observer for native x86-64 callee addresses.
- System V x86-64 padding-only eightbytes retain NO_CLASS and consume no
  argument or result register. `ir_classify_abi_value` publishes only live
  pieces, preserving their offsets in the complete aggregate storage image;
  spilling still copies that complete image. Win64's indirect aggregate
  convention is unchanged. `ir_tests` covers leading and trailing padding
  across all ABI uses; `compiler_driver_test_sysv_padding_eightbytes` exchanges
  aligned float/integer records in both directions with the configured host
  compiler and available Linux GCC, including register exhaustion, aggregate
  returns and variadic access in every native allocator/frontend form.
  The MIR SysV variadic reader consumes live ABI parts in registers while
  retaining the complete aligned storage image in the overflow area. A record
  containing only ignored fields has zero transport parts and currently hits
  the frontend's unsupported zero-part signature gate; the LLVM negative
  fixture pins that earlier refusal and absence of a produced artifact.
- A GNU zero-size struct or union is the distinct supported zero-part SysV
  case in the native x86-64 MIR path. It consumes no argument register,
  stack slot, variadic cursor space or hidden result pointer. Loads preserve
  their place provenance while moving no bytes, and stores are zero-byte
  operations after the lvalue and value have been evaluated. Nonempty
  all-NO_CLASS records retain the refusal above; LLVM keeps its structured
  empty-signature refusal.
- The generic JIT loads already-produced host-native objects and resolves
  explicit bindings. It is not a second source-language compiler and must stay
  independent of frontend semantic structures.
- The command-line driver accepts C source/preprocessed C plus native
  objects/archives where the selected action permits them. Unknown languages,
  retired module-root options, and unsupported source extensions must fail
  explicitly rather than being forwarded or guessed.

- `__builtin_va_list` is a builtin type spelling, never a `void *` macro.
  Both primitive-type scanners preserve its identity, and arbitrary typedef
  aliases share the unqualified `C_TYPE_VA_LIST`. Typedef names alone do not
  confer that identity. `va_start` and `va_copy` lower their destination through
  the existing place continuation and retain it while evaluating the copy
  source. List members, subscripts and dereferences follow the same address
  conversion as named objects; every operand is evaluated once. Invalid list
  types and nonmodifiable destinations fail in the frontend before VA IR is
  published. This does not change target layouts or the public AArch64 list ABI.

  The builtin Windows `stdarg.h` honors the CRT's `_VA_LIST_DEFINED` guard.
  Its public `va_list` has the CRT pointer representation; macros address that
  storage through an explicit builtin-list place cast. This supports either
  header order without turning ordinary pointer typedefs into builtin types.
  The modern CRT `__crt_va_*` macros use the same bridge when already defined.

## Declarator constraints

Semantic expression queries type a string literal through its target and dialect
element type before applying subscripts. Both `literal[index]` and
`index[literal]` retain that element identity for `typeof`; `_Generic` applies
the ordinary lvalue conversion, and arithmetic applies integer promotions.
The expression task stack queries both operands without evaluating either one.
`c_test_generic_string_subscripts` checks all five prefixes, parentheses,
concatenation, commuted subscripts, computed indices, promotions and rejected
operand pairs on six native layouts in GNU17/GNU23 and both frontend forms.
Its embedded native fixture checks every allocator and preserves unevaluated
index effects.

Character literal expression queries retain the prefix's scalar identity:
ordinary constants are `int`, `L` follows the target's `wchar_t`, `u` is
`unsigned short`, `U` is `unsigned int`, and C23 `u8` is `unsigned char`.
The allocation-free literal query and the type machine share this leaf policy;
`typeof`, `_Generic` and inferred declarations agree with lowering, while
arithmetic still applies the ordinary integer promotions. Prefix and dialect
admission remain owned by existing lexing and semantic validation.
`c_test_character_literal_query_types` checks fixed type, promotion and size
expectations on six native layouts in GNU17/GNU23 through both query routes and
both frontend forms. Its embedded native fixture checks signedness after `-1`
assignment for `typeof`, GNU `__auto_type` and C23 `auto` in every allocator.

Local C23 `auto name = expression` inference uses the existing initializer
binding, value conversion and qualified type publication. The initializer is
bound before its new identifier becomes visible, so an outer identifier may
be shadowed. Prefix and suffix qualifiers are retained on the inferred object.
An explicit type, including a visible typedef, `typeof` or `_Atomic(type)`,
keeps `auto` as a storage specifier; pre-C23 typed declarations retain their
existing behavior. `c_test_c23_auto_local_declarations` checks these controls
and rejects missing initializers, multiple or non-identifier declarators,
duplicate inferred specifiers and self-reference without an outer binding.
This local automatic-object slice does not complete issue #1254: file-scope,
`constexpr`, static, external and thread-local inference, and broader
statement-expression queries remain outside its contract.

Function types reject array and function return types when their declarators
are formed, including unused prototypes, typedef return types and nested
function-pointer declarators. Pointer return types keep their array/function
pointees. Ordinary function declarators also reject a second array or function
suffix instead of silently discarding it.

`c_parse_parameter_list_unprototyped` records the empty-list distinction at
construction: `()` leaves parameters unspecified before C23 and is a
zero-parameter prototype in C23/GNU23, as is `(void)` in every dialect.
Direct, parenthesized, nested and block-local function declarators share this
rule, so compatibility sees the same fact as call validation and canonical
type mapping. A C23 `int f();` conflicts with `int f(int);`, including when
the mismatch is inside a function pointer, typedef or callback signature.
Pre-C23 compatible redeclarations retain their existing behavior. The rule
follows [WG14 N3096](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n3096.pdf)
6.7.6.3 paragraphs 13–14.

`c_test_c23_empty_list_prototypes` checks explicit C17/GNU17/C23/GNU23
expectations through semantic-only validation and both canonical frontend
forms on three native target layouts. It inspects the original function-type
markers, checks structured diagnostic parity and refused programs, and
independently validates accepted canonical IR. Local declaration controls
avoid the separately tracked repeated-linkage restriction (#1562).

All parameter-list paths share the void and ellipsis constraints. The void
sentinel is sole, unnamed and unqualified, including through a void typedef;
ellipsis terminates the list and requires a fixed parameter before C23. C23
allows a list containing only ellipsis. Array `static` needs an expression;
`[*]` belongs to prototype scope and is rejected in the definition's own
parameter derivations. A nested function-pointer parameter still introduces
its own prototype scope. The syntax/object diagnostic-equivalence corpus
checks rejection, legal neighbors and both frontend SSA forms.

Parameter declarations give a visible typedef name priority over a parameter
name in an ambiguous parenthesized group (C11 6.7.6.3p11). `T (T)` and
`int (T)` derive an unnamed function parameter and then adjust it to a pointer;
`T (T (T))` retains the inner function-parameter adjustment. Redundant abstract
groups such as `int ((T))` carry that classification down the explicit type
machine. Pointer-led named groups, `int T`, `const T T`, member names and
ordinary block-scope shadowing keep their existing rules.

A typedef remains a type in the function body. Checked expression leaf queries
reject its use as a value, including within parentheses or operator operands;
casts and type operands keep their type-name binding. The statement-expression
walker recognizes typedef-led `for` initializer declarations in the enclosing
scope and leaves their validation with the declaration owner.

`c_test_parenthesized_typedef_parameters` checks the original prototype/body
pairs, a char typedef, nested and deeply grouped abstract forms, retained
typedef visibility, and rejected typedef-name expressions. Semantic-only and
both canonical frontend forms check structured diagnostics on six native
layouts in C11/GNU17/GNU23, with carried and zeroed symbol IDs. Embedded native
sources exercise all four allocators at O0/O2 in both forms; hosted Linux x86-64
GCC/Clang compile and execute the same original expected-value sources and
independently reject the invalid typedef-name expression.

## GNU callback storage through void pointers

In GNU dialects, native x86-64 and AArch64 Linux/macOS admit assignment,
initialization, return and argument conversions between a function pointer
and `void *`. This is an explicit Buster extension for callback storage,
including QuickJS's `(void *)dbuf_printf` initializer. It changes only the
immediate function/void pointee pair; `void **` is not a generic callback-slot
type. ISO C99/C11/C17/C23 modes still diagnose the implicit conversion, as do
other target families. Android/iOS are outside this initial policy.

N1570 6.5.16.1p1 restricts the ISO assignment exemption to object pointers,
and 6.7.9p11 applies that constraint to initialization. Annex J.5.7 records
explicit function-pointer casts as a common extension. POSIX `dlsym`
describes a callable conversion through `void *`; it does not require
implicit assignment acceptance. GCC/Clang acceptance is implementation
evidence, not the definition of this extension.

A genuine function address may round-trip through `void *` and be called
through its original compatible signature. Arbitrary data addresses and
calls through incompatible signatures have no supported execution contract.
Direct mismatched function signatures, non-void object/function-pointer
assignments and discarded pointee qualifiers remain errors. Explicit casts
retain their pre-existing policy.

`c_parse_assignment_conversion_message` owns admission; lowering uses the
existing canonical pointer conversion. `c_test_void_function_pointer_policy`
checks the independent dialect/target acceptance table, and
`compiler_driver_test_void_function_pointer_roundtrip` compiles, links and
runs both source and separately emitted object routes across the four native
allocators and both frontend SSA forms on eligible hosts. Semantic checks
for a target are distinct from executing that target.
See the [pinned portfolio evidence](../../capability-portfolios/callback-storage.md)
for exercised configurations and remaining external-harness blockers.

## Resolved non-returning call effects (#1350)

`c_ir_emit_call_target` uses the resolved signature's `is_noreturn` contract.
The C name and assembler name identify the callee; they do not independently
add a non-returning effect. This keeps continuation after a returning internal
function named `abort` and agrees with indirect calls. Explicit standard/GNU
attributes and effects on later declarations remain authoritative. Existing
void placeholders, call consumers and terminator ordering remain unchanged.

The registered `c_test_resolved_call_effects` validates named returning and
marked callers in GNU17/GNU23, six native data models and both frontend forms.
It checks exact CALL counts, RETURN/UNREACHABLE presence and canonical validity.
Independent runtime oracles cover direct, parenthesized, macro, pointer and
shadowed calls, storage live after a call, conditional continuation, used integer
results and an explicit assembler name. Supported desktop execution covers all
four allocator modes and both forms with strict codegen verification. Standard,
GNU and later-declaration non-returning helpers each exit through the explicitly
marked `_Exit`; a continuation that executes instead fails the runtime oracle.
