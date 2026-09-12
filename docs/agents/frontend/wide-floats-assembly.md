# Long double, assembly, and Wasm boundaries

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

`signbit` reads the original float representation through canonical memory
operations: bit 31 for binary32, bit 63 for binary64, byte-eight bit 15 for
x87 and byte-eight bit 63 for binary128. It does not widen or narrow a value
before observing the sign. This preserves signaling NaNs, signed zero and
floating exception state. `basic_c_signbit_images.c` and its independent host
observer cover those images across the native target/mode/frontend/PIC matrix.
AArch64 binary128 widening uses ordinary MIR frame images; see the machine
guide for its exact conversion and native floating-environment checks. This
does not claim binary128 scalar ABI or arithmetic support.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- **`long double` is 80-bit x87 on System V x86-64, and it is memory-only.**
  Transport, the four arithmetic operators, negation, the six comparisons,
  truth conversion, and the conversions to and from the narrower floats and
  integers through 64 bits all lower, and a variadic argument takes the sixteen-
  byte, sixteen-aligned overflow slot the ABI requires. Every one of those is
  a *closed* x87 transaction: it loads its operands from frame slots, operates,
  stores the result back, and leaves the x87 stack as empty as it found it, so
  no value is ever live in an ST register across a machine instruction and the
  register allocators need no x87 class. Machine selection carries resolved
  SysV f80 values in sixteen-byte frame slots and selects arithmetic, negate,
  unordered-aware comparisons, f32/f64 conversion, signed integers through
  64 bits and unsigned integers through 64 bits. The six `MACHINE_X64_F80_*`
  rows have explicit frame operands and memory/barrier effects. Only the ABI
  bridges leave ST(0), or ST(0)/ST(1) for complex results, live beside a call or
  return. Single-f80 wrappers use the same ABI-proven bridge. SSA joins use
  two ordinary integer limbs and restore the frame value at block entry.
  Unsigned-64 conversion composes signed conversion, comparison, scalar masks
  and an exact zero/2^63 correction at extended precision; it preserves all
  four rounding modes and restores the complete control word after truncation.
  The direct implementation is `codegen_canonical_x64_emit_f80_*` in
  `codegen.c`; f80/i128 conversions remain unsupported by both backends.
  Preserve the caller's
  x87 control word: canonical truncate helpers and the MIR conversion row
  may temporarily change only rounding control for a C integer cast, then
  restore the exact saved word. `tests/basic_c_f80_machine.c` checks this
  subset under strict MIR; its HOST/LIBRARY/FENV modes support independent
  Clang callers and callees. `tests/basic_c_f80_u64.c` covers unsigned
  thresholds, fractions and positive zero, with CLIENT/LIBRARY/FENV modes
  for Clang boundary checks across all four rounding-control modes.
  An **aggregate** carrying an f80 payload takes one of two paths, and which
  one is the ABI classification's answer, never a walk of the fields. System
  V's merger algorithm orders its rules equal, NO_CLASS, MEMORY, INTEGER, x87,
  SSE — **INTEGER beats x87** — so musl's `union ldshape`, an f80 overlaid
  with `struct { uint64_t m; uint16_t se; }`, classifies as two INTEGER
  eightbytes and rides two general-purpose registers, which is what clang
  compiles it to (`{ i64, i64 }`) and is not what a literal "an x87 class
  makes the value MEMORY" reading produces. A shape the merger cannot
  reconcile — `union { long double; unsigned long; }`, whose X87_UP tail is
  then unaccompanied, or `union { long double; double; }`, or anything past
  two eightbytes — goes to memory whole, byval in and sret out. Neither asks
  anything of the x87 stack: their bytes are copied. Only a classification
  that really carries an X87/X87_UP part needs the x87 vocabulary, and
  `ir_abi_value_has_x87_part` is the predicate every gate asks. Objects of
  more than one `long double` — `long double v[2]`, local or global — lower
  the same way, because the array is memory-class and its elements are reached
  one f80 at a time. `tests/basic_c_long_double_aggregate.c` covers the
  semantics under all four allocators; the ABI itself is only pinned by
  `tests/basic_c_long_double_aggregate_{caller,callee}.c`, linked against the
  host compiler in both directions, because a caller and a callee this
  compiler produced agree with each other whatever they agree on.
  Reading one back out of a `va_list` admits exactly the two shapes the
  argument side does, and for the same reason. The X87/X87_UP pair classifies
  into memory, so a variadic `long double` is never in the register save area
  whatever the argument counters hold: `va_arg` realigns the overflow cursor
  to sixteen, takes the slot, and advances past all sixteen bytes. Canonical
  emission copies through x87 and clears padding; MIR copies the ten payload
  bytes directly, preserving payload/sign and making no padding promise.
  An opaque aggregate reads back through the ordinary eightbyte path.
  `tests/basic_c_va_arg_long_double.c` pins both under all four
  allocators, including a read through a `va_list *` and one past a `va_copy`
  — the spellings musl's `pop_arg` uses. Strict MIR selection, allocation and
  execution are also registered for this fixture.
  A *static* x87 initializer is folded rather than emitted:
  `c_ir_ext80_fold_initializer` in `c_gen.c` evaluates a constant expression
  over numeric literals — `+ - * /`, unary sign, parentheses — straight into
  the 64-bit significand and sign/exponent pair the object writer stores, for a
  scalar global, a function-local static, and each x87 element of an aggregate.
  Every leaf rounds in its own declared type and every operation rounds in the
  common real type the usual arithmetic conversions pick, which is what
  reproduces C's per-operation rounding and makes the bytes Clang-identical; a
  decimal fold would not, because `1/LDBL_EPSILON` is exactly 2^63 only once
  musl's spelling of the epsilon has rounded to 2^-63 first. Overflow becomes
  an infinity, underflow a signed zero, and the invalid operations — infinity
  minus infinity, infinity times zero, infinity over infinity, zero over zero —
  the default *positive* quiet NaN whatever the operand signs were, which is
  the answer Clang's own folder gives and the one
  `c_ir_fold_float_invalid_operation` gives the same expression inside a
  function; the two have to stay together, because a disagreement prints `nan`
  from one and `-nan` from the other. Those last two matter because musl spells
  `INFINITY` as `1e5000f` and `NAN` as `(0.0f/0.0f)` for a compiler that does
  not advertise the GNU builtins, so they are what a `long double` table in
  libc-test is made of. An operation between two *integer* operands is refused
  on purpose: it does not round at all, and `1/3` would need integer division
  semantics (zero), rather than floating-point division — which is also why
  `0/0` between two integer literals is a diagnostic here rather than the NaN
  its floating spellings fold to, Clang rejecting that initializer outright.
  So is an addition that aligns a genuine subnormal against a near-maximum
  operand, which spans more of the
  x87 exponent range than the folder's bignum holds — a zero operand is taken
  as the exact identity instead of aligned, so only a real subnormal reaches
  that edge. `tests/basic_c_long_double_static_initializer.c` pins the finite
  arithmetic and `tests/basic_c_long_double_static_special.c` everything from
  the infinities out, both against bytes read out of Clang's own object.
  The canonical emitter still refuses a fixed wide-float parameter of a
  variadic *definition* (the SysV `va_start`
  register-save area does not account for it), an aggregate whose
  classification carries an X87 class without being the ABI-proven single-f80
  or complex shape, and every wide float on a target whose `long double` is
  not this format.
