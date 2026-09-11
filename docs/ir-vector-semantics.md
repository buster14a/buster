# Canonical vector semantics and predicates

`ir_vector_operation_semantics` classifies dedicated vector operations.
`ir_simd_operation_shape` owns exact-intrinsic arity and the integer/predicate
boundaries. The frontend result type and canonical validator consume that
metadata. `ir_simd_operation_supported` is the shared target gate for both
native x86 emitters. These contracts concern program semantics, not a promise
that every backend currently implements every generic operation.

## Generic operations

Every `IR_UNARY_VECTOR_*` and `IR_BINARY_VECTOR_*` operation below has class
`IR_VECTOR_SEMANTICS_GENERIC`. Each lane has the corresponding scalar
operation's semantics. The vector type fixes lane width, count and signedness;
float comparisons produce integer lanes of the same width.

| Canonical family | Operations (enum suffixes) |
|---|---|
| `IR_UNARY_VECTOR_` | `INTEGER_NEGATE`, `FLOAT_NEGATE`, `INTEGER_BITWISE_NOT` |
| `IR_BINARY_VECTOR_`, integer arithmetic | `INTEGER_ADD`, `INTEGER_SUBTRACT`, `INTEGER_MULTIPLY`, `SIGNED_DIVIDE`, `UNSIGNED_DIVIDE`, `SIGNED_REMAINDER`, `UNSIGNED_REMAINDER` |
| `IR_BINARY_VECTOR_`, floating arithmetic | `FLOAT_ADD`, `FLOAT_SUBTRACT`, `FLOAT_MULTIPLY`, `FLOAT_DIVIDE` |
| `IR_BINARY_VECTOR_`, shifts | `SHIFT_LEFT`, `SIGNED_SHIFT_RIGHT`, `UNSIGNED_SHIFT_RIGHT` |
| `IR_BINARY_VECTOR_`, bitwise | `INTEGER_BITWISE_AND`, `INTEGER_BITWISE_OR`, `INTEGER_BITWISE_XOR` |
| `IR_BINARY_VECTOR_`, integer comparison | `INTEGER_EQUAL`, `INTEGER_NOT_EQUAL`, `SIGNED_LESS`, `SIGNED_LESS_EQUAL`, `SIGNED_GREATER`, `SIGNED_GREATER_EQUAL`, `UNSIGNED_LESS`, `UNSIGNED_LESS_EQUAL`, `UNSIGNED_GREATER`, `UNSIGNED_GREATER_EQUAL` |
| `IR_BINARY_VECTOR_`, floating comparison | `FLOAT_EQUAL`, `FLOAT_NOT_EQUAL`, `FLOAT_LESS`, `FLOAT_LESS_EQUAL`, `FLOAT_GREATER`, `FLOAT_GREATER_EQUAL` |

A generic comparison produces **all ones or zero in each integer lane**. It
does not produce a packed scalar bit mask or an internal predicate. Negating
a floating vector preserves the scalar sign-bit operation, including signed
zero. No reassociation, changed rounding, NaN comparison behavior or extra
observable memory access is licensed by the generic class.

Legalization may split vectors, widen or narrow intermediate representations,
or expand into scalar lanes, provided every result and observable effect is
preserved. Width changes must truncate/extend at the original lane boundaries;
extra lanes cannot introduce faults or accesses. Unsupported defined operations
must fail explicitly. Generic status is permission to legalize correctly, not
permission to silently omit work.

Ordinary type-driven `ARGUMENT`, `LOCAL`, `LOAD`, `STORE`, `ARRAY`, `INDEX`,
`CALL`, `CAST` and `RETURN` rows, plus block parameters/edge arguments, can
transport vector values. These are structural operations under the ordinary
canonical type, memory and ABI rules; they do not request an exact ISA form.
Their vector representation can be split or stored in a frame while preserving
those rules. The classification API returns `NONE` for such rows because they
are not dedicated vector operations. A cast still needs an independently legal
canonical conversion; vector type alone does not authorize reinterpretation.

