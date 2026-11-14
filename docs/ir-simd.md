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

The opcode table then filled its 64-entry byte-permute register (two registers'
128 entries since the masked loads and stores, below). A scalar
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

Tier 1 takes that AND back into the comparison (#1847, `tier1_select_masked_compare`):
the pattern of an AND of masks one of whose operands is a `vcmp` only it reads,
in its block, is the comparison under the other operand as its writemask
(`vpcmpd k1{k2}`, `vcmpps k1{k2}`), which leaves 0 in the lanes the writemask
lacks, so that a masked comparison's row is one instruction again, as it was as a
machine row. Its masked-off lanes raise no exception, as the row's didn't; the
AND of an unmasked comparison would have. Where the comparison has another use
it stays its own record, and the AND a `kand`.

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

## Width changes (#1840)

Moving bits between vector widths, the next family of #1840's rows (counted in
the section above), takes three unary or binary kinds of `intrinsic`:

```text
%4 = intrinsic.v4i32 %3 extract 3
%9 = intrinsic.v16i32 %1 widen
%13 = intrinsic.v16i32 %3, %1 insert 2
```

An intrinsic's `flags` are its immediate: a width change's part, in units of the
narrower vector, and 0 for every other kind (which the verifier now checks per
kind rather than as lanes).

- `extract` (`BUSTER_IR_INTRINSIC_EXTRACT`): the `flags`th part of a, of the
  result's width. The result is a narrower vector: 16 bytes of 32 or 64, or 32
  of 64.
- `widen` (`BUSTER_IR_INTRINSIC_WIDEN`): a in the result's low bytes and zeros
  past them. a is a narrower vector; `flags` is 0.
- `insert` (`BUSTER_IR_INTRINSIC_INSERT`): a, of the result's type, with its
  `flags`th part of b's width replaced by b, a narrower vector.

They read no lanes and move bits whole. The text names the part after an
extract's or an insert's kind. `tests/ir/widths.ir` round-trips every form.

The model (`ir_simd_model.c`'s `IR_SIMD_EXTRACT`, `IR_SIMD_WIDEN` and
`IR_SIMD_INSERT`, its action's `index` the part) moves whole words, and the
interpreter runs it. SCCP folds a width change once its operands are constant.
GVN and LICM take it as a pure operation, its part in their key. Dead code
removes an unused one.

`ir_simd_typed_width` decodes three kinds of row:

- **Casts.** The rows of no record of one vector operand (or two of one width,
  the second kept for its effects alone), whose result is the operand's
  register, of another width. These are the intrinsics' casts, as
  `c_check_program_vector_builtins.c` gives every shuffle that moves no element.
  A narrower result is an extract of part 0; a wider one is a widen, since Tier
  0 and Tier 1 both zero-extend an operand into its role's register.
- **Inserts.** BINARY_IMMEDIATE's `vinsert` rows (`vinserti32x4` to
  `vinsertf64x4`, and the `vinsertf128` builtins) become inserts of the
  immediate's part.
- **Masked extracts.** EXTRACT_LANE's `vextract` rows under a writemask, merged
  into w, become an extract and a `vselect` of the form's elements' lanes. The
  `vselect` is left out where the typed stream's writemask has every lane, as
  #1623's comparisons do.

Each record must be register-direct with no other field, and the target must
have AVX512F and VL, the rows' own forms' features (DQ for `32x8` and `64x2`)
and, merged, the mask's moves. Otherwise the row stays a machine row.

Tier 1 selects each kind as one EVEX instruction (`BUSTER_TIER1_VECTOR_WIDTH_CHANGE`):
- an extract of part 0 as the narrower width's move, left out when its
  registers are the same;
- a widen as the narrower width's EVEX move (`vmovdqa64`), even into its own
  register, since that move zeroes the register past it. Legacy SSE's `movaps`
  would keep the bytes past 16 of a register that held the wide value;
- another extract as `vextracti32x4` or `vextracti64x4`;
- an insert as `vinserti32x4` or `vinserti64x4`.

Their six register forms have new Zen 5 rows, measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-width-changes-1840-2026-11-09),
buster-performance#354; [the model guide](x86-zen5.md)).

Measured by `--tier1 --tier1-report -march=znver5`, both compilers compiling
main `cb608c1`'s sources:

| Unit | Main rows | Records | Row moves | This change rows | Records | Row moves |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `main_unit_frontend.c` | 1,207 | 576,053 | 782 | 1,111 | 575,941 | 693 |
| `main_unit_backend.c` | 1,735 | 1,490,643 | 1,010 | 1,643 | 1,490,520 | 926 |

With #1878's selects, merged since, the two families together leave 833 and
1,204 rows: measured by main `48c0839`'s compiler with this change, compiling the
same sources (main's own improvements since change the records, 538,003 and
1,393,252, so they compare with neither column).

The tests:

- `test-ir`: both verifiers admit each kind at every pair of widths and part
  below 5 exactly where the widths and part agree (19 forms), and refuse another
  kind's flags. The model's values are checked against bytes written out.
- `test-ir-parse`: round-trips the forms.
- `test-ir-optimize`: SCCP folds, by both paths, an insert's part back, a
  widen's zeros and another part's own value.
- `test-ir-simd`: decodes every insert rule (16) and masked extract rule (12)
  at 8 immediates, and every cast's row. Features are written out, and each
  row stays a machine row without each feature it needs and under 9 changes.
- `test-tier1-masks`: runs 81 programs, each rule's every part and the 6 casts,
  each its only row, and 3 round trips, a cast to a narrower width and back,
  whose widen writes the register that held the wide value. Every lane of an extract is merged under an input word or
  an unmasked writemask. They go through both lowering paths, the interpreter
  at every stage and Tier 1's native code, against a C model.
- `test-checker-driver`'s machine-rows unit: its inserts, extracts and casts are
  now the IR's. It has 65 rows and 32 moves (main: 75 and 39), 51 and 26 with
  the selects, and its native hashes are clang -O0's by both paths.

## Permutes (#1840)

Four more kinds of `intrinsic` move lanes of two vectors of the result's type,
their immediate in `flags`:

```text
%4 = intrinsic.v16i32 %3, %2 permute 0
%7 = intrinsic.v4i32 %1, %2 shuffle
%8 = intrinsic.v16i32 %3, %6 align 47
%11 = intrinsic.v16i32 %8, %6 shuffle_lanes 228
```

- `permute`: lane i is a's lane that b's lane i's low bits name, modulo the
  lanes; its lanes are `1 << flags` bytes (0 to 3). These are `vpermb` to
  `vpermq`, the variable permutes.
- `shuffle`: byte i is 0 where b's byte i has its top bit set, and else a's byte,
  within the same 16 bytes, that the low 4 bits of b's byte i name (`vpshufb`;
  flags 0).
- `align`: lane i is lane i + count of b's lanes followed by a's. Its lanes are
  `1 << (flags >> 4)` bytes (4 or 8), and its count is flags' low 4 bits, below
  the lanes (`valignd`, `valignq`).
- `shuffle_lanes`: each 16-byte lane j comes from a for the result's low half
  and from b for its high half: the lane that flags' bits from j times log2(the
  lanes) name. A 32-byte vector takes 2 bits of flags and a 64-byte one any
  byte (`vshufi64x2`, and `vshufi32x4`'s same lanes); a 16-byte one has none.

`ir_permute_flags_valid` is the verifier's rule. The model's
`IR_SIMD_PERMUTE` to `IR_SIMD_SHUFFLE_LANES` moves each byte, and the
interpreter, SCCP, GVN and LICM take them as they take the width changes.

`ir_simd_typed_permute` decodes the rows of one record of these forms at the
result's width, of two vectors of that width:

- the variable permutes, their indices in VECTOR(1) (vvvv) and their lanes in
  VECTOR(0) (rm), as the front end swaps the builtins' arguments;
- `vpshufb` of VECTOR(0) by VECTOR(1);
- `valign*` and `vshufi*` of VECTOR(0) and VECTOR(1) by the immediate. An
  align's count is the immediate's bits below the lanes, and a 32-byte shuffle
  of lanes uses its low 2 bits: the bits the instruction reads, though the front
  end passes the builtin's whole byte.

The destination is VECTOR(0). The target needs the form's features (BW for
words and `vpshufb`, VBMI for `vpermb`) and the vectors' moves.

Tier 1 selects each as its form at the same width (`BUSTER_TIER1_VECTOR_PERMUTE`,
`tier1_select_permute_form`), `vshufi64x2` for both shuffles of lanes. Its 19
register forms, every one but `vpermb`'s 64-byte form, have new Zen 5 rows
measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-width-changes-1840-2026-11-09),
buster-performance#354): 2 per cycle, and a latency of 2 cycles at 16 bytes,
3 to 4 at 32 and 5 at 64 (2 at every width for `vpshufb`). A permute of a width with no form (a doubleword's or a
quadword's of 16 bytes, a quadword's of 32) gets no candidate, though no row
makes one.

Measured by `--tier1 --tier1-report -march=znver5` on main `cb608c1`'s sources,
with the width changes too:

| Unit | Rows | Records | Row moves |
| --- | ---: | ---: | ---: |
| `main_unit_frontend.c` | 990 | 575,941 | 693 |
| `main_unit_backend.c` | 1,510 | 1,490,520 | 926 |

That is 121 and 133 rows fewer than the width changes alone (1,111 and 1,643),
with the same records and moves around the rows: each of these rows was one
record that took no move.

The three-operand permutes (`vpermt2*`, which `__builtin_shufflevector`'s rows
use too) wait on the owner's decision on #1840.

The tests:

- `test-ir`: both verifiers take each kind at each width and every flags byte
  exactly where the rule, written out again, admits it (317 forms). They refuse
  operands of another type and a missing b. The model's bytes are checked
  against a byte formula on random vectors.
- `test-ir-parse`: round-trips the forms.
- `test-ir-optimize`: SCCP folds each kind, by both paths alike, to the
  interpreter's value.
- `test-ir-simd`: decodes every rule of these forms (vpermb, vpermw at 3 widths,
  vpermd at 2 and vpermq at 1; vpshufb at 3; the aligns at 3 and the shuffles of
  lanes at 2, at 8 immediates: 92 rows) to its kind and flags, written out, at
  exactly its features. Each row stays a machine row without each feature it
  needs and under 8 changes.
- `test-tier1-masks`: runs 89 programs, every row but vpermb's (the
  pipeline's x86-64-v4 has no VBMI), each as a program's only row, through both
  lowering paths, the interpreter at every stage and Tier 1's native code. They
  are checked against a C model written from Intel's manual.
- `test-checker-driver`'s machine-rows unit: its `permutexvar` and
  `shuffle_epi8` are now the IR's, leaving 61 rows and 32 moves.

## Byte shuffles by an immediate (#1840)

`vpshufd`, `vpshuflw`, `vpshufhw`, `vpslldq` and `vpsrldq` move the same bytes
within every 16-byte lane, so each of them is a `shuffle` (`vpshufb`) of its
vector by a constant. There is no new IR operation. `vpshufd $0x55`, which puts
doubleword 1 in every place, is:

