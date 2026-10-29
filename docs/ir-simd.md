# Vector and mask IR slice

Issue [#732](https://github.com/buster14a/buster/issues/732) supplies vector and
mask values to shared IR. The current implementation includes shared types,
scalar/vector verification, canonical text, explicit consumer refusals and a
bounded independent execution oracle. The typed adapter is subsequent
integration work; this checkpoint alone does not complete the issue.

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
This decoder does not yet translate a complete VECTOR/X86 typed function; that
handoff remains coordinated with the active #131 corpus-adapter owner.

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
shape zero. Only the explicit SIMD oracle uses `IrSimdValue`'s eight words.
Ordinary IR constants and `InterpreterValue` retain their two-word layouts.
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
AVX-512 instructions in a baseline output target. The eventual typed adapter
must validate #717's X86 forms and target features independently of the compiler
host. The ordinary interpreter, SSA promoter, SCCP/DCE/CFG, GVN, LICM, SROA,
inliner and native Tier 1 selection reject the whole function before rewriting
or execution, including SIMD in unused ABI/slot metadata. Register
assignment/spilling and native SIMD selection
remain [#179](https://github.com/buster14a/buster/issues/179) and
[#141](https://github.com/buster14a/buster/issues/141), respectively.

The original model tests compare integer arithmetic with Clang C vector
operators, selection and comparisons with independent lane expectations, and
memory effects with guarded bounded slices. `test-ir` exercises actual IR
execution, malformed types/predicates/SSA/effects and sparse storage;
`test-ir-parse` roundtrips the hand-written vector/mask fixture;
`test-ir-simd` checks transactional unsupported-consumer refusals in both
verification modes. These tests establish no native Tier 1 support or
performance improvement.
