# C frontend, canonical IR, and places

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

## C frontend and canonical IR rules

- The public frontend API is `compiler/frontend/c/c.h`. In non-unity builds the
  implementation is split across `c_source.c`, `c_parse.c`, and `c_gen.c`;
  `c.c` preserves the unity include order and diagnostic mapping.
- Keep the frontend pipeline explicit: source loading and preprocessing,
  parsing and semantic construction, then canonical-IR lowering. Do not add a
  parallel frontend-specific IR or route code generation around canonical IR.
- Invalid user input must produce structured C diagnostics and a failed driver
  result. Assertions and `BUSTER_TODO()` are for violated internal invariants,
  never ordinary syntax or semantic errors.
- Arena ownership is part of the API contract. Returned source, syntax,
  semantic, and IR structures may reference earlier-stage storage; callers must
  retain the translation-unit arena until every downstream consumer finishes.
- Zero-initialize aggregate tables before publishing a partially resolved type.
  Recursive and mutually dependent declarations can expose an aggregate while
  later members are still unresolved; an uninitialized `IrField` must never be
  mistaken for a valid type or source reference.
- Canonical IR owns format-neutral functions, blocks, instructions, values,
  symbols, source ranges, types, and relocations. Shared layers must not contain
  frontend entity IDs, parser AST pointers, or language-specific invalid
  sentinels.
- Validate IR before machine selection or Wasm emission. A diagnosed frontend
  failure must not publish an apparently valid partial function to codegen.
- Constant initialization must preserve the source type and every value limb.
  The direct aggregate leaf paths in `c_gen.c` use a nonzero test for `_Bool`
  and `c_ir_constant_integer_to_float` for unsigned integers, with rounding at
  the destination precision. The general aggregate writer stores both limbs
  of a 128-bit integer. `c_ir_constant_float_to_integer` decodes binary64 bits,
  truncates fractions before checking the destination range, and refuses
  nonfinite or unrepresentable conversions without executing an undefined host
  cast. Automatic nested initializers recognize a string as its whole array
  subobject before brace elision. Pin these paths with independent object bytes
  as well as runtime comparisons: the initializer fixtures also exposed a
  selected x86-64 float-to-u64 conversion whose binary32 threshold encoded
  2^31 instead of 2^63. Runtime float-to-128-bit conversion on x86-64 remains
  unsupported; constant conversion supports both integer limbs.
- **GNU's `__alignof__` takes an expression; `_Alignof` takes only a type
  name.** Both spellings reach the same fold in `c_gen.c`, and it resolved an
  expression operand only for a compound literal until libc-test's
  `tls_align_dso.c` reached it: the file fills a table with `__alignof__(x)`
  over four `__thread` objects, and every other shape refused with "could not
  lower logical expression core". The operand now goes through the resolver
  `sizeof v` already used, which answers the alignment of the operand's **own
  type** -- `__alignof__(arr)` over a `char[7]` is 1, not the 8 the expression
  type prediction would give the pointer it decays to in any other context.
  The prediction is still the last resort for both words, under the same
  guards: an inline aggregate definition and an object whose array type never
  mapped are refused rather than guessed at. `tests/basic_c_alignof_expression.c`
  is the fixture, and every value in it was compared against clang.