```text
%2 = vconst.v4i32 0x07060504070605040706050407060504
%3 = intrinsic.v4i32 %1, %2 shuffle
```

**Indices** (`ir_simd_shuffle_immediate_indices`, `ir_simd_convert_forms.c`,
shared by the decoder and Tier 1): index i names the byte of the lane that
result byte i takes, or is `0x80` for a zero.
- `vpshufd`: each doubleword is the source doubleword that 2 bits of the
  immediate name.
- `vpshuflw`, `vpshufhw`: each low or high word is the source word that 2 bits
  name. The other quadword is copied.
- `vpslldq`, `vpsrldq`: the bytes move up or down by the immediate. A count of
  16 or more zeroes the lane.

**Decoder** (`ir_simd_typed_shuffle_immediate`): a row of one record of an
`ir_simd_shuffle_immediate_forms` EVEX form, register-direct and undecorated:
- VECTOR(0) in rm into VECTOR(0) (reg; vvvv for the byte shifts, whose reg is
  the opcode's extension);
- by an immediate, whose low 8 bits are the instruction's;
- at the result's width, from one vector of that width.

These are the builtins' `__builtin_ia32_pshufd` at three widths (its 32-byte
row is AVX2's VEX form), `pshuflw`, `pshufhw`, `pslldqi` and `psrldqi`. The
target needs AVX512F, VL below 64 bytes and the EVEX form's features: BW for
all but `vpshufd`.

**Tier 1** (`BUSTER_TIER1_PERMUTE_SHUFFLE_IMMEDIATE`, `tier1_select_shuffle_constant`):
- A `shuffle` whose indices are a constant matching an immediate form becomes
  that form alone, which leaves the constant unused. The match
  (`ir_simd_shuffle_immediate_match`) reads the indices as `vpshufb` does: the
  zeroing bit, else the low 4 bits. Every 16-byte lane must be the same, and
  the first kind in the order above whose indices at the immediate the first
  lane names are the lane's wins. An identity is `vpshufd` 0xe4; every byte zero
  is `vpslldq` 16.
- This candidate comes first, as the shift's immediate form does. Any other
  constant, or indices that aren't one, is `vpshufb`.

**Zen 5:** the 15 EVEX register forms had no row. They were measured on the
9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-shuffle-immediates-1840-2026-11-13)):

| Forms | Latency | Throughput |
| --- | ---: | ---: |
| `vpshufd`, `vpshuflw`, `vpshufhw`, every width | 2 cycles | 4 per cycle |
| `vpslldq`, `vpsrldq`, every width | 2 cycles | 2 per cycle |

`vpshufb` has 2 cycles of latency and runs 2 per cycle, and it loads its
indices, so the immediate form is never slower.

Machine rows at `--tier1 -march=znver5`, both compilers (Develop builds)
compiling this change's sources, against main (`26f0bb0e`):

| Unit | Rows before | Rows after | Records |
| --- | ---: | ---: | ---: |
| `main_unit_frontend.c` | 1 | 1 | 635,525 both |
| `main_unit_backend.c` | 268 | 170 | 1,600,450 both |

The 96 `vpshufd` rows of a zmm register and 2 `vpslldq` rows were each one
record, and are one record of the same form now, so spills and the moves
around rows are unchanged.

The tests:

- **`test-ir-simd`:** each of the 15 front-end rules decodes at 8 immediates
  to a shuffle by its kind's indices, at exactly its features (120 rows). Each
  is refused without each feature it needs and under 9 changes. The match is
  checked by brute force:
  - every kind's indices at every immediate and width, with the bits `vpshufb`
    ignores set at random, match back to the first kind and immediate with the
    same indices;
  - random indices match exactly where some kind's at some immediate are theirs;
  - indices whose 16-byte lanes differ never match.
- **`test-tier1-masks`:** the 120 run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against Intel's definitions
  of each instruction. Tier 1's code by both paths has the matched immediate
  form's 4 records and no `vpshufb`.

## Conversions (#1840)

Two more unary kinds of `intrinsic` widen or narrow integer lanes. Their
`flags` hold the source's lanes' bytes' log2 (bits 0 and 1), the result's (bits
2 and 3) and the mode (bits 4 and 5):

```text
%4 = intrinsic.v16i32 %1 extend 8
%8 = intrinsic.v8i32 %3 truncate 22
```

- `extend` (`BUSTER_IR_INTRINSIC_EXTEND`): lane i is a's lane i,
  zero-extended (mode 0) or sign-extended (1) to a wider lane. It has as many
  lanes as the result holds, read from a's low bytes, which must have them
  (`vpmovzx`, `vpmovsx`).
- `truncate` (`BUSTER_IR_INTRINSIC_TRUNCATE`): lane i is a's lane i, for every
  lane of a, narrowed by wrapping (mode 0), by saturating a signed lane to the
  signed range (1, `vpmovs`), or by saturating an unsigned lane to the unsigned
  range (2, `vpmovus`, whose source Intel's manual has unsigned). The lanes go in
  the result's low bytes with zeros past them; the result is 16 bytes, or the
  lanes' bytes if more (`vpmov`).

`ir_convert_flags_valid` is the verifier's rule. The model's `IR_SIMD_EXTEND`
and `IR_SIMD_TRUNCATE` convert lane by lane, and the interpreter, SCCP, GVN
and LICM take them as they take the permutes.

`ir_simd_typed_convert` decodes two kinds of row:

- `__builtin_convertvector`'s rows between integer vectors of other element
  sizes: one record of an extension's form at the result's width (destination
  in reg) or a truncation's at the source's (destination in rm). These are
  `ir_simd_convert_forms.c`'s tables, which Tier 1's selection shares.
- TRUNCATE's rows, the `vpmov*` builtins under a mask: the truncation into
  VECTOR(1) under MASK(2), then its move into VECTOR(0). They become the
  truncate merged into w by a `vselect` at the result's lanes, or alone where
  the typed stream's writemask has every lane.

A truncation under a mask whose lanes leave the result's upper bytes zero
(`vpmovdb` of 16 or 32 bytes, `vpmovqb`, `vpmovqw` but of 64) stays a machine
row: its merge isn't a `vselect` of the result's lanes. The target needs the
form's features (BW for words' lanes) and AVX512F's and VL's.

Tier 1 selects each as its form (`BUSTER_TIER1_VECTOR_CONVERT`,
`tier1_select_convert_form`): an extension with its destination in reg, a
truncation with its destination in rm, which zeroes the register past its
lanes. Its 88 register forms that had no Zen 5 row were measured on the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-width-changes-1840-2026-11-09),
buster-performance#354).

Measured by `--tier1 --tier1-report -march=znver5` on main `cb608c1`'s sources,
with the width changes and the permutes:

| Unit | Rows | Records | Row moves |
| --- | ---: | ---: | ---: |
| `main_unit_frontend.c` | 938 | 575,882 | 676 |
| `main_unit_backend.c` | 1,406 | 1,490,439 | 902 |

That is 52 and 104 rows fewer than with the permutes (990 and 1,510).

The tests:

- `test-ir`: both verifiers admit each kind between every pair of vector widths
  at every flags byte below 64 exactly where the lanes, mode and widths agree
  (156 forms). The model is checked against C's conversions of each lane on
  random vectors with saturating edges.
- `test-ir-parse` and `test-ir-optimize`: the text and SCCP's folds, as the
  permutes'.
- `test-ir-simd`: decodes, by the real producer, every convertvector row
  between integers of other sizes (102 extensions, 16 truncations at every
  width and operand width the producer takes) and every TRUNCATE rule's row
  (22 fill their result and decode; 30 stay machine rows). Each is checked
  against kinds, flags and features written out, and refused without each
  feature it needs and under changes.
- `test-tier1-masks`: runs 74 programs, convertvector's and the masked
  truncations, the latter under an input word or unmasked. They go through
  both lowering paths, the interpreter at every stage and Tier 1's native code,
  against a lane-by-lane C model. Its runs caught the unsigned saturation's
  source, which an earlier version took as signed.
- `test-checker-driver`'s machine-rows unit: 59 rows and 32 moves.

## Casts into SSE carriers and their extensions (#1840)

An SSE carrier (#1280) is a vector of 4 or 8 bytes, an `f32` or `f64` in the IR,
in the vector class's registers. clang's headers make one before many
conversions. `_mm256_cvtepu8_epi32`, for example, shuffles a vector's first
8 bytes into a narrower vector, then converts that. The front end gives the
pair as two rows: a cast with no record, then the extension of the carrier.
Both are now the IR's, with no new operation:

```text
%3 = intrinsic.f64 %1 lane_extract 3
%5 = intrinsic.v8i32 %3 extend 8
```

- **The cast** (`ir_simd_typed_carrier_cast`): a row of no record of a vector of
  16, 32 or 64 bytes into one of 4 or 8. It is `lane_extract` of lane 0 of
  doublewords or quadwords at the carrier's type. The verifier now admits an
  `f32` or `f64` result for that lane alone, and `IR_INTRINSIC`'s result types
  include them. Every other kind's rule still refuses them.
- **The extension** (`ir_simd_typed_convert`): `extend`'s source may be a
  carrier, whose bytes it reads as a vector's low ones. The verifier checks the
  lanes against the type's bytes. The model takes the scalar operand's bits
  (no shape), at most 8 bytes of them.
- **The target** needs AVX512F, VL and the vectors' moves, as the conversions do.

**Tier 1:**
- The cast (`BUSTER_TIER1_VECTOR_LANE_EXTRACT`, `source_width` the carrier's
  bytes) is nothing where the carrier takes the vector's register. Elsewhere
  it is `tier1_select_carrier_move` into its own register, zero-extended. A
  carrier may hold anything past its bytes, and every row reads one
  zero-extended.
- The extension is its form, which reads the carrier register's low bytes.
- In the compiler's own units the allocator gives every such carrier its
  vector's register. A fold of the cast into the extension (reading the vector
  directly) gave the same records, so there is none.

Machine rows at `--tier1 -march=znver5`, both compilers (Develop builds)
compiling this change's sources, against main (`26f0bb0e`), with the byte
shuffles by an immediate above:

| Unit | Rows before | Rows after | Records before | Records after |
| --- | ---: | ---: | ---: | ---: |
| `main_unit_frontend.c` | 1 | 1 | 635,525 | 635,525 |
| `main_unit_backend.c` | 268 | 132 | 1,602,817 | 1,602,779 |

The 19 casts and 19 extensions (`vpmovzxbd` of 32 bytes, `vpmovzxbq` of 64)
leave the units. The 38 records fewer are the moves each extension's row made
of its carrier into its role.

The tests:

- **`test-ir`:**
  - both verifiers admit `extend` of an `f32` or `f64` into every width whose
    lanes it holds, at every flags byte (28 forms), and the model's lanes are
    C's;
  - `lane_extract` into an `f32` or `f64` is admitted for lane 0 of
    doublewords or quadwords alone, at every width.
- **`test-ir-parse`:** `tests/ir/carriers.ir` round-trips.
- **`test-ir-simd`:**
  - the 6 casts (4 and 8 bytes, from 16, 32 and 64) and the real producer's 18
    whole-lane extensions of a carrier decode at exactly their features;
  - each is refused without each feature and under changes.
