# Packed layout, alignment, and bit-fields

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- A VLA's declared alignment travels on `IR_OPCODE_STACK_ALLOCATE`. For an
  alignment above the native stack's sixteen-byte guarantee, both canonical
  and machine emitters compute `align_down(old_sp - size, alignment)` and
  probe the complete distance to that address, including alignment padding.
  Rounding the byte count alone preserves a misaligned incoming stack pointer.
  Keep the ordinary sixteen-byte path and the existing save/restore lifetime
  semantics; test real addresses across different incoming stack residues and
  page-crossing sizes.
- Partial subscripting of a pointer-to-VLA already yields a decayed row pointer.
  `c_ir_lower_expression_core_consume_place` loads only `IR_VALUE_PLACE`
  operands; loading the decayed value creates invalid IR and forces both native
  selectors to reject the function. `tests/basic_c_pointer_to_vla.c` covers row
  addresses, runtime strides, local arrays and indirect comparison calls.
- **`__attribute__((packed))` and `__attribute__((aligned(N)))`** decide object
  representation, so ignoring them is an ABI divergence rather than a missing
  optimization: a Buster-only program agrees with itself whatever it agrees on,
  and the disagreement only appears against another compiler's object or an
  offset a program computes by hand. `packed` and the aggregate's own
  `aligned` live in `CParseResult.aggregate_attributes`, a side table keyed by
  type index rather than a field on every `CType`: the population is a handful
  of aggregates against tens of thousands of types. A member's `packed` is a
  bit on `CMember`; an object declarator's `aligned` joins the specifier-level
  alignment specifiers in the one contiguous run `alignment_start`/
  `alignment_count` names, which is why the trailing scan runs immediately
  after the specifier one. `#pragma pack(N)` asks the same question -- the
  ceiling a member's alignment is clamped to -- and `packed` is that ceiling at
  one byte, so both feed one knob. **Two layout engines read it and must agree**:
  `c_parse_type_layout` in `c_parse.c` folds `sizeof`/`_Alignof` during the
  parse and `c_lower_to_ir` in `c_gen.c` builds the `IrType`. They disagreed
  about `#pragma pack` before this: the fold packed and the IR did not, so a
  folded size contradicted the object it sized.
  A packed bit-field takes the next bit rather than the next storage unit of
  its declared type, which is what Clang and GCC do and what makes
  `struct __attribute__((packed)) { int a : 3; int b : 30; }` five bytes. The
  unit such a field is *read* through is chosen after the aggregate's size is
  known, by sliding it back until it lies inside the object -- a
  read-modify-write through a unit hanging off the end would clobber the next
  object -- and where the object is too small for the declared type to sit
  anywhere, by narrowing the unit to the smallest power of two that covers the
  field: `struct __attribute__((packed)) { char c; int b : 5; char t; }` is
  three bytes, so `b` is read through the byte at offset one, which is again
  what Clang and GCC do. **The narrowed width lives on `IrField.access_size`**,
  zero meaning the declared type's size, and `ir_field_access_size` is what
  every reader asks: the load and the read-modify-write in `c_gen.c`, the four
  constant-initializer folds there, the `IR_OPCODE_AGGREGATE` selectors in
  `machine_x86_64.c` and `machine_aarch64.c`, and the two canonical emitters in
  `codegen.c`. It is also the one place a `LOAD` or `STORE` may disagree with
  its place's type, which `ir_place_narrow_bit_field_access` is what validation
  admits it through. **A field whose bits cross every unit that fits has no
  single-unit access even then**, which is every width whose byte count is not
  a power of two: `union __attribute__((packed)) { long long b : 40; }` is five
  bytes and there is no five-byte load. `access_size` then carries the *span*
  rather than a unit, and `ir_field_access_pieces` decomposes it into the
  descending powers of two that cover it -- five is four plus one, seven is
  four plus two plus one, and nine, the widest span there is, is eight plus
  one. Clang writes the same access as `i40` and lowers it to the same
  sequence. Every reader walks the pieces: the frontend assembles a load out of
  them and read-modify-writes each one in turn, reaching the bytes past the
  first through a `unsigned char*` taken from the member place, and the
  constant folds deposit the bits one byte at a time because no integer is nine
  bytes wide. Both native `IR_OPCODE_AGGREGATE` selectors walk the same pieces,
  merging only each field's intersection with the current piece. Siblings
  sharing the complete storage unit accumulate together; zero-width separators
  emit no stores. Keep the packed, anonymous and unnamed initializer fixtures
  in the strict MIR corpus. **A `_Bool` bit-field is extracted
  like any other**: its declared type is not an integer, so it used to skip the
  extraction and hand back the whole `_Bool` unit -- every neighbour's bits with
  it, normalized to one, which is why a `_Bool` that was the only set bit still
  read correctly -- and the write clobbered them. The unit it is read and
  written through is the unsigned integer of the same width, because the shift
  has to see the raw byte. A zero-width bit-field keeps aligning to its declared
  type even inside a packed aggregate, which is also what Clang and GCC do.
  **A zero width belongs to the *unnamed* bit-field alone**: C requires a named
  one to be at least one bit wide (C23 6.7.3.2p4) and both reference compilers
  refuse `int b : 0;`, where accepting it laid out a member that occupies no
  bits and can still be assigned and read back (issue #710). The width is
  checked in `c_lower_to_ir` where the constant expression is folded, so the
  expression spelling `int b : 1 - 1;` is refused with the literal one rather
  than only the spelling the parse fast path folds. The report shares the
  one-diagnostic-per-type budget with the rejected alignment specifier -- they
  are one `definition_rejection` slot whose kind travels with the message --
  and the definition still lays out, the way a rejected alignment specifier
  still hands back an alignment, so the program hears about the member it wrote
  rather than about a type that never got a layout.
  On AArch64 the accesses this reaches land at whatever byte offset packing
  chose, and the scaled unsigned-immediate load/store addresses only multiples
  of its own width, so `codegen_canonical_a64_memory_operation_base` falls back
  to the unscaled form -- `LDUR`/`STUR`, any byte offset in a nine-bit field --
  and materializes the address beyond that. It used to fail the whole
  compilation instead, which `struct __attribute__((packed)) { char a; int v :
  32; }` already reached before any of this.
  **A bit-field is not a field for System V's unaligned-field rule.** Its
  declared type is not what it occupies -- `int value : 20` at bit eight of a
  packed record occupies twenty bits inside the first eightbyte, not four
  bytes at an offset no `int` would sit at -- so
  `ir_system_v_abi_classify_bit_field` merges INTEGER into each eightbyte the
  field's *bits* fall in and leaves the declared type out of it, which is what
  the psABI writes and what clang and GCC compile. Asking the declared type
  instead sent every such record to memory, so the five bytes of
  `struct __attribute__((packed)) { char lead; int value : 20; char tail; }`
  came back through a hidden pointer where System V returns them in `rax`
  (issue #721); a program agreed with itself and disagreed with the object
  next to it. An *unnamed* bit-field is padding for this and contributes no
  class at all, which is observable beside a float: clang returns
  `struct { float f; int : 20; }` in `xmm0` and the same record with the field
  named in `rax`. The class is always INTEGER, since C admits no bit-field of
  floating type. AArch64 never asked: its aggregates up to sixteen bytes take
  INTEGER parts by size once `ir_homogeneous_float_abi` declines them.
  **A union member starts at bit zero whichever kind it is**, so a union sizes
  to the bits its widest member *occupies* rather than to that member's
  declared type: `union __attribute__((packed)) { char c; int b : 5; }` is one
  byte. The unpacked spelling needs no arm of its own -- rounding the size up
  to the alignment its declared type asks for is what gives it its four bytes
  back -- and the unit the field is read through is chosen by the same slide a
  struct's is, so the one-byte union reads its five bits through a byte
  (issue #706). Sizing it from the declared type also hid the original
  single-unit limitation from unions: a widest bit-field of 17 to 24 or 33
  to 56 bits sizes the union to a width no power-of-two unit fits inside --
  three bytes hold no unit covering 24 bits. These spans now use
  `ir_field_access_pieces`, just as the struct spelling of those widths does.
  **A unit is never written whole.** Sliding and narrowing are what make a unit
  reach bytes it does not own: it can cover an ordinary member -- the four-byte
  unit of `struct __attribute__((packed)) { char c; int a : 5; int b : 7; char
  t; }` starts at offset zero, where `c` is -- and two units can share a byte,
  because packing narrows one field's unit and not the next one's. So every
  writer of a bit-field is a read-modify-write, including the one inside an
  aggregate initializer, where the members are materialized into a zero-filled
  slot and it is tempting to treat the accumulated word as the whole unit: the
  canonical emitters spell it `OR mem, reg` and the two `IR_OPCODE_AGGREGATE`
  selectors seed the accumulator with a load of the unit rather than with zero.
  Ordering the members differently does not substitute for it -- a whole-unit
  store loses whichever neighbour ran first, and two overlapping units lose one
  of themselves whatever the order (issue #705).
  **Integer promotion uses the bit-field width, not its storage width.** An
  `unsigned int : 3` promotes to `int`, while an `unsigned int : 32` remains
  unsigned. `c_ir_mark_unsigned_bit_field_value` keeps this distinction in a
  lazily allocated frontend table; canonical types and field layout retain
  the declared type. Arithmetic, unary plus, default arguments, and switches
  consult the promotion fact. Explicit casts discard it, and assignment
  results retain it after masking to the stored width, without rereading a
  volatile field. The strict operand type walk receives the promotion context
  explicitly so `_Generic(+field)` and conditional arms agree with emitted
  arithmetic while a direct type query still sees the declaration's type.
  `tests/basic_c_bit_field_promotion.c` covers widths 1, 3, 31, and 32,
  anonymous members, casts, assignments, and argument promotion under every
  allocator (GitHub #218).
  Automatic nested initializers select known fields by index, preserving the
  initializer expression's source range without inventing a token for an
  anonymous member. Positional cursors and brace-elided descent skip unnamed
  bit-fields, including zero-width fields, while anonymous structs and unions
  remain initializable subobjects. Indexed places inherit both the enclosing
  place's volatility and the field type's volatility. The frontend IR check
  and `tests/basic_c_unnamed_initializer_members.c` cover these rules under all
  four allocators (GitHub #323). Bit extraction uses the unqualified value
  type returned by the load, including its shift and mask constants. Volatility
  remains on the memory access; it must not create mismatched arithmetic types.
  The strict driver corpus independently validates the complete canonical IR.
  A bit-field declarator carries a list of its own in exactly one place, *after*
  the width -- Clang rejects `int b __attribute__((packed)) : 5` -- so
  `c_type_parse_aggregate_segment_step` trims the width's token range with
  `c_parse_trailing_attribute_start`, the helper the parenthesized-declarator
  path already uses. Left untrimmed the list is part of the constant expression
  and the width never folds, which loses the aggregate's whole layout while
  `sizeof` still answers (issue #693). It is the third spelling of the packed
  bit-field layout above, and reaches the same narrowed unit as the other two. The trimmed tokens stay inside
  `[declarator_start, declarator_end)`, which is the range the per-declarator
  `packed` and `aligned` scans read, so the attribute reaches the layout with no
  second pass.
  **`aligned` written on a typedef is a different question**: it sets the
  alignment of the type the name declares rather than raising a declaration's,
  which makes it the one spelling that *lowers* an alignment without `packed`
  -- `typedef int pair __attribute__((aligned(2)))` is two-byte aligned in
  Clang and GCC alike. The request lives in `CParseResult.type_alignments`, a
  side table keyed by type index for the reason `noreturn_function_types` is
  one, and it keys on a *copy* of the type the declarator arrived at:
  `typedef int cache_line __attribute__((aligned(64)))` names the one builtin
  `int`, so marking that would realign every `int` in the translation unit.
  The copy carries `has_unqualified_type`, which is what gives both layout
  engines one place to read the natural alignment from and keeps the alias
  compatible with what it aliases. **Which of the two positions it is written
  in decides how many names it reaches**: after the declarator it belongs to
  that declarator alone, and among the specifiers it belongs to the
  declaration's type, so `typedef int __attribute__((aligned(16))) t5, t6;`
  aligns both names. That position is also where `_Alignas` is a constraint
  violation rather than a request -- a typedef declares no object for a
  declaration's alignment to apply to, and Clang and GCC both refuse
  `typedef _Alignas(16) int t;` -- so the run is *partitioned* by spelling
  rather than rejected by position, which had dropped the whole declaration
  (issue #715). `c_parse_typedef_alignment_run` in `c_parse.c` does that for
  both the file-scope and the block-scope parser, reading the spelling back
  out of the token stream with `c_alignment_specifier_is_standard`, and it
  rewinds the specifier table with the records it drops so the
  declarator-position ones each parser appends next stay contiguous with what
  survives. A **function** keeps the position rejection whole: neither
  reference compiler raises a function's alignment through it.
  **A qualifier cannot take the request away**, and a qualified copy points
  *past* the alias at the type it strips to, so `c_parse_add_qualified_type`
  gives the copy its own record rather than
  leaving the layout engines to walk a chain that no longer names the alias --
  `const cache_line` folded `_Alignof` 4 where Clang and GCC answer 64, in
  every position and with no diagnostic (issue #714). `_Atomic` applied to an
  aligned alias is the exception, and it is the one place the two references
  disagree: Clang gives `_Atomic cache_line` the alignment an atomic of that
  width gets and GCC keeps the alias's, so the record is inherited only when
  the step does not add `_Atomic`. **Two places build that type and one rule
  answers for both**, `c_parse_atomic_drops_type_alignment`: the copy a
  declaration makes goes through `c_parse_add_qualified_type`, while a type
  name in an expression is resolved during lowering by
  `c_ir_type_name_prefix` in `c_gen.c`, which builds no `CType` at all and so
  reached the aligned alias's own `IrType` and kept the request the typedef
  spelling had already dropped -- one type answering `_Alignof` 64 written
  inline and 4 written through a typedef of it, which is two layouts for one
  object across two translation units (issue #726).
  `c_ir_atomic_over_aligned_alias` is the lowering half, and both of that
  resolver's spellings ask it: `_Atomic` as a qualifier before or after the
  name, and the `_Atomic ( T )` operator, whose branch reads its operand's
  typedef out of the tokens because an alias and the type it aliases can map
  to one `IrType`. It rebuilds from the alias's *unqualified* type exactly as
  the type-mapping pass builds the typedef spelling, so
  `c_ir_add_qualified_type`'s dedup hands back the very `IrType` that spelling
  mapped to; the qualifier spelling runs before the pointer run because
  `_Atomic cache_line *` qualifies the pointee. The `_Atomic` shapes in
  `tests/basic_c_packed_layout.c` are written twice, once each way, and pin
  the pair rather than only the number; they stay out of the cross-linked
  `basic_c_packed_layout_shapes.h`, whose other half is whichever host
  compiler the platform has and where a GCC host answers the alias's number.
  A *qualified* copy is built where the qualifier
  is written, which is after the aggregate that embeds it, so the
  scalar seed in `c_lower_to_ir` is cleared for every recorded type: the
  mapping round that lays the aggregate out would otherwise read the seed's
  natural alignment before the alias branch replaced it.
  On the System V side one more rule follows:
  "contains unaligned fields" there means unaligned for the field's *natural*
  alignment, so `struct { char tag; pair value; }` is passed in memory even
  though `value` sits where its type asked. `IrTypeLayout::natural_alignment`
  carries that, and it is zero for every type nothing lowered.
  It is also the only way an **array element can end up over-aligned**, which
  Clang and GCC both refuse and so does this: an element has to be addressable
  at its own alignment in every slot, so its size has to be a multiple of that
  alignment, and `cache_line a[2]` puts the second element four bytes into a
  sixty-four-byte alignment. The scan is at the end of the type-mapping rounds
  in `c_lower_to_ir`, over a settled table, which is what makes one report per
  array type automatic and reaches a typedef no object ever names; it is
  skipped whole on an empty `type_alignments`, because every other type is
  sized at a multiple of its alignment by construction -- an aggregate's own
  `aligned` rounds its size *up*, so `struct __attribute__((aligned(16))) { char c; } a[2]`
  is thirty-two bytes and stays well-formed (issue #703). The report is counted
  on the *bound record* rather than on the type, because a qualified array and
  a typedef of an array are copies carrying the same bound, and one report per
  written `[N]` is what Clang produces; two identical declarations that intern
  to one array type therefore report once where Clang reports twice. Two
  spellings reach that report down a different road (issue #713), because
  neither reaches the type table as a `C_TYPE_ARRAY`. A parenthesized
  declarator's `cache_line (*p)[2]` builds one only once the syntax scan stops
  reading a top-level `(` after an identifier as the parameter list of a
  function that identifier names -- see the declarator note below. An array
  type name in an expression, `sizeof(cache_line[2])` and the compound literal,
  is resolved during lowering and never reaches the table at all, so
  `c_ir_type_name_suffix` records the earliest offending bracket on the builder
  and `c_lower_to_ir` makes one report from it at the very end, after every
  declaration has been lowered: that resolver runs inside speculative attempts
  that are rolled back and revisits the same tokens many times, so keying on
  the token index is what makes the report independent of the order the
  attempts run in, and it has no bound record to count on. It records without
  refusing the type, the way the settled-table scan reports without refusing
  one: the report is what refuses the translation unit.
