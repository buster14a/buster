# `_Float16`, long double, assembly, and Wasm boundaries

[Agent instructions](../../../AGENTS.md) · Paths and commands below are relative to the repository root.

`signbit` reads the original float representation through canonical memory
operations: bit 31 for binary32, bit 63 for binary64, byte-eight bit 15 for
x87 and byte-eight bit 63 for binary128. It does not widen or narrow a value
before observing the sign. This preserves signaling NaNs, signed zero and
floating exception state. `basic_c_signbit_images.c` and its independent host
observer cover those images across the native target/mode/frontend/PIC matrix.
AArch64 binary128 widening and scalar transport use ordinary MIR frame
images; see the machine guide for their exact conversion and ABI-boundary
checks. Arithmetic, comparison, truth conversion and general narrowing remain
separately unsupported.
The host FENV fixture in `tests/host_aarch64_float_to_f128.c` uses ordinary
GNU inline asm for `mrs`/`msr` reads and writes of `fpsr`/`fpcr`; the baseline
AArch64 inline-assembly vocabulary selects these checked system-register rows
as closed MIR transactions, with strict no-fallback compilation retaining
their source/debug locations.

Read the matching sections; [the frontend index](../frontend.md) lists these notes in their original order. Cross-references such as “above” and “below” follow that order.

- **`_Float16` is IEEE-754 binary16, and it is a real type rather than a
  storage alias.** Two naturally aligned bytes on every supported target
  (`TargetDataLayout.float16_type`, `C_TYPE_FLOAT16`), its own rank below
  `float` in the usual arithmetic conversions, its own place in the
  `vector_size` element ladder, and `_Float16 _Complex` beside it as clang's
  extension (`C_TYPE_FLOAT16_COMPLEX`, two contiguous halves). The C23
  `f16`/`F16` constant suffix and the `__FLT16_*__` prelude macros carry it,
  and every constant rounds through one encoder pair,
  `c_ir_float16_bits_from_f64` / `c_ir_float16_to_f64` in `c_gen.c`: no host
  half type is used, because the compiler builds under four C compilers and
  cross-compiles. Every static-initializer writer routes its 16-bit case
  through that pair; the byte strings in `c_test_float16_type` were taken
  from clang 18 compiling the same spellings.

  **Native code generation implements the binary16 runtime vocabulary.**
  Scalar arguments and results use the ABI's real floating position: the low
  sixteen bits of an XMM register on System V and Win64 x86-64, and the H/V
  register position on AArch64. The direct emitter and the MIR value-shape
  tables agree on that classification, so `none`, `mir-stack`, `fast` and
  `quality` compile the same signatures without machine fallback. Baseline
  targets need no F16C or AVX512-FP16 feature. On x86-64, lowering widens each half through
  `__extendhfsf2`, performs arithmetic in binary32, and rounds immediately back
  through `__truncsfhf2`; a binary64 source uses `__truncdfhf2`. Darwin x86-64's
  compiler-runtime entry points carry the half bits in the integer ABI even
  though ordinary `_Float16` still uses XMM, so lowering bridges those symbols
  through an internal `unsigned short` view. AArch64 uses baseline scalar `FCVT`
  instructions for the same half/wider conversions, without runtime imports.
  Apple AArch64 packs named stack-only scalar arguments at their natural
  alignment; unnamed variadic arguments retain eightbyte-aligned slots;
  the canonical and MIR call layouts share that rule. Comparisons and truth
  conversion widen exactly, negation flips the binary16 sign bit without
  quieting a NaN, and vector arithmetic/comparisons scalarize through the same
  per-lane operations. Hosted x86-64 links therefore need the compiler-runtime/libgcc
  builtins, just as complex multiply/divide links do. The Wasm, eBPF and LLVM
  bitcode backends retain their own structured ABI/instruction checks; this is
  a native x86-64/AArch64 contract, not a claim that those formats gained a
  binary16 ABI.
