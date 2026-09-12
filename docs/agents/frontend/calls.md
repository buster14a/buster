# Declarators, typeof, calls, and driver boundaries

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

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
- A by-value parameter's local copy retains its type's natural alignment.
  `c_ir_emit_parameter` must pass the resolved layout alignment to
  `c_ir_emit_local`, just as an ordinary declaration does. Rounding a slot's
  frame-relative offset alone cannot honor alignment greater than the frame
  pointer guarantee; the canonical native emitter uses the place's alignment
  to reserve and materialize dynamically aligned storage. The parameter
  alignment tests inspect IR on all six native targets and use an opaque,
  separately host-compiled observer for native x86-64 callee addresses.
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
