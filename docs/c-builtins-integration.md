# Typed builtin checking and runtime expansion

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

The optional `tag_completions`, `tag_completion_count` and `source_position`
fields retain the source-time completion boundary. The checker must not treat
an enum as complete merely because a later declaration completed its table
entry. Node identities are diagnostic locations, not source positions. A null
completion slice deliberately selects the current-table contract for direct
action callers. The extension mask travels with each action.

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

## Shared names and feature queries

`c_builtin_catalog.c` supplies exact bounded-name resolution and stable IDs
for the integer actions and the remaining source families. The original
integer IDs and their sentinel retain their values. Each catalog entry keeps
folding, source checking, source constant evaluation, Tier 0 and Tier 1
capabilities separate. A standalone checker or runtime test must not set the
source capability. Source availability requires the source path and both
runtime consumers; constexpr additionally requires source constant evaluation.

The preprocessor uses this inventory in its normal implemented mode and keeps
its separate Clang compatibility mode. Until source and two-tier acceptance
are established, these new entries remain unavailable to `__has_builtin`.
`test-preprocessor` checks the exact catalog and the advertised list, including
raw macro names, unknown bounded names and guarded input boundaries.

## Varargs contracts

`c_check_varargs.c` takes the canonical target `va_list` type and explicit
function context. It checks the source-time type of `va_arg`, pointer decay and
qualifiers, C17/C23 spelling and argument rules, and the last named parameter.
Its evaluated-operand mask excludes `va_start`'s last-parameter expression.
Warnings, list lifetime effects and list mutation are separate metadata.

`c_builtin_varargs_lower.c` expands scalar SysV plans into the existing typed
stream. `va_start` requires real producer-supplied register-save-area and
overflow-argument addresses, plus the named prefix's GP/FP offsets. `va_arg`
advances exactly the selected cursor, `va_copy` copies exactly 24 bytes after
snapshotting its source, and `va_end` needs no machine operation. The supported
plans cover ordinary integer/pointer and F32/F64 register or stack slots;
aggregates, high-half plans and wider classes are explicit refusals.

The ordinary Tier 0 live-value spill area is not a SysV variadic register-save
area. Incoming variadic callee support must reserve and populate that separate
area before overwriting argument registers and provide genuine frame
addresses. An addressable local `va_list` needs 24 bytes, beyond the ordinary
eight-byte scalar local slot. Tests consuming a host-created list establish
the expansion contract only; a Clang caller invoking a generated variadic
callee is still required for source-level acceptance.

## Hints, memory and frame checking

`c_check_builtin_misc.c` checks hints, memory/string builtins and frame queries
without reading source memory. The result records converted arguments,
evaluation masks, warnings, terminal effects, memory effects and explicit
unsupported requirements. `__builtin_expect` converts both runtime arguments
to `long` and evaluates each once. Its separate `constant_value` and
`constant_evaluated_operands` describe the source constant evaluator's first
argument only; using that folded value must not erase the second runtime
argument's effects. Neither argument has a general ICE requirement.

Prefetch hints retain default promotions and bounded constant constraints;
alignment hints retain the original pointer, requested alignment and optional
converted offset. Frame levels require an integer constant expression.
Nonzero levels and unsupported wide/complex cases remain explicit metadata,
not fabricated frame addresses or successful generated code.

`c_builtin_misc_lower.c` consumes the already evaluated locals. It expands
`memcpy`, `memmove`, `memset`, `memcmp` and `strlen` as existing typed byte
loops, with absolute enclosing-stream branch targets and live-stack
preservation. Zero-length operations perform no memory accesses. `memmove`
selects a backward loop for overlapping higher destinations; `memcmp` compares
unsigned bytes and returns their signed difference. Hints and baseline x86
return-address extraction use the documented identity/no-op behavior.
Dynamic stack allocation and genuine frame addresses remain explicit refusals
in this adapter. Terminal helpers require a separate terminal source-graph
path. Byte loops are the scalar memory fallback allowed by the issue; this
adapter makes no memory-throughput claim.

## Unevaluated queries