- **`void` is one byte, and an object of it is still refused.** GNU gives
  `void` a size and an alignment of one so that arithmetic on a `void *` steps
  by bytes, and clang and gcc both fold `sizeof(void)`, `sizeof(const void)`
  and `_Alignof(void)` to 1. This compiler folded 0, and the index that `p + 3`
  becomes is scaled by the pointee's layout size, so the pointer did not move
  at all -- a silently wrong address rather than a diagnostic, and `q - p` was
  refused outright because the divide by the element size would have been a
  divide by zero (#743). The answer lives in `c_parse_builtin_type_layout`,
  which is the one table **both layout engines** read: `c_parse_type_layout`
  folds `sizeof` through it during the parse and `c_ir_scalar_type` builds the
  `IrType` from it, so there is no second place to keep in step. Nothing
  downstream had to change, because every backend already scales an
  `IR_OPCODE_INDEX` by the element type's size and every question about `void`
  that is *not* its size is asked of `IR_TYPE_VOID` rather than of a zero.
  The size is an extension for that arithmetic and for `sizeof`, **not a
  licence to declare a `void` object**: C 6.7p7 wants a complete type and both
  reference compilers refuse `void v;`, `void a[4];` and a `void` member. Those
  refusals used to fall out of the zero size -- an aggregate whose layout never
  resolved, a local whose alignment was zero, a file-scope object that reached
  code generation and failed there naming `main` rather than the object -- so
  they are asked of the kind now, through `c_ir_type_is_void_object` in
  `c_gen.c`, at the two local-declaration sites, the global definition walk,
  and the aggregate member layout, which reports through the same
  one-per-type slot a rejected alignment specifier uses. The predicate
  descends array elements alone: a qualified copy keeps the base's kind, so
  `const void` answers the same, and a `void *` is a pointer and answers no.
  `tests/basic_c_void_size.c` pins every runtime answer under all four
  register allocators -- both orders of the addition, the two subtractions,
  `++`/`--`/`+=`/`-=`, and the qualified pointees -- reading each stepped
  pointer back through a live object so an address that folds correctly and
  lowers wrongly still fails; `c_test_void_object_refusals` pins both layout
  engines' number and the four refusals.
- An integer converted to a pointer reaches pointer width in the frontend,
  before `IR_CONVERSION_INTEGER_TO_POINTER`, sign-extending when the operand is
  signed. All four backends lower that conversion as a plain register copy and
  LLVM's own `inttoptr` zero-extends, so a narrower operand that arrives
  un-widened silently loses its top half: `(void *)-1` — musl's `MAP_FAILED` —
  came out as `0x00000000ffffffff` and hung four libc-test units for as long as
  it was expressible. `c_ir_emit_integer_to_pointer` is the one place that
  performs it, a null pointer constant is built at pointer width so it costs no
  instruction there, and `ir_canonical_conversion_valid` rejects a
  non-pointer-width operand outright. `tests/basic_c_integer_to_pointer.c`
  pins the answers under every allocator.
- `main` is the one function whose fall-off is defined: reaching the `}` that
  terminates it returns 0 (C 5.1.2.2.3), where every other non-void function's
  is undefined (C 6.9.1p12) and terminates with `IR_OPCODE_UNREACHABLE` — the
  `ud2` Clang and GCC also emit. `CIrSignature.returns_zero_at_end` decides it
  once, with the signature, from a file-scope declaration named `main` whose
  return type is `int`; the terminator does not match a name. Getting it wrong
  is silent until the program ends: `regression/sem_close-unmap` and
  `functional/mntent` in libc-test have no return statement anywhere and both
  ran correctly to their last statement before dying on the brace with SIGILL.
  `tests/basic_c_main_implicit_return.c` pins it under every allocator, and
  exit zero is reachable there only through the closing brace.
- `builder->size_type` and `builder->ptrdiff_type` are chosen against the width
  of the scalar type the lowering built, not against `program->data_layout`'s
  own `unsigned long` entry. The two can disagree: the layout comes from the
  *preprocess* result and the scalar types come from `c_lower_to_ir`'s `target`
  argument, so a caller that preprocesses with one target and lowers with
  another — every frontend test does, preprocessing with a default target —
  otherwise gets a `size_t` narrower than a pointer on an LLP64 host. Nothing
  noticed while that only reached arithmetic; the widening above notices
  immediately, which is how a platform-independent defect first showed up as
  one platform's CI failure.
  `c_test_pointer_width_integer_conversion` lowers the same source for an LLP64
  target and an LP64 one and checks both.
- `IrFunction.opcode_summary` answers *may this function contain opcode X*
  without a scan, and it answers only for the `IR_OPCODE_SUMMARY_TRACKED`
  list: an opcode outside that list is never recorded, so
  `ir_function_may_contain_opcodes` rejects a query naming one rather than
  reporting a confident absence. Adding a query means adding its opcode to
  the list in the same change. The summary is exact only for functions
  created by `ir_module_add_function` and filled by
  `ir_function_add_instruction`; IR whose rows were written straight into
  `instructions` reads as unknown and every consumer keeps a scan for it.
- Source diagnostics in shared layers use canonical `IrSourceRange` and
  `IrSourcePosition`. Do not reintroduce parser-specific source-range APIs into
  codegen, debug information, object writing, or the linker.
- A value defined by `IR_OPCODE_GLOBAL` is a *place*: its frame slot holds the
  object's address, so it is an eightbyte whether or not the object's type has
  a resolved layout. That is what lets C's `extern struct opaque object;` be
  addressed without ever being completed, and musl's `src/include/stdio.h`
  depends on it — it suppresses the definition of `struct _IO_FILE` and then
  declares `extern FILE __stderr_FILE`, so every `stderr` in the tree takes
  the address of an incomplete object. Codegen's two value-sizing loops
  therefore require a resolved layout of every value *except* a global place;
  requiring it of all of them cost eight musl units with an
  `INVALID_IR` blamed on whichever function happened to be first in the
  module.
- **An array lvalue is never loaded**, whichever expression names it. A named
  array keeps its place, and so does `*p` on a pointer to an array: C 6.5.3.2p4
  says the dereference designates the array object, and what follows either
  decays it or indexes it in place, neither of which reads anything. Loading
  one instead emits an `IR_OPCODE_LOAD` of the array type, which the code
  generator honours by copying the whole object into a frame temporary — so
  `(*p)[1] = v` indexed the copy and the store was dropped with no diagnostic
  (#719), while `p[0][1] = v` next to it worked. The expression walk in
  `c_gen.c` returns the place from its dereference arm for the same reason its
  address-of arm accepts one; `tests/basic_c_packed_layout.c` runs all three
  spellings of the store and `c_test_frontend_global_types` pins that no lowered
  function holds a load of array type. The decay is the half a libc trips
  over: musl's strftime writes into `*s` through `snprintf` and returns it from
  a `char (*s)[100]` parameter, so the copy took every formatted specifier with
  it and libc-test's `functional/strftime` failed all 64 of its checks.
  `tests/basic_c_pointer_to_array_place.c` pins that shape under all four
  register allocators.
- **An aggregate member the next `.` walks through is a place, not a value.**
  `((T *)p)->a.b` names b, and C 6.5.2.3 gives the route to it no read of its
  own. The expression walk in `c_gen.c` loaded `a` anyway and then recovered
  the place it needed from that load's own operand, so every answer was right
  and the copy was dead — but a dead copy is still a read of memory, and
  offsetof is written on the null pointer. A compiler that does not advertise
  `__builtin_offsetof` gets musl's other spelling,
  `((size_t)( (char *)&(((type *)0)->member) - (char *)0 ))`, so a member named
  through two accesses copied a whole object out of address zero. musl's
  `__pthread_exit` takes exactly that offset for every mutex on the robust list
  — `_m_next` is `__u.__p[4]` — and died there with SIGSEGV, which is what
  libc-test's `functional/pthread_robust` and `regression/pthread-robust-detach`
  reported as #737. Predefining `__GNUC__` in every dialect moved musl to
  `__builtin_offsetof` and closed both units before this rule landed, so the
  spelling that reaches the walk is now a program's own rather than a libc's:
  measured 2026-08-30, the libc-test classification is identical with and
  without this rule. The member arm keeps the place when the following token
  is a `.`, which is the same rule as the array arm beside it; a chain that
  ends at the aggregate still loads it, so a by-value read is unchanged.
  `tests/basic_c_member_chain_place.c` pins the offsets against a live object
  under all four register allocators, spelling the pointer form directly so it
  does not depend on which offsetof a header picks, and faults the way musl did
  if the copy comes back. The peek only sees the token after the member
  identifier, so a group hides the `.` that follows from it — `(*o).a.b` and
  `&(((T *)0)->a).b[i]` reach the next access with the load already emitted,
  the dereference arm having emitted the first one and the member arm the
  second (#741). The place `c_ir_emit_field_place_from_value` recovers from
  such a load therefore *drops* it, through the same
  `c_ir_recover_place_from_value` the address-of arm of `c_ir_apply_operation`
  uses for `&E`: the load must still be the last instruction emitted, which it
  is because the access that recovers from it is the next thing the walk does.
  A group is not a frame of its own — an ordinary one is a `C_CONDITIONAL_OPEN`
  marker on the same expression frame's operator stack — so nothing about this
  needs a place-or-value request threaded through the lowering machines.
  `c_test_frontend_global_types` pins that no lowered function holds a load of
  a struct or union type nothing reads, and that the by-value read beside it
  keeps the one it needs.
- **A value never carries a qualifier.** The frontend builds a qualified copy
  of a type wherever a qualifier is written, because a place, a pointee or a
  member has to carry it, and that copy keeps the base's kind and layout: it is
  one representation under two ids. Both load emitters and the store emitter
  therefore unqualify a `volatile` place the way they already unqualified
  `_Atomic` -- an lvalue conversion yields the unqualified type of the object
  (C 6.3.2.1p2) and an assignment converts to the unqualified type of the left
  operand (C 6.5.16.1p2) -- and `c_ir_emit_cast` treats a difference of only
  `volatile` as no conversion at all. The value ladder there spans the scalar
  kinds, so while a struct crossing the qualifier had to find an arm in it,
  `volatile sigset_t oldset = set2` was refused outright and every unit written
  around `setjmp` failed to compile (#735). Volatility itself never travelled on
  the type: a load and a store carry `volatile_access`, taken from the place's
  own flag, which is why none of this changes which accesses are volatile.
  `ir_types_differ_only_in_volatile` is the one predicate, and
  `ir_validate_canonical_module` admits exactly that difference between a plain
  `IR_OPCODE_LOAD` or `IR_OPCODE_STORE` and its place -- the pairing the atomic
  opcodes were always validated with. `tests/basic_c_volatile_aggregate.c` pins
  both directions of the qualifier under all four register allocators.
- Native lowering is `canonical IR -> machine IR -> scheduling/register
  allocation -> encoding`. Selection patterns and scheduling classes remain
  separate metadata domains even when they share instruction-form IDs.