## Exact x86-512 intrinsics

Every `IR_SIMD_*` row is `IR_VECTOR_SEMANTICS_EXACT_X86_512`. The complete
inventory follows; operand positions start at zero. A mask is a C-visible
64-bit integer bit pattern even when only sixteen bits participate.

| `IR_SIMD_` suffix | Semantics | Predicate boundary |
|---|---|---|
| `LOAD` | Read exactly 64 unaligned bytes | None |
| `LOAD_MASKED` | Read active bytes; zero inactive bytes without accessing them | Operand 1, `predicate<64>` |
| `STORE` | Write exactly 64 unaligned bytes | None |
| `STORE_MASKED` | Write active bytes only | Operand 1, `predicate<64>` |
| `SPLAT_BYTE` | Repeat one byte in 64 lanes | None |
| `COMPARE_EQUAL_BYTE` | Pack byte equality results into bits | Result, `predicate<64>` |
| `COMPARE_LESS_BYTE` | Pack unsigned byte less-than results | Result, `predicate<64>` |
| `SIGN_MASK_BYTE` | Pack each byte's high bit | Result, `predicate<64>` |
| `TEST_MASK_BYTE` | Pack byte-wise AND-nonzero results | Result, `predicate<64>` |
| `PERMUTE2_BYTE` | Select from concatenated low/high vectors using low seven index bits; zero inactive lanes | Operand 0, `predicate<64>` |
| `COMPRESS_BYTE` | Pack active bytes in original order; zero the tail | Operand 0, `predicate<64>` |
| `COMPRESS_STORE_BYTE` | Write only packed active bytes in original order | Operand 1, `predicate<64>` |
| `WIDEN_BYTE_TO_WORD` | Zero-extend one selected 16-byte quarter into sixteen u32 lanes; immediate 0–3 | None |
| `SHIFT_LEFT_WORD` | Shift each u32 lane left; immediate 0–31 | None |
| `TERNARY_WORD` | Apply the eight-bit three-input truth table independently to each bit | None |
| `COMPARE_EQUAL_WORD` | Pack sixteen u32 equality results; clear upper 48 bits | Result, `predicate<16>` |
| `SPLAT_WORD` | Repeat one u32 in sixteen lanes | None |
| `COMPARE_LESS_WORD` | Pack sixteen unsigned u32 less-than results; clear upper 48 bits | Result, `predicate<16>` |
| `COMPRESS_WORD` | Pack selected u32 lanes in original order; zero tail; ignore mask bits 16–63 | Operand 0, `predicate<16>` |

The current exact lowering requires x86-64 AVX512F and AVX512BW for the whole
vocabulary, including its 64-byte frame transfers. `PERMUTE2_BYTE` additionally
requires AVX512VBMI; `COMPRESS_BYTE` and `COMPRESS_STORE_BYTE` additionally
require AVX512VBMI2. `COMPRESS_WORD` does not require VBMI2. Unknown operation
numbers always fail the shared gate. Feature bits on another architecture
cannot make an x86 intrinsic supported.

An exact intrinsic must lower through its specified target form. Supporting
loads, stores and integer/predicate bridges may surround that form. A backend
cannot silently replace the intrinsic with generic arithmetic or a scalar
implementation. Missing ISA features or an unimplemented exact lowering yield
a structured failed result. `<buster/lib/simd.h>` chooses its deliberate scalar
fallback **before canonical IR construction**, through target/compiler guards;
that fallback is ordinary C and may lower to generic canonical operations.
The fallback is part of the header contract, not an implicit backend rewrite.

## Internal predicate<N> contract

`predicate<N>` denotes an internal lane-activity value with exactly one Boolean
per lane, for `1 <= N <= 64`. It is distinct from a C integer, from a C
all-ones/zero comparison vector, and from a physical register number. The
current canonical vocabulary keeps C masks as integers and specifies internal
predicate creation/consumption through `IrSimdShape.predicate_operand_mask`,
`predicate_result` and `predicate_lane_count`; it does not expose a new C type
or publish a canonical `IR_TYPE_PREDICATE` that existing consumers cannot lower.