`c_check_builtin_query.c` returns an empty runtime evaluated-operand mask.
Speculative operand faults and volatile reads do not become query effects.
Source constraints still diagnose. For
`constant_p`, explicit `string_literal_start` and
`absolute_address_constant` witnesses retain source provenance that a generic
pointer-to-integer conversion can otherwise discard. They certify the actual
final expression, not an object-ID convention or an unknown value's default
fields. Interior string pointers have no first-character witness.
The separate `complex_constant` witness certifies foldability of the complete
complex expression, with the same fault/effect guards and an actual complex
type. Complex type alone or one constant component cannot establish this proof.

For `object_size`, the producer supplies four independent known-bound flags
and values: whole-object maximum, subobject maximum, whole-object minimum and
subobject minimum. This action does not infer object bounds from pointee size.
The separate per-mode `object_size_source_constant` flags certify that the
bound is available in source constant evaluation. Runtime bounds alone do not
prove this: Clang's string-literal whole-object bound can be runtime-known
while only its subobject bound is source-constant. A known zero retains its
known flag. Source-proven bounds may ignore operand effects. Unknown effectful
pointers yield a source-constant sentinel without executing those effects.
Otherwise `action.value` has no constant payload and retains only unevaluated
ICE eligibility; `runtime_value` separately carries the real bound or fallback
(`ULONG_MAX` for unknown modes 0/1, zero for 2/3). A runtime fallback must not
become a static initializer or an evaluated ICE. Mode conversion to `int`
precedes the 0..3 range check, while original ICE provenance remains required.

## Floating checking

`c_check_builtin_float.c` retains the declared real format and constructs
infinities and quiet NaNs exactly for float, double and target x87 long double.
NaN payload metadata is the entire decoded narrow string literal after
parentheses/casts, excluding only the final terminator. Embedded zero bytes
remain part of that slice and prevent constant folding. Unknown or invalid
payloads remain typed runtime calls; no source pointer is dereferenced.
Unsupported real formats are never approximated through host long double.

`isinf_sign` yields -1, zero or 1 for the actual real format. The `fabs`,
`fabsf` and `fabsl` actions apply their declared floating parameter conversions,
including the real component of a complex argument. Their floating results
do not become integer constant expressions. Unsupported payload conversions
retain explicit metadata. F32/F64 runtime recipes clear only the sign bit,
preserving every remaining bit, including NaN payloads.

`fpclassify` retains all five selector expressions and an explicit evaluation
mode. Successful source constant evaluation visits the FP argument and only
its selected selector. Otherwise its runtime schedule visits FP operand 5,
ZERO selector 4, NAN selector 0 when nonzero, INF selector 1 when non-NaN, and
both NORMAL/SUBNORMAL selectors 2/3 when finite and nonzero. A possible-operand
mask must never be flattened into eager evaluation. The definite mask and
fault provenance follow that schedule.

`c_builtin_float_lower.c` expands F32/F64 classification using a fresh
addressable local of the actual FP class and an exact-width integer load of
its representation. It uses integer masks and comparisons, preserves the
enclosing stack and leaves I32. Its `fpclassify` result is a category index
0 NaN, 1 infinity, 2 normal, 3 subnormal, 4 zero; the source producer still
owns conditional selector evaluation. Certified infinity/NaN constants use
their exact raw bits. Unknown NaN calls, long double and wider runtime
formats need another lowering path and remain explicit refusals here.

## Atomic and terminal helper calls

`c_check_builtin_atomic.c` checks the corpus-driven scalar `load_n`, `store_n`,
sync fence and atomic thread/signal-fence slice. It retains actual object and
value types, qualifiers, all argument effects, ordinary `int` order conversions,
and known/dynamic order metadata. Orders do not generally require ICE. Invalid
known load/store orders carry warnings; fence handling retains its own rules.
GNU `_Atomic` wrappers and unsupported families are not treated as ordinary
integer objects. Read/modify/write checking and runtime leaves are separate
modules described below; other recognized atomic/sync families remain
explicit refusals.
The source-domain rules follow the approved Clang 23.1.1 release: real and
floating-complex `_n` objects are valid, while direct x87 long double and
`_BitInt` objects are rejected. F32/F64 retain exact source conversions;
extended/complex runtime formats and unavailable payload conversions carry
unsupported metadata. A raw helper's U64 bit ABI requires representation
loads/stores for FP values, never numeric FP/integer conversion. Corresponding
signed/unsigned pointer-valued stores retain Clang's pointer-sign warning.

