# Vector and mask IR slice

Issue [#732](https://github.com/buster14a/buster/issues/732) supplies vector and
mask values to shared IR. The first implementation checkpoint contains the
independent `ir_simd_model.c` semantic model and its tests. Shared IR admission,
text, consumer refusals and the typed adapter are subsequent integration work;
this checkpoint alone does not complete the issue.

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
The planned shared type IDs fit in a byte; the 18 admitted types require a
checked 32-bit result admission set in place of the current 16-bit set. Type
and opcode limits must be compile-time assertions, rather than implicit shifts
or narrowing conversions.

The bounded memory oracle loads/stores the exact shape width at an explicit
offset into a caller-supplied slice. Reads, writes and ordered volatile-byte
accesses have explicit counters. Bounds and counter overflow are checked before
an access; failure preserves output and memory. No masked memory, atomic or
unordered operation is inferred from these load/store actions. Memory and value
views are disjoint; input values may alias the result. There is no allocation.

This abstract model can run on a baseline host. It does not authorize native
AVX-512 instructions in a baseline output target. The eventual typed adapter
must validate #717's X86 forms and target features independently of the compiler
host. Consumers without these semantics must reject the function before
rewriting or execution. Register assignment/spilling and native SIMD selection
remain [#179](https://github.com/buster14a/buster/issues/179) and
[#141](https://github.com/buster14a/buster/issues/141), respectively.

The original model tests compare integer arithmetic with Clang C vector
operators, selection and comparisons with independent lane expectations, and
memory effects with guarded bounded slices. They do not assert IR integration,
native Tier 1 support or a performance improvement.