- **`test-tier1-masks`:** 48 programs through both lowering paths and Tier 1
  natively, against C:
  - the 18 extensions of a cast, and of a carrier loaded from memory, through
    the interpreter at every stage too;
  - the 6 casts stored whole;
  - the 6 casts whose vector stays live past them into a machine row's
    `vpaddd` or `vpaddq`, so the carrier needs its own register. With the
    carrier's move left out, these 48 runs fail.

## Shifts by a scalar count (#1840)

One more binary kind of `intrinsic`, `shift` (`BUSTER_IR_INTRINSIC_SHIFT`),
shifts every integer lane of a vector by one count:

```text
%7 = intrinsic.v16i32 %4, %1 shift 11
```

- a is the vector, of the result's type; b is an `i32`, the count, read
  unsigned.
- `flags` hold the lanes' bytes' log2 (bits 0 and 1: 1, 2 or 3, words to
  quadwords) and the shift (bits 2 and 3: 0 left, 1 logical right, 2
  arithmetic right).
- A count of the lane's bits or more gives 0, or every bit the sign for an
  arithmetic shift, as `vpsllw` to `vpsraq` do by a count in an xmm register's
  low quadword. The lane-wise `shl`, `lshr` and `ashr` can't express this
  without clamping first: a count of 65,537 splatted into 16-bit lanes would
  wrap to 1.

`ir_shift_flags_valid` is the verifier's rule. The model's `IR_SIMD_SHIFT`
shifts lane by lane with the lane operations' own shifts, and the interpreter,
SCCP, GVN and LICM take it as they take the conversions.

`ir_simd_typed_shift` decodes the x86 builtins' shifts by an int
(`CHECK_X86_LOWER_SHIFT`, `__builtin_ia32_psllwi512` to
`__builtin_ia32_psraqi128`, 27 rules). clang's header wrappers
(`_mm512_slli_epi64` and the rest) pass the count as a parameter, so the row is
`vmovd` of the count into VECTOR(1), which zeroes its bytes past 32 bits, then
the shift's form by that xmm count. The target needs the forms' features (BW
for words), AVX512F's and VL's below 64 bytes.

Tier 1 selects it (`BUSTER_TIER1_VECTOR_SHIFT`, `tier1_select_vector_shift`)
from the forms the decoder shares (`ir_simd_convert_forms.c`):

- a count that is a constant, as it is once the wrapper is inlined, by the
  immediate form alone (`vpsrlq $32, %zmm0, %zmm0`), a count of 255 or more as
  255, past every lane. This candidate comes first, since the bottom-up path
  takes its rule's first candidate;
- any other count by `vmovd` into the vector class's scratch register, then the
  form by that xmm count, the row's two records.

Its 53 register forms that had no Zen 5 row were measured on the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-shifts-1840-2026-11-11),
buster-performance#388): each has 2 cycles of latency and runs 2 per cycle.

Measured by `--tier1 --tier1-report -march=znver5`, both compilers compiling
the same sources (#1972's head `2461071`):

| Unit | Rows before | Records before | Rows after | Records after |
| --- | ---: | ---: | ---: | ---: |
| `main_unit_frontend.c` | 660 | 543,674 | 628 | 543,620 |
| `main_unit_backend.c` | 967 | 1,542,016 | 792 | 1,541,683 |

No shift by an int stays a machine row, and the moves around rows are
unchanged (535 and 708). Spills are within 3 of before.

The tests:

- `test-ir`: both verifiers admit `shift` at each vector width, for every
  flags byte below 32 and a count of `i32`, `i64`, `i16` or the vector's type,
  exactly where the flags name a lane size and shift and the count is an `i32`
  (27 forms). The model is checked against C's shifts of each lane at counts
  around every lane's bits, past 16 and 32 bits, and with high bits above the
  count's 32.
- `test-ir-parse` and `test-ir-optimize`: the text, and SCCP's folds of a
  constant vector by a constant count by both paths.
- `test-ir-simd`: decodes, by the real producer, every shift rule's row (27),
  checked against flags and features written out from the builtin's name. Each
  is refused without each feature it needs and under 8 changes, among them a
  64-bit count (`vmovq`), which stays a machine row.
- `test-tier1-masks`: each of the 27 rules runs as a program's only row twice,
  once with counts loaded from memory (5, the lane's bits less 1, its bits, and
  65,537) and once with constant counts (1, bits less 1, bits plus 1, and 300).
  They go through both lowering paths, the interpreter at every stage and Tier
  1's native code, by a register and by an immediate, against a lane-by-lane C
  model.

Rotates (`vprold` and the rest by an immediate) stay machine rows: the
compiler's own units have one.

## Lane operations (#1840)

One more binary kind of `intrinsic`, `lane_op` (`BUSTER_IR_INTRINSIC_LANE_OP`),
computes what the IR's opcodes lack on two vectors' integer lanes:

```text
%6 = intrinsic.v16i32 %3, %3 lane_op 29
```

- a and b are of the result's type.
- `flags` hold the lanes' bytes' log2 (bits 0 and 1: bytes to quadwords) and
  the operation (bits 2 to 4): the unsigned (0) and signed (1) minimum, the
  unsigned (2) and signed (3) maximum, and, of bytes and words alone, the
  unsigned (4) and signed (5) saturating sum and the unsigned (6) and signed (7)
  saturating difference a − b.

`ir_lane_op_flags_valid` is the verifier's rule. The model's `IR_SIMD_LANE_OP`
computes each lane, and the interpreter, SCCP, GVN and LICM take it as they
take the shifts.

`ir_simd_typed_lane_op` decodes clang's `__builtin_elementwise_min`, `_max`,
`_add_sat` and `_sub_sat` of integer vectors, which clang's headers'
`_mm512_min_epu8` to `_mm512_subs_epi16` call. Their rows are one record of
`vpminub` to `vpmaxsq` or `vpaddusb` to `vpsubsw`, VECTOR(0) (vvvv) and
VECTOR(1) (rm) into VECTOR(0). The target needs the form's features (BW for
bytes and words), AVX512F's and VL's below 64 bytes. Tier 1 selects the form
back (`BUSTER_TIER1_VECTOR_LANE_OP`), from the table the decoder shares
(`ir_simd_lane_op_forms`). Their 72 register forms had no Zen 5 row; they were
measured on the 9700X: 2 cycles of latency, the minima and maxima of bytes to
doublewords 4 per cycle and the others 2
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-shifts-1840-2026-11-11),
buster-performance#388). The reductions (`__builtin_reduce_min` and the rest,
several records) stay machine rows.

The producer's forms (`check_vector_elementwise_forms`) moved to
`c_check_program_vector_convert.c` with the conversions' row, so that the
backend unit's tests read them.

Measured by `--tier1 --tier1-report -march=znver5`, both compilers compiling
#1972's sources (`2461071`), against the masks' conversions:

| Unit | Rows before | Rows after | Records |
| --- | ---: | ---: | ---: |
| `main_unit_frontend.c` | 596 | 541 | 543,612 both |
| `main_unit_backend.c` | 784 | 752 | 1,541,683 both |

The tests:

- `test-ir`: both verifiers admit `lane_op` at each vector width, for every
  flags byte below 64 and a second operand of the result's type or another
  width's, exactly where operation, lanes and operands agree (72 forms). The
  model is checked against C's minimum, maximum and clamped sum and difference
  of each lane, on random vectors with extreme lanes.
- `test-ir-parse` and `test-ir-optimize`: the text, and SCCP's folds of two
  constant vectors by both paths.
- `test-ir-simd`: every form the producer has decodes (72: minima and maxima
  of 4 element sizes, both signs, 3 widths; saturations of bytes and words),
  checked against flags and features written out. Each row is refused without
  each feature it needs and under 6 changes.
- `test-tier1-masks`: each of the 72 rows runs as a program's only row, through
  both lowering paths, the interpreter at every stage and Tier 1 natively,
  against C's operations on inputs with lanes at the saturations' edges.

## Permutes of two tables (#1840)

The ternary kinds of `intrinsic` have a third operand, which no instruction
field holds. The owner chose this design on #1447: one IR operation and one
instruction, which later ternary kinds reuse (ternary logic, the dot products, a
masked load's pointer, mask and passthrough). Their `c` is the kind in its low
8 bits and, past them, the index in the function's extra array of the third
operand's value, as a phi's and a call's operands are there:

```text
%6 = intrinsic.v16i32 %3, %3, %3 permute2 3
```

- `ir_intrinsic_kind`, `ir_intrinsic_ternary`, `ir_intrinsic_extra` and
  `ir_intrinsic_third` read the layout; `ir_intrinsic_ternary_c` writes it. An
  index past 2^24 − 1 gives a `c` the verifier refuses, never another operand.
- A binary kind's `c` is its kind alone, so the existing kinds' tests of `c`
  are unchanged.
- `ir_operand` and `ir_operand_count` give the third operand at position 3, so
  the use lists, the dominance check, SSA's undefined values and every pass that
  walks operands see it. `ir_value_mask3` and `ir_value_third` give it where a
  pass reads the operand columns. The vector use lists and the dataflow's vector
  path read it from the extra array beside a call's arguments.
- Every pass that rebuilds a function's extra array gives the operand a slot of
  its own, never one shared with the source: the optimizer's rewrite, GVN's
  alias view, LICM, if-conversion, SSA's rebuild, the inliner (the caller's
  instructions and a callee's), `ir_rebuild`, `ir_wide`, the vector split and
  the metamorphic transforms. GVN keys a ternary intrinsic on its kind and its
  three operands' classes.

`permute2` (`BUSTER_IR_INTRINSIC_PERMUTE2`) is the first kind. Lane i is, of
a's lanes followed by the third operand's, the one that b's lane i's low bits
name. Enough bits are read to name twice the lanes, and `flags` hold the lanes'
bytes' log2 (0 to 3). Its operands are the order of
`_mm512_permutex2var_epi32(a, idx, b)`. The model's `IR_SIMD_PERMUTE2` computes
it, and the interpreter, SCCP, GVN and LICM take it as they take the binary
permutes.

`ir_simd_typed_permute2` decodes the rows of the permutex2var builtins and
`__builtin_shufflevector` of one or two vectors. Each row is one record of
`vpermt2b` to `vpermt2q`, with VECTOR(0) the destination and first table (reg).
The indices are VECTOR(1) (vvvv) and the second table VECTOR(2) (rm), or the
other way around for `__builtin_shufflevector`; a shuffle of one vector has the
indices VECTOR(1) and VECTOR(0) both tables. A table may be narrower than the
result (16 or 32 bytes: a shuffle of narrower vectors into a wider one). Its
register's upper bytes are then unspecified, and the indices never name them
where the result is defined, so the IR takes the table's `widen` (zeros past
it). The masked builtins' rows are the same record and a separate select. The target needs the form's features (VBMI for
bytes, BW for words) and AVX512F's and VL's below 64 bytes.

Tier 1 (`BUSTER_TIER1_VECTOR_PERMUTE2`) picks the instruction by the register
its destination already holds:

