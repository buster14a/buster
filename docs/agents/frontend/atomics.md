# Atomic layout, ABI, and accesses

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- **`_Atomic T` is a type built from T, not a qualified T, and its layout says
  so.** Clang pads it up to the next power of two and aligns it there, so a
  value the `__atomic` builtins could reach lock-free has an instruction that
  covers it: there is no three-byte atomic access and there is a four-byte one.
  `_Atomic` of a three-byte record is four bytes aligned four, of a five-byte
  one eight aligned eight, of a twelve-byte one sixteen aligned sixteen, and a
  zero-sized aggregate still takes a byte; every atomic scalar ends up aligned
  to its size, which is the same sentence that answers `_Alignof(_Atomic
  cache_line)` above and what moves `_Atomic _Complex double` from eight-byte
  alignment to sixteen. The ceiling is the target's maximum lock-free width,
  `TargetDataLayout::atomic_max_width` -- 128 bits everywhere here but wasm64
  and BPF, where it is 64 -- and a type wider than that keeps T's own layout,
  so the rule is not "round every aggregate up": `_Atomic` of a seventeen-byte
  record is seventeen bytes aligned one in Clang too. GCC pads nothing and
  raises the alignment only where the size is already a power of two, so this
  is the second place the two references disagree and Clang is the oracle for
  both (#731). `c_atomic_promoted_layout` in `c_parse.c` is the one rule and
  both engines ask it: `c_parse_type_layout` at the seed, at the exit that
  answers a builtin kind outright, and at an atomic branch of the solve that
  answers for every kind a qualified copy can carry -- ahead of the branches
  that would lay that copy out from its own kind -- and
  `c_ir_add_qualified_type` for the copy the mapping pass builds. An **alias
  over an atomic type** replaces the alignment and keeps the padding, so the
  aligned-alias branch runs first and takes only the promoted size from the
  rule: `typedef at3 t __attribute__((aligned(32)))` is four bytes aligned
  thirty-two. A **type name** builds the atomic type as well, in each of the
  three spellings that reach one: `c_ir_type_name_prefix` qualified only an
  aligned alias before, which was invisible while `_Atomic T` was laid out like
  T and became `sizeof` 3 written inline against 4 through a typedef the moment
  it was not. The fourth spelling is `_Atomic` written in
  front of a `struct`, `union` or `enum` keyword, and it reaches the type
  through the declaration-specifier run rather than through a resolver of its
  own: `c_parse_atomic_type_specifier_at` is what tells the qualifier apart
  from the `_Atomic ( T )` specifier, a `(` right after the keyword being the
  only difference, and the prefix scans stop only at the specifier. Stopping
  at both left the qualifier uncollected, so the aggregate branch handed back
  the tag's own type and `_Atomic struct three` was `sizeof` 3 against the
  other three spellings' 4 -- and, worse than a number, an assignment to such
  an object was an ordinary aggregate copy where the program asked for an
  atomic store (#761). `const struct S` and `volatile struct S` ride that same
  run and always reached the type through it. Two scans ask the question: the
  declaration-specifier run in `c_type_parse_scalar_step`, whose existing
  completion applies whatever the run collected, and
  `c_parse_machineless_base_type`, the operand walk the enum-constant
  evaluator uses because the type-parse machine is already running the body it
  is folding for -- that walk dropped the leading run before the tag and the
  trailing run after it alike, which failed a `const struct S` operand outright
  rather than mis-sizing it. That walk had no branch for the `_Atomic ( T )`
  spelling either, so it resolved nothing for a tag, a typedef name and `int`
  equally, and the enumerator that spelled it failed -- which fails the enum
  type and leaves every enumerator beside it undeclared for the rest of the
  file (#784). The specifier's operand is a whole type name rather than a base
  type, so the walk resolves it the way it resolves the outer one -- base type,
  then `c_parse_machineless_declarator_suffixes`, the pointer/qualifier/array
  chain shared with the sizeof operand walk -- and then applies the refusals
  `c_type_parse_scalar_step` diagnoses at `C_DIAGNOSTIC_INVALID_ATOMIC_TYPE`:
  an array, a function, `void`, or an already-qualified operand. It refuses
  silently, because the walk has a fallback -- the caller tries the operand as
  an expression next -- and a diagnostic would land in the caller's throwaway
  copy of the parse result; an enum constant that does not fold reports the way
  every other one does. `_Atomic(_Atomic(int) *)` is legal C, so the nesting
  follows the source and the levels go on an explicit stack
  (`C_PARSE_MACHINELESS_ATOMIC_LEVELS`, eight) rather than on the C call stack;
  past it the operand does not fold, which is where every depth of it stood
  before, and the machine-bearing path folds it at any depth either way. On
  the argument side the promotion moves nothing *on System V*: a promoted
  four-byte record is one INTEGER eightbyte where the three-byte one already
  was. It does move something on Win64, where the class is a function of the
  size alone -- four bytes ride a register and three are passed indirectly --
  so the classification is made from the promoted type rather than from the
  record on every convention, and that is what makes an argument position
  carry the atomic type; see the parameter bullet below.
  The **LLVM bitcode writer** maps an atomic type onto its
  operand's LLVM type, which is exact for every atomic scalar -- `_Atomic int`
  and `int` are one type -- and short by the padding for an aggregate, so an
  atomic type whose `layout.size` exceeds its operand's gets a record of its
  own instead: the operand followed by a byte array of the padding, which is
  what Clang writes as `{ %struct.three, [1 x i8] }` (#767). The operand is
  then the atomic type's only dependency, so `llvm_bc_type_dependencies_ready`
  answers for it directly rather than from the fields the qualified copy shares
  with it -- otherwise the table walk builds the copy as a plain struct before
  the operand it is meant to wrap has an id, and the padding is lost.
  `tests/basic_c_atomic_bitcode.c` holds the three positions a type is built
  for, and the native object it also runs pins the sizes the bitcode has to
  agree with.  `tests/basic_c_packed_layout.c` pins the promoted sizes
  next to the one-byte aggregate that agrees without them, once for each
  engine, and they stay out of the cross-linked
  `basic_c_packed_layout_shapes.h` for the same reason the shapes above do.
- **A qualifier decides nothing about how a value is passed.** `_Atomic T` and
  `volatile T` take T's argument class on every convention: the qualified copy
  carries T's kind and T's fields, `_Atomic` changes only the size and the
  alignment, and neither qualifier introduces a class of its own. So a record
  holding an `_Atomic int` is the INTEGER eightbyte its `int` spelling is, a
  record holding an `_Atomic float` beside a plain one is still two floats --
  SSE on System V, a homogeneous float aggregate on AAPCS64 -- and an atomic
  record passed by value is classified using its fields and promoted layout.
  Initially no fixture could pin that: an atomic aggregate parameter failed
  code generation in every spelling (#786), and the one spelling that compiled
  did so only because it did not reach the type at all (#761), so a fixture
  written that way would have passed while testing nothing. Those gaps are now
  repaired; `tests/basic_c_atomic_aggregate.c` covers parameters and returns in
  all four spellings through the conversions described below. The wrapped shape --
  `struct { char c; _Atomic(struct pair) v; }` -- reaches the same walk and is
  in the pair. `ir_abi_unqualified_type` in `ir.c` is the one step that says
  so, and the AAPCS64 homogeneity walk is its only caller: System V already
  reads the leaf kind, which a qualified copy keeps.
  **This is the one place the oracle is not followed, and it is a decision
  rather than an oversight** (#763). Measured 2026-08-30 against Clang 22.1.8
  and GCC 16.2.1: GCC classifies every one of these shapes exactly as the
  unqualified spelling on x86-64 (its AArch64 answer was not measurable on the
  machine that measured this -- no cross-GCC). Clang sends any record
  *containing* an atomic member, and any atomic record, to MEMORY -- but only
  on System V x86-64; the same Clang passes a record *containing* an atomic
  member in registers on Win64, on AAPCS64 and on Darwin AArch64. An atomic
  *record* passed by value shows the fallthrough in a third shape on Win64
  (measured 2026-08-30): Clang expands it into one argument per member, the
  padding byte among them, and returns it through `sret`, where the four-byte
  record it is built from would ride one register both ways. Its AArch64
  homogeneity test declines separately, so `struct { _Atomic float a, b; }`
  rides X0 there where `struct { float a, b; }` rides S0/S1. Both refusals are
  the same accident: `X86_64ABIInfo::classify` asks `Ty->getAs<RecordType>()`
  and `isHomogeneousAggregate` asks for a builtin type, an `AtomicType` is sugar
  over nothing, and each walk falls through to its "everything else" tail. The
  psABI and AAPCS state no such rule, `volatile` reaches neither refusal, and
  Clang contradicts itself across three of its own conventions, so following
  it would mean writing a rule neither document states and disagreeing with
  GCC everywhere and with Clang on every convention but one. The cost is real
  and is recorded rather than hidden: a `struct { char c; _Atomic int v; }`
  argument sits in a register on this side of a System V x86-64
  translation-unit boundary and in memory on Clang's.
  The same reading fixed a divergence from *both* references: identity in the
  homogeneity walk was a type id, so `struct { float a; volatile float b; }`
  was an integer pair where Clang and GCC both keep the two-register
  aggregate. `tests/basic_c_atomic_abi_shapes.h` and the callee/caller pair
  around it pin all of it -- both halves through this compiler under every
  allocator, mixed with a real GCC on System V x86-64 (the host compiler
  cannot stand in: on that convention Clang is the half that disagrees), and
  mixed with Clang on AArch64 under qemu, where the one shape Clang answers
  differently leaves through `ATOMIC_ABI_REFERENCE_DECLINES_ATOMIC_HFA` and its
  `volatile` twin stays behind to pin the same mechanism. Both directions were
  verified to fail when the rule is taken away: the Clang-paired System V link
  fails at the first record, and restoring the type-id identity fails the
  Clang-paired AArch64 link at the `volatile` shape.
- **An atomic aggregate is loaded and stored as one integer access of its
  promoted width**, which is what the promotion above exists for: a three-byte
  record is read and written through four bytes, and the padding the promotion
  added is written as zero, because Clang copies the value through a zeroed
  temporary and that is the oracle. The widths the canonical emitters lower
  that access at are one, two, four and eight bytes on both targets, plus
  sixteen on x86-64 where `cx16` gives them `CMPXCHG16B`, and on AArch64
  through exclusive-pair loops -- the sequences `_Atomic __int128` already
  uses, which also take aggregates promoted into that width. The
  AArch64 selector now handles the promoted aggregate loads/stores through
  sixteen bytes as well as full-width integer exchange/RMW/CAS. Its pair
  update rows consume the integer images described below; they do not widen
  arbitrary smaller frame objects. Any other target-specific selection miss
  remains visible in fallback statistics pending native-backend retirement.
  Anything wider would need a `libatomic` lock and there is none here,
  so lowering refuses it with a diagnostic naming the width rather than leaving
  code generation to fail internally (#762). The refusal is
  `c_ir_atomic_aggregate_accesses_lowerable` in `c_gen.c`, and it runs over the
  *finished* body rather than where the access is built, because an operand is
  lowered as a value before an expression that only wanted its address recovers
  the place and drops the load again: refusing at the emit site rejects
  `&object.atomic_member`, which performs no atomic access at all and is what
  `tests/basic_c_packed_layout.c` writes over its seventeen-byte atomic
  member. The sixteen-byte x86-64 access requires `cx16`, so the refusal is
  target-dependent where the layout rule above is not.
  `tests/basic_c_atomic_aggregate.c` runs the bytes under every allocator with
  Clang's answers baked in, including the padding.
- **Aggregate C11 exchange and compare-exchange use integer representations
  in canonical IR.** `c_ir_atomic_aggregate_bits_place` preserves the atomic
  qualifier on an integer pointer view of the same promoted object.
  `c_ir_atomic_aggregate_bits_value` converts record values through private
  scalar storage, zeroing the promoted tail before writing a smaller record.
  The returned bits become a record again for exchange results and failed-CAS
  expected-value writeback. Existing aggregate loads/stores retain their
  canonical types; the IR validator and native integer atomic emitters keep
  their kind constraints. The supported-width policy remains the existing
  aggregate atomic access policy. GNU `_n` builtins do not accept records.
  The fixture covers three 16-byte shapes, strong/weak CAS, stale expected
  writeback, load/store/exchange, natural alignment, and a three-byte record
  promoted to four bytes, a volatile record with six bytes of members, and a
  union. The 16-byte runtime cases execute on x86-64 and AArch64. Raw IR
  validation covers both frontend SSA paths.
- **A parameter and a return value of an atomic aggregate carry the atomic type
  itself, and what converts between it and the record is an object rather than
  an instruction.** Every access in between carries the unqualified type -- a
  load of an atomic place yields the record and a store takes one, which the IR
  validates rather than merely allows -- but an ABI position cannot: the
  `IR_OPCODE_ARGUMENT` type is validated against the signature's parameter type,
  and code generation classifies an argument from the type the instruction
  carries, so a record-typed value there would be classified from three bytes
  where the object is four. The two are therefore two aggregates of two sizes
  meeting in one expression, which no ladder in `c_ir_emit_cast` spans, and
  every spelling of the type failed code generation with an internal message
  rather than a diagnostic (#786). `c_ir_atomic_aggregate_pair` in `c_gen.c`
  recognizes the pair and `c_ir_emit_atomic_aggregate_conversion` converts
  through a slot of the atomic type, written through one view and read back
  through the other: widening is the settled atomic store -- one integer access
  of the promoted width, which is what zeroes the padding -- followed by a
  plain read of the whole slot, private storage nothing else can observe, and
  narrowing is that pair reversed. Both accesses are the settled ones above, so
  a promoted width past the target's lock-free ceiling is refused by the same
  walk over the finished body rather than failing inside code generation, and
  nothing in the IR, in validation or in code generation had to change. The
  parameter and return round trips are in `tests/basic_c_atomic_aggregate.c`
  in all four spellings and in both positions, with Clang's bytes -- the
  padding the promotion added among them -- baked in the way the rest of that
  fixture bakes them.
