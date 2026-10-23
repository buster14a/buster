# Typed integer builtins and runtime expansion

The integer portion of [#148](https://github.com/buster14a/buster/issues/148)
has three consumers of one resolved `CBuiltinId`: the original independent
[constant actions](c-builtins.md), typed checking in `c_check_builtin.c`, and
runtime expansion in `c_builtin_lower.c`. The IDs cover the 18 byte-swap and
bit-operation spellings and the three generic overflow builtins.

These modules do not by themselves complete source binding, executable source
lowering, Tier 1 coverage or corpus acceptance. The source hooks are coordinated
with the active #133 owner. The preprocessor continues to advertise only
implemented source capabilities; recognition and action-level folding do not
make a builtin available to source programs.

## Typed action handoff

`check_builtins` consumes a bounded array of `CheckBuiltinAction` and writes the
corresponding `CheckBuiltinResult` array. Each action has a resolved ID, exactly
one or three original `CheckValue` operands, the actual argument count and a
stable node identity. It validates arity and real `TypeId` constraints before
forming the integer descriptors used by the fold kernel. Ordinary unary
builtins perform their declared parameter conversion. Generic overflow instead
retains each input's original integer width and signedness.

The result contains the ordinary `CheckActionResult`, a separate destination
and stored value, the fold status and an explicit store requirement. A valid
overflow action always retains that store, including when its overflow boolean
and destination bits are constant. Its boolean is `_Bool`; it is not an integer
constant expression. Volatile destination qualification remains on the store
conversion. The helper computes metadata and values; it does not write into an
input program's object.

Clang's generic overflow builtins accept non-const integer destinations,
including `_Bool` and enums. This differs from GCC's documented exclusion of
boolean and enumerated destinations. Bit-precise descriptors through 64 bits
are accepted by the action kernel; wider overflow operands are refused
explicitly. The runtime memory representation has a narrower contract below.

The input and output arrays are disjoint, naturally aligned C arrays with
exactly `count` logical entries and no padding. They must also be disjoint from
the writable type table. Zero count permits null arrays. The type table may
intern decay types in source order. Groups of at most eight ready actions feed
the existing scalar or AVX-512 fold kernel; an incomplete group flushes at the
end of every call. No buffered work survives a call. Legal partition changes
must preserve values, stores, diagnostics and operand identities.

The graph consumer should use explicit fixed-operand builtin nodes: one edge
for a unary operation, three for an overflow operation, and `members[node]` as
the resolved builtin ID. This preserves the existing graph scheduler's three
dependency columns. Overflow remains an effect barrier. Binding must recognize
the callee before ordinary unresolved-identifier diagnostics; a fabricated
function prototype would lose the generic overflow rules.

## Executable typed-stream handoff

`cbuiltin_lower` and `cbuiltin_lower_scalar` append a recipe to
`CBuiltinLowerBuffer`. `CBuiltinLowerCall` names existing local slots holding
the already evaluated arguments, plus original integer descriptors for
overflow. The source producer owns evaluating each argument exactly once and
performing the unary parameter conversions. The overflow destination is a U64
pointer local. The expander never reevaluates the source expression or aliases
its argument slots as scratch.

The successful result publishes the appended entry range, its result machine
class and a new temporary-local range. It leaves one result on the typed value
stack and preserves values already below it. Overflow stores the converted
low result through the supplied pointer before producing the boolean. Runtime
descriptors currently represent ordinary signed/unsigned 8-, 16-, 32- and
64-bit integers and unsigned one-bit booleans in byte storage. Boolean stores
retain the low mathematical bit; they do not use the ordinary nonzero-to-true
cast. Other bit-precise memory representations return an explicit unsupported
status.

The caller supplies mutually disjoint kind, class, value, local-class and
local-flag columns, writable to their declared capacities and disjoint from
the buffer and call records. Counts describe initialized prefixes and must not
exceed capacities. Every existing argument local must have the required
machine class. The expander stages a bounded recipe before publishing it;
invalid or unsupported input and insufficient entry/local capacity leave all
caller bytes and counts unchanged. Publication uses masked tails, with a
scalar copy twin. No allocations or retained scratch storage are required.

The recipes use the existing target-independent `BUSTER_TYPED_*` operations.
Byte swaps and counts use fixed shifts, masks and arithmetic. Full overflow
products use four 32-by-32 products and exact two-limb correction. Generated
programs therefore require baseline x86-64; the host's vector publication does
not grant AVX-512, POPCNT, LZCNT or TZCNT to output code. Zero arguments to clz
or ctz remain undefined by the source contract, independently of the sequence's
incidental numeric result. Zero ffs returns zero.

Tier 0 can consume the complete ordinary-width recipes. The current separate
Tier 1 pipeline only supports a subset: straight-line 32/64-bit integer and
pointer operations, fixed shifts and 32/64-bit memory accesses. Narrow stores,
signed widening and other unsupported shapes must remain explicit refusals;
Tier 0 execution cannot establish two-tier acceptance.

## Validation commands

On the approved 9700X, through a separate shared-host lock for each command:

```sh
./build/compiler test-builtins
./build/compiler test-check-builtins
./build/compiler test-builtin-lowering
```

The tests distinguish typed constraints and fold metadata from native execution
of the emitted recipes. Scalar/vector agreement is supplemented with Clang's
builtin behavior, boundary inputs, real destination writes and guarded-capacity
checks. The CI workflow registers both new suites alongside the existing
integer-action suite. Results and full source/two-tier acceptance must be
reported for the exact tested revision; this document makes no throughput
claim.

## Provenance

The modules, recipes and fixtures are original material under the project's
default license. The existing type, checker, typed-stream, encoder and ABI
interfaces are reused. Clang is an external behavior and execution oracle;
no Clang implementation or external fixture corpus is imported.