- **A module-level `__asm__` block emits into the module's text through
  `codegen_emit_global_assembly` in `codegen.c`.** It interprets the
  directives itself — `.text`, `.byte`, `.p2align`, and the symbol directives
  `.globl`/`.global`, `.weak`, `.hidden`, `.type` and `.size` — and hands
  every instruction it does not have a fixed encoding for to `assembly_encode`,
  the same assembler the inline-assembly path uses, one line at a time. That
  is where relocation support comes from: a `call sym` or a
  `lea sym(%rip),%reg` against a name the block does not define becomes a
  `CodegenModuleRelocation`. The assembler's PC-relative addend already carries
  the distance from the relocated field to the end of its instruction, and the
  module vocabulary's does not — the object writer subtracts four on the way
  out — so the emitter adds those four back rather than restricting the shapes
  it accepts. A symbol the block names does not need a C declaration: the
  emitter adds one to `IrProgram.symbols` when the search misses, which is what
  lets a startup object define `_start`. Two things follow from the AT&T/Intel
  dialect being x86-only: the emitter passes `ASSEMBLY_SYNTAX_DEFAULT` for
  every other target, and the assembler's AArch64 vocabulary is the bootstrap
  control-flow set, so an AArch64 block gets `bl`, `brk`, `ret` and `nop` and
  not an ADRP/ADD page pair. A block that fails reports through
  `CodegenModule.failed_in_assembly` and the block/line beside it, which is what
  keeps the driver's diagnostic off the next C function in the file. The
  *inline*-assembly arm of `codegen_generate_canonical_module_attempt` shares
  the last two of those: once its template is substituted,
  `codegen_emit_inline_assembly` walks the result a line at a time and hands a
  leading-dot line to the same directive table and everything else to the same
  `codegen_global_assembly_encode_instruction`, so a `lea sym(%rip),%0` in a
  GNU asm template becomes the same relocation. What the two do not share is
  where a new symbol's name may point: an inline template is a copy the
  code-generation attempt owns and the retry rewinds, so the name recorded is
  the one in the instruction's IR literal (`codegen_assembly_durable_name`).
  Labels are refused inside a template rather than defined, because a template
  is emitted once per instruction rather than once per file.
- The Wasm64 backend consumes canonical IR directly. Unsupported ABI or
  instruction shapes must be diagnosed; never silently fall back to a native
  backend.