- the first table's: `vpermt2*`;
- the indices': `vpermi2*`, the same permute, which overwrites the indices and
  whose semantics rows are new;
- any other: a move of the first table into it first, the vector scratch
  register keeping the second table across that move where the destination
  holds it.

The 24 register forms of both families had no Zen 5 row. On the 9700X they
measured 3, 4 and 5 cycles of latency at 16, 32 and 64 bytes, 2 per cycle
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-permute2-1840-2026-11-11)).

Measured by `--tier1 -march=znver5`, both compilers compiling this change's
sources, against the lane operations (`f324f2ac`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 541 | 350 |
| `main_unit_backend.c` | 753 | 642 |

No `vpermt2` row is left in either unit.

The tests:

- `test-ir-parse`: `tests/ir/widths.ir` round-trips a permute2 at each width,
  and a third operand is required of a ternary kind and refused of the others.
- `test-ir-optimize`: SCCP folds a permute2 of three constant vectors on both
  paths, at each lanes' size and width; a loop with an invariant permute2 twice
  and a loop-carried one runs as the interpreter says under every set of passes.
- `test-ir-licm`: GVN gives the two invariant permute2s one value and LICM
  hoists it with their sum, alike on both paths, each result the interpreter's.
- `test-ir-inline`: an inlined callee's permute2s, one of which takes the
  callee's parameter, get slots of the caller's, remapped.
- `test-ir-simd`: the 24 rows decode at exactly their features, refused without
  each and under 6 changes; of narrower tables, one 16-byte table twice and two
  of half the width decode, and a table wider than the result or indices
  narrower stay machine rows.
- `test-tier1-masks`: the 30 rows the x86-64-v4 target allows (`vpermt2b` is
  VBMI's), of both orders at each width and of narrower tables at 32 and 64
  bytes, run as a program's only row, through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against a C model whose
  narrow tables are zero-extended.
- `test-ir-source`: the vector and scalar use lists agree on C files of every
  permutex2var builtin and shuffles of one and two vectors, narrower ones
  included.

## Bitwise functions of three vectors (#1840)

`ternlog` (`BUSTER_IR_INTRINSIC_TERNLOG`) is the second ternary kind:

```text
%4 = intrinsic.v16i32 %2, %2, %2 ternlog 232
```

- Its flags are a truth table: each result bit is bit `a << 2 | b << 1 | c` of
  the flags, where a, b and c are that bit of a, b and the third operand.
- All three operands are of the result's type, and any flags byte is valid.
- The model's `IR_SIMD_TERNLOG` computes it, a word at a time from the table's
  minterms. The interpreter, SCCP, GVN and LICM take it as they take permute2.

`ir_simd_typed_ternlog` decodes the ternarylogic builtins' rows. Each is one
record of `vpternlogd` or `vpternlogq` at the result's width, with VECTOR(0) in
reg, VECTOR(1) in vvvv and VECTOR(2) in rm, and its immediate as the table. Its
writemask is MASK(3), with a bit per lane:

- merging, for the mask builtins, into the first operand;
- zeroing, for the maskz builtins;
- every lane, for the unmasked builtins, which pass -1.

The IR is the `ternlog`, then a `vselect` at the form's lanes into the first
operand or a zero vector. The select is left out where the typed stream's
writemask has every lane. The target needs AVX512F, VL below 64 bytes, and DQ
for a one-byte mask.

Tier 1 (`BUSTER_TIER1_VECTOR_TERNLOG`) emits `vpternlogd` with whichever
operand already holds the destination's register as its first operand, the
table's bits permuted to match the new order. Only where the destination holds
none of them does it move the first operand in first. The forms already had
Zen 5 rows.

Machine rows at `--tier1 -march=znver5`, both compilers compiling this change's
sources, against the permutes of two tables (`4f18b961`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 350 | 340 |
| `main_unit_backend.c` | 642 | 587 |

No `vpternlog` row is left in either unit.

The tests:

- `test-ir`: both verifiers admit ternlog with every 15th table at each width,
  and refuse a third operand of another width. The model is checked against
  each bit's table entry.
- `test-ir-parse` and `test-ir-optimize`: `tests/ir/widths.ir` round-trips it,
  and SCCP folds it of three constants with 4 tables, on both paths.
- `test-ir-simd`: the 18 rows decode at exactly their features: both lane
  sizes, 3 widths, and no writemask, merging or zeroing. Each is refused without
  a feature it needs and under 6 changes.
- `test-tier1-masks`: 48 rows run as a program's only row, through both
  lowering paths, the interpreter at every stage and Tier 1 natively, against a
  C model. They cover 4 tables, both lane sizes and 3 widths, merging and
  zeroing, each under a mask from the input.

## Compresses and expands (#1840)

`compress` and `expand` (`BUSTER_IR_INTRINSIC_COMPRESS`, `_EXPAND`) are two
more ternary kinds:

```text
%3 = intrinsic.v16i32 %2, %1, %2 compress 2
```

- **Operands:** a is the source, b the result's mask at the lanes in flags
  (their bytes' log2, 0 to 3), and the third operand the passthrough.
- **Compress:** a's lanes whose bits are set, packed from lane 0, followed by
  the passthrough's lanes past them.
- **Expand:** a's next lane from lane 0 at each set bit's lane, and the
  passthrough's lanes elsewhere.
- **Passes:** the model's `IR_SIMD_COMPRESS` and `IR_SIMD_EXPAND` compute them.
  The verifier and SCCP read each operand's own shape, the mask's among them.

`ir_simd_typed_compress` decodes the compress and expand builtins' register
rows. Each is two records:

- `vpcompressb` to `vpcompressq` (source in reg, destination in rm) or
  `vpexpandb` to `vpexpandq` (destination in reg, source in rm), merged under
  MASK(2) into VECTOR(1), the passthrough;
- then a whole move of VECTOR(1) into VECTOR(0).

The target needs VBMI2 and BW for bytes and words, VL below 64 bytes, and DQ
for a one-byte mask or BW for a mask of 4 bytes or more. The memory forms, the
compress-stores, stay machine rows; the masked stores are now the IR's
`masked_store` ("Masked loads and stores" below), which they could follow.

Tier 1 (`BUSTER_TIER1_VECTOR_COMPRESS`) merges into the destination under the
mask's k register, with a move of the passthrough there first unless the
destination already holds it. The vector scratch register keeps the source
across that move where the destination holds the source.

The 24 masked register forms had no Zen 5 row. On the 9700X they measured
about 2.25, 4 and 5 cycles of latency at 16, 32 and 64 bytes, one per cycle and
two macro-ops each
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-compress-1840-2026-11-11)).

Machine rows at `--tier1 -march=znver5`, both compilers compiling this change's
sources, against the bitwise functions (`3249be86`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 340 | 299 |
| `main_unit_backend.c` | 587 | 570 |

The tests:

- `test-ir`: both verifiers admit them exactly where the flags name b's mask
  type. The model is checked against C's lanes on random vectors and masks.
- `test-ir-parse` and `test-ir-optimize`: the text form round-trips, and SCCP
  folds them of constants under a constant mask, on both paths.
- `test-ir-simd`: the 24 rows decode at exactly their features. Each is refused
  without a feature it needs and under 6 changes.
- `test-tier1-masks`: the 12 rows the x86-64-v4 target allows (dwords and
  quadwords) run under a mask from the input, through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against C's lanes.

## AES rounds (#1840)

`aes` (`BUSTER_IR_INTRINSIC_AES`) is a binary kind of `intrinsic`:

```text
%2 = intrinsic.v16i32 %1, %1 aes 0
```

- a is the state and b the round key, both of the result's type.
- Each 16-byte lane takes ShiftRows, SubBytes and MixColumns of a, then b is
  xor'ed in, as FIPS 197 and x86's `vaesenc` define a round.
- Flags 1 is the last round (`vaesenclast`), without MixColumns.

The binary kinds now come after the ternary ones, which end at
`BUSTER_IR_INTRINSIC_TERNARY_END`. The scalar intrinsics' range checks stop at
`smulo` instead of the count.

- **Model:** `IR_SIMD_AES` works on x86's byte layout. It computes the S-box
  (the multiplicative inverse in GF(2^8), then the affine map) instead of
  tabulating it.
- **Passes:** the interpreter, SCCP, GVN and LICM take it as a pure binary
  kind.
- **Decoder:** `ir_simd_typed_aes` decodes the aesenc builtins' rows. Each is
  one record of `vaesenc` or `vaesenclast`, EVEX at every width or VEX at 16
  and 32 bytes, with VECTOR(0) the state (reg and vvvv) and VECTOR(1) the key
  (rm). The target needs the form's VAES or AES.
- **Tier 1** (`BUSTER_TIER1_VECTOR_AES`) emits the EVEX form, or the VEX form
  where the target has no VL.

The 10 forms had no Zen 5 row. On the 9700X they measured 4 cycles of
latency at every width, 2 per cycle
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-aes-1840-2026-11-11)).

Machine rows at `--tier1 -march=znver5`, both compilers compiling this change's
sources, against the compresses (`ec902c4b`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 299 | 234 |
| `main_unit_backend.c` | 570 | 557 |

No AES row is left in either unit.

The tests:

- **`test-ir`:** both verifiers admit it with flags 0 and 1 alone. The model
  gives FIPS 197 Appendix B's round 1 in every lane of each width. For the last
  round, it gives the 9700X's `vaesenclast` of that appendix's round-10 key.
- **`test-ir-parse` and `test-ir-optimize`:** the text form round-trips, and
  SCCP folds both rounds of constants on both paths.
- **`test-ir-simd`:** the 10 rows decode at their form's features. Each is
  refused without AES and VAES and under 4 changes.
- **No `test-tier1-masks` run:** the x86-64-v4 target it lowers for has neither
  AES nor VAES. Instead, a C file with every width, both rounds and a chain of
  ten rounds, compiled by `--tier1`, matches clang's results natively.

## Masked loads and stores (#1840)

`masked_load` and `masked_store` are memory opcodes of their own (the owner's
"Option B",
[#1840](https://github.com/buster14a/buster/issues/1840#issuecomment-6802289363)),
`IR_MASKED_LOAD` and `IR_MASKED_STORE`, not intrinsic kinds: an intrinsic stays
pure and total.

```text
%5 = masked_load.v16i32 %0, %1, %4 align=1 lanes=i8
%9 = masked_store.void %0, %6, %1 align=64 lanes=i32
```

- **Operands:** a load's `a` is the address, `b` the mask and `c` the
  passthrough, of the result's type; a store's `a` is the address, `b` the
  stored vector and `c` the mask.
- **Flags:** the lanes, an `IrSimdLane` of integers (`i8` to `i64`) whose mask
  (`ir_lane_mask`) the mask operand is. The alignment is a load's or a store's.
  The text always names both.
- **Semantics:** a load's lane i is memory's lane i where the mask's bit i is
  set and the passthrough's lane i where it isn't; a store writes lane i where
  the bit is set. A lane whose bit is clear is never accessed, so it can't
  fault: the masked builtins' tail reads past an array's end are defined. A
  masked load is therefore not a load followed by a `vselect`.

**The opcode tables.** They were full at 64 entries, each one byte-permute
register. They now hold 128 entries (`BUSTER_IR_OP_TABLE`), two registers each,
which `vpermi2b` looks up in one instruction where `vpermb` did: the verifier's
operand, field, rule and terminator tables, the dataflow's, SCCP's roots, GVN's
pure and LICM's safe operations. The result-type table's 128 words take four
`vpermi2d` and three blends instead of two and one.

**Passes.** Each pass that treats a load or a store specially learns them:

- **Verifiers:** their own rules (`IR_RULE_MASKED_LOAD`, `_STORE`), which the
  batch verifier defers to the scalar check.
- **Dead code:** both are roots, as a load and a store are. SCCP never folds
  them; GVN and LICM never merge or move them (they aren't pure or safe).
- **SSA promotion and SROA:** a slot whose address one takes is escaped, as for
  any operation that isn't a plain load or store.
- **Interpreter:** each set lane's bytes are checked against the bindings and
  accessed, and no other byte. Its alignment is checked as a whole access's. A
  load's passthrough must be defined only where some lane's bit is clear, as a
  `vselect`'s inactive input.
- **Tier 1:** a masked store is a barrier, and both are fold barriers, as a
  load is. Its scheduler keeps both in place, and every operand is in a register.

**Decoder.** `ir_simd_typed_masked_memory` decodes the masked load and store
builtins' rows (`loaddqu*`, `storedqu*`, `loadups`, `loadupd`, `loadaps`,
`loadapd`, `movdqa32load`, `movdqa64load`, their stores, and `loadss128` and
`loadsd128` of the whole vector):

- A load is the move of the vector's width from `[INTEGER(0)]` into VECTOR(1),
  the passthrough, under MASK(2), then VECTOR(1) moved whole into VECTOR(0).
- A store is the move of VECTOR(1) to `[INTEGER(0)]` under MASK(2).
- The lanes are the move's element: `vmovdqu8` to `vmovdqu64`'s, and
  doublewords or quadwords for `vmovdqa32`, `vmovdqa64` and the floating moves.
- An aligned move's (`vmovdqa`, `vmovaps`, `vmovapd`) alignment is the vector's
  bytes, which it faults without.
- The target needs the forms' features (BW for bytes and words, VL below 64
  bytes) and the mask's: DQ for a byte mask, BW for one of 4 bytes or more.
- Under the typed stream's mask of every lane (a constant), the lowering emits
  the plain `load` or `store` of the whole vector.
- The expanding loads stay machine rows (an `expand` of a plain load would read
  lanes past the selected ones).

**Tier 1** selects them as `TIER1_RULE_FLOAT_LOAD`'s and `_STORE`'s with their
lanes in `condition` (`tier1_select_masked_move`): `vmovdqu8` to `vmovdqu64`
by the lanes, under the mask's k register. A load merges into the destination,
which takes the passthrough first unless it holds it. The address is a base
register alone; the mask takes the index's pattern value, so no address folds
until the patterns have a fourth value (item 5).

The 11 masked stores that had no Zen 5 row (`vmovdqu8` to `vmovdqu64` at
every width but `vmovdqu32`'s zmm one) were measured on the 9700X: 2 per cycle
at 16 and 32 bytes, 1 at 64, as the unmasked stores
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-masked-memory-1840-2026-11-11)).
A masked load merging into its destination has no row in the model, which
measures a load's row zeroing ([the model's guide](x86-zen5.md)).

