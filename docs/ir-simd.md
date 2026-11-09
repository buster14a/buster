# Vector and mask IR slice

Issue [#732](https://github.com/buster14a/buster/issues/732) supplies vector and
mask values to shared IR. The current implementation includes shared types,
scalar/vector verification, canonical text, explicit consumer refusals, a
bounded independent execution oracle, the bounded VECTOR/X86 typed-function
adapter and sparse vector execution in the shared interpreter.

The disjoint typed-operation decoder reads #717's real `Tier0Operation` rows.
It recognizes exact EVEX i32 arithmetic/bitwise forms and compare-mask plus
mask-to-0/-1 expansion at all three widths, retaining operand classes and byte
widths. The forms' output-target requirements are explicit: AVX512F, AVX512VL for
the 128- and 256-bit forms, and AVX512DQ for compare expansion (`VPMOVM2D`). Lane arithmetic/comparisons on floating or non-i32 elements,
unmodeled predicates, changed register roles, addresses and predicated record decorations refuse before
publishing a semantic action. Tests use the actual producer to make rows;
baseline and native compiler-host modes must make identical admission decisions.
Bitwise AND/OR/XOR/complement rows carry no element type in `Tier0Operation`:
the producer uses identical records for i8/i16/i32/i64 elements at equal total
byte widths. These representation-independent operations normalize to the i32
bit container without inferring narrow/wide lane arithmetic or signedness.
`ir_lower_typed_target` and its scalar twin compose this decoder with #131's
whole-function adapter. Their explicit `target_features` argument must be the
producer's selected output target, including per-function target attributes.
The old entry points grant no features and continue to refuse SIMD. Constants,
128/256/512-bit vector loads/stores, object locals, DUP/DISCARD and values crossing
branches preserve their exact byte widths. Temporary edge slots accommodate the
widest value in the function; object accesses use the object's actual alignment,
not a stronger alignment inferred from its width. All count/fill decisions use
the same replay. Unsupported operations, widths and output targets preserve the
caller's result and roll back the output/scratch arenas.

Only the decoded i32 arithmetic/bitwise/comparison subset is admitted. C comparisons
become `vcmp` followed by `vselect` of all-ones and zero vectors, preserving C's
0/-1 lane representation rather than returning a compact mask. Scalar, aggregate,
variadic and indirect-control-flow lowering retains #131's existing behavior.
This is semantic producer integration, not a native instruction selector.

The chosen initial vector shapes are `v4i32`, `v8i32` and `v16i32`: 128, 256 and
512 bits containing 32-bit integer lanes. Signedness belongs to comparison
predicates. Other integer element widths and floating lanes are unsupported
until they have explicit operations and tests. Masks are distinct `m8`, `m16`,
`m32` and `m64` values, independent of scalar integers and of an x86 register.
There is no special k0 value or implicit all-active mask.

The model supports wrapping lane addition/subtraction/multiplication, bitwise
logic, ten signed/unsigned integer comparison predicates, and lane selection.
Comparison returns m8 for v4i32/v8i32 and m16 for v16i32, clearing unused high
bits. Selection uses the low mask bit for each lane; its explicit false operand
defines merging. A zero false operand defines zeroing. Unused high m8 bits do
not affect v4i32 selection. Wrong mask widths and mismatched vector shapes fail.
Mask logic is limited to bitwise operations, never integer arithmetic; a mask's
arithmetic goes through `bitcast` to and from the integer of its bits, which the
verifier admits for `mN` and `iN` of the same bits, both ways, and a mask to
itself, but for no other pair with a mask.

Values are canonical little-endian bit patterns, with every bit beyond the
shape zero. The SIMD oracles and sparse shared-interpreter payloads use
`IrSimdValue`'s eight words. Ordinary IR constants and `InterpreterValue` retain
their two-word layouts.
The shared type IDs retain every old scalar ID and fit in the existing byte
column. The 18 admitted types use checked 32-bit result admission sets in place
of the old 16-bit sets. Compile-time assertions enforce type/opcode limits.
Only vector constant rows allocate 64-byte sparse payloads; scalar functions
allocate no wide payload storage.

The bounded memory oracle loads/stores the exact shape width at an explicit
offset into a caller-supplied slice. Reads, writes and ordered volatile-byte
accesses have explicit counters. Bounds and counter overflow are checked before
an access; failure preserves output and memory. No masked memory, atomic or
unordered operation is inferred from these load/store actions. Memory and value
views are disjoint; input values may alias the result. The action model
allocates no memory.

`ir_simd_execute.c` executes the shared IR subset directly after complete SSA
validation. It supports one straight-line block with vector/mask parameters,
constants, arithmetic, comparison, selection, copies, returns and bounded
loads/stores. Pointer arguments and constants are logical slice offsets;
promised alignment applies to these offsets. Its private arena frame stores
wide values only for SIMD results. Calls, arbitrary pointer arithmetic, control
flow, undef and other unimplemented forms refuse before execution. Allocation
or admission failure performs no access; a dynamic access fault retains earlier
completed effects. Input/function/memory views are disjoint from the unused
scratch remainder; no borrowed scratch view escapes.

This abstract model can run on a baseline host. It does not authorize native
AVX-512 instructions in a baseline output target. The typed adapter validates
#717's X86 forms and target features independently of the compiler host.

The shared interpreter now executes these vector operations alongside its scalar
CFG, dynamic stack and checked-memory machinery. Only vector-producing
instructions receive 64-byte payloads and a sparse index; scalar-only functions
allocate neither. Loop phis snapshot all incoming wide values before any write.
Loads/stores, including volatile accesses, touch exactly the value width.
`vselect` ignores mask bits beyond its lane count and does not consume a wholly
unselected undefined operand; any selected undefined operand still diagnoses.
Copy, scalar select and 128-bit scalar/vector bitcasts preserve their definedness
and bit representations. Masks stay width-bounded logical bit patterns, not
native opmask registers. Step/value limits and scratch rollback remain active.

First-class vector entry arguments/results and vector/mask call signatures still
refuse before executing a frame: the 16-byte scalar argument/result API and native
bridge have no such ABI contract. Existing aggregate-by-pointer machinery is not
replaced. A slot of a vector type, read and written whole at its type, promotes
as a scalar's does. SCCP tracks sparse wide constants and folds supported vector
operations without treating a vector as an `i128`. It folds floating lanes only
where every floating environment gives the same bits, and keeps their original
rows for their flags and traps ahead of the constant their users receive. GVN and LICM admit SIMD with lane
metadata and full constant payloads in their keys, excluding floating effects;
LICM also retains memory operations and undefined operands. SROA requires its
`simd` option, and the inliner clones SIMD callees. See
[IR optimization](commands/ir-optimization.md) for the exact boundaries. Native Tier 1 selection takes a
function's vectors ([#141](https://github.com/buster14a/buster/issues/141), [the
selection guide](tier1-selection.md)'s "Vectors"), and C's vectors passed by
value, which are aggregates in a vector register whole, and takes masks:
[#179](https://github.com/buster14a/buster/issues/179) selects their operations
and allocates them in the opmask class ([the selection
guide](tier1-selection.md)'s "AVX-512's masks").

The original model tests compare integer arithmetic with Clang C vector
operators, selection and comparisons with independent lane expectations, and
memory effects with guarded bounded slices. `test-ir` exercises actual IR
execution, malformed types/predicates/SSA/effects and sparse storage;
`test-ir-parse` roundtrips the hand-written vector/mask fixture;
`test-ir-simd` checks typed admission and consumer behavior in both verification
modes. These tests alone establish no code-quality or throughput improvement.

## Integration fixtures and consumer handoff

`test-ir-lowering` exercises 156 actual #717 producer cases: three widths, signed
and unsigned comparisons, thirteen operations and both conditional arms. Both
adapter paths must agree column-for-column, including sparse constants. Shared
interpretation is checked against independent ordinary-C lane formulas and exact
memory canaries; the same typed programs pass through both Tier 0 compilers and
encoders, and run natively only when the machine has the required features.
Unavailable native execution is reported, not counted as passed execution.

The scalar-splat integration adds 21 producer cases: six scalar lane types at
three widths and three word broadcasts with distinct high bits in a 32-bit
carrier. Both adapters must emit the same single semantic splat, no machine row
or row call, and an explicit truncation where the lane is narrower than its
carrier. Shared interpretation and gated native Tier 0 execution compare exact
bytes and surrounding canaries against an independent byte-repetition formula.

`test-ir-interpreter` additionally checks simultaneous vector phi cycles, logical
masks through 64 bits, unused high mask bits, undefined selection alternatives,
128-bit bitcasts, unaligned volatile memory, short/read-only memory refusal and
step/value budgets. Its splat fixtures cover all six scalar types at all three
widths with integer boundaries, both zero signs, denormals and signaling/quiet
NaN encodings; undefined scalar inputs must refuse before a following store.
The original straight-line executor remains an independent
consumer; the shared interpreter is not routed through it.

`test-ir-source --simd -march=x86-64-v4 tests/ir/vector-i32.c tests/ir/mask-builtins.c tests/ir/bmi-builtins.c tests/ir/vector-rows.c tests/ir/carrier-rows.c`
drives real C through the checker, typed producer, both adapters and both text
parsers. This explicit mode also requires a function with vectors to promote by
both SSA promoters (#141), counting them, as a function whose only SIMD values
are masks must (#179): `mask-builtins.c`'s four functions of the `_k*` builtins
at every width, one of them a loop. `bmi-builtins.c`'s four functions lower the
scalar BMI builtins of both widths, one of them in a loop, and an elementwise
deposit of a vector to the IR's `intrinsic` (#141). `vector-rows.c`'s seven functions lower
vector operators on bytes, words and quadwords and vector builtins to row calls (#141's
machine rows): a masked store of no result, a shift by a count from a general register,
a permute and a loop whose row results are phis; and a compare into a mask to the IR's
own `vcmp` (#1623, "Comparisons into a mask" below). `carrier-rows.c`'s five
functions lower #1280's SSE carriers, vectors of 4 and 8 bytes, as `f32` and `f64` values in and out of row calls:
narrowing shuffles, widening conversions, loads, stores, constants, operators and a loop whose carriers are phis
([the IR guide](ir.md)'s "SSE carriers"). `test-ir-simd` runs a function of each
vector width through a vector slot, an addition, compares and a selection,
lowered, promoted and optimized by both paths, on the shared interpreter. Without `--simd`, source-corpus acceptance continues to
require promotion. Source preparation retains each function's selected
output-target features rather than inferring them from the host.

For #141, the handoff is verified shape-qualified IR with explicit `vcmp`/`vselect`
and wide memory transports, which Tier 1 selects at the output target's encoding
level.
For #179, masks have explicit `m8`/`m16`/`m32`/`m64` types and exact-bit semantics,
which Tier 1 keeps in the opmask registers (k1 to k7, k0 the class's scratch), moved
and spilled at their widths; no ABI passes one. This change makes no native Tier 1
quality or throughput claim.

## Masks from the typed stream (#179)

`ir_lower_typed_target` and its scalar twin take Tier 0's mask class (`MASK`, its
stack codes `BUSTER_TIER0_STACK_MASKS` to `+ 3`) as `m8` to `m64`: a mask's
loads, stores, frame objects, `DUP`, `DISCARD`, `?:`'s joins and logic (`AND`,
`OR`, `XOR`, `NOT`) are the IR's at its width, aligned to its bytes. A `CONVERT`
between a mask and an integer class is Tier 0's (`ir_typed_convert_mask`): to a
mask, the integer truncated, or extended by its signedness, to the mask's bits
and their `bitcast`; from one, the `bitcast` to an integer of its bits and that
unsigned integer's extension or truncation. A mask entry needs AVX512F, DQ for
`m8` and BW for `m32` and `m64` (Tier 1's `kmov` forms of those widths), refused
as `IR_SIMD_TYPED_TARGET` otherwise.

An `X86` row of masks is decoded by `ir_simd_typed_row`:

- `ir_simd_typed_mask_row`, a mask result's: records of `kand`, `kor`, `kxor`,
  `kxnor`, `kandn`, `kadd`, `kunpck`, `knot`, `kshiftl`, `kshiftr` at the masks'
  width, or `kmov` between registers at it or wider, each register-direct with
  its fields its roles alone (`MASK(j)`, `MASK_RESULT`, `MASK_SCRATCH(k)`) and no
  decoration, every register it reads written before. Its steps run over the
  operands' registers, its result the first operand's (`MASK_RESULT`'s place,
  `tier0_operation_mask_plan`), or `MASK_RESULT`'s in a row of no operand.
- `ir_simd_typed_mask_test`, an int's of two masks: `ktest` or `kortest` of
  `MASK(0)` and `MASK(1)`, then `sete` or `setb` into `SCRATCH(0)` and its zero
  extension into `INTEGER_RESULT`, its one step the flag's int.

These are the front end's `_k*` builtins' rows, as `check_x86_lower_masks` moves
them into the class: every rule of `KBINARY`, `KSHIFT`, `KUNARY`, `KMOV` and
`KTEST`. A row's features are its forms' (AVX512F; DQ for the byte forms,
`kaddw`, `ktestb`, `ktestw` and `kortestb`; BW for the doubleword and quadword
ones). Any other row with masks refuses as unsupported: a vector compare into a
mask, a masked move or a select (vectors, #141's), a row that names opmask
registers by number, or one that moves between a mask and a general register
inside the row. `ir_typed_mask_row` replays the steps: `and`, `or`, `xor` and
`not` on the masks, `xnor` and `andn` as their compositions, a move as the
value; `kadd`, the shifts, `kunpck` and the tests through the integer of the
mask's bits (zero-extended to `i32` for `m8` and `m16`, `i64` for `m64`), a
shift by the mask's bits or more the zero mask, and a test's flag an `icmp eq`
with 0 of the operation whose zero sets it (`ktest`'s `a & b` or `~a & b`,
`kortest`'s `a | b` or its complement), zero-extended to `i32`.

## Scalar x86 builtins (#141)

An `X86` row of an `unsigned int` or `unsigned long`, of two operands of its
class, is decoded by `ir_simd_typed_intrinsic`: one register-direct record of
BMI2's `pdep`, `pext` or `bzhi` or BMI1's `bextr` at the class's width, its
`reg` `INTEGER_RESULT` and its `vvvv` and `rm` `INTEGER(0)` and `INTEGER(1)` in
either order, refused as `IR_SIMD_TYPED_TARGET` without its feature. These are
the front end's `GENERAL` rows (`__builtin_ia32_pdep_si` and `_di`,
`_pext_*`, `_bzhi_*`, `__builtin_ia32_bextr_u32` and `_u64`, whose `swap`
places `bzhi`'s and `bextr`'s source in `rm`) and the elementwise deposits' and
extractions' (`__builtin_elementwise_pdep` and `_pext`,
`check_lower_vector_deposit`, one row per element). `ir_typed_intrinsic` makes
the IR's `intrinsic` of the operands the record places: its `a` the source
(`vvvv`'s for `pdep` and `pext`, `rm`'s for `bzhi` and `bextr`), its `b` the
mask, index or control. Tier 1 selects each as its one VEX instruction
(`TIER1_RULE_INTRINSIC`), whose three registers are its own; its emission's
target allow-list refuses one for a target without the feature.

Before them, `ir_simd_typed_bits` decodes the bit builtins' rows (#1374,
`check_lower_bits_row`): one operand of an integer class of 32 or 64 bits, and
one record of `tzcnt`, `bsf`, `lzcnt`, `bsr` or `popcnt` of the operand's width,
its `reg` `INTEGER_RESULT` and its `rm` `INTEGER(0)`, into an integer of 32 bits
or of the operand's width; or a move of the operand into `INTEGER_RESULT` and
`bswap` of it there, of the operand's class. Each is the IR's unary `intrinsic`
of its name, of the operand's type (a 64-bit count truncated to an `i32`
result), refused as `IR_SIMD_TYPED_TARGET` without its form's feature (BMI,
LZCNT, POPCNT). `test-ir-simd` decodes each written out, of each class and
result, checks its features against `x86_target_requirement`'s, and refuses ten
changes of each as anything but at most a machine row.

After them, `ir_simd_typed_overflow` decodes the signed overflow builtins' rows
(#1385, `check_lower_overflow_row`): two operands of an int's or a long's class,
an int result, and three records, `add`, `cmp` or `imul` of the class's width
with `reg` `INTEGER(0)` and `rm` `INTEGER(1)`, `seto` of `SCRATCH(0)` and
`movzx` of it into `INTEGER_RESULT`. Each is the IR's binary `intrinsic`
`saddo`, `ssubo` or `smulo` of the operands' type (a long's truncated to the
`i32` result), at every target. `test-ir-simd` decodes each of both widths
written out and refuses thirteen changes of each as anything but at most a
machine row.

## Scalar splats (#1450)

The opcode table already fills its 64-entry byte-permute register. A scalar
broadcast therefore uses a unary kind of `intrinsic`, retaining every opcode
number and the existing instruction columns:

```text
%7 = intrinsic.v8i32 %3 splat
```

`c` is `BUSTER_IR_INTRINSIC_SPLAT`, `a` is a scalar of type `i8`, `i16`, `i32`,
`i64`, `f32` or `f64`, and `b` is absent (`BUSTER_IR_NONE`). The result is a
16-, 32- or 64-byte vector container. The scalar's type gives the lane width;
the instruction's flags remain zero. Each lane receives exactly the scalar's
bits. It performs no floating arithmetic, leaves NaN signaling bits and
payloads untouched, preserves signed zero, and consumes an undefined source.
Existing scalar intrinsic kinds keep their two operands and scalar result
rules. The operand table makes `b` optional and the semantic verifier enforces
which kind needs it; use enumeration, remapping and text round trips retain
that distinction.

`ir_simd_typed_splat` admits the producer's exact single-record EVEX broadcasts:
`vpbroadcastb/w/d/q` from a general register, `vbroadcastss` from an XMM register,
and a double's `vpbroadcastq` or `vbroadcastsd` from XMM. It checks the result
width, scalar class, role registers and every record field. Decorations, memory
forms, fixed registers and extra records do not silently become a pure splat.
The target needs the form's features and the vector transport features; a
missing feature refuses before publication. Byte and word inputs have already
been extended to a 32-bit register by the producer, and half-precision bits use
the same word form. Lowering explicitly truncates a wider integer carrier
before the splat. Baseline SSE/VEX producer sequences remain machine rows in
this bounded decoder.

The shared interpreter repeats bits into its sparse vector payload. SCCP can
fold constant splats, and GVN/LICM can reuse or move a pure splat where their
ordinary dominance and definedness rules permit. Tier 1 selects a dynamic
splat through its vector rule: EVEX broadcasts directly; SSE/VEX targets use
bit moves, low unpacks and shuffles, with a 128-bit insert for a 32-byte result.
No expansion uses a temporary stack object or floating arithmetic.

`tests/ir/splats.ir` round-trips all 18 scalar-type/vector-width combinations.
`test-ir` checks type admission, one-operand use enumeration and malformed
neighbors in both verifier paths. `test-ir-simd` checks actual producer rows,
exact feature requirements and mutations of their roles and effects.
`test-ir-lowering` and `test-ir-interpreter` check execution as described above.
The existing `tier1-lanes` source fixture now requires zero machine rows where
its 14 remaining rows were scalar splats, and compares both Tier 1 paths with
Clang. Measured by `--tier1 --tier1-report -march=x86-64-v4` on that unit, main
`ec5f0f6b` has 14 rows by each path and this change none.

The compiler's own units have no broadcast rows at `-march=znver5`, so the
splats change none of their counts: `main_unit_frontend.c` keeps its 1,673 rows
either way and `main_unit_backend.c` its 2,432. The frontend unit's rows are:

| Rows | Kind |
| ---: | --- |
| 459 | a comparison into a mask alone (an intrinsic's `vpcmp`, without the expansion the decoder takes) |
| 450 | masked or element-typed moves |
| 404 | permutes and shuffles |
| 87 | conversions and mask moves |
| 65 | AES rounds |
| 46 | other integer operations (minimum, maximum, saturation, products' high halves) |
| 32 | shifts by a scalar, rotates |
| 10 | ternary logic, tests, blends |
| 114 | others |

A comparison into a mask alone is the IR's own `vcmp`, which #1623 decodes
(below). Shuffles and conversions need new operations, which the full opcode
table can only take as `intrinsic` kinds, as the splat does.

## Comparisons into a mask (#1623)

An intrinsic's comparison into a mask (`_mm512_cmpeq_epi8_mask`, its masked
`_mm512_mask_` form and the rest of `vpcmp`, `vpcmpu`, `vcmpps` and `vcmppd` at
each width) is one row in the mask class: two vectors and a writemask in, the
mask out, the record's immediate its predicate. `ir_simd_typed_compare_mask`
decodes it as the IR's `vcmp` of its lanes where the immediate is an IR
predicate: vpcmp's `==`, `<`, `<=`, `!=`, `>=` and `>` (signed, or unsigned for
vpcmpu), and a float's `EQ_OQ`, `LT_OS`, `LE_OS`, `NEQ_UQ`, `GE_OS` and `GT_OS`,
the immediates Tier 1 selects for C's operators, so that the instruction it
selects is the one the row named, its exceptions too. The front end gives
clang's `GT_OS` and `GE_OS` as `LT_OS` and `LE_OS` of the operands swapped
(`check_x86_lower_float_predicate`, as LLVM selects them); the decoder takes
either order, the other as the reversed predicate (`a < b` as `b > a`, exact for
every lane and NaN).

The mask is the `vcmp`'s ANDed with the writemask, but where the typed stream
gives the writemask as a constant of every lane converted to the mask just
before the row (`ir_typed_unmasked`), as clang's intrinsics without a mask do;
both lowering passes read that from the typed stream, so they decide alike. The
features are those of the forms Tier 1 selects: the comparison's own, AVX512F,
VL below 64 bytes, DQ for a mask of 8 lanes or fewer (`kmovb`, `kandb`) and BW
for 32 or 64. Any other immediate (vpcmp's false and true, a float's other 26),
a scalar's or FP16's compare, SAE's form, a memory operand or any other
decoration, and a target without one of those features leave the row a machine
row, which runs it as before.

Measured by `--tier1 --tier1-report -march=znver5`, each compiler compiling
the same sources: on `main_unit_frontend.c`, main `cf38957a` has 1,675 machine
rows and 581,418 records, and this change 1,212 rows (−463) and 581,189
records; on `main_unit_backend.c`, 2,458 rows and 1,492,985 records, and 1,718
rows (−740) and 1,492,371 records. The moves around the rows are unchanged
(787 and 995): the comparisons took none as rows.

The tests:

- `test-ir-simd` decodes every COMPARE rule's row at every immediate clang
  takes (960 rows, a float's as LLVM selects it and strictly), 252 of them the
  IR's comparisons, against predicates, lanes, mask widths and features written
  out; without each feature it needs, a row stays a machine row; its operands
  swapped, it decodes as the reversed predicate; and 14 changes of each
  (decorations, displacement, memory, registers, immediates 3 and 7, the
  result's mask of another width, an operand of another width or class, a
  record too many, SAE's form) leave it a machine row.
- `test-tier1-masks` takes each of the 384 rows of the integer and packed float
  rules as a program's only row, 8 statements of two vectors under a writemask
  of every bit or of an input word, through both of Tier 0's paths natively
  against a C model of the lanes, then both lowering paths, the IR's interpreter
  at every stage (the 216 of an IR predicate; the others are machine rows) and
  Tier 1's native code against the model.
- `test-checker-driver`'s machine rows' unit has eleven such comparisons, two
  under a writemask and a float's, run natively against clang's; its bound of a
  move for every two rows counts them with the rows, where they took no move.

## Selects and undefined vectors (#1840)

The x86 builtins' remaining rows ([#1840](https://github.com/buster14a/buster/issues/1840))
are decoded family by family into the IR's operations. Families that need a new
operation get a kind of `intrinsic`, as the splat does. Counted at Tier 1's
emission of each machine row (`--tier1 --tier1-report -march=znver5`, inlined
copies included), main `cb608c1` has 1,207 machine rows in `main_unit_frontend.c`
and 1,735 in `main_unit_backend.c`. They break down by kind as follows:

| Kind | Frontend | Backend |
| --- | ---: | ---: |
| selects (a masked move: `_mm512_mask_mov_*`, `_mask_blend_*`, every masked operation's merge) | 257 | 403 |
| permutes and shuffles (`vpermt2*` 263 of both units' 567, `vpermb/w/d/q` 177) | 312 | 255 |
| masked loads | 151 | 247 |
| masked stores | 45 | 91 |
| shifts by a scalar or an immediate, rotates | 32 | 176 |
| other integer operations (minimum, maximum, saturation, products' high halves) | 60 | 55 |
| casts between widths (rows of no record) | 55 | 55 |
| 16- and 32-byte lanes inserted and extracted | 41 | 48 |
| compress and expand | 41 | 18 |
| AES rounds | 65 | 13 |
| extensions | 27 | 84 |
| truncations | 25 | 31 |
| masks to vectors and back | 32 | 8 |
| one element, movemask | 23 | 23 |
| undefined vectors | 21 | 36 |
| ternary logic, tests, blends | 10 | 54 |
| reductions | 9 | 31 |
| gathers, scatters, compressing stores | 1 | 71 |
| floating arithmetic and conversions, others | 0 | 36 |

The first family needs no new operation. `ir_simd_typed_select` decodes a SELECT
rule's row (`__builtin_ia32_select*` of each element and width) as the opmask
conversion leaves it. The row has a mask and two vectors of one width. Its two
records are `vmovdqu64` of VECTOR(2) into VECTOR(0), then the move of the
elements (`vmovdqu8` to `vmovdqu64`) of VECTOR(1) into it under MASK(0), both
register-direct with no other field or decoration. It becomes the IR's
`vselect` of the mask, VECTOR(1) and VECTOR(2), its lanes the elements'
integers, as Tier 1 selects it already (`vpblendmb` to `vpblendmq`). A float's,
a double's, a half's and a bf16's select has the integer lanes of its width,
whose bits it copies without evaluating them. The mask must be the lanes'
(`ir_lane_mask`'s bytes). The features are those of the forms Tier 1 selects:
AVX512F, VL below 64 bytes, BW for byte and word lanes, and the mask's moves
(DQ for a byte's, BW for 32 and 64 lanes').

`ir_simd_typed_zero` decodes an UNDEFINED rule's row (`_mm512_undefined_epi32`
and the rest): no operand and one `vpxorq` of VECTOR(0) with itself at the
result's width. It becomes the IR's zero vector of that width, the value the row
gives, which refines an undefined vector.

Any other form, decoration or target leaves the row a machine row, as before:
zeroing, a different register, a memory operand, a record too many, a mask of
another width, or a target without one of the features.

Measured by `--tier1 --tier1-report -march=znver5`, each compiler compiling
the same sources, the change removes 278 frontend rows and 439 backend rows:

| Unit | Main `cb608c1` rows | Records | This change rows | Records |
| --- | ---: | ---: | ---: | ---: |
| `main_unit_frontend.c` | 1,207 | 576,053 | 929 | 575,650 |
| `main_unit_backend.c` | 1,735 | 1,490,643 | 1,296 | 1,490,048 |

The moves around the rows fall from 782 to 636 in the frontend and from 1,010 to
818 in the backend.

The tests:

- `test-ir-simd` decodes every SELECT rule's row (24: bytes to quadwords, floats,
  doubles, halves and bf16s at three widths) and the three UNDEFINED rules'.
  Their lanes, masks and features are checked against values written out. A
  row stays a machine row:
  - without each feature it needs;
  - under 10 changes (decorations, zeroing, displacement, memory, registers,
    an immediate, a mask or vector of another width, a record too many).
- `test-tier1-masks` takes each of the 27 rows as a program's only row, in
  four statements of vectors and masks from an input buffer. It runs them
  through both lowering paths, the IR's interpreter at every stage, and Tier
  1's native code, against a C model. Tier 0 doesn't run them there: its
  register-direct moves have no Zen 5 row yet, which its builds require.
- `test-checker-driver`'s machine rows' unit keeps its masked operations and
  blends, now the IR's selects. It has 61 machine rows where main has 75, with
  33 moves where main has 39, still within its bound of a move for every two
  rows.

## Lanes: the vectors' element types (#141)

The IR's vector types are containers of 16, 32 and 64 bytes (`v4i32`, `v8i32`
and `v16i32`, the names of their default lanes), whatever their elements: the
typed stream's `VECTOR` class is bytes alone, so loads, stores, copies, phis,
selects, calls and row calls carry a vector as its bytes. Each operation that
reads its operands as elements names its lanes in its `flags` column, an
`IrSimdLane`, as WebAssembly's `v128` operations do: 32-bit integers (`I32`, 0,
the default, so that the operations before the lanes keep their meaning), 8-,
16- and 64-bit integers, floats and doubles. The IR's own opcodes take them, no
new opcode or type:

| Operation | Lanes | Semantics |
| --- | --- | --- |
| `add`, `sub`, `mul` | integers | wrapping, each lane |
| `shl`, `lshr`, `ashr` | integers | each lane by the other operand's same lane, unsigned: 0 past the lane's bits, an arithmetic shift every bit the sign (the `vpsllv` family's) |
| `fadd`, `fsub`, `fmul`, `fdiv` | floats, doubles | IEEE under the runtime MXCSR's rounding, FTZ and DAZ; a NaN operand's quieted bits (the first's where both are NaNs, x86's rule), else the default NaN of an invalid one. The interpreter models the default environment; SCCP folds a lane only where every environment agrees |
| `vcmp` | any | a mask of a bit per lane (`m8` for 8 lanes or fewer, else `m16`, `m32`, `m64`); floating lanes' predicates are C's (`eq` to `ge`: ordered but `ne`, unordered or unequal) |
| `vselect` | any | each lane by its own mask bit |

`and`, `or`, `xor` and `not` read no lanes (their flags stay 0), nor do a
scalar's operations. The text names the lanes in the type: `add.v16i8` is an add
of a `v4i32`'s 16 bytes as 8-bit lanes, `fadd.v8f64` of a `v16i32`'s as doubles;
a comparison, whose type is its mask's, after its predicate: `vcmp.m16 %1, %2 ult
lanes=i8` (`ir_lane_type_names`, `ir_parse_lane_type`). The verifier checks each
operation's lanes (`ir_lane_valid`, `ir_lane_mask`), the vector path deferring a
lane's instruction to the scalar oracle; `tests/ir/vector-lanes.ir` round-trips
every form, and `test-ir` refuses its neighbours (an `fadd` of integer lanes, an
integer `add` of floats, a bitwise operation's lanes, a mask of the wrong width,
an unsigned floating comparison, a vector shifted by a scalar, a scalar's lanes)
by both verifiers.

The decoder (`ir_simd_typed_operation`) takes the operators' rows of every
element type, EVEX's of AVX-512 and VEX's of AVX and AVX2, into these
operations: `vpadd`, `vpsub` of bytes to quadwords, `vpmullw`, `vpmulld` and
`vpmullq`, the `vpsllv`, `vpsrlv` and `vpsrav` of words to quadwords, `vaddps` to
`vdivpd`, and every comparison and its expansion (`vpcmp[u]b` to `vpcmp[u]q`,
`vcmpps` and `vcmppd` with `vpmovm2b` to `vpmovm2q`), each at its form's own
features. The single-record scalar splats above are semantic intrinsics;
multi-record splats, a floating negation (a sign bit's broadcast and `vpxorq`),
half-precision arithmetic and other builtins stay machine rows. The
interpreter models each lane (`ir_simd_evaluate`): `test-ir`'s model check
compares every lane kind and width with C's scalar operators lane by lane, and
`test-tier1-masks` runs three fixtures of every lane kind natively by Tier 1
against the interpreter, random NaNs, infinities and denormals among the floats.
The passes see a pure operation where they saw a call: SSA promotion, SCCP, dead
code and CFG cleanup take them; SCCP folds supported constant vectors, and GVN
and LICM take pure operations with their lane metadata. SCCP folds a floating
lane only where its value is the same under every rounding, FTZ and DAZ: an exact
result, no subnormal operand or result, or a NaN operand ([IR
optimization](ir-optimization.md)'s "Constant SIMD lanes"). Dead code keeps every
floating operation, folded or not, for its effects on the runtime floating
environment: the arithmetic, and a comparison of floating lanes
(`ir_optimize_roots`' kind 3, #1413's review: a NaN operand raises MXCSR's
invalid flag, or traps where it is unmasked), whose mask may go unused; an
integer comparison goes with its mask. `test-ir-optimize` checks both paths,
and `test-checker-driver` an unused comparison of a NaN natively, by both tiers.

The lanes cost compile time even without vectors. On the 9700X, `--tier1 -c
sqlite3.c` (no vectors) ran 29.2M more user instructions (+0.44%) with #1413's
lanes than at its base. Exact counts per pipeline stage (#1450's item 5) put
19.7M in the IR verifier's checks of lane metadata (+2.21% of its work), 6.1M in
the optimizer's own passes, 2.7M in Tier 1's emission and 1.4M in the typed
lowering, with selection 0.7M cheaper; nothing outside Tier 1 changed. Cycles
were flat, so the cost is accepted. [The
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/tier1-remaining-1450-2026-11-06)
(buster-performance#303) has the measurement patch, the counts and a sampled
profile that inlining and throttling made unusable.

## Machine rows (#141)

Any other `X86` row, one the decoders above don't take, is the IR's machine row
(`ir_simd_typed_machine_row`, [the IR guide](ir.md)'s "Machine rows"): a row call
runs it, its arguments the row's operands. Its operands and result must be of the
classes a row call takes (`ir_row_type`: integers of 1 to 8 bytes, floats and
doubles, vectors of 16, 32 or 64 bytes whatever their elements, masks, and `void`
for its result), refused as `IR_SIMD_TYPED_UNSUPPORTED` otherwise (a vector of 4
or 8 bytes, a long double, a 128-bit integer), and the target must have its
features, refused as `IR_SIMD_TYPED_TARGET` otherwise: the union of its forms'
(`x86_target_requirement`'s, less `BUSTER_X86_TARGET_KNOWN`, which classified rows
carry) and of its vectors' and masks' moves, Tier 1's (AVX512F, VL below 64 bytes,
DQ for a byte mask, BW for a doubleword or a quadword one). A row the decoders
take but the target refuses stays refused. So the decoders' refusals above are
now each a machine row, or a refusal by class or target: `test-ir-simd`'s
mutations of the `_k*` and BMI rows check that none decodes as the front end's
operation, and that the decoder of its family refuses it. `test-ir-simd` also
decodes a saturating byte addition's, a masked store's of no result and a shift's
of a count from a general register as machine rows, with their classes and exactly their
features, each refused without each feature it needs.

Consumers classify vectors with `ir_function_has_vectors`, 64
instructions at a time on the vector path, with a scalar twin: a vector type
anywhere, a vector constant or a `vconst`, `vcmp` or `vselect`, in its
instructions, parameters, calls and slots. `ssa_ir_promote` and its twin promote
mask slots as any other (the SSA core compares types alone, `ssa.c`'s codes now
past the IR's mask types), and `ir_optimize`'s SCCP, dead code and CFG cleanup
take masks (`ir_optimize_prepare`'s `masks`), as do GVN and LICM; SROA requires
#1282's `simd` option. The inliner
clones them with its callees (#1282, [the inlining guide](ir-inlining.md)'s
"SIMD callees and by-value vectors"). Tier 1 selects the bitcasts as `kmov`s
([the selection guide](tier1-selection.md)'s "AVX-512's masks").

With `--tier1 -march=x86-64-v4`, a C function of `_k*` builtins compiles by Tier
1, the header's wrappers among them; a caller that passes an `__mmask8` or
`__mmask16` is still refused for its call, whose narrow integer arguments Tier 1
doesn't pass yet, not for its masks. A test's row is a `kmov` to a general
register and a compare there, not `kortest` and `setcc`, which Tier 1 doesn't
fuse yet; this makes no code-quality or throughput claim.

The tests:

- `test-ir-simd` decodes every `_k*` rule's row at a target of every feature
  (the shifts at counts around every width): a step per record of its form's
  kind, its features exactly `x86_target_requirement`'s for its forms
  (`x86_target_rows.c`'s, not the decoder's table), `IR_SIMD_TYPED_TARGET`
  without each with the result untouched, and twelve mutations of each row
  (decorations, memory, a register by number, widths, classes, a test's condition, an unwritten
  register, a record too many, an immediate) refused; and a function of masks
  alone through both SSA paths, SCCP/DCE/CFG, GVN and LICM, refused by SROA
  without `simd`, and taken by the inliner.
- `test-ir` checks the bitcast rule's pairs both ways, accepted and refused, and
  `test-ir-interpreter` their values at each width.
- `test-tier1-masks` takes Tier 0's random mask programs (`tier0_mask_test.c`'s
  generator) through both lowering paths into the same IR, the IR's interpreter
  on the lowered, both promoted and the optimized functions against the
  generator's model, then both of Tier 1's paths natively; programs with rows the
  decoders refuse (an integer in a row, opmask registers by number) have them as
  machine rows (#141), which the interpreter refuses at every stage and Tier 1's
  native runs alone check against the model. Then every `_k*`
  rule's row as the program's only row, compiled by both of Tier 0's paths and
  run natively against a C model of each instruction written from Intel's
  manual, so that the processor checks the model, then through the same
  pipeline to Tier 1's native code against it.
