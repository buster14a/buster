# Scalar replacement of private slot fields

`ir_optimize_sroa.c` supplies the field-splitting stage of
[#139](https://github.com/buster14a/buster/issues/139). It consumes the shared
`IrFunction`; it does not introduce another aggregate representation or SSA
constructor. Include it after `ir_optimize.c` and call
`ir_optimize_sroa(function, output, scratch, ir_sroa_options(), result)`.
`IrSroaResult.function` is a fresh verified function containing typed scalar
slots. Pass that result to the existing `ssa_ir_promote` or
`ssa_ir_promote_scalar`, with a separate output arena, to obtain scalar SSA.
The split function must remain alive through promotion. This explicit
composition respects the upstream promoter's independent-arena contract.
Promotion additionally requires an entry block with no incoming edges;
typed-stream IR lowering supplies that preheader. A split function with an
entry backedge must satisfy that stronger contract before promotion.

The stage accepts only a complete, understood pointer-use graph for a private
slot. It recognizes `STACK_ADDR`, pointer copies, pointer-to-pointer bitcasts
and `ADDRESS` with a constant displacement and no index. Reachable ordinary
SSA dominance makes reverse postorder sufficient to propagate the slot and
byte offset. Checked unsigned arithmetic handles positive and negative
displacements without host overflow. Every pointer use is then examined.
Calls, returns, pointer comparisons/conversions, stored addresses, phis,
selects and unknown indexed addresses exclude the whole source slot.
Address-taken or volatile slot flags and any volatile access also exclude it.
The vector path packs observed pointer uses from the stable sparse use list
into 16-use SoA packets, then classifies memory, forwarding and escaped uses
with opcode/position/flag masks. Partial packets flush explicitly. Scalar
reduction applies escaped roots, including repeated roots in one packet.
The scalar oracle independently enumerates instruction operands. Both paths
consider the same reachable uses, and the complete scan precedes rewriting.

A field key is the source slot, byte offset and scalar type. Every access must
fit within the original slot. Distinct keys must have disjoint byte ranges;
overlapping loads/stores or different types at the same offset exclude the
whole slot. Exact repeated keys share one field. The new scalar slot has the
field's byte size and an alignment at least as strong as the original slot
and every access promise. Temporary flags survive. Unaccessed padding is not
materialized as a field.

The vector field query compares eight roots, offsets, types and byte ranges
at a time. The scalar query independently walks those fields. All columns
have exact logical bounds; each tail load is masked and a zero field count
performs no access. Full range/key comparisons establish identity and
overlap, without hashes. Collection order and output layout are identical
between paths. Field queries are quadratic in the worst case; no measured
profitability or scalability claim is made for very large aggregates.

Only after every use and overlap check succeeds are matching pointer
definitions replaced with direct typed `STACK_ADDR`s. Shared dead-code
removal removes obsolete address chains while retaining loads, stores,
faulting operations, calls and control flow. Unused original slots are
compacted, so the later promoter does not retain a dead aggregate allocation.
A slot's `lifetime` markers (#177, [the IR guide](ir.md)'s "Slot lifetimes")
refer to it as its addresses do: a split slot's markers go to its first field,
which no access reaches outside the slot's lifetime, its other fields living
throughout the function, so the split slot itself goes; an unsplit slot keeps
its markers, and a slot that only markers refer to stays, renumbered with them.
The field stage itself leaves all memory operations present. The upstream
SSA promoter owns their elimination, incoming-value selection and phi
construction. CFG, uses and dominance are rebuilt or consumed only within
their valid transformation phase; the result receives a fresh SSA verifier.

Input ownership, arena rollback and unsupported entry phis follow the
[shared optimization contract](ir-optimization.md). Failure preserves the
input, output length and result descriptor, and scratch is always restored.
Neither the field stage nor the SSA adapter is called with input columns
owned by its output or scratch arena.

`test-ir-sroa [--seed N]` is `run_ir_sroa_tests(argc, argv)`, with the command
in `argv[0]`. Include it after `ir_optimize_sroa.c`,
`ir_optimize_test.c` and upstream `ssa_ir.c`. The tests compare exact scalar
and vector splitting, guarded interval queries, partial allocation rollback,
and both upstream promotion algorithms. An independent byte-memory fixture
interpreter checks returned values and faults for direct fields, pointer
copies, checked negative offsets, randomized gaps, conditional stores and
loops. Private nonescaping stores are excluded from the observable trace
when comparing promoted results; refused volatile/escaped cases preserve
their original trace. Accepted fixtures end with no memory accesses or
residual aggregate slots after promotion.

Memcopies, partial/overlapping field splitting, dynamic array indexing,
pointer phi/select analysis and aggregate call ABI handling remain outside
this conservative implementation. Target-host compile-time, stack-size and
generated-code measurements remain pending.

The implementation is original. LLVM's [pass documentation](
https://llvm.org/docs/Passes.html#sroa-scalar-replacement-of-aggregates) was
consulted for the field-splitting followed by scalar-promotion model. No
implementation source was imported.