`c_builtin_atomic_helpers.c` emits original baseline SysV leaf bodies with
explicit parameter/result classes. Naturally aligned 1/2/4/8-byte loads
return zero-extended raw bits; width-specific stores followed by MFENCE and
MOV loads provide conservative sequential consistency. Thread/sync fences
use MFENCE. Signal fences require no machine instruction. Every helper CALL,
including a signal fence, must remain an unknown-effect compiler barrier;
a RET by itself does not constrain optimization. Each original order,
pointer and value expression must still execute once even when the helper
does not inspect its numeric order.

The producer must establish natural alignment at least `required_alignment`.
Unknown or insufficient alignment, wider objects and formats outside the
selected helper need another real fallback. A helper must not make unaligned
ordinary MOV appear atomic. The semantic result retains alignment evidence
and unsupported requirements without turning valid source into fake success.

`c_check_builtin_atomic_rmw.c` checks 30 scalar GNU and legacy spellings:
exchange, compare-exchange, old/new-value add/sub/AND/OR/XOR/NAND, legacy
lock-test-and-set and lock-release. It retains actual source types, converted
fixed operands, old/new/boolean/void result kind, conditional object/expected
writes and explicit runtime format. Checking never reads or folds a source
memory object. GNU pointer add/sub converts the delta to LP64 `ptrdiff_t`
(`long`) and uses bytes without element-size scaling. Legacy pointer
operations convert their fixed values to the pointer value type before raw
address-bit operations. GNU `_BitInt` pointees are rejected. Legacy `_BitInt`
accepts power-of-two bit widths; widths below eight retain a distinct
sub-byte representation gap.

GNU calls require exact arity. Every fixed operand, including dynamic weak
and order expressions, evaluates exactly once. The source producer retains
prior diagnostics within legacy protected-variable tails. Those tails are
never evaluated or passed to a helper; their effects and evaluation faults
do not propagate. Known orders remain separate from source ICE witnesses
after ordinary conversion. Only witnessed invalid orders acquire Clang's
order warning; dynamic orders remain valid. Clang 23.1.1 also permits failure
orders stronger than success.
The consumer must retain all these evaluation and warning distinctions even
when the machine implementation strengthens the ordering.

`c_builtin_atomic_rmw_helpers.c` emits naturally aligned 1/2/4/8-byte baseline
SysV leaves, using U64 raw bits and at most six integer argument registers.
Integer and pointer arithmetic is modular. LOCK CMPXCHG loops strengthen
valid orders to sequential consistency, retry from the newly observed value,
and return the successful desired register for new-value operations. Strong
CAS implements weak requests. GNU CAS takes an expected address, reads its
exact object width once, and stores the observed value there only on failure.
Success permits read-only expected storage and `expected == object` without
overwriting the desired object value. Legacy CAS instead takes an expected
value. Legacy lock-release is a pure exact-width MOV of zero followed by
MFENCE: it adds no object read or read/modify/write event. All calls remain
unknown-effect compiler barriers, including release, and require proven
natural object alignment and operation-appropriate accessibility.

Source-valid operations can still require an unsupported runtime format.
Canonical boolean exchange/CAS/AND/OR/XOR/release preserve 0/1 storage.
Boolean add/sub/NAND may produce noncanonical bytes and remain explicit
runtime gaps rather than being silently normalized. Floating exchange alone
has F32/F64 raw-bit leaves; floating arithmetic, wider/complex formats,
sub-byte integer representations, objects wider than eight bytes and absent
or insufficient alignment proof need another consumer. Floating helper
transport never numerically converts between FP and integer bits. No module
grants source, constant-expression or either-tier capability merely because
typed checking or an isolated helper succeeds.

`c_builtin_terminal_helpers.c` emits one baseline UD2 record for trap and a
conservative realization of unreachable behavior. Its source consumer must
retain terminal control flow with no successor. An ordinary void CALL alone
does not express that fact. The in-place miscellaneous expander continues to
refuse these IDs so a consumer must choose the explicit terminal path.

Publish helper records as per-unit local TEXT atoms and define their symbols
before Link validation. Local symbols must share the caller's unit. Existing
ordinary-call ABI planning and fixups provide the call path; no new shared
Tier 0 opcode is introduced. The Tier 1 direct-call follow-up belongs to the
active #141 owner and needs its own exact-revision acceptance evidence.