- **`__bf16` has a distinct bfloat16 representation.** Its own
  `C_TYPE_BFLOAT16`, `TargetDataLayout.bfloat16_type` (two naturally aligned
  bytes), and `IrType.float_format` discriminating `IR_FLOAT_FORMAT_BFLOAT16`
  from `IR_FLOAT_FORMAT_IEEE` at the same 16-bit width; an equal-width
  identity conversion between the two formats is invalid. Scalar constants
  round once from the binary64 carrier through
  `c_ir_bfloat16_bits_from_f64`, and integer-to-bfloat16 conversion uses
  precision 8. No backend implements bfloat16 runtime operations, so a
  `__bf16` value needed at run time is still refused by the structured codegen
  diagnostic; unlike binary16, it has no compiler-runtime lowering. Mixed
  `_Float16`/`__bf16` arithmetic selects
  `_Float16`: bfloat16 carries the lower conversion rank
  (`c_ir_float_conversion_rank` ranks it below binary16 despite equal storage
  width), so the usual arithmetic conversions convert `__bf16` to `_Float16`
  numerically, never as an identity. This can reduce exponent range.

  Wide-source constants retain their target x87/binary128 payload in the two
  integer limbs of `CIrConstantValue`; `c_ir_constant_float_literal` rounds a
  literal in its source format first, and `c_ir_constant_wide_float_cast`
  rounds directly to the destination through the existing rational machinery.
  Neither casts nor arithmetic/comparison/truth/integer consumers may read a
  wide value through the binary64 carrier. Scalar, array, aggregate and local
  static BF16 initializers use this path. An explicit cast to `double` still
  deliberately rounds to binary64. The binary128 rational converter uses a
  two-limb quotient, not a host extended type or a new numeric dependency.
  This does not add native BF16 arithmetic/ABI or binary128 arithmetic support.

  `c_parse_bfloat16_builtin` carries the LLVM18 BF16/AVX-NE-CONVERT signatures
  and the select/FMA dependencies used by the pristine resource headers.
  Identifier binding and type queries record their uses in a sparse worklist;
  `c_parse_validate_bfloat16_builtin_calls` checks arity and operand types
  before unused definitions are omitted. Nested, unreachable and unevaluated
  calls are still checked. Same-sized GNU arithmetic vectors are convertible
  by value; pointer pointees retain format/size identity, with ordinary C
  null/void-pointer conversions. This signature checking is not a claim of
  native intrinsic lowering. `c_test_bfloat16_semantic_acceptance` covers
  source-format rounding on six layouts in both frontend forms, mixed-format
  identity, positive/negative builtin operands, and deep nested calls.
- **Base AAPCS64 `long double` is IEEE binary128 and supports scalar
  transport.** A scalar argument or result is one complete sixteen-byte image
  in a Q register; after V0-V7 are exhausted, named arguments occupy their
  sixteen-byte-aligned stack slot. Canonical and MIR backends keep the value
  slot-backed internally and bridge only at the ABI edges, so assignment,
  literal return, direct/indirect calls and mixed Clang/Buster linkage preserve
  every payload bit, including negative zero. `c_ir_signature_type_supported`
  admits only the exact scalar shape proven by `ir_type_abi_value`; aggregates,
  variadic wide parameters, arithmetic, comparisons, truth conversion and
  general conversions remain behind their existing structured rejections.
  `compiler_driver_test_aarch64_binary128_transport` covers Q0/Q1, ninth-argument stack
  spill and both mixed-compiler directions on native Linux AArch64, with strict
  no-fallback compilation across the AAPCS64 target/mode/frontend/PIC matrix.
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
  `codegen.c`. The machine selector also lowers i128 casts to and from f80
  through two frame limbs and closed x87 transactions. Its final addition
  selects 24-, 53-, or 64-bit precision for one row, preserving the caller's
  complete control word; f80-to-i128 extracts high and low unsigned limbs
  at 64-bit precision before restoring a signed result.
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
  every other target. The assembler's AArch64 vocabulary contains the
  bootstrap control-flow set (`bl`, `brk`, `ret` and `nop`) and the checked
  baseline `mrs`/`msr` system-register rows for `fpsr` and `fpcr`, but not an
  ADRP/ADD page pair. A block that fails reports through
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