The lowering boundaries are explicit:

- **Integer to predicate<N>:** lane `i` takes integer bit `i`; bits `N` and above
  are ignored. This is truncation of a bitset, never a scalar nonzero test.
- **Predicate<N> to integer:** lane `i` becomes bit `i`; all bits above `N` are
  zero. The current intrinsic result is an unsigned 64-bit integer. Any later
  C conversion to `__mmask8`, `__mmask16`, `__mmask32` or `__mmask64` obeys the
  ordinary integer conversion rules.
- **Predicate to C comparison vector:** an explicit expansion must produce
  all-ones/zero lanes at the destination width. A packed bitset cannot be
  substituted for that vector. No such conversion is implicit in this IR.
- **Integer uses:** arithmetic, population count, shifts, storage, calls,
  returns, debug-visible values and other C uses retain their integer
  semantics. A backend retaining a mask in predicate registers must bridge
  back at these boundaries. Predicate widths may change only through an
  explicit lane-preserving conversion; high bits cannot become stale lanes.

The direct x86 emitter uses `KMOVQ` at its frame/integer boundaries and fixed
scratch `k1` for predicates. MIR may preserve these values in the separate
mask register class; allocatable k1–k7 lifetime management is the work of
[#37](https://github.com/buster14a/buster/issues/37). C masks and their ABI remain
integers under either allocation strategy. `k0` is not an active writemask.

## Backend obligations

| Backend | Generic vector values | Exact x86-512 operations | Internal predicates |
|---|---|---|---|
| x86-64 native | Native forms where supported, otherwise correct lane/frame expansion | Shared feature gate, exact forms, structured refusal on missing support | Hardware k bank; direct emitter uses k1/frame bridges; mask allocation is separate from C integer allocation |
| AArch64 native | NEON where supported, otherwise scalar MIR or direct scalar lanes | Refused, including when stray x86 feature bits are present | No SVE predicate lowering is currently implemented; future predicates can use explicit per-lane Boolean vectors or normalized packed integer bits, never masquerade as NEON C comparison results |
| Wasm64 | Current backend rejects unsupported vector values/operations explicitly | Refused | No predicate bank; future lowering must use explicit lane Booleans or normalized low-N-bit integer representation |
| eBPF | Current backend rejects unsupported vector values/operations explicitly | Refused | No predicate bank; any future supported lowering uses normalized packed integer bits or explicit scalar lane Booleans |
| LLVM bitcode | Emit supported LLVM vector operations; LLVM performs subsequent target legalization | Refused until exact intrinsic emission exists, even with an x86 triple | Future predicate lowering uses `<N x i1>`; integer bridges require explicit bit packing/unpacking and zero high bits |

A specified future representation is not a claim of current support. Current
Wasm64/eBPF failures may name an unsupported vector type before reaching the
exact opcode; they must still return no artifact. LLVM exact failures name
`IR_OPCODE_SIMD`. Native exact failures identify the opcode and explain that
an explicit source fallback is required. A change adding backend support must
update this table and its negative tests together.

## Coverage

`vector_contract_tests` constructs all nineteen exact operations without
preprocessor guards, verifies that each survives as one canonical SIMD row,
checks its integer mask boundary, and exercises x86-64 (full, baseline and F/BW-only),
AArch64, Wasm64 and eBPF. Native paths run all four allocator modes. LLVM exact
refusal and a generic vector arithmetic/comparison control are covered.
All sixteen combinations of F/BW/VBMI/VBMI2 are checked for every operation
on every architecture. Invalid enum values must remain unsupported.

The existing `basic_c_simd.c` execution fixture covers exact lane behavior,
including zeroed upper word-comparison bits and ignored high word-mask bits.
`basic_c_simd_translate.c` exercises the production header's explicit fallback
instead of compiling the test body away. The native vector fixtures cover
all-ones comparison lanes and scalar legalization independently of exact SIMD.