## Corpus-driven typed overflow and rounding

`c_check_builtin_typed_overflow.c` implements `umull_overflow` through ordinary
`unsigned long`, `unsigned long`, `unsigned long*` parameter conversions before
the generic multiply-overflow action. Its formal destination is unqualified;
discarded pointer qualifiers remain diagnostics. A successful action always
retains the destination write. Wider or complex conversion payloads without a
real implementation remain explicit unsupported cases.

`c_check_builtin_rounds.c` treats `flt_rounds` as a zero-argument, nonconstant
`int` query of the floating environment. Its helper reads x87 rounding control
and maps the four encodings to C's 1/3/2/0 values. It preserves the controls
and carries an environment-read/compiler-barrier requirement. The existing
FNSTCW memory form is admitted to baseline generated-code profiles. Native
fixtures use both encoders and real Tier 0 CALLs, including all sixteen pairs
of deliberately divergent x87/MXCSR rounding modes against Clang's query.

The frozen WASI math source additionally requires float/double `sqrt`,
`copysign`, `ceil`, `floor`, `trunc`, `nearbyint` and `rint`. The separate
`c_check_builtin_math.c` action applies their declared prototypes and retains
converted operands. Only `copysign` folds in Clang's source evaluator;
the other operations remain runtime actions even for literal operands.
Effectful `copysign` calls carry no operation constant payload. Nonidentity
conversions under unrepresented nondefault FP settings retain an explicit
conversion requirement instead of a guessed default-rounding fold.

`c_builtin_math_helpers.c` transports raw F32/F64 bits through U64 parameters
and results; numeric integer/FP conversion is forbidden at that boundary.
Original baseline leaves use integer IEEE rounding, current MXCSR control
reads and scalar FP instructions for square root, NaN quieting and `rint`'s
inexact exception. They preserve controls and existing status flags;
`nearbyint` creates no inexact exception. `sqrt` also takes the current
thread's genuine writable `int32` errno address and uses the Linux SysV
`EDOM` value. The producer owns that address and all preceding conversions.
No helper calls a math symbol that could recurse when the source defines it.
The helper body requires normal branch sizing and unknown-effect call
semantics. These adapters do not establish source or Tier1 acceptance.

## Validation commands

On the approved 9700X, through a separate shared-host lock for each command:

```sh
./build/compiler test-builtins
./build/compiler test-check-builtins
./build/compiler test-builtin-lowering
./build/compiler test-check-varargs
./build/compiler test-builtin-varargs
./build/compiler test-check-builtins-misc
./build/compiler test-check-builtins-query
./build/compiler test-check-builtins-float
./build/compiler test-builtin-misc-lowering
./build/compiler test-builtin-float-lowering
./build/compiler test-check-builtins-atomic
./build/compiler test-builtin-atomic-helpers
./build/compiler test-check-builtins-atomic-rmw
./build/compiler test-builtin-atomic-rmw-helpers
./build/compiler test-builtin-terminal-helpers
./build/compiler test-check-builtins-typed-overflow
./build/compiler test-check-builtins-rounds
./build/compiler test-builtin-rounds-helper
./build/compiler test-check-builtins-math
./build/compiler test-builtin-math-helpers
```

The tests distinguish typed constraints and fold metadata from native execution
of the emitted recipes. Scalar/vector agreement is supplemented with Clang's
builtin behavior, boundary inputs, real destination writes and guarded-capacity
checks. Command/workflow registration, results and full source/two-tier
acceptance must be reported for the exact tested revision; this document
makes no throughput claim.

## Provenance

The modules, recipes and fixtures are original material under the project's
default license. The existing type, checker, typed-stream, encoder and ABI
interfaces are reused. Clang is an external behavior and execution oracle;
no Clang implementation or external fixture corpus is imported.
The atomic RMW modules and fixtures are original C. Exact
[Clang 23.1.1 checking](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp),
[atomic emission rules](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGAtomic.cpp),
[legacy builtin emission](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp)
and [LLVM's atomic code-generation description](https://llvm.org/docs/Atomics.html#atomics-and-codegen)
supply behavior facts; their implementation and tests are not copied.
