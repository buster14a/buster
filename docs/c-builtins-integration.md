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
64-bit integers, unsigned one-bit booleans in byte storage, `_BitInt`s of the
other widths to 64 bits, and 128-bit integers (`BUSTER_CBUILTIN_LOWER_WIDE`,
signed or not: a width's seven bits stop at 127). Boolean stores retain the low
mathematical bit; they do not use the ordinary nonzero-to-true cast. A
`_BitInt`'s slot is the narrowest class of 8, 16, 32 or 64 bits that holds it,
its value extended from its width there, as clang stores it and the source
producer keeps it, and a `_BitInt` destination takes its value extended so,
by a pair of shifts. A 128-bit operand's slot is an address-taken frame object
(class VOID) holding its 16 bytes, which the recipe reads as two 64-bit limbs,
and a 128-bit destination takes its two limbs in two stores. Wider bit-precise
types return an explicit unsupported status.

The caller supplies mutually disjoint kind, class, value, local-class and
local-flag columns, writable to their declared capacities and disjoint from
the buffer and call records. Counts describe initialized prefixes and must not
exceed capacities. Every existing argument local must have the required
machine class. The expander stages a bounded recipe before publishing it;
invalid or unsupported input and insufficient entry/local capacity leave all
caller bytes and counts unchanged. Publication uses masked tails, with a
scalar copy twin. No allocations or retained scratch storage are required.

Integer callers may supply an optional `CBuiltinLowerScratch`. It owns up to
20 private U64 local identities, disjoint from every argument and source local;
the append reuses them and grows the pool transactionally when a larger recipe
first needs more. Its result's temporary range describes only new locals.
An overflow recipe and its product use one cursor, so their simultaneously live
scratch values have separate identities. Recipe results reach the typed value
stack before the next recipe can overwrite scratch. Miscellaneous, floating
and varargs recipes keep their existing allocation contract.

Source lowering keeps this recipe pool per function. Argument slots are cached
separately by active integer-builtin nesting depth, argument position and value
class. Each outer argument therefore survives nested calls in later arguments
or in the destination expression; signed and unsigned 128-bit arguments retain
their own 16-byte frame objects. Both pools start fresh for every body, including
bodies lowered by different lanes. Repeated independent calls add typed entries
and source locals, but no new builtin scratch once the applicable slots exist.
Capacity refusals from integer recipes name the recipe bound, the per-function
entry or local limit, or the lowering storage that actually ran out (#665).

The recipes use the existing target-independent `BUSTER_TYPED_*` operations.
Byte swaps and counts use fixed shifts, masks and arithmetic. Full overflow
products use four 32-by-32 products and exact two-limb correction. With a
128-bit operand or destination, the exact result is three limbs of 64 bits for a
sum or a difference and four for a product, whose columns' 64-by-64 products are
U128 multiplications of zero-extended limbs, less 2^128 times the other
operand's pattern for each negative operand; an unsigned product never sets its
top two limbs all ones, so it can't pass as a negative value. Generated
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
source capability. Source availability now requires the source checker and the shipped Tier 0
source path (#806); Tier 1 remains a separate capability, not a gate on the
existing C compiler. Constexpr additionally requires source constant evaluation.

The preprocessor uses this inventory in its normal implemented mode and keeps
its separate Clang compatibility mode. The catalog now explicitly advertises the implemented source paths; recognized
but refused names would remain zero (none is now), and the vector builtins
the checker checks but no lowering takes (`__builtin_elementwise_clmul`, the
masked memory builtins and the matrix ones) have `BUSTER_CBUILTIN_CAP_CHECKER`
alone, so `__has_builtin` answers 0 for them. clang's sized legacy atomics
(`__sync_fetch_and_add_4` and the others) aren't catalog entries: the catalog
resolves each to its family's builtin, with its capabilities
(`cbuiltin_sized_entry`).
`test-preprocessor` checks the exact catalog and the advertised list, including
raw macro names, unknown bounded names and guarded input boundaries. An
advertised name also needs clang's own conditions for the unit, as clang's
`__has_builtin` has them: its row's language modes and required target
features (the preprocessor guide's "feature queries"; `CBuiltinSourceScope`
keeps each scoped intrinsic's, and `cbuiltin_languages` the catalog's).

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
plans cover ordinary integer/pointer and F32/F64 register or stack slots, and
structs and unions (#137): with `CBuiltinVaLowerCall.size` and `.object`, a
register plan checks both cursors against the eightbytes it needs, copies them
from the save area into the frame object (both general, both vector, one of
each in either order, the high eightbyte alone, or a 16-byte vector's SSE and
SSEUP halves from one XMM register's slot) and advances both cursors;
otherwise, and for a stack plan, the value is the aligned overflow area's
address, which advances past the aggregate's slots. An ignored (empty) one's
value is its object's address. A `long double` (Tier 0's F80) is always in the
overflow area: its plan aligns the area to 16, loads the value's 16 bytes and
advances past them. Wider classes, such as `_Complex long double`, are explicit
refusals.

The ordinary Tier 0 live-value spill area is not a SysV variadic register-save
area. The optional [incoming variadic frame](tier0.md#incoming-variadic-frames)
reserves and populates a separate 176-byte area before parameter moves and
exposes genuine save/overflow anchors through address-only locals. A `VA_LIST`
local reserves 24 bytes, beyond the ordinary eight-byte scalar local slot.
`test-tier0-variadic-frame` supplies real native callers of generated callees;
exact-revision target qualification and source/Tier 1 integration remain
separate acceptance requirements. Host-created-list tests alone establish
only the recipe expansion contract.

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
`memcpy` through baseline SSE 16-byte `COPY` operations, `memmove` through
8-byte loads/stores, `memset` through repeated 8-byte integer patterns and `memcmp` through equal 8-byte
words. Exact byte tails handle the remainder; a differing comparison word
falls into the unsigned-byte comparison at that word's first byte. A higher
overlapping move copies backward, loading each whole chunk before storing it.
All branches have absolute enclosing-stream targets and preserve live values.
Zero-length operations perform no memory accesses. `memcmp` returns the first
unsigned-byte difference, independently of the host's integer byte order.

The source producer retains a checked constant size that fits a `uint32_t` in
`CBuiltinMiscLowerCall.constant_size`, guarded by `constant_size_known`, while
still evaluating every argument once. Known copies and fills up to 32 bytes
expand directly. Moves up to 16 bytes load one or two pieces before either
store, so close overlap is safe in either direction. Comparisons of zero or
one byte have direct results. Larger constants use the bounded runtime loops;
no recipe grows with an unbounded source length. Unknown lengths have direct
zero-, one- and eight-byte paths before loop setup and overlap classification.
A move's word is loaded before it is stored even on that fast path; it does
not rely on `COPY`, whose shared-IR contract requires equal or disjoint ranges.
Known larger sizes omit these small-size tests. The recipe caller supplies
locals with the usual pointer, size and value classes; size metadata is its
proof about the converted size local, not a request to skip source effects.
Only ordinary, fault-free integer rvalues without evaluation or floating
environment effects supply that proof. A size evaluated under `FENV_ACCESS`
keeps its runtime rounding behavior even if the checker retains a folded value.
Queries also keep their separate runtime plan: a query's source proof is not
used as the memory operation's runtime size.

`strlen` probes its first eight bytes in order with one conditional branch per
byte, stopping at the first zero. If all eight are nonzero, it starts at the
next aligned word (rechecking at most seven bytes) and uses the integer
zero-byte predicate `(x - 0x0101010101010101) & ~x & 0x8080808080808080`.
Three binary splits locate the first marked byte of the terminating word;
no target bit-count instruction or byte loop is required. It never reads before
the string or crosses an unreadable page: x86
page boundaries are multiples of eight. As with an ordinary library word
scan, the terminating word may include readable bytes after the terminator;
this is for ordinary nonvolatile strings, not byte-granular volatile access.

These recipes require no external runtime symbols, optional target feature or
host AVX-512 output permission. They also apply to explicit `__builtin_` calls
in freestanding and no-builtin modes; the existing policy for recognizing
ordinary function names is unchanged. The compiler's masked vector publication
and scalar publication produce the same typed columns. Hints and baseline x86
return-address extraction use the documented identity/no-op behavior.

The stack's and the frame's builtins are clang -O0's code (#148):

- `__builtin_alloca`, which glibc's `<alloca.h>` makes of `alloca`, is Tier 0's
  `ALLOCA` of its size: rounded up to 16 bytes and taken from the stack. A
  variable-length array's scope releases it at its end exactly where clang's
  `stackrestore` would: an allocation after the block's first array goes with
  the arrays, one before it stays until the function returns.
- `__builtin_frame_address(0)` is Tier 0's `FRAME_ADDRESS`, the frame pointer,
  which the entry gives its function as the stack's kinds do. The address
  points at the caller's saved rbp, 16 bytes below the incoming arguments.
  `__builtin_return_address(0)` is the word 8 bytes above it. Their level is a
  constant of their own, which nothing evaluates, so the recipes take no
  arguments.
- A level past 0 is a caller's frame, which clang -O0 reaches through the frame
  pointers' chain. Tier 0's functions keep a frame pointer only where they need
  one, so the lowering refuses those levels by name: "unsupported:
  `__builtin_frame_address` and `__builtin_return_address` of a caller's frame".

Terminal helpers require a separate terminal source-graph
path. The native recipe tests retain independent libc comparisons, exact
guarded tails, every small size across the expansion cutoffs, close overlap in
both directions, high-bit bytes, and operations through 1 MiB. The checker
tests also compile and execute source fixtures with retained size proofs and
argument side effects against Clang.

The [#599 qualification](https://github.com/buster14a/buster-performance/tree/afaf2b45b826e4a20b3bb5970ccb071e2ec5adeb/docs/measurements/memory-builtins-599-2026-10-27)
compares main `92c025e7` with implementation `f499888c` on the locked 9700X,
Clang 23.1.1, CPU 2. Dynamic disjoint/equal cases have these medians:

| Operation | 4 KiB before / after, ns | 1 MiB speedup |
| --- | ---: | ---: |
| memcpy | 1016.4 / 70.1 | 17.1x |
| memmove | 1016.6 / 148.5 | 7.4x |
| memset | 760.4 / 115.4 | 8.0x |
| memcmp, equal | 1494.8 / 203.7 | 8.0x |
| strlen | 773.2 / 193.8 | 4.1x |

The eight-byte dynamic copy is 1.62 ns versus 3.06 ns before; known 32-byte
copies are 1.44 versus 8.23 ns. Empty dynamic copy/compare add about 0.18 ns
of fixed overhead. The 41-wrapper object grows from 4486 to 6467 bytes of
text. Compiling the wrappers adds 0.85% instructions; whole SQLite compilation
is effectively unchanged (146.953 / 146.971 ms, overlapping ranges), with
about 3 MiB more peak RSS. The record retains libc/Clang references, all
alignments, overlap and mismatch shapes, code size, compiler cost, identities,
guarded tests and measurement limits. These are emitted-wrapper measurements;
they establish neither application-wide gains nor libc parity.

## Constant and object-bound queries

`c_check_builtin_query.c` separates source constant proof from the runtime
evaluation plan. For `constant_p`, source proof is independently INFER, ZERO,
ONE or DEFERRED. The runtime plan independently skips the operand, evaluates
it before a known result, defers the compiler's constantness test, or remains
unresolved. A known source answer does not establish runtime deadness.
Strict floating-point evaluation and nested queries can preserve runtime
effects even when the final answer is known. Conversely, a source-constant
answer of one can coexist with a skipped runtime answer of zero.

The producer supplies context-pruned runtime effects, including potential
faults, floating-environment reads and possible exceptions. SKIP suppresses
operand effects; EVALUATE retains them in `runtime_value`, without ICE flags,
even for a known zero or one. `action.value` remains the separate clean source
proof. A successful speculative fold precedes syntactic side-effect fallback;
an unchosen call or increment cannot veto an independently proved fold.
Context-sensitive inputs missing either explicit proof or plan remain
unresolved. All-zero INFER preserves the original context-independent
contract. Deferred constantness testing requires an actual compiler consumer;
it is an explicit unsupported requirement, never a fabricated CPU test.

The context-independent `constant_p` path retains explicit `string_literal_start` and
`absolute_address_constant` witnesses for source provenance that a generic
pointer-to-integer conversion can otherwise discard. They certify the actual
final expression, not an object-ID convention or an unknown value's default
fields. Interior string pointers have no first-character witness.
The separate `complex_constant` witness certifies foldability of the complete
complex expression, with the same fault/effect guards and an actual complex
type. Complex type alone or one constant component cannot establish this proof.
The separate context-aware certificates do not depend on these legacy value
witnesses. Malformed certificates diagnose; impossible fold domains cannot
acquire a source-one certificate.

`object_size` always leaves its runtime operand unevaluated and ignores the
constant-query-only certificates. Its producer supplies four independent known-bound flags
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

## Type-first bit casts

`c_check_builtin_bit_cast.c` checks the separately parsed destination type and
one raw value operand. It preserves the source type without array/function
decay, atomic loading or half promotion. Complete, equally sized, trivially
copyable C object types are required; valid aggregate, vector and complex
types retain explicit payload/runtime gaps when this representation model
cannot carry them. The one operand's evaluation and effects remain visible.

Known representation bytes do not establish source constant proof. The
producer supplies that proof separately as NONE, CONSTANT or ICE, and optional
bounded object-representation bytes must match the exact type size. Integer
bits are reinterpreted without numeric conversion. Raw floating results stay
in the result's byte payload; the checker never reads or constructs a host
floating value, including for signaling NaNs. A representation-aware consumer
can use proven bytes while retaining the explicit CheckValue payload gap.
Wider, padding-sensitive and invalid-representation cases remain explicit;
missing runtime lowering does not by itself make otherwise valid source types
incompatible. The existing generated `bit_cast` parser-gap row remains a gap
in ordinary prototype decoding, independent of this type-first action.

Qualified array destination spellings can collapse into one canonical TypeId
while Clang retains different result qualifiers. Those cases report the
explicit `RESULT_TYPE` gap and an unknown result TypeId (zero), retaining
`representation_type` for object constraints. Consumers must check the gap
before using the result type. Unqualified arrays and scalar half types do
not have that ambiguity.

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
their exact raw bits. Unknown NaN calls, long double constants and wider
runtime formats need another lowering path and remain explicit refusals here.

A long double's (F80) classification and `fabsl` read the argument's frame
object, which the producer makes for the call: the significand's 64 bits, its
top bit the explicit integer bit, and the sign and exponent's 16. Clang 23's
-O0 code classifies an x87 value partly by the x87's compares (`isnan`,
`fpclassify` and `isinf_sign`'s test), which take the encodings the x87 doesn't
support (a nonzero exponent without the integer bit: unnormals, pseudo-zeros,
pseudo-infinities and pseudo-NaNs) as unordered and a pseudo-denormal (a zero
exponent with it) as the number it is, at least the smallest normal's
magnitude; and partly by its bits (`isinf`, `isfinite`, `signbit`). The recipes
give each builtin's results for every encoding with integer tests alone:

| builtin | clang's test | result |
| --- | --- | --- |
| `isnan` | `fucomip` of x with x | (e != 0 and no integer bit) or (e = 0x7fff and m > 2^63) |
| `isnan`, FENV_ACCESS | LLVM's strictfp integer test | (e = 0) = integer bit, or (e = 0x7fff and m > 2^63) |
| `isinf` | bits | e = 0x7fff and m = 2^63 |
| `isinf_sign` | `fucomip` of \|x\| with infinity | `isinf`'s, by the sign |
| `isfinite` | bits | e = 0, or e != 0x7fff with the integer bit |
| `fpclassify` | `fucomip`s with 0, x, infinity and the smallest normal | zero e = 0 and m = 0; subnormal e = 0, no integer bit, m != 0; normal the integer bit and e != 0x7fff; infinity as `isinf`; NaN the rest |
| `signbit` | bits | the sign |

(e is the exponent and m the significand.) Under FENV_ACCESS, `fpclassify` and
`isinf_sign` keep the producer's quiet comparisons, the x87's for a long
double, which raise invalid as clang's do; the recipe's isnan takes the call's
FENV_ACCESS (`CBuiltinFloatLowerCall.fenv_access`). `fabsl` clears the object's
sign bit in place and leaves its value, F80: no x87 operation touches the
bits, so a signaling NaN keeps its payload, as the x87's `fabs` keeps it.

`isgreater`, `isgreaterequal`, `isless`, `islessequal`, `islessgreater` and
`isunordered` (`check_builtin_float_compare`) take two arguments. The usual
arithmetic conversions of the two, as clang makes them for a comparison, must
give a real floating type. Otherwise it's clang's error, "ordered compare
requires two args of floating point type", at the first argument: two integers,
a pointer, a complex value, a struct. Both convert to that type, an integer to
a `float` rounded (`isgreater(16777217, 16777216.0f)` is 0). Two constants fold
to the int a quiet comparison gives, an integer constant expression, as clang
folds them in every context. An argument with an effect or no constant value
leaves the comparison to run, both arguments evaluated in order.

The producer lowers them with quiet compares, as clang -O0's are (`ucomiss`,
`ucomisd`, `fucomip`), so a quiet NaN raises no exception
(`check_lower_floating_compare`). Tier 0's `EQ` and `NE` are quiet in every
class. Its `LT` and `LE` of floats and doubles signal (`cmpsd`'s `LT_OS` and
`LE_OS`, which C's relational operators take). The first four retain their
common format: both arguments finish in temporaries before eager quiet
`(a == a) & (b == b)` tests guard the ordered comparison. Either signaling
NaN therefore raises invalid, including one beside a quiet NaN. A float's or
double's comparison retains MXCSR's DAZ policy, and a second argument that
clears exceptions finishes before the comparison raises one. Long double's
ordered comparisons use its quiet `fucomip` directly. `islessgreater` is
`(a == a) & (b == b) & (a != b)` and `isunordered` `(a != a) | (b != b)`, of
temporaries that hold the two.

`__has_builtin` now advertises these source-lowered floating builtins (#806).
This does not grant their separate source-constexpr or experimental Tier 1 capabilities.
`test-check-builtins-float` checks every pair of formats' foldings against
IEEE's rule stated apart, the conversions and the refusals. `test-checker`'s
`floating-comparison-builtins` program checks their results and the exceptions
they raise on the harness's special values (zeros, infinities, quiet and
signaling NaNs, subnormals) in each format against clang's build, and
mingw's `fmaxl`. Separate programs check float/double subnormals with DAZ on
and off, and each ordered builtin's signaling-NaN exception after a second
argument clears the flags, including quiet-NaN/signaling-NaN pairs.

## Atomic and terminal helper calls

`c_check_builtin_atomic_c11.c` supplies the C11 `__c11_atomic_*` family used by
both `<stdatomic.h>` implementations. Its pointer must address `_Atomic(T)`;
load removes the atomic wrapper, writes reject const objects, compare-exchange
takes a pointer to plain `T`, and pointer fetch-add/sub scales by the pointee's
stride. Init and the weak/strong forms retain their source arities. Every source
argument, including dynamic orders, is evaluated once. Constant lock-free
queries for zero and 1/2/4/8 bytes retain Clang's integer-constant-expression
proof; other sizes use the runtime query.

The C11 scalar forms use the existing GNU raw-bit leaves. Small record and
complex representations use those same leaves through an owned byte buffer,
including zeroed atomic storage padding. Other widths or insufficient alignment
use libatomic's generic size-and-pointer load, store, exchange and compare-exchange
ABI. Objects wider than eight bytes therefore require `-latomic` when linked;
this implementation does not advertise a lock-free 16-byte path. The ordinary
value representation limits still apply to `_Float16`, `_Float128` and arbitrary
`_BitInt` values.

`c_check_program_atomic.c` also lowers ordinary `_Atomic` lvalue reads, assignment,
compound assignments and increments. Accesses use sequential consistency;
compound updates snapshot the address and RHS before a compare-exchange loop,
including an owned copy of a complex RHS. Failed attempts refresh the expected
representation without reevaluating source operands. Integer promotions,
pointer strides, prefix/postfix results, complex arithmetic and 128-bit division
reuse the ordinary lowering's type and runtime contracts. Atomic locals remain
addressable, and static-data emission retains the atomic storage size/alignment
separately from its unwrapped value type.
Atomic parameters retain their declared ABI classification while argument values
are captured in padded objects. Atomic aggregate returns similarly copy only the
underlying value's bytes into zeroed atomic storage. Compatible atomic rvalues
retain their identity for initialization, assignment and calls.

Floating updates follow Clang's current compare-exchange loop behavior. Clang
23.1.1's `CGExprScalar.cpp` explicitly leaves saving/restoring floating exceptions
across retries as a FIXME; this implementation likewise does not claim full C
floating-environment conformance for failed retries.

`c_check_builtin_atomic.c` checks the corpus-driven scalar `load_n`, `store_n`,
sync fence and atomic thread/signal-fence slice. It retains actual object and
value types, qualifiers, all argument effects, ordinary `int` order conversions,
and known/dynamic order metadata. Orders do not generally require ICE. Invalid
known load/store orders carry warnings; fence handling retains its own rules.
GNU `_Atomic` wrappers and unsupported families are not treated as ordinary
integer objects. Read/modify/write checking and runtime leaves are separate
modules described below; other recognized GNU atomic/sync families remain
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

An atomic object's alignment is the one clang's code generation gives its
address (`EmitPointerWithAlignment`), the owner's decision for #126 (2026-10-26):
`check_graph_atomic_alignment` (`c_checker.c`) works it out from the pointer
argument's subgraph as clang does.

- A pointer value gets its pointee type's alignment: an aligned typedef's, which
  may be less.
- An object at the base of an `&` path gets its declaration's alignment
  (`getDeclAlign`): an aligned attribute's alone, which may also be less, or else
  its type's, at least 16 for an array of 16 bytes or more. The reader's witness
  (`check_input_declaration_alignment`) gives it.
- A member gets its base's alignment at its offset, so a packed record's members
  get 1.
- An element and pointer arithmetic get the alignment at the element's offset,
  or at its size for an index that isn't a constant.
- An explicit cast gets its own pointee type's alignment, unless its operand's
  came from a declaration.

clang calls libatomic for an object aligned below its size, and the lowering
refuses that one: "a misaligned atomic object, which clang calls libatomic for".
It also refuses a path it doesn't model: "an atomic object whose address's
alignment this doesn't work out". `test-checker` compiles 30 such accesses with
clang. The lowering must refuse exactly the ones whose clang object calls an
`__atomic_` function. A helper must not make an unaligned ordinary MOV look
atomic. The semantic result keeps the alignment evidence and unsupported
requirements without turning valid source into a fake success.

`c_check_builtin_atomic_rmw.c` checks 34 scalar GNU and legacy spellings:
exchange, compare-exchange, old/new-value add/sub/AND/OR/XOR/NAND, clang's
min/max extensions (`__atomic_fetch_min`, `__atomic_max_fetch` and the other
two: integers and its floating types, never pointers), legacy
lock-test-and-set and lock-release. The source checker takes every one of them
(#126): `check_graph_builtin_op` gives them the atomic op, and the graph and
the declaration-time reader send them to this module, with the orders' ICE
proofs from the source's own analysis (`check_builtin_atomic_rmw_order_ice`).
The lowering calls the leaves below: `c_check_program_helpers.c` plans one per
builtin, width and format. It takes integers, pointers and bool, and GNU's
exchange of `float` and `double` through the F32/F64 leaves. A legacy call's
protected variables, floating arithmetic, min and max, bool arithmetic and
NAND, and integers narrower than a byte stay refused by name. The checker retains actual source types, converted
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
unknown-effect compiler barriers, including release, and require natural
object alignment, as above, and operation-appropriate accessibility. clang's
min and max compare in the object's width, unsigned for integer and bool bits
and signed for `SIGNED_BITS`, which no other operation takes. The loop keeps
the operand where it is smaller or larger (CMP, then CMOV).

Source-valid operations can still require an unsupported runtime format.
Canonical boolean exchange/CAS/AND/OR/XOR/release preserve 0/1 storage.
Boolean add/sub/NAND may produce noncanonical bytes and remain explicit
runtime gaps rather than being silently normalized. Floating exchange alone
has F32/F64 raw-bit leaves; floating arithmetic, wider/complex formats,
sub-byte integer representations, objects wider than eight bytes and absent
or insufficient alignment need another consumer. Floating helper transport
never numerically converts between FP and integer bits: `check_lower_helper`
passes and takes a `float` or `double` as its bits, through a frame
temporary. Atomic loads and stores of them are lowered so too; before #126's
alignment rule they were refused, and a numeric conversion would have
changed their values. No module
grants source, constant-expression or either-tier capability merely because
typed checking or an isolated helper succeeds.

### The rest of clang's atomics (#148)

clang 23.1.1 answers `__has_builtin` 1 for 160 more atomic names in C on x86-64, of
five kinds. All are implemented, checked as clang checks them and lowered to
code whose runs equal clang -O0's on both paths.

- **The sized legacy forms**, `__sync_fetch_and_add_1` to `_16` and the other 16
  families' (`__sync_swap` among them, 85 names). clang's Sema rewrites a call of one
  by its object's size alone, whatever its suffix says (`BuiltinAtomicOverloaded`:
  `__sync_fetch_and_add_1` of a `long` is an 8-byte addition), so the catalog
  resolves each to its family's builtin (`cbuiltin_sized_entry`), whose checking and
  leaves it is. `__sync_swap` is `__sync_lock_test_and_set`'s sequentially consistent
  exchange, with a rule and a leaf of its own. A 16-byte object stays refused, as
  the unsized forms' is ("atomic objects of other than 1, 2, 4 or 8 bytes").
- **clang's legacy minimum and maximum**, `__sync_fetch_and_min`, `_max`, `_umin`
  and `_umax`: ordinary prototypes, `int (volatile int *, int)` and `unsigned int
  (volatile unsigned int *, unsigned int)` (the rule's `prototype`). Their first
  argument converts to the parameter's pointer as a call's does (a pointer to the
  other signedness with clang's pointer-sign warning, an incompatible one an
  error), they take exactly two arguments, and the leaves are the min and max
  ones, signed for the first two. A `void *` argument's object has the
  parameter's alignment, as clang's code generation gives it.
- **GNU's other forms**: the wrapping increment and decrement (`__atomic_fetch_uinc`
  and `_udec`, clang's `atomicrmw uinc_wrap` and `udec_wrap`: integers alone, bool
  among them, compared unsigned in the object's width) are read/modify/write rules
  with leaves of their own; the byte flag (`__atomic_test_and_set` and
  `__atomic_clear`) takes a pointer to any object that isn't const, void and
  incomplete ones too, and accesses its first byte, the order its second argument
  (a clear's a store's order); the lock-free queries (`__atomic_always_lock_free`
  and `__atomic_is_lock_free`, prototypes of `bool (size_t, const volatile void
  *)`) are constants where clang's evaluator answers them
  (`check_builtin_atomic_lock_free`: a power of two, 0 among them, up to the
  target's inline width, 16 bytes with cx16 (`TypeTable.atomic_inline_16`) and 8
  without, for one byte, a constant address aligned to the size or a pointee at
  least as aligned that isn't incomplete: a function's, whose type GNU aligns to
  4 bytes, or one complete where clang evaluates the call), their arguments then
  never evaluated, as clang folds them; otherwise `__atomic_is_lock_free` calls
  libatomic's and `__atomic_always_lock_free`, of a size that isn't a constant, is
  refused as clang's code generation refuses it. Clang evaluates a constant
  context's call where it reads it, and folds one in code where it emits the
  function: an external function once it has read its whole definition, a
  static or inline one at the unit's end. So a pointee whose tag the unit
  completes after the call answers by the incomplete type in a constant context,
  by its type at an external function's closing brace in that function's code (a
  block-scope tag completed later in the same body is complete there), and by
  the complete type in a deferred function's: the checker folds a constant
  context's call by the type at the call, and leaves a call in code to the
  lowering, which takes the answer of its function's emission
  (`CheckBuiltinAtomicResult.late_completion`, `CheckProgram.body_eager`, which
  the selection makes for such a unit; #932's reviews). The C11 query follows the same width now,
  and the declaration-time evaluator checks every family
  (`check_builtin_atomic_family`), so the queries fold in enumerators and bounds.
- **GNU's generic forms**, `__atomic_load`, `__atomic_store`, `__atomic_exchange`
  and `__atomic_compare_exchange` (`check_builtin_atomic_generic_one`): any
  complete object of a size but an `_Atomic` or `_BitInt` one (records, unions,
  arrays and long double among them), whose values are pointers converting to
  pointers to its unqualified type, but a store's value and a compare-exchange's
  desired one to the first argument's own pointer type, qualifiers kept, as
  clang's ByValType. The lowering copies the values into C11's representation
  buffers and the results back out (`CheckLowerAtomic.by_pointer`), through the
  native leaves for aligned objects of 1, 2, 4 or 8 bytes and libatomic's generic
  functions otherwise, as clang's code calls them.
- **The scoped families**: clang's `__scoped_atomic_*` (GNU's operations), its
  `__hip_atomic_*` (C11's on an object that isn't `_Atomic`, any type of a size,
  their pointer arithmetic scaled as C11's) and its `__opencl_atomic_*` (C11's)
  are their base builtins' operations (`check_builtin_atomic_base`) with a scope
  after their arguments (but `__opencl_atomic_init`). x86-64 has the system's
  scope alone (clang's code switches over a scope that isn't a constant to the
  same code), so a scope converts to int, is checked against its family's model
  where it is an integer constant expression (the generic scopes 0 to 5, HIP's 1
  to 6, OpenCL's 1 to 4: clang's "synchronization scope argument to atomic
  operation is invalid"), and is evaluated after the base's arguments and
  dropped; `__scoped_atomic_thread_fence (int, int)`'s scope is an ordinary int.
  The helper planner and the lowering plan and call the base's helpers on the
  base's arguments.

- clang's atomic IEEE 754-2019 maximum and minimum, `__atomic_fetch_fmaximum`,
  `_fmaximum_num`, `_fminimum` and `_fminimum_num` (and their scoped forms), are
  read/modify/write rules of clang's floating types alone (complex ones among
  them) but long double ("must be a pointer to floating point type"). A float's
  and a double's have leaves; the other types are valid and refused by name at
  the lowering. clang's code computes them through LLVM's x86 expansion of
  `llvm.maximum` and the others on every target without AVX10.2 (baseline to
  znver5 alike, at -O0 and -O2): `maxss` or `minss` of the old value and the
  operand, the result's sign cleared where the old value's is clear (maximum) or
  set where it is set (minimum), the old value where it is a NaN, and in the
  `_num` forms the operand where only the old value is one. So their NaN results
  aren't IEEE's (for one, `fmaximum(+0, sNaN)` is the signaling NaN, unquieted),
  and the leaves (`cbuiltin_atomic_rmw_ieee_body`) compute the same sequence.
  With AVX10.2 clang's code takes `vminmaxss` instead, whose NaN results differ,
  so a function whose target has AVX10.2 refuses them by name.

`test-check-builtins-atomic` checks the queries' constants against clang's
evaluator's rules, the generic forms' conversions and constraints and the scoped
families' models (`check_builtin_atomic_test_families`);
`test-check-builtins-atomic-rmw` the added rules
(`check_builtin_atomic_rmw_test_added`); `test-builtin-atomic-rmw-helpers` the new
leaves against the host compiler's own builtins at every width (the IEEE
maximum's and minimum's on every pair of 14 values of each width);
`test-preprocessor` the sized names' resolution, also at a guard page; and
`test-checker` their acceptance and first errors against clang's (35 fixtures
of the IEEE family's among them) and five programs of them against clang -O0's
builds (`c_check_program_atomic_test.c`: sized legacy forms, wrapping, flags and
lock-free queries, generic forms, scoped families, and the IEEE maximum and
minimum on every pair of 13 values, signaling NaNs among them).

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

## Intrinsic checking contracts

The later maintainer decision in #156 includes target intrinsics in #148's
scope. Metadata recognition, prototype checking, special operand constraints,
source constant evaluation, target-feature eligibility and runtime emission
are distinct requirements. None alone grants a preprocessor capability.

`c_builtin_intrinsic_catalog.c` looks up the 3,775 exact records from the
installed Clang 23.1.1 generic, common-x86 and x86-64 tables. The C generator
hash-pins all four inputs before publishing output. Derived LLVM data retains
its own attribution and license in `THIRD_PARTY_NOTICES.md`; exact package,
input, tool and output provenance is in
[`builtin-metadata/clang-23.1.1-inputs.json`](builtin-metadata/clang-23.1.1-inputs.json).
The 1,543-row frozen own-main subset includes explicit `bit_cast` parser and
`va_list` predefined-type gaps. These are not invented function declarations
and are absent from the ordinary 3,775-record catalogue. A catalogue miss is
not a general `__has_builtin` answer. Header/language restrictions and custom
rules remain consumer obligations.

`builtin_intrinsic_own_main_lookup` exposes that frozen subset, including its
two explicit gap rows. `builtin_intrinsic_own_main_x86_in_scope` accepts exactly
the 1,492 recorded x86/x86-64 members. Neither a name prefix nor membership in
the complete installed catalogue attests the audited operand-constraint scope.
Membership also does not replace successful ordinary prototype checking.

`c_builtin_intrinsic_types.c` decodes bounded Clang 23.1.1 descriptors through
the existing Linux/x86-64 LP64 type factories. It retains exact integer
identities, fixed vector element/lane types, parameter adjustment and a
64-argument ICE bitmap. Custom `t` signatures return `CUSTOM`; `T` additionally
marks a decoded signature as ignored for ordinary call checking. Missing
context and unrepresented forms return `UNSUPPORTED`. Malformed input and
factory exhaustion publish no partial prototype. Earlier interned subtypes
can remain in the monotonic type table after a later failure.

`c_builtin_intrinsic_features.c` evaluates the exact comma/pipe/parenthesis
grammar against a caller-supplied, complete and already closed target-feature
table. It distinguishes missing features, unknown atoms, invalid syntax and
insufficient nesting storage. Every alternative is validated, even after a
true alternative. CPU defaults and per-function target-attribute closure
remain producer obligations. These modules do not implement instructions or
establish source constant-expression acceptance.

`c_check_builtin_x86.c` adds 450 audited SemaX86 constraint facts for 422
affected names in the frozen 1,492-name own-main target-builtin scope and the
supplementary scope: 87
rounding sets, 90 SAE sets, 6 gather/scatter scale sets (1, 2, 4 or 8, a hard
error as clang's `CheckBuiltinGatherScatterScale` has it) and 267 direct ranges.
#126 added the 26 x86 names this compiler's build calls that the first
inventory lacked (`main.c`'s 25, `__rdtsc`, `__builtin_ia32_pause` and `xgetbv`
among them, and the macros' `ucmp`, gather, scatter, insert, extract, align,
shuffle, rotate and mask-shift builtins, and `executable_benchmark.c`'s
`scattersiv16si`), with their 17 ranges and 6 scales, and
`__builtin_unpredictable` and `__builtin_assume`. #619 added
`__builtin_ia32_ucmpd128_mask`, which `c_check_program_batch.c` reaches through
`_mm_cmplt_epu32_mask`, with its range. #148 added the SSE builtins that
stb_image, dr_flac, dr_mp3 and minimp3 call and the scope lacked, with their 8
ranges: `pshufd`, `pshufhw`, `pshuflw` and `shufps` (0 to 255),
`psrldqi128_byteshift` and `palignr128` (0 to 255), `vec_ext_v8hi` and
`vec_set_v8hi` (0 to 7). Their target features (SSE2, and SSSE3 for
`palignr128`) are no more checked than any x86 builtin's.
The separately attested supplementary scope (`c_builtin_supplementary_scope.inc`,
each name with its required target features in clang's metadata, in strcmp's
order for its lookups' binary searches) holds the x86 names past the inventory.
#799 added `shufpd` (immediate 0 to 3), and `pblendw128`, `pshufd256`,
`permdi256` and `palignr256` (0 to 255). Their lowering uses VEX forms and
requires AVX or AVX2; the existing vector backend also requires AVX-512F and
AVX-512VL. The attestation does not change the frozen own-main inventory or
admit other catalogue entries; `test-builtin-intrinsic-catalog` checks that
each supplementary name is an x86 record of the catalogue the inventory hasn't,
and that the names are sorted.
`builtin_intrinsic_own_main_prototype_in_scope` also admits
the generic records no checker module owns whose clang checking is their
prototype alone (`builtin_intrinsic_prototype_names`: the eight rotates,
`__builtin_unpredictable` and `__builtin_assume`, whose side-effect warning
this checker doesn't give); they take the x86 path, which adds no rule for
them. `_Generic`'s controlling expression uses the captured intrinsic
prototype on both checker paths (#488), including in declaration-time bounds
and type queries. Ordinary argument conversions determine the exact result
type; the control and its arguments remain unevaluated. These paths do not
fold the intrinsic call in a constant context.
The producer attests exact catalogue membership and successful ordinary
prototype checking, and supplies both the generated required-ICE bitmap and
independent source ICE witnesses. Existing ordinary-call diagnostics remain
first. Generated ICEs precede hard rounding/SAE sets and cumulative direct
ranges; both rules for each `rndscale` spelling are retained.

#148's SSE builtins lower through `c_check_program_x86.c`'s table, as the
other x86 builtins do, where the target has AVX-512F and AVX-512VL, which the
lowering requires of every vector. A row each in its EVEX form for the 8 above
but `vec_ext_v8hi` and `vec_set_v8hi`, which had rows; the packs,
multiplications, `maxps`, `minps` and `cvtps2dq` their intrinsics reach too are
rows of the families at every width (`BUSTER_CHECK_X86_BINARIES` and
`BUSTER_CHECK_X86_SSE`). VPSHUFD, VPSHUFHW, VPSHUFLW, VSHUFPS, VPSRLDQ and
VPALIGNR take the immediate's whole byte, as the instructions do: a byte shift
or an align past the vector's bytes gives zeros. VMAXPS and VMINPS take the
builtin's first operand in vvvv, so that a NaN or a pair of zeros gives the
second, as MAXPS does, and VCVTPS2DQ rounds by MXCSR. `vec_ext_v8hi`'s VPEXTRW
(C5) takes its general register in reg and its vector in rm, the other
extracts' forms the other way round. `test-checker`'s x86 programs
(`c_check_program_x86_test.c`) call them directly, at their immediates' edges
and on NaNs, zeros of either sign and halfway values, with `cvtps2dq` also
under each of MXCSR's four rounding modes, set and restored around it, and through
`<smmintrin.h>`'s intrinsics, by both paths against clang -O0's code for
znver5. The program that includes the header lowers only what its roots reach,
as `-c` does (`check_test_program_includes`), since the header's MMX functions
call builtins no row takes.

#148's rest of clang's x86 builtins, which no build of this compiler calls, join
the supplementary scope by family, each with its SemaX86 facts and the rows of
the existing families or of new ones:

- the unsigned compares of quadwords and words at 16 and 32 bytes
  (`ucmpq128_mask` to `ucmpw256_mask`), whose rows the compares' family had; a
  quadword's extract and the insertions of a byte and a quadword
  (`vec_ext_v2di`, `vec_set_v16qi`, `vec_set_v2di`), which had rows too;
- the rotates by an immediate at each width (`prold`, `prolq`, `prord` and
  `prorq` of 128, 256 and 512 bits), VPROL's and VPROR's: the count is the
  immediate's whole byte, which the instruction takes modulo the elements' bits
  as clang's funnel shifts take theirs;
- one vector or two by an immediate at the widths the inventory lacked: the
  aligns (`alignd`, `alignq`), the shuffles of 128-bit lanes (`shuf_f32x4` to
  `shuf_i64x2_256`), of floats and doubles (`shufps256` to `shufpd512`) and of
  words (`pshufhw256` to `pshuflw512`), the byte shifts and align of each lane
  (`pslldqi256_byteshift` to `psrldqi512_byteshift`, `palignr512`), the
  permutes by an immediate (`permdf256`, `permdf512`, `permdi512`, `vpermilpd`
  to `vpermilps512`, and VEX's `vperm2f128_*` and `permti256`) and VEX's blends
  (`blendps`, `blendpd`, `pblendd`, `pblendw256`). The immediate is the
  instruction's whole byte, which reads the bits clang's code reads: an align's
  low bits, a shuffle's and a blend's bit per element, a permute's bits 3 and
  7, which zero its lanes, and a byte shift past its lane, which gives zeros;
- the rest of the elements and lanes: the elements of 256-bit vectors and of
  8-byte ones (`vec_ext_v8si` to `vec_set_v32qi`, `vec_ext_v4hi` and
  `vec_set_v4hi`) at the index's low bits, a 256-bit vector's lane taken out
  into another register around an insertion and put back (VINSERTI32X4: an
  insertion into 16 bytes zeroes its register past them), and a float's
  (`vec_ext_v4sf`) into the floating result by VPERMILPS, its bits a float's
  where an integer result would be converted by its value; the lanes of 128
  and 256 bits inserted (`insertf32x4` to `inserti64x2_512`, and VEX's
  `vinsertf128` builtins as VINSERTF32X4) and extracted, under a mask into w
  (`extractf32x4_mask` to `extracti64x2_512_mask`) or not (`extract128i256`),
  at the index's low bits; and `insertps128`, INSERTPS's element of b into a
  under its zero mask;
- the ternary logic of 16 and 32 bytes merged under a mask, and zeroed under one
  at every width (`pternlogd128_mask` to `pternlogq512_maskz`); the
  concatenating shifts by an immediate (`vpshldd128` to `vpshrdw512`, VPSHLD's
  and VPSHRD's: a's and b's elements joined, as clang's funnel shifts join
  them, the count modulo the elements' bits); the sums of absolute differences
  (`dbpsadbw128` to `dbpsadbw512`, and VEX's `mpsadbw128` and `mpsadbw256` and
  AVX10.2's `mpsadbw512`), the carry-less products (`pclmulqdq128`, VEX's with
  PCLMUL, to `pclmulqdq512`) and the affine transforms of GF(2^8)
  (`vgf2p8affineqb_v16qi` to `vgf2p8affineinvqb_v64qi`), each with the
  immediate's whole byte; and the masks' other shifts (`kshiftliqi` to
  `kshiftridi`), whose count past the mask's bits gives 0;
- the rest of AVX-512's operations of floats by an immediate: the mantissas
  (`getmantps128_mask` to `getmantsd_round_mask`), the reductions
  (`reduceps128_mask` to `reducesd_mask`) and the roundings to a scale
  (`rndscaleps_128_mask` to `rndscalesd_round_mask`) into w under a mask, the
  ranges of two vectors (`rangeps128_mask` to `rangesd128_round_mask`) merged
  into w, the classes into a mask (`fpclassps128_mask` to `fpclasssd_mask`,
  the mask family's of an immediate) and the fix-ups (`fixupimmps128_mask` to
  `fixupimmsd_maskz`, the ternary family's, merged into a or zeroed). The
  64-byte and scalar ones take the exceptions' suppression their last argument
  asks for; a scalar one's immediate follows its mask (but getmant's), which
  the lowering finds past it;
- the compares of floats by a predicate: into vectors (`cmpps`, `cmppd`,
  `cmpss`, `cmpsd`, `cmpps256`, `cmppd256`, VEX's VCMPPS to VCMPSD), into masks
  under masks (`cmpps128_mask` to `cmpsd_mask`, and AVX512-FP16's
  `cmpph128_mask` to `cmpsh_mask`) and into ints (`vcomiss` and `vcomisd`, as
  `vcomish`: VCMPSS's and VCMPSD's opmask bit). A packed one's predicate is
  clang's code's: CGBuiltin's fcmp of the immediate's low 4 bits ignores the
  predicate's signaling, and LLVM selects that fcmp with the operands swapped
  for a greater-than (ULT as NLE, ULE as NLT, OGE as LE, OGT as LT), so the
  lowering does the same (`check_x86_lower_float_predicate`; FALSE and TRUE,
  which clang's code makes constants, are the instruction's predicates of
  them), and clang's code ignores a 64-byte one's rounding control. A scalar
  one's predicate is the immediate's whole byte, as clang's code passes it
  (`vcomiss`'s and `vcomisd`'s its low 5 bits, as `vcomish`'s, all the
  instruction reads), with the exceptions' suppression it asks for.

`test-checker`'s programs of them (`c_check_program_x86_rest_test.c`) call each
at its immediate's edges, and past them, where a pragma turns off clang's
`-Wargument-outside-range` and both compilers' code takes the immediate's low
bits (an index's, a predicate's), against clang -O0's code for znver5, run
where the host has the features. `test-check-builtins-x86` compiles every
supplementary fact's edges with clang (`check_builtin_x86_test_clang_supplementary`):
a function per value of the constrained argument, the others at values their
facts accept, declared at the prototype's types as clang spells them. Clang
must refuse exactly the values the fact refuses, with the fact's bounds.

Direct range failures are deferred diagnostic events of warning class with
default error severity. Source constant-evaluated context skips only these
ranges. Runtime suppression and proven call reachability independently select
ready, deferred or suppressed events. Unknown reachability cannot silently
discard one. These checks establish no target-feature or runtime capability.

For #487, `check_program_x86_diagnostics` resolves pending range events after
the checked program's control columns are complete, before any lowering. Its
worklist follows each function's entry, jumps, loops, switch dispatch and
expression evaluation edges. Constant conditions select their executable
edges; unknown conditions retain both. `sizeof`, `alignof`, short-circuited
operands and unreachable statements do not make a call executable. Initializer
parts, assembly operands and statement-expression regions supply their own
roots. A reached automatic compound-literal value follows its declaration's
initializer plan and queues its `CHECK_INIT_VALUE` parts; those runtime edges
are stored outside the leaf's graph operands. Nested compound literals use
the same bounded worklist, while dead or unevaluated literals supply no roots.
The driver reports each surviving event at the builtin's source
location, with the converted immediate and its valid bounds, through
`-Wargument-outside-range`'s default-error policy. Diagnostic pragmas can
downgrade or suppress it; `-w` suppresses it too. A failed or incomplete program
leaves unknown events for conservative reporting rather than dropping them.

Only an actual range event allocates the temporary reachability worklist.
It uses one visited byte and one 64-bit stack slot per graph node/control,
each queued at most once, and restores scratch on success and failure. The
scalar walk handles the dependent edges of this exceptional diagnostic path;
ordinary valid inputs retain the existing scalar/vector checking kernels.
`test-checker` compares independent source fixtures with Clang, one translation
unit per fixture, through both production driver schedules. It checks
acceptance, severity, complete range messages and source locations, including
negative immediates, dead control flow, unevaluated operands and pragma state.

For #562, `check_input_x86_contexts` supplies source diagnostic context before
`check_x86_action` consumes the sparse `CheckX86Call` records. An unselected
`__builtin_choose_expr` arm (or `_Generic` association) diagnoses its range
violations eagerly, even inside a dead statement or a short circuit. This is
separate from the call's executable reachability. Enclosing `sizeof`, `alignof`,
`typeof` and generic controlling operands still suppress range diagnostics;
hard immediate constraints and source-point pragma policy keep their existing
behavior. Selected arms continue through the deferred executable-graph walk.

This source diagnostic policy also covers bounds written inside `sizeof` VLA
type names: the recorded Clang 23 references accept `sizeof(int[n + BAD])`
without the range diagnostic, including selected and unselected choose arms
inside the bound. This does not make the VLA bound unevaluated at runtime;
the existing explicit VLA captures still execute. Isolated differential
fixtures pin this distinction, including the review reproducer with an
unrelated choose expression elsewhere in the same function.

The producer uses the retained selection roots and parser subtree bounds,
without reparsing or changing executable operands. Only inputs containing both
selections and x86 calls allocate its scratch: one u32 difference column with
one sentinel and two byte columns, six bytes per token plus four bytes. The
two bounded interval scans reuse the packed-member diagnostic's vector prefix
kernel and its scalar twin. Each x86 record receives `unselected_context` and
`runtime_diagnostic_suppressed`; these booleans need no token-wide retained
storage. Allocation failure marks the source input as a resource failure,
leaves call metadata untouched and restores scratch. Differential fixtures
exercise both driver schedules, nested selections, dead statements, unevaluated
contexts and pragma/`-w` policy; storage tests cover every allocation cut.

`c_check_builtin_vector.c` checks the 23 observed elementwise, reduction,
shuffle, conversion, nondeterministic and nontemporal spellings, and the 52
other generic vector builtins of clang 23 (#148), at catalog identities from
0x320 on (`cbuiltin_vector`):

- the floating elementwise functions of one operand (acos ... tanh, the
  roundings and canonicalize) take sqrt's rule, a floating scalar or a vector
  of floating elements, and those of two (atan2, fmod, pow, copysign and the
  maximum and minimum functions) the min/max rule restricted to floating ones:
  operands of one element type and lanes, a GNU vector and an
  `ext_vector_type` one alike, but in copysign, which compares its operands'
  types whole as clang's does;
- ldexp takes a floating value and an integer exponent of its lanes, a scalar
  one with the integer promotions, a vector one of any element size;
- ctzg takes clzg's rule, and clmul, pdep and pext two integer operands of one
  element type and lanes;
- reduce_xor takes a vector of integers, reduce_maximum and _minimum one of
  floating elements, and reduce_assoc_fadd (with an optional start value) and
  _in_order_fadd (with a required one) a vector of floating elements and a
  floating scalar start, converted to the element type as by assignment;
- the masked builtins take a mask of bools (an `ext_vector_type`'s), a pointer
  to a scalar, neither volatile nor atomic nor, for a store, const, and a load's
  passthru of exactly its result type, the `ext_vector_type` of the pointer's
  type and the mask's lanes; clang's vectors of pointers, structs, arrays,
  functions and void here are refused as unsupported, the type table having
  none;
- the matrix builtins are an error at every call, as clang's are without
  `-fenable-matrix`, which buster hasn't: no unit has a matrix type.

`test-checker` compares each family's valid calls and one error each with
clang's acceptance and first error line (`c_check_builtin_vector_graph_test.c`),
and `test-check-builtins-vector` their type relations with independent
expectations that clang confirms. Their lowering is in the checker guide's
"Generic vector builtins". The module's exact
type relations are separate from #147's ordinary vector operators. Shuffle
indices use the full bounded source slice; conversion carries a separate type
operand. Nondeterministic values inspect their argument's type without
evaluating it. Nontemporal stores retain explicit initialization policy and
conversions. Floating arithmetic/conversions retain environment and exception
obligations. Missing vector constant and complex memory representations stay
explicit; the module provides no numerical fold or runtime implementation.

An optional bounded shuffle-index witness distinguishes strict C ICE from a
GNU source extension. It contains one byte per scalar index, excluding the
first two vector arguments; its exact count is checked before access. A GNU
witness requires `allow_gnu_index_ice`. Null/zero preserves the original strict
local-ICE fallback. Source witnesses preserve payload, type, effect, fault and
range checks, and the producer retains the extension diagnostics. In
particular, a source-certified GNU fold must not retain a stale runtime shift
fault in its supplied value. Ordinary C complex-scalar initialization of a
vector nontemporal destination is rejected; matching complex memory types
retain their explicit payload gap. System-macro-specific conversion exceptions
are not attested by this ordinary source contract.

## Validation commands

On the approved 9700X, through a separate shared-host lock for each command:

```sh
./build/compiler test-builtins
./build/compiler test-builtin-intrinsic-types
./build/compiler test-builtin-intrinsic-features
./build/compiler test-check-builtins-x86
./build/compiler test-check-builtins-vector
./build/compiler test-builtin-intrinsic-catalog
./build/compiler test-builtin-intrinsic-inventory
./build/compiler test-check-builtins
./build/compiler test-builtin-lowering
./build/compiler test-check-varargs
./build/compiler test-builtin-varargs
./build/compiler test-tier0-variadic-frame
./build/compiler test-check-builtins-misc
./build/compiler test-check-builtins-query
./build/compiler test-check-builtins-bit-cast
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

The action modules, recipes, extractor, lookup and fixtures are original
material under the project's default license. Generated builtin metadata
retains its separate LLVM provenance and terms. The existing type, checker,
typed-stream, encoder and ABI interfaces are reused. Clang is an external
behavior and execution oracle; no Clang implementation or external fixture
corpus is imported.

The atomic RMW modules and fixtures are original C. Exact
[Clang 23.1.1 checking](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaChecking.cpp),
[atomic emission rules](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGAtomic.cpp),
[legacy builtin emission](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/CodeGen/CGBuiltin.cpp)
and [LLVM's atomic code-generation description](https://llvm.org/docs/Atomics.html#atomics-and-codegen)
supply behavior facts; their implementation and tests are not copied.
