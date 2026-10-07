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
transport, reads/writes and exactly-once argument evaluation with FAST and QUALITY
in both frontend forms.

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
  both macros under FAST and QUALITY;
  `c_test_typeof_conditional_type` pins the resolved types themselves.
  Both engines type an operand in time linear in its token count (GitHub
  #2715). A left-associative chain such as `a + b + ... + z` folds left to
  right instead of splitting at its last operator and retyping the prefix:
  the gen-side `CIrSizeofFrame` keeps the operands folded so far in
  `first_type` and resumes `c_ir_sizeof_operator_scan` after each operator to
  find the next one, while the parse side pushes a chain's prefixes at once. A
  shift chain is typed by its first operand alone. A conditional's false arm
  receives its own top-level `?` and `:` (`question_hint`/`colon_hint`, and
  `c_parse_expression_next_conditional` on the parse side) instead of
  rescanning the rest of the chain. `c_test_tall_expression_types` types
  10,000-operand chains.
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
  Its desktop runtime matrix covers FAST and QUALITY at O0/O2, with
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
  with FAST and QUALITY and zero machine fallback (GitHub #1263).
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
  unit; before the repair, the default path silently declined FAST for all of SQLite, through
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
  pointer guarantee; native frame placement and encoding use the place's
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

  Literal `__va_start(&cursor, last)` also admits the CRT's modifiable public
  `char *` cursor on Windows x86-64 and AArch64. Its addressed place is evaluated
  once, then the existing typed address/cast/dereference helpers view that
  storage as the target's builtin list. The original C pointer type remains
  intact; a volatile cursor retains a volatile list store. Arity and variadic
  function checks remain in force. The CRT's non-addressed
  `__builtin_va_start(cursor, last)` and `__builtin_va_end(cursor)` take the
  same bridge when `cursor` is a modifiable place of that type; `va_end` lowers
  the bridged list address to the ordinary `IR_OPCODE_VA_END`, exactly as for a
  builtin list. `__builtin_va_copy` and `__builtin_va_arg` keep refusing a
  bare `char *` cursor; the CRT reads arguments through the explicit builtin-list
  place cast. Other builtin spellings, non-Windows targets,
  rvalue or array operands, const/atomic destinations and non-character or
  qualified character pointees keep their refusal. Canonical validators still require
  `IR_TYPE_VA_LIST`; ordinary pointer typedefs do not gain builtin identity.

  `c_test_windows_va_start_cursor` checks live named/member/subscript/dereference
  and volatile cursors, fixed integer/double/long-long values, builtin-list and
  explicit-copy controls, plus negative neighbors through semantic validation
  and both canonical frontend forms on Windows/Linux/macOS x86-64/AArch64.
  Native Windows execution covers both forms and FAST/QUALITY, with
  original source readback, finite process-group deadlines and full transport
  failure checks. The earlier unused-body diagnostic sources and real SDK
  formatting witnesses remain unchanged. This fixture is a validation contract,
  not evidence of an executed or passing repair.

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

Pointer-to-array declarator shapes (#1262): `int (*f(args))[3]` declares a
function returning a pointer to an array, whether it is a prototype or a
definition (`c_parse_parenthesized_function_name` accepts a `[` after the group
when the name carries a parameter list). `int (*)[]` points at an array of
unknown bound: `c_ir_collect_flexible_array_types` maps that pointee like a
flexible array and `c_ir_type_name_suffix_bounds` accepts it in type names.
Pointer difference compares pointee array types by representation
(`c_ir_representation_types_compatible`), since each spelling owns its IR array.
`c_test_pointer_to_array_shapes_runtime` runs all of these under every allocator
and both frontend forms.

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

`c_parse_types_compatible_walk` applies two additional C17 compatibility
requirements: a prototype paired with a pre-C23 unspecified parameter list
must use parameter types unchanged by default argument promotions, and both
parameter spellings undergo array/function adjustment. Documentary outer
array bounds and parameter-only qualifiers therefore disappear from
compatibility; inner bounds and pointee qualifiers remain. Promotion checks
read existing parameter rows and resolve an enum's compatible integer kind;
the shared pair stack compares adjusted pointees without creating types or
evaluating removed outer bounds. C23's empty-list constructor is unchanged.

`c_test_function_parameter_compatibility` uses 56 fixed source cases over
C17/GNU17/C23/GNU23, six desktop target layouts and both frontend forms
(2,688 configurations). It preserves the existing C23 empty-list rule and
checks semantic-only diagnostics, diagnostic parity, failed program
nonpublication and independent canonical validation of accepted sources.
The cases include original float/char/short/Bool promotion conflicts, both
declaration orders, promoted scalar/enum/pointer controls, typedefs and
definitions, fixed/VLA/static array bounds, function parameters and nested
qualifier/inner-bound refusals. GNU type-compatibility queries retain fixed
answers independent of the compatibility implementation. Two further fixed
negative declarations pair an incompatible nested callback or inner array
bound with an otherwise compatible aggregate sibling. They require a failed
comparison to remain failed through the existing pair-stack walk; the loop
stops at the first incompatible pair, so later siblings cannot restore a
successful verdict.

`c_test_function_parameter_compatibility_runtime` keeps a literal 1,299-byte
source with fixed results for promoted scalars and adjusted array/callback
parameters. Desktop native execution uses GNU17/GNU23 × FAST/QUALITY ×
two frontend forms (8 profiles). Linux additionally requires GCC and Clang
at both dialects and O0/O2 (eight build/run controls), and separately compiles
each original float/char/short/Bool conflict in C17/GNU17 (16 required
refusals). Both reference commands disable only `-Wstrict-prototypes`: the
legal pre-C23 empty declarations are deprecated, and Clang otherwise upgrades
that warning under `-pedantic-errors`. Actual conflicting-type errors remain
required, including a captured `conflicting` diagnostic for every refusal.
Original source bytes are read back before launch. Every child
owns its process group and has a thirty-second deadline; bounded captured
diagnostics fail on truncation/overflow. Cleanup/retained-reservation/lost-
ownership failures stop later child admission. Temporary executables and
sources are deleted before their arena lifetime ends. These are registered
requirements, not execution results for this checkpoint.

The pre-C23 requirements are C17 6.5.2.2p6 and 6.7.6.3p7–8,p15; the
same promotion/adjustment text is present in the public
[WG14 N1256](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf)
6.7.5.3p15. This work does not alter call-site promotions, declaration
construction, live enum publication or target ABI policy.

`c_test_attribute_call_roles` checks GNU attribute-name/function collisions
through semantic-only validation, both canonical frontend forms and the
native driver. Attribute heads designate attributes; calls inside argument
expressions still require their declared arity. Distinct cleanup callbacks,
their retained entity identities and one callback per scope exit are checked
independently, alongside wrong-arity calls, incompatible callbacks and local
shadowing. The named-call candidate walk distinguishes specifier names while
retaining candidates inside argument expressions. Eight local frames cover
ordinary nested payloads; deeper nesting spills into a private arena bounded
by existing attribute positions and destroyed without pooling before returning.
Repeated shared
queries retain no role buffers in their model or message arenas, even when
those owners occupy both scratch arenas. No broad pass or persistent role table
is added. The regression keeps explicit returns to isolate this constraint
from non-void falloff (#1357).

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
sources exercise FAST and QUALITY at O0/O2 in both forms; hosted Linux x86-64
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
runs both source and separately emitted object routes with FAST and QUALITY
across both frontend SSA forms on eligible hosts. Semantic checks
for a target are distinct from executing that target.
See the [pinned portfolio evidence](../../capability-portfolios/callback-storage.md)
for exercised configurations and remaining external-harness blockers.

## C99 inline function identity

A body whose file-scope declarations all specify inline without extern is an
inline definition, which supplies no external definition. Calls and address
expressions share the external function identity; a referenced body must not
become a second strong definition. A compatible extern or non-inline
declaration in the same unit supplies the external definition. Static inline
and GNU inline semantics retain their separate rules.

The registered c_test_c99_inline_linkage checks symbol linkage, definition
status and canonical function state in both frontend forms across six native
target layouts and C99/C11/C17. Controls retain static inline, a preceding
non-inline prototype and both GNU attribute directions. Its two-unit program
checks the literal result 14, distinct private static helpers and equal
function pointers. All four native allocator modes compile the units together
and separately; Linux x86-64 also links the Buster objects with GCC and Clang
and runs independent host-built versions of the same source recipe.

The existing per-entity declaration scan decides whether a body supplies an
external definition. A referenced C99 inline-only body retains a canonical
external declaration and emits no body; calls and addresses bind to the
external definition from another unit. Pre-created function symbols take the
same definition decision, preserving real alias definitions; GNU inline-only
bodies do not mark those symbols defined. Unused bodies retain their existing
dropped state. No inline optimizer, dependency walk or GNU policy is added.

Needed Windows `__inline`/`__forceinline` bodies retain callable definitions
and their shared function identity. The Windows predefines preserve `__inline`
and map `__forceinline` to that spelling. The existing reachability walk admits
transitive header helpers; its roots and worklist are unchanged. The existing
entity-definition map makes every redeclaration share the needed body decision,
including a later raw-inline prototype. Only the two late registration/body
predicates exempt those Windows bodies from C99 dropping. Unused header bodies
remain omitted, so an unused intrinsic header
cannot introduce an unavailable runtime import. Plain `inline`, GNU `__inline__`,
explicit GNU-inline attributes and Linux `__inline` retain their existing rules.
This bounded compatibility policy does not implement full MSVC mixed-spelling
synonyms or multi-TU COMDAT coalescing.

The registered `c_test_windows_inline_bodies` checks transitive helpers, source
static/extern storage, function address and local option-word identity, and unused
nondefinitions. Original inline sources and real Windows `<stdio.h>` formatting
run in both frontend forms and all four native allocator modes.

## Calls through returned function pointers

A function-pointer result is a call target, including when its producing call
has an empty argument list: `get()(3)`, `get_free()(p)` and `l2()(1)(4)`
are ordinary postfix call chains. Empty argument lists on the producing call
must retain the same dependency as nonempty lists. A scalar result cannot be
called, and a returned function pointer still follows its own parameter list.

`c_test_call_result_callees` records this contract with explicit `(void)`
factory declarations, independently of dialect-specific empty prototype
policy. Two programs cover statement and value uses, typedef callbacks,
pointer arguments, grouped and dereferenced callees, empty middle/final calls,
nonempty-list neighbors and lazy operands. Named canonical functions retain
their exact call counts in both frontend forms across six target layouts and
GNU17/GNU23. Runtime checks use separate factory, callback and argument
counters in both native allocator modes; an untaken lazy operand calls
neither factory nor callback. Invalid scalar callees and missing/extra
callback arguments require diagnostics and an uncertified result.

Call discovery accepts an empty argument-list group only when the existing
active-call stack links its exact opening and closing delimiters to the
producing call. Abstract pointer and type-name groups retain their exclusions.
Existing prepared-call ordering emits the producing call once and consumes
its returned pointer for the subsequent call; no extra source walk is added.
Semantic constraints name a nonfunction computed target with the
"called object is not a function or function pointer (have '<type>')"
message before indirect lowering refuses the call.

## Non-void closing-brace return edge

C 6.9.1p12 makes a non-void closing-brace falloff undefined when the caller
uses its result. A discarded result retains the return edge and all earlier
side effects. It does not justify UNREACHABLE. Explicit returns, void returns,
main's implicit zero and actual noreturn effects have their separate contracts.

`c_test_nonvoid_falloff` checks typed canonical RETURN/no-UNREACHABLE for
integer, pointer, floating and small/4-KiB aggregate results, plus the explicit
return, void and noreturn neighbors, across six target layouts, GNU17/GNU23
and both frontend forms. Separate caller/callee sources exercise sixteen
independently selected runtime paths through combined and separately linked
objects in both native allocators. VLA repetition, GNU cleanup, indirect,
void-cast and comma-discard calls keep their observable scope effects. Main
falls off only after checking the exact effects, so its defined zero is also
exercised. Bare/incompatible returns and wrong arity remain refused. No runtime
oracle uses an unspecified fallen-off result.

The final ordinary non-void root-body path uses the existing iterative typed
zero-value constructor and canonical RETURN. That deterministic carrier is an
implementation detail, not a guarantee for a source program that uses a missing
result. Scope cleanup and stack restoration run before it; statement-expression
continuations, explicit returns, main and actual noreturn calls retain their
existing paths. The historical control-flow expectation now requires both
returning branches and no manufactured UNREACHABLE. Constructor allocation
and aggregate materialization costs are unmeasured; no performance claim is made.

## Fixed scalar register-to-stack regression

`compiler_driver_test_scalar_argument_boundaries` extends the existing driver
harness with first-party, separate caller/callee translation units under
`src/buster/tests/compiler/driver/fixtures/scalar_boundary_*.c`.
It checks each argument position, exactly representable floating values, twenty
volatile counter updates, and three pointer writes. Both mixed compiler
directions run with an eligible GNU-compatible configured reference compiler at
O0 and O2, without
LTO, across FAST/QUALITY and both frontend SSA forms.
The reference/reference control must return zero; a deliberately wrong final
pointer expectation must return exactly 73. A signal, timeout or launch failure
does not satisfy that negative control.

The fixtures assert each selected target's scalar size and alignment (int/float
4, long long/double/pointer 8); they never infer layout from the executing host
or compare padding. GP arities 3–9, FP arities 3–5 and 7–9, interleaved arguments,
and pointers after 4/6/8 integers bracket these independent contracts:

| Fixed-prototype target | Register and overflow contract |
| --- | --- |
| SysV x86-64 LP64 | Independent six GP and eight SSE arguments; exhausted classes use eightbyte stack slots while the other bank remains available. |
| Windows x64 | Four shared argument positions choose GP or FP registers; later arguments use the stack after four eightbyte home slots. |
| AArch64 Linux/Windows | Independent eight GP and eight FP arguments; stacked scalar arguments occupy eightbyte slots. |
| Darwin AArch64 | The same bank limits, with naturally sized named stack arguments: adjacent spilled ints occupy four bytes each before subsequent eightbyte alignment. |

Expectations come from the ABI documents below, independent of Buster's
classifier. The 13-argument `abi_boundary_packed_stack` case puts adjacent
four-byte ints before an eight-byte integer after GP exhaustion; its FP argument
uses an available FP register on SysV/AAPCS64 and a positional stack slot on
Win64. The name describes stack packing, not a packed C record.

| Inspected contract | Exact provenance and license |
| --- | --- |
| [SysV ABI 1.0, Parameter Passing](https://github.com/susematz/x86-64-ABI/blob/a0f552021583de8dc3d264cce337aeb99a16723b/x86-64-ABI/low-level-sys-info.tex) | ABI editor Michael Matz's source mirror at `a0f552021583de8dc3d264cce337aeb99a16723b` (2019-02-28). License unspecified in inspected source/root; official GitLab retrieval unavailable, so this is not claimed to be its current head. |
| [AAPCS64 2025Q4, rules C.1/C.5–C.6/C.9/C.13–C.17](https://github.com/ARM-software/abi-aa/blob/daa7a94ca55973736c0e434a67a6e4bbcd35d7fa/aapcs64/aapcs64.rst) | `daa7a94ca55973736c0e434a67a6e4bbcd35d7fa`; issue date 2026-01-23. [CC-BY-SA-4.0 plus patent grant](https://github.com/ARM-software/abi-aa/blob/daa7a94ca55973736c0e434a67a6e4bbcd35d7fa/aapcs64/LICENSE). |
| [Microsoft x64](https://github.com/MicrosoftDocs/cpp-docs/blob/f2355df9f7136d8a2097193fc507882a7caeb5f5/docs/build/x64-calling-convention.md) and [Windows ARM64](https://github.com/MicrosoftDocs/cpp-docs/blob/f2355df9f7136d8a2097193fc507882a7caeb5f5/docs/build/arm64-windows-abi-conventions.md) | `f2355df9f7136d8a2097193fc507882a7caeb5f5`; documentation CC-BY-4.0, code examples MIT. No example code copied. |
| [Apple ARM64 deviations](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms) | Official documentation consulted 2026-10-02 through its official JSON representation. No public revision exposed; copyright Apple, all rights reserved. |

Native execution only occurs for the target matching the desktop runner.
Six-target object generation and nonempty named text-symbol inspection are
separate structural checks, not execution or proof of register placement.
Unexecuted native platforms and MSVC-configured reference execution remain
pending; Apple x86-64 is best-effort only. Mobile apps do not package these
desktop component fixtures and report this slice pending without reading them.
The slice does not cover variadic calls, aggregates, vectors or LLVM export.
Its first-party fixtures import no external source or dependencies; Buster's
first-party license remains unselected per `LICENSES/README.md`.

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
results and an explicit assembler name. Supported desktop execution covers FAST
and QUALITY and both forms with strict codegen verification. Standard,
GNU and later-declaration non-returning helpers each exit through the explicitly
marked `_Exit`; a continuation that executes instead fails the runtime oracle.