Machine rows at `--tier1 -march=znver5`, both compilers (Debug builds)
compiling this change's sources, against main `e43dfc10`:

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 241 | 38 |
| `main_unit_backend.c` | 558 | 218 |

The tests:

- **`test-ir`:** both verifiers admit them exactly at integer lanes whose mask
  is their mask operand, a `ptr` address and an alignment's log2 up to 31; the
  float lanes, an invalid lane, another mask, a vector address and 2^32 are
  refused, at every width.
- **`test-ir-parse`:** `tests/ir/masked-memory.ir` round-trips, every lane
  width and both opcodes among it.
- **`test-ir-simd`:** the 60 rows (10 moves, load and store, 3 widths) decode
  at exactly their features. Each is refused without a feature it needs and
  under 7 changes. A scatter is now the machine-row test's row of no result
  (a compress-store was, until compressing stores became the IR's own, below).
- **`test-tier1-masks`:** `vmovdqu8` to `vmovdqu64` at each width, load and
  store (24 rows), under a mask from the input, through both lowering paths,
  the interpreter at every stage and Tier 1 natively, against C's lanes. Each
  program's fourth access starts half a vector before its buffer's end with
  its mask's upper half cleared, so the interpreter's binding checks prove a
  masked-off lane is never read or written. An interpreter that read every
  lane is refused on every one of them.

## Gathers, compressing stores and truncating stores (#1840)

Item 4's last machine rows in `main_unit_backend.c` (75 gathers and a scatter,
13 compressing stores and 4 masked truncations at `--tier1 -march=znver5`, main
`8023523`) are memory effects, so they follow the owner's Option B as the
masked loads and stores do: opcodes of their own, an intrinsic staying pure and
total ([the design](https://github.com/buster14a/buster/issues/1840#issuecomment-6875721515)).

```text
%6 = gather.v16i32 %0, %1, %3 scale=4 lanes=i32
%11 = compress_store.void %0, %10, %3 align=1 lanes=i32
```

**`gather`** (`IR_GATHER`) loads like a masked load, from an address per lane:

- **Operands:** `a` the base address, `b` the index vector and `c` the mask
  of the result's lanes. The index has one element per lane, a dword or a
  qword: its bits over the result's lanes (`ir_gather_index_valid`).
- **Flags:** the lanes, `i32` or `i64` (x86's gathers have no others). The
  `alignment_log2` place holds the scale's log2, 0 to 3, which the text
  prints as `scale=`.
- **Semantics:** lane i is the lane's bytes at `a + sext(b[i]) * scale`
  where the mask's bit i is set, and 0 where it isn't. A lane whose bit is
  clear is never read, so it can't fault.
- **The passthrough** is a `vselect` of the gather under the same mask.
  Nearly all of the sources' masked gathers merge into
  `_mm512_setzero_si512()`, which then needs no `vselect` at all, and a
  passthrough is a fourth value, which no IR instruction's direct operands
  hold.

**`compress_store`** (`IR_COMPRESS_STORE`) is laid out as `masked_store` (`a`
the address, `b` the vector, `c` the mask, its flags the lanes, its alignment
a store's). It writes the lanes whose bits are set one after another from the
address, in the lanes' order, and no other byte: the mask's population count of
lanes. For aliasing it is a store of the vector's bytes at the address.

**Truncating stores** need no opcode: the `truncate` intrinsic, then a
`masked_store` of its lanes. Only those whose truncated lanes fill 16 bytes or
more decode (a truncation's result is at least a 16-byte vector,
`ir_convert_flags_valid`); `vpmovqw [p]{k}` of a zmm is the backend's.

**Masked truncations into fewer bytes than their result** (`vpmovqb xmm{k},
zmm`: 8 bytes merged under the mask, the upper 8 zeroed) aren't a `vselect` of
the result's lanes, which would take the upper bytes from the passthrough. They
decode as `truncate(vselect(k, a, extend(w)))` (`merge_partial`): the
passthrough's lanes extended back to the source's, selected under the mask at
the source's lanes, then truncated, whose upper bytes are the truncation's 0s.
The extension is a sign extension for a signed saturation and a zero extension
otherwise, so the truncation gives each passthrough lane back unchanged. Under
the typed stream's mask of every lane (`_mm512_cvtepi64_epi8`'s, the backend's
two) it is the truncation alone.

**Passes.** Both opcodes join the masked accesses (`ir_masked_access`): their
verifiers' metadata, the text form, SCCP's roots, the dead-code roots, the
rebuild's and the dataflow's operands, Tier 1's scheduler barriers and its
operands in registers. A gather is a load's effect and a compressing store a
store's. The interpreter checks and touches each set lane's bytes alone; a
gather's masked-off lanes are 0.

**Decoder.**

- `ir_simd_typed_gather` decodes the gather builtins' rows: one record of
  `vpgatherdd` to `vpgatherqq` (or `vgatherdps` to `vgatherqpd`, the same
  bits) into VECTOR(0), the passthrough, under MASK(3) at `[INTEGER(1) +
  VECTOR(2) * scale]`.
  - The shapes are those whose index has an element per lane
    (`ir_simd_gather_forms`): every width of `dd` and `qq`, `dq` of 32 and 64
    bytes, and `qd` of 16 and 32.
  - The other two (`vpgatherdq xmm`, `vpgatherqd` into 8 bytes) read only part
    of their index's register and stay machine rows, as do AVX2's gathers by a
    vector mask.
  - The target needs AVX512F, VL where either vector is below 64 bytes (Tier
    1's EVEX moves), and DQ for a byte's mask.
- `ir_simd_typed_masked_memory` also decodes `vpcompressb` to `vpcompressq`
  to `[INTEGER(0)]` under MASK(2): VBMI2 and BW for bytes and words, VL below
  64 bytes.
- It decodes the truncations' memory forms (`vpmovwb` to `vpmovusqd`) the same
  way, their flags a truncation's, which need VL and BW for bytes' and words'
  lanes.
- Under the typed stream's mask of every lane, the lowering emits the gather
  alone, the plain store of the whole vector, and the plain store of the
  truncation.

**Tier 1.**

- A gather (`BUSTER_TIER1_VECTOR_GATHER`, `tier1_select_gather`) is
  `vpgatherdd` to `vpgatherqq` into the destination.
  - `vselect m, gather(a, b, m), w` of a gather whose only use it is folds
    into it, the destination taking w first unless it holds it. A gather
    alone, or merged into a zero vector, has its destination zeroed (`vpxor`):
    its masked-off lanes are 0, and the instruction doesn't wait on the
    register's last writer.
  - The instruction clears its mask register as it completes. Tier 1 has no
    scratch opmask register (the row path borrows them), so the mask is kept
    in the general scratch register around it: `kmovw r11d, k` and back, two
    instructions of 16 bits for up to 16 lanes.
  - Its destination may not be its index's register: an index the allocation
    put there moves into the vector scratch register first.
- A compressing store is `TIER1_RULE_FLOAT_STORE`'s masked move, `operation`
  1: `vpcompressb` to `vpcompressq [p]{k}`.
- A `masked_store` of a `truncate` whose only use it is folds into the
  truncation's memory form, `operation` 2: one `vpmov* [p]{k}`, as the row was.

**Zen 5 rows.** The 45 forms Tier 1 now emits were measured on the 9700X
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-gathers-1840-2026-11-14)):

| Forms | Latency | Throughput | Macro-ops |
| --- | ---: | ---: | ---: |
| `vpgather*` of 16 bytes (`dd`, `qd` by a ymm, `qq`) | 16 to 18.5 | 3 to 4 | 13 to 19 |
| `vpgather*` of 32 bytes (`dd`, `dq` by an xmm, `qd` by a zmm, `qq`) | 17.4 to 25 | 4 to 6.75 | 19 to 35 |
| `vpgatherdq zmm`, `vpgatherqq zmm` (`vpgatherdd zmm`: 30.1, 12.6) | 22.6, 24.75 | 7, 7 | 38, 40 |
| `vpcompress* [p]{k}` (the register form: 1 a cycle, 2) | — | 3 | 7 to 8 |
| `vpmov* [p]{k}` | — | 0.5 | 2 |

The memory compress is no Zen 4-like slow path: at one every 3 cycles it stays
Tier 1's selection, one instruction, where a register compress, the mask's
population count into a prefix mask and a masked store would take five or six
instructions.

Machine rows at `--tier1 -march=znver5`, both compilers compiling this change's
sources, against main `8023523`:

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_backend.c` | 93 | 1 |

The one left is the scatter, whose four values (base, index, vector, mask)
need a fourth operand no IR instruction has.

The tests:

- **`test-ir-parse`:** `tests/ir/gathers.ir` round-trips: every gather shape
  but the 64-byte `qd`, three scales, and compressing stores of three lanes.
- **`test-ir-simd`:** 80 gather rows (20 shapes, both kinds, 4 scales), 12
  compressing stores and 24 truncating stores decode at exactly their features,
  each refused without a feature it needs and under 7 changes; the two partial
  gather shapes stay machine rows. Every masked truncation builtin's row
  decodes, those whose lanes leave their result's upper bytes zero as
  `merge_partial`'s.
- **`test-tier1-masks`:** 10 gathers (every integer shape, each at a scale of
  its own), 6 compressing stores and 24 truncating stores run under a mask from
  the input, through both lowering paths, the interpreter at every stage and
  Tier 1 natively, against C's lanes. Each gather program's third statement
  has a constant mask of every lane, and its fourth statement's index points
  far past every buffer in each masked-off lane, so the interpreter's binding
  checks prove such a lane is never read. Each store program's fourth statement
  ends at the output's end. The conversions' programs now take every masked
  truncation, the partial ones among them, under an input mask and under one of
  every lane.
- **The compiler itself:** its backend unit, built by `--tier1` with these
  operations, linked with the clang-built rest, gives the clang-built
  compiler's results on `test-ir`, `test-tier1-masks`, `test-tier1-select`,
  `test-tier1-pipeline`, `test-ssa-ir`, `test-ir-parse`, `test-tier1-cfg` and
  `test-dataflow`, and the same object for `--tier1 -c main_unit_backend.c`.

## One lane's moves and movemasks (#1840)

Three more `intrinsic` kinds move one lane between a vector and a general
register, or gather the lanes' sign bits:

```text
%6 = intrinsic.i32 %3 lane_extract 2
%10 = intrinsic.v4i32 %3, %1 lane_insert 14
%12 = intrinsic.i32 %3 movemask 0
```

- **`lane_extract`** (unary): a's lane, zero-extended into an `i32` (a lane of
  1, 2 or 4 bytes) or an `i64` (8 bytes). Its flags hold the lane's bytes' log2
  in bits 0 and 1 and its index in bits 2 to 7, below the vector's lanes.
- **`lane_insert`** (binary): a, of the result's type, with that lane replaced
  by b's low bits. b is an `i32`, or an `i64` for a quadword. Same flags.
- **`movemask`** (unary): an `i32` whose bit i is the sign bit of a's lane i, a
  of 16 or 32 bytes. Its flags are the lanes' bytes' log2: 0, 2 or 3.
- **Model:** `IR_SIMD_LANE_EXTRACT`, `_LANE_INSERT` and `_MOVEMASK`. A scalar
  result is the model's word 0, its shape none, as a shift's count is. The
  interpreter runs the scalar-result kinds through the model too.
- **Passes:** SCCP leaves them overdefined, and GVN and LICM treat them as
  impure (neither merges nor moves them). Folding them is left for later.

**Decoder** (`ir_simd_typed_lane`):

- An extraction is `vmovd`/`vmovq` of lane 0, or `vpextrb` to `vpextrq` of a
  lane of the low 16 bytes. A lane past them takes `vextracti32x4` into
  VECTOR(1) first. Its integer is the lane's bytes, or 4 for a narrower lane
  (the instruction zero-extends); the lowering narrows the kind's `i32` to it.
- An insertion is `vpinsrb` to `vpinsrq` into a 16-byte vector. A narrower
  integer is zero-extended to the kind's `i32` first.
- A movemask is `vpmovmskb`, `vmovmskps` or `vmovmskpd` of a 16- or 32-byte
  vector.
- The target needs the records' features and the vector's moves.

**Tier 1** selects each as a `TIER1_RULE_VECTOR` kind (`tier1_select_lane`),
the scalar results' destination a general register:

- `vmovd` or `vmovq` for lane 0 of 4 or 8 bytes, else `vpextr*`, after
  `vextracti32x4` into the vector scratch register for a lane past the low 16
  bytes.
- `vpinsr*` in place in a 16-byte vector's low lane; otherwise through the
  scratch register and `vinserti32x4`.
- `vpmovmskb`, `vmovmskps` or `vmovmskpd`.
- The integer intrinsics' rule now stops at `smulo`, so these kinds can't be
  taken for BMI instructions.

The 14 forms that had no Zen 5 row (`vpextrb` to `vpextrq`, `vpinsrb` to
`vpinsrq`, and the three movemasks at 16 and 32 bytes) were measured on the
9700X ([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-lane-moves-1840-2026-11-11)):

- **`vpextrb` to `vpextrq`:** 6.5 cycles of latency (derived across the
  register files), 2 per cycle, two macro-ops.
- **`vpinsrb` to `vpinsrq`:** 4.5 cycles, 1 per cycle, two macro-ops.
- **Movemasks:** 4.5 cycles of an xmm register and 6.5 of a ymm one, 2 per
  cycle, one macro-op.

Machine rows at `--tier1 -march=znver5`, both compilers (Debug builds)
compiling this change's sources, against the masked loads and stores
(`3b4e02cc`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 38 | 15 |
| `main_unit_backend.c` | 218 | 193 |

The tests:

- **`test-ir`:** both verifiers admit the three kinds exactly at a lane below
  the vector's lanes, with the scalar of the lane's width, and a movemask's
  flags 0, 2 or 3 of a 16- or 32-byte vector, at every width and all 256 flags.
  The model gives C's lane, replaced lane and sign bits on random vectors.
- **`test-ir-parse`:** `tests/ir/lane-moves.ir` round-trips.
- **`test-ir-simd`:** the 246 rows (210 extractions over every lane of every
  part and width, 30 insertions, 6 movemasks) decode at exactly their features.
  Each is refused without each feature it needs, and under 4 changes.
- **`test-tier1-masks`:** 50 rows run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against C's lanes: 36
  extractions, 8 insertions, 6 movemasks.
- **Native check:** a C file of `_mm_extract_epi*`, `_mm_insert_epi*`,
  `_mm*_movemask_*` and `_mm_cvtsi128_si*`, compiled by `--tier1
  -march=x86-64-v4`, matches clang's output; its 17 machine rows are all gone.

## Reductions (#1840)

One more unary `intrinsic` kind, `reduce` (`BUSTER_IR_INTRINSIC_REDUCE`), reduces
a vector's doublewords or quadwords:

```text
%6 = intrinsic.v4i32 %3 reduce 14
%9 = intrinsic.i64 %8 lane_extract 3
```

- **Operands:** a is a vector of 16, 32 or 64 bytes; the result is a 16-byte
  vector (`v4i32`), every lane of which holds the reduction of all of a's lanes.
- **Flags:** the lanes' bytes' log2 in bits 0 and 1 (2 or 3) and the operation
  in bits 2 to 5 (`BUSTER_IR_REDUCE_*`): the unsigned (0) and signed (1)
  minimum, the unsigned (2) and signed (3) maximum, as `lane_op`'s, then the
  wrapping sum (4) and product (5), and the and (6), or (7) and xor (8).
- **Why a vector result:** clang's code halves the vector (`vextracti64x4`,
  `vextracti32x4`, then `vpshufd 0x4e` and, of doublewords, `0xb1`), each half
  combined with the other. That leaves the result in every lane of the last 16
  bytes, and `lane_extract`'s lane 0 (`vmovd`, `vmovq`) then takes it. The
  front end splits a doubleword reduction of 64 bytes into two rows (nine
  records); its first row is exactly this vector.
- **Model and passes:** `IR_SIMD_REDUCE`. SCCP folds a constant vector's
  reduction, and GVN and LICM take it as pure and total.

**Decoder** (`ir_simd_typed_reduce`): a row of one vector into an integer of
the lane's bytes, every halving to the lane and then `vmovd` or `vmovq` of lane
0, is `reduce` followed by `lane_extract`. The same halvings alone into a
16-byte vector are `reduce`. The combining forms are `ir_simd_reduce_form`'s:
`lane_op`'s minima and maxima, `vpaddd`/`vpaddq`, `vpmulld`/`vpmullq`, and the
quadwords' `vpandq`, `vporq` and `vpxorq` whatever the lanes, as
`check_vector_elementwise_forms` gives them. Bytes and words, whose last
halvings shift rather than shuffle, stay machine rows. The producer's rows
moved with the elementwise forms to `c_check_program_vector_convert.c`
(`check_vector_reduce_shape_rows`), so that the backend unit's tests read them.

**Tier 1** selects it as a `TIER1_RULE_VECTOR` kind (`tier1_select_reduce`):
each halving moves the high half into the vector scratch register and
combines both into the destination. A 64-byte doubleword reduction has eight
records, so a node's room (`BUSTER_TIER1_SELECT_RECORDS_PER_NODE`) grew from 6
to 8. Every form already had its Zen 5 row.

Machine rows at `--tier1 -march=znver5`, both compilers (Debug builds)
compiling this change's sources, against main (`a221f633`):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 15 | 6 |
| `main_unit_backend.c` | 193 | 162 |

`test-checker-driver`'s machine-rows fixture goes from 13 rows to 9, so its
floor drops from 10 to 5.

The tests:

- **`test-ir`:** both verifiers admit `reduce` exactly at a `v4i32` result, of
  a vector of each width, at every flags byte whose lanes are doublewords or
  quadwords and whose operation is one of the nine. The model gives C's
  reductions on random vectors with lanes at the signed and unsigned edges.
- **`test-ir-parse`:** `tests/ir/reductions.ir` round-trips.
- **`test-ir-simd`:** the producer's 54 reductions (min and max of both signs
  and the five others, of doublewords and quadwords at 3 widths) decode at
  exactly their features, the 9 split ones' second rows as `lane_extract`. Each
  is refused without each feature it needs and under 6 changes.
- **`test-tier1-masks`:** the 54 run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against C's reductions.
## The lanes' unary operations (#1840)

One more unary `intrinsic` kind, `lane_unary` (`BUSTER_IR_INTRINSIC_LANE_UNARY`),
computes one of three things of each of a vector's integer lanes:

```text
%6 = intrinsic.v16i32 %3 lane_unary 7
```

- **Operands:** a is of the result's type, a vector of any width.
- **Flags:** the lanes' bytes' log2 in bits 0 and 1, and the operation in bits
  2 and 3 (`BUSTER_IR_LANE_UNARY_*`):
  - **0, population count:** each lane's set bits, of any lanes (`vpopcntb` to
    `vpopcntq`: BITALG for bytes and words, VPOPCNTDQ for the others);
  - **1, leading zeros:** the lane's bits for a zero lane, of doublewords and
    quadwords (AVX-512CD's `vplzcntd`, `vplzcntq`);
  - **2, conflicts:** bit j set for each lane j below it that equals it, of
    doublewords and quadwords (`vpconflictd`, `vpconflictq`).
- **Model and passes:** `IR_SIMD_LANE_UNARY`. SCCP folds a constant vector's,
  and GVN and LICM take it as pure and total.

**Decoder** (`ir_simd_typed_lane_unary`):

- One record of `ir_simd_lane_unary_forms`' from VECTOR(0) into it is the kind
  itself. These rows are clang's `__builtin_elementwise_popcount`, its one-operand
  `__builtin_elementwise_clzg`, and the x86 builtins' `_mm*_popcnt_*`,
  `_mm*_lzcnt_*` and `_mm*_conflict_*`.
- `__builtin_elementwise_clzg` of two operands is three records: the count into
  VECTOR(2), `vptestnm` of VECTOR(0) into k1, then `vpblendm` of VECTOR(2) and
  VECTOR(1) under k1. It decodes to the kind, then a `vselect` of the second
  operand where a `vcmp` finds the first's lanes equal to zero.

**Tier 1** selects the one instruction back (`BUSTER_TIER1_VECTOR_LANE_UNARY`).

**Zen 5:** the 24 forms had no row. They were measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-lane-unary-1840-2026-11-12)):

| Forms | Latency | Throughput |
| --- | ---: | ---: |
| population counts and leading zero counts, every width | 2 cycles | 2 per cycle |
| conflicts of an xmm register | 3 cycles | 2 per cycle |
| conflicts of a ymm or zmm register | 7 cycles, two macro-ops | 1 per cycle |

Machine rows at `--tier1 -march=znver5`, both compilers (Debug builds)
compiling this change's sources, against main (`d96b2e73`, with the reductions):

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 6 | 1 |
| `main_unit_backend.c` | 162 | 141 |

`test-checker-driver`'s machine-rows unit goes from 9 rows to 7.

The tests:

- **`test-ir`:** both verifiers admit `lane_unary` exactly of its own type at
  every flags byte and width. The model gives C's counts and conflicts on random
  vectors whose lanes repeat or are zero.
- **`test-ir-parse`:** `tests/ir/lane-unary.ir` round-trips.
- **`test-ir-simd`:** the 30 rows (popcounts of 4 lane sizes, both leading zero
  counts, the conflicts, at 3 widths) decode at exactly their features. Each is
  refused without each feature it needs and under 6 changes.
- **`test-tier1-masks`:** the 30 run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against C. The population
  counts' target adds BITALG and VPOPCNTDQ to x86-64-v4's; a host that runs
  Tier 1's code natively without them leaves those 12 to `test-ir-simd`.
## Floating operations at their own rounding (#1840)

One more binary `intrinsic` kind, `fround` (`BUSTER_IR_INTRINSIC_FROUND`), is
a floating operation of two 64-byte vectors' lanes at a rounding of its own,
its exceptions suppressed:

```text
%3 = intrinsic.v16i32 %2, %1 fround 13
```

- **Operands:** a and b are of the result's type, which is `v16i32`: AVX-512
  embeds a rounding in its 64-byte register forms alone.
- **Flags:** the operation in bits 0 and 1 (`a + b`, `a - b`, `a * b`,
  `a / b`), the lanes in bit 2 (floats, doubles) and the rounding in bits 3 and
  4 (to nearest, down, up, toward zero: EVEX's and MXCSR's order).
- **Semantics:** each lane is x86's packed operation's in that rounding,
  whatever MXCSR holds, and sets none of its flags. So the kind reads no
  floating environment.
- **Model:** `IR_SIMD_FROUND` (`ir_simd_float_rounded`) runs the host's scalar
  SSE instruction (`addss` to `divsd`) with MXCSR's rounding set to the flags',
  then restores MXCSR and its flags. That is the lane exactly as the packed form
  computes it, NaNs included; the compiler runs on x86-64 alone.
- **Passes:** SCCP folds two constant vectors by the model. GVN and LICM take it
  as pure and total, since it reads no MXCSR and writes none of it.

**Decoder** (`ir_simd_typed_fround`): the builtins' `_mm512_add_round_ps` to
`_mm512_div_round_pd` with `_MM_FROUND_NO_EXC` and a direction are one record of
`ir_simd_fround_forms`' `_ER` form, VECTOR(0) in vvvv and VECTOR(1) in rm into
VECTOR(0), its decorations the rounding alone. With `_MM_FROUND_CUR_DIRECTION`
they are the ordinary forms, the IR's `fadd` to `fdiv`.

**Tier 1** selects the same record (`BUSTER_TIER1_VECTOR_FROUND`), the rounding
in its decorations. Seven of the eight `_ER` forms had no Zen 5 row; they were
measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-fround-1840-2026-11-12)), as
their forms without rounding are: `vaddpd`, `vsubps` and `vsubpd` 2 cycles,
`vmulps` and `vmulpd` 3, `vdivps` 10 and `vdivpd` 13, 2 per cycle for the
first five, and a `vdivps` every 2.5 cycles and a `vdivpd` every 4.

Machine rows at `--tier1 -march=znver5`, both compilers (Debug builds)
compiling this change's sources, against main (`91a87ab2`), with the
truncating conversions below:

| Unit | Rows before | Rows after |
| --- | ---: | ---: |
| `main_unit_frontend.c` | 6 | 6 |
| `main_unit_backend.c` | 162 | 136 |

The tests:

- **`test-ir`:** both verifiers admit `fround` exactly of two 64-byte vectors
  of its type at every flags byte below 32. The model is checked against C's
  operation in that rounding mode (`host_fesetround`, volatile operands) on
  random lanes, ties, overflow, underflow, zeros, infinities and NaNs, and
  leaves MXCSR as it was.
- **`test-ir-parse`:** `tests/ir/fround.ir` round-trips.
- **`test-ir-simd`:** the 32 rows (4 operations, 2 lane sizes, 4 roundings)
  decode at exactly AVX512F, refused without it and under 6 changes.
- **`test-tier1-masks`:** the 32 run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against C in each rounding
  mode, so that the processor checks the model.

## Truncating conversions of lane 0 (#1840)

One more unary `intrinsic` kind, `cvtt` (`BUSTER_IR_INTRINSIC_CVTT`), converts a
16-byte vector's lane 0 into an integer as x86 does:

```text
%4 = intrinsic.i64 %1 cvtt 3
```

- **Flags:** bit 0 makes the lane a double (else a float), bit 1 the result an
  `i64` (else an `i32`), which the verifier checks against the type.
- **Semantics:** rounded toward zero. A NaN or a value past the result's range
  gives its smallest value, x86's integer indefinite, where C's `fptosi` (and
  the interpreter's) would be undefined. So the builtins' rows can't be the
  IR's `fptosi`.
- **Model:** `IR_SIMD_CVTT`, a scalar result in word 0, as `lane_extract`'s.
  SCCP leaves it overdefined, and GVN and LICM don't move it, as the lane moves.

**Decoder** (`ir_simd_typed_cvtt`): `_mm_cvttss_si32` to `_mm_cvttsd_si64` are
one record of `vcvttss2si` or `vcvttsd2si`'s EVEX form, INTEGER_RESULT in reg
and VECTOR(0) in rm. **Tier 1** selects the same record into the general
destination (`BUSTER_TIER1_VECTOR_CVTT`). The four EVEX forms had no Zen 5 row;
they were measured on the 9700X in the same record: 7.5 cycles of latency
across the register files, as the legacy forms', 2 per cycle, two macro-ops.

Machine rows: 10 of the 26 the table above counts in `main_unit_backend.c`
are these conversions.

The tests:

- **`test-ir`:** both verifiers admit `cvtt` exactly of a `v4i32` into its
  flags' integer. The model gives x86's results at both ranges' edges, for
  infinities and for NaNs, against bounds written out in long double.
- **`test-ir-parse`:** `tests/ir/fround.ir`'s second function.
- **`test-ir-simd`:** the four rows decode at exactly their features, refused
  without AVX512F and under 5 changes.
- **`test-tier1-masks`:** the four run through both lowering paths, the
  interpreter and Tier 1 natively on values of every magnitude around both
  ranges, infinities and NaNs.

## Bit shuffles into a mask (#1840)

One more binary `intrinsic` kind, `bitshuffle` (`BUSTER_IR_INTRINSIC_BITSHUFFLE`),
picks a bit for each byte of a vector, into a mask:

```text
%6 = intrinsic.m64 %3, %4 bitshuffle
```

- **Operands:** a and b are vectors of one type, any width. The result is the
  mask of their bytes' lanes (`m16`, `m32` or `m64`). Its flags are 0.
- **Semantics:** bit i is the bit of a's quadword i / 8 that b's byte i's low
  6 bits name (`vpshufbitqmb`, BITALG).
- **Model and passes:** `IR_SIMD_BITSHUFFLE`. The interpreter runs it as it runs
  a comparison into a mask. SCCP folds two constant vectors by the model, and
  GVN and LICM take it as pure and total.

**Decoder** (`ir_simd_typed_bitshuffle`): the vpshufbitqmb builtins' rows
(`__builtin_ia32_vpshufbitqmb128_mask` to 512's) as the opmask conversion
leaves them, as a comparison into a mask is:
- one record of the form at the vectors' width, `VECTOR(0)` in vvvv and
  `VECTOR(1)` in rm, into `MASK_RESULT` under `MASK(2)`;
- the IR's `bitshuffle`, ANDed with the writemask, the third operand, which
  the lowering leaves out where the typed stream's writemask has every lane
  (`ir_typed_unmasked`);
- the target needs BITALG, BW, AVX512F and VL below 64 bytes.

**Tier 1** (`BUSTER_TIER1_VECTOR_BITSHUFFLE`): `vpshufbitqmb` into the
destination's opmask register. A `bitshuffle` ANDed with another mask, as its
only use, is `vpshufbitqmb` under that mask (`{k}`), as #1847 folds a
comparison's AND (`tier1_select_masked_compare` takes both).

**Zen 5:** the 6 forms (register and masked, at 3 widths) had no row. They were
measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-bitshuffle-quadword-1840-2026-11-13)):
3 cycles at 16 bytes, 4 at 32 and 6 at 64, 2 per cycle, the same under a
writemask.

Machine rows at `--tier1 -march=znver5`, against main (`26f0bb0e`), with the
byte shuffles and the carriers above: `main_unit_backend.c` 132 → 105. No
`vpshufbitqmb` row is left.

The tests:

- **`test-ir`:**
  - both verifiers admit `bitshuffle` exactly of two vectors of one type into
    their bytes' mask, at flags 0;
  - they refuse another width's b, another mask and a vector result;
  - the model is checked against the instruction's definition on random
    vectors.
- **`test-ir-parse`:** `tests/ir/bitshuffle.ir` round-trips.
- **`test-ir-simd`:** the 3 rules' rows decode at exactly their features, each
  refused without each and under 6 changes.
- **`test-tier1-masks`:** the 3 run, under an input word's writemask and under
  every lane, through both lowering paths, the interpreter at every stage and
  Tier 1 natively. Both paths' code has the form's 8 records and no `kand`.
  With the fold left out, the 8 `kand`s fail it.

## Quadword operations (#1840)

One more binary `intrinsic` kind, `quadword_op` (`BUSTER_IR_INTRINSIC_QUADWORD_OP`),
computes each quadword of the result from the bytes of a and b in it:

```text
%3 = intrinsic.v4i32 %1, %2 quadword_op 0
```

- **Operands:** a and b are of the result's type, any width.
- **Flags:** the operation (`BUSTER_IR_QUADWORD_*`):
  - **0, multishift:** byte j of each quadword is the 8 bits of b's quadword
    from the bit a's byte j's low 6 bits name, wrapping around the quadword
    (`vpmultishiftqb`, VBMI);
  - **1, sum of absolute differences:** each quadword is the sum of
    |a's byte − b's byte| over its 8 unsigned bytes (`vpsadbw`).
- **Model and passes:** `IR_SIMD_QUADWORD_OP`. SCCP folds two constant vectors
  by the model, and GVN and LICM take it as pure and total.

**Decoder** (`ir_simd_typed_quadword_op`): the builtins'
`__builtin_ia32_vpmultishiftqb128` to 512 and `__builtin_ia32_psadbw128` to 512
are one record of `ir_simd_quadword_op_forms`' form at the result's width:
`VECTOR(0)` in vvvv and `VECTOR(1)` in rm into `VECTOR(0)`. The target needs BW
(and VBMI for the multishifts), AVX512F and VL below 64 bytes.

**Tier 1** (`BUSTER_TIER1_VECTOR_QUADWORD_OP`) selects the same form back.

**Zen 5:** the 6 forms had no row. They were measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-bitshuffle-quadword-1840-2026-11-13)):
`vpmultishiftqb` and `vpsadbw` take 3 cycles at every width and run 2 per cycle.

Machine rows at `--tier1 -march=znver5`, against main (`26f0bb0e`), with the
families above: `main_unit_backend.c` 105 → 96. No `vpmultishiftqb` or
`vpsadbw` row is left.

The tests:

- **`test-ir`:** both verifiers admit `quadword_op` exactly of two vectors of
  the result's type at flags 0 and 1, at every width. The model is checked
  against both instructions' definitions, written out bit by bit, on random
  vectors.
- **`test-ir-parse`:** `tests/ir/quadword-op.ir` round-trips.
- **`test-ir-simd`:** the 6 rules' rows decode at exactly their features, each
  refused without each and under 6 changes.
- **`test-tier1-masks`:** the 6 run through both lowering paths, the
  interpreter at every stage and Tier 1 natively, against the definitions. The
  multishifts' target adds VBMI to x86-64-v4's. A host that runs Tier 1's code
  natively without it leaves them to `test-ir-simd`.

## Funnel shifts and rotates (#1840)

One more ternary `intrinsic` kind, `funnel` (`BUSTER_IR_INTRINSIC_FUNNEL`), is
the funnel shift of each lane of two vectors by a third's:

```text
%6 = intrinsic.v16i32 %3, %4, %5 funnel 6
```

- **Operands:** a (the high halves), b (the low halves) and the third operand
  (the counts) are of the result's type. The third is in the extra array, as
  the other ternary kinds' are.
- **Flags:** the lanes' bytes' log2 in bits 0 and 1 (1 to 3, words to
  quadwords) and the direction in bit 2 (`BUSTER_IR_FUNNEL_RIGHT`):
  - **left:** the high half of a:b shifted left (C's `fshl`, `vpshldvw` to
    `vpshldvq`);
  - **right:** its low half shifted right (`fshr`, `vpshrdvw` to `vpshrdvq`).
  The count is taken modulo the lanes' bits. A rotate is the funnel of one
  vector twice.
- **Numbering:** the ternary kinds are a range of kind numbers, so `funnel`
  takes 30, the old `aes`. `aes` to `quadword_op` each move up by one (31 to 40).
- **Model and passes:** `IR_SIMD_FUNNEL`. SCCP folds three constants, and GVN
  and LICM take it as pure and total, as the other ternary kinds.

**Decoder** (`ir_simd_typed_funnel`):
- **clang's `__builtin_elementwise_fshl` and `fshr`** of words to quadwords,
  three vectors, as `c_check_program_vector_builtins.c` writes them:
  - fshl's one `vpshldv` into `VECTOR(0)` (a) from `VECTOR(1)` by `VECTOR(2)`;
  - fshr's `vpshrdv` into `VECTOR(1)` (b) from `VECTOR(0)` by `VECTOR(2)`,
    then `VECTOR(1)` moved into `VECTOR(0)`;
  - the target needs VBMI2 and BW (their target rows'), AVX512F and VL below 64
    bytes.
- **The rotates by an immediate:** `__builtin_ia32_prold`, `prolq`, `prord`
  and `prorq` at each width. One `vprold` to `vprorq` of `VECTOR(0)` is the
  funnel of the vector twice by a constant, the immediate modulo the lanes'
  bits in every lane. The target needs AVX512F and VL below 64 bytes.

**Tier 1** (`BUSTER_TIER1_VECTOR_FUNNEL`, `tier1_select_funnel`):
- **A rotate by an immediate:** where a and b are one value and the counts a
  constant of one value in every lane (`tier1_select_lane_splat`), the first
  candidate, the counts left unused. It is `vprold` to `vprorq`, or VBMI2's
  `vpshldw` or `vpshrdw` of the vector twice for words.
- **Otherwise** `vpshldv` or `vpshrdv` by the counts' register. Their
  destination is their first source: the high half for a left shift, the low
  half for a right one. The destination takes that half first by a move where
  it isn't already its register. Where it holds the other half or the counts,
  the vector class's scratch register keeps them across that move.

**Zen 5:** the 36 forms had no row: the 18 variable funnels, the 12 rotates
and the 6 word funnels by an immediate. They were measured on the 9700X ([the
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-funnel-1840-2026-11-13)):
every one takes 2 cycles and runs 2 per cycle.

Machine rows at `--tier1 -march=znver5`, against main (`26f0bb0e`), with the
families above: `main_unit_backend.c` 96 → 92. The 3 `vpshrdvd` rows and the
`vprold` row are gone.

The tests:

- **`test-ir`:**
  - both verifiers admit `funnel` exactly of three vectors of the result's
    type, at flags 1 to 3 and 5 to 7;
  - the model is checked against C's `fshl` and `fshr`, written out a bit at a
    time, on random vectors whose counts are often zero or past the lanes'
    bits.
- **`test-ir-parse`:** `tests/ir/funnel.ir` round-trips.
- **`test-ir-simd`:** the 18 funnels' rows and the 4 rotate rules at 3 widths
  and 4 immediates decode at exactly their features. Each is refused without
  each and under 6 changes.
- **`test-tier1-masks`:** the 18 funnels and the 72 rotates (6 immediates each)
  run through both lowering paths, the interpreter at every stage and Tier 1
  natively, against C.
  - A rotate's code by both paths has its immediate form's 4 records.
  - The funnels' target adds VBMI2 to x86-64-v4's. A host without it, running
    Tier 1's code natively, leaves them to `test-ir-simd`.
  - With the scratch register's save left out, 144 of their runs fail; with a
    wrong rotate count, 576.

## Masks to vectors and back (#1840)

The masks' conversions to vectors and back need no new operation:

- `ir_simd_typed_mask_vector` decodes the MASK_VECTOR rules' rows
  (`__builtin_ia32_cvtmask2b` to `_cvtmask2q`: `vpmovm2b` to `vpmovm2q` of
  MASK(0) into VECTOR(0)) as the IR's `vselect` of the mask, all ones and zeros,
  at the elements' lanes.
- It decodes the VECTOR_MASK rules' rows (`__builtin_ia32_cvtb2mask` to
  `_cvtq2mask`: `vpmovb2m` to `vpmovq2m` of VECTOR(0) into MASK_RESULT) as the
  IR's `vcmp lt` of the signed lanes and a zero vector: the mask of the lanes'
  sign bits.

Both rows must be register-direct and undecorated, as the opmask conversion
leaves them, with a mask of a bit per lane. The target needs the forms'
features (BW for bytes and words, DQ for doublewords and quadwords), AVX512F's,
VL's below 64 bytes and the mask's moves (DQ for a byte's, BW for 32 and 64
lanes').

Tier 1 matches the two shapes back to the single instructions
(`BUSTER_TIER1_VECTOR_MASK_EXPAND` and `_SIGN_MASK`,
`tier1_select_mask_vector`). The candidate comes first, since the bottom-up
path takes its rule's first, and reads neither constant, so neither is
materialized where nothing else uses it:

- a `vselect` whose true operand is a vector constant of every bit and whose
  false one is zeros is `vpmovm2b` to `vpmovm2q` at its lanes (a float's are a
  doubleword's bits, a double's a quadword's). This includes the expansion of
  every C comparison of vectors, which was `vpblendm` of two materialized
  constants;
- a `vcmp lt` of signed integer lanes and a zero vector is `vpmovb2m` to
  `vpmovq2m`.

Their doublewords' and quadwords' forms are AVX512DQ's, which every vector
comparison of the front end needs already. At a target without it, the
emission's form check sends the function to Tier 0. The 12 `vpmov*2m` register
forms had no Zen 5 row; on the 9700X they measured 3, 4 and 6 cycles of latency
at 16, 32 and 64 bytes, 2 per cycle
([the record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/ir-shifts-1840-2026-11-11),
buster-performance#388).

Measured by `--tier1 --tier1-report -march=znver5`, both compilers compiling
#1972's sources (`2461071`), against the shifts' change:

| Unit | Rows before | Records before | Rows after | Records after |
| --- | ---: | ---: | ---: | ---: |
| `main_unit_frontend.c` | 628 | 543,620 | 596 | 543,612 |
| `main_unit_backend.c` | 792 | 1,541,683 | 784 | 1,541,683 |

No mask conversion stays a machine row (there were 32 and 8).

The tests:

- `test-ir-simd`: decodes, by the real producer, every MASK_VECTOR and
  VECTOR_MASK rule's row (12 each), checked against lanes and features written
  out. Each is refused without each feature it needs and under 6 changes.
- `test-tier1-masks`: each of the 24 rules runs as a program's only row: a mask
  from an input word into a vector, or a vector into a mask stored zero-extended.
  Each goes through both lowering paths, the interpreter at every stage and Tier
  1's native code, against a model from Intel's manual.

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
decodes a byte average's, a masked store's of no result and a shift's of a 64-bit
count from a general register as machine rows, with their classes and exactly their
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
