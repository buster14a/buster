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
Mask logic is limited to bitwise operations, never integer arithmetic.

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
replaced. The SSA promoter, SCCP/DCE/CFG, GVN, LICM, SROA and inliner continue to
reject the whole SIMD function before rewriting, including SIMD in unused
ABI/slot metadata. Native Tier 1 selection rejects a function with vectors, which
are [#141](https://github.com/buster14a/buster/issues/141)'s, and takes masks:
[#179](https://github.com/buster14a/buster/issues/179) selects their operations
and allocates them in the opmask class ([the selection
guide](tier1-selection.md)'s "AVX-512's masks").

The original model tests compare integer arithmetic with Clang C vector
operators, selection and comparisons with independent lane expectations, and
memory effects with guarded bounded slices. `test-ir` exercises actual IR
execution, malformed types/predicates/SSA/effects and sparse storage;
`test-ir-parse` roundtrips the hand-written vector/mask fixture;
`test-ir-simd` checks transactional unsupported-consumer refusals in both
verification modes. These tests establish no native Tier 1 support or
performance improvement.

## Integration fixtures and consumer handoff

`test-ir-lowering` exercises 156 actual #717 producer cases: three widths, signed
and unsigned comparisons, thirteen operations and both conditional arms. Both
adapter paths must agree column-for-column, including sparse constants. Shared
interpretation is checked against independent ordinary-C lane formulas and exact
memory canaries; the same typed programs pass through both Tier 0 compilers and
encoders, and run natively only when the machine has the required features.
Unavailable native execution is reported, not counted as passed execution.

`test-ir-interpreter` additionally checks simultaneous vector phi cycles, logical
masks through 64 bits, unused high mask bits, undefined selection alternatives,
128-bit bitcasts, unaligned volatile memory, short/read-only memory refusal and
step/value budgets. The original straight-line executor remains an independent
consumer; the shared interpreter is not routed through it.

`test-ir-source --simd -march=x86-64-v4 tests/ir/vector-i32.c` drives real C through
the checker, typed producer, both adapters and both text parsers. This explicit
mode also requires transactional SIMD refusal by both SSA promoters; it does not
count those functions as promoted. Without `--simd`, source-corpus acceptance
continues to require promotion. Source preparation retains each function's
selected output-target features rather than inferring them from the host.

For #141, the handoff is verified shape-qualified IR with explicit `vcmp`/`vselect`
and wide memory transports. The selector's refusal of vectors remains in place.
For #179, masks have explicit `m8`/`m16`/`m32`/`m64` types and exact-bit semantics,
which Tier 1 keeps in the opmask registers (k1 to k7, k0 the class's scratch), moved
and spilled at their widths; no ABI passes one. This change makes no native Tier 1
quality or throughput claim.
