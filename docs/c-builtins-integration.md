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

The optional `const_values` and `constant_evaluated` fields carry the unit's
constant objects' receipts and whether the action is in a constant context.
Only then does the operand's lvalue conversion in `check_action_operand`,
before `check_action_conversion`'s parameter conversion, read a const
object's constant initializer, as clang's evaluator does when it folds a
builtin call: the folded integer result without side effects is an integer
constant expression there (#990). The atomic action carries the same pair
into `check_builtin_atomic_argument`, which the typed-overflow prototype
conversion reuses. An ordinary call's runtime view doesn't request the read —
the graph still supplies `const_values` and only leaves `constant_evaluated`
false — so it reads none; volatile objects, mutable objects and
runtime-initialized const objects fold nowhere, and an overflow's store keeps
the call out of every constant context.

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
are accepted by the action kernel; wider overflow operands are runtime actions,
never folded, and past 128 bits a signed `_BitInt` of `mul_overflow`'s, as an
operand or the destination, is clang's error at that argument ("__builtin_mul_overflow
does not support 'signed _BitInt' operands of more than 128 bits",
`CHECK_DIAG_OVERFLOW_BIT_INT`, #1447). The runtime memory representation has a
narrower contract below.

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
Byte swaps and counts use fixed shifts, masks and arithmetic. An overflow
builtin's recipe computes its exact result as cheaply as its operands allow
(#1385, "The overflow builtins' instructions" below): one 64-bit operation of
operands of at most 32 bits, the 64-bit operation with its own carry or sign test
for a sum or a difference of operands of the destination's signedness, one U128
product for a product of a 64-bit operand, and two limbs for a sum or a
difference of mixed signedness. With a
128-bit operand or destination, the exact result is three limbs of 64 bits for a
sum or a difference and four for a product, whose columns' 64-by-64 products are
U128 multiplications of zero-extended limbs, less 2^128 times the other
operand's pattern for each negative operand; an unsigned product never sets its
top two limbs all ones, so it can't pass as a negative value. Generated
programs therefore require baseline x86-64; the host's vector publication does
not grant AVX-512, POPCNT, LZCNT or TZCNT to output code. Zero arguments to clz
or ctz remain undefined by the source contract, independently of the sequence's
incidental numeric result. Zero ffs returns zero.

### The bit builtins' instructions (#1374)

The source producer doesn't take the recipes for the byte swaps and counts
where an instruction computes them: `check_lower_bits_row`
(`c_check_program_lower.c`) lowers `__builtin_bswap16` to `__builtin_ffsll`
to an `X86` row of one instruction, as clang -O0's code does, by the function's
own target (its `-march` and `target` attribute), and keeps
`c_builtin_lower.c`'s recipe (whose module and tests are unchanged) for
popcount and parity at a target without POPCNT alone:

| builtin | x86-64 | with the feature |
| --- | --- | --- |
| `ctz` | `bsf` | `tzcnt` (BMI) |
| `clz` | `bsr`, then `xor` with the width less one | `lzcnt` (LZCNT) |
| `popcount` | the recipe | `popcnt` (POPCNT) |
| `parity` | the recipe | `popcnt`, then `and 1` |
| `ffs` | `bsf`, plus one, times whether the operand isn't 0 | `tzcnt`, likewise |
| `bswap16/32/64` | the operand moved into the result's register and `bswap` there (`bswap16`'s 32-bit swap shifted right by 16) | |

`bsf`, `bsr`, `tzcnt` and `lzcnt` agree on every operand but 0, whose ctz and
clz the source leaves undefined; `ffs`'s product makes its 0 whatever the
count's instruction gives. A row's operand is the parameter's class (`bswap16`'s
zero-extended to 32 bits), its result an int's, or the byte swap's unsigned
class, and the steps after it (the `xor`, the `and`, the sum and product, the
shift) are ordinary typed entries, which Tier 0 fuses as any. A function with
such a row is one with vectors (`Tier0Function.vectors`), as any function with
an `X86` entry is. The IR takes each row as its intrinsic (`tzcnt`, `bsf`,
`lzcnt`, `bsr`, `popcnt`, `bswap`: [the IR guide](ir.md)'s "Intrinsics"), a
pure operation rather than a row call, and the typed stream's interpreter
evaluates the rows by the same intrinsics' semantics (`ir_intrinsic_bits`).
`test-checker --program-bits` runs every one of them of boundary values at
`x86-64`, `x86-64-v2`, `x86-64-v3` and `znver5` by both paths against clang -O0's
build at the same `-march`, and checks that the rows' forms are the target's;
`test-checker-driver`'s Tier 1 fixture of them does so for `--tier1` at the
first three.

On the 9700X (CPU 2 under the lock, best of five repetitions per process, the
median of three rounds), the issue's kernel (`__builtin_popcountl(x) +
__builtin_ctzl(x | 1) + __builtin_clzl(x | 1)`, 20 M calls from a clang -O2
loop of a xorshift generator, which alone takes 21.6 ms) went from 90.8 ms
(Tier 0) and 84.2 ms (Tier 1) to 21.9 ms and 21.6 ms at `x86-64-v3` and
`znver5`, clang -O0's 22.2 ms and -O2's 21.6 ms, and to 35.7 ms and 33.6 ms
at `x86-64`, whose popcount stays the recipe (clang -O0: 29.7 ms). The byte swaps
of #1374's comment went from 44.2–44.4 ms and 42.9 ms to 21.6–21.8 ms at every
level, clang's time. The kernel is 16 instructions by Tier 0 and 11 by Tier 1 at
`x86-64-v3`, where it was 188 and 120. sqlite3.c's `-c` is unchanged (132.57 and
132.46 ms, its one `__builtin_clzll`). [The
record](https://github.com/buster14a/buster-performance/tree/28689ea8be9e0bbbe4e6b91a0371ed36093510b6/docs/measurements/bit-builtins-1374-2026-11-07)
has the commands, receipts and limitations.

### Generic and C23 bit instructions (#1652)

The generic bit builtins, `clzs`/`ctzs`, `clrsb*`, `bswapg`, and C23's
`stdc_*` counts and rounding operations use the same primitive rows as the
classic builtins. `check_lower_bits_operation` constructs each row from the
function's own target; `check_lower_miscellaneous_bits` supplies the required
32- and 64-bit row indices to `c_builtin_bits_lower.c`'s recipe. The optional
`CBuiltinBitsRows` descriptor stores each index plus one, with zero retaining
the recipe, and identifies counts whose instructions define the zero result.
The recipe appends those indices through its existing transactional scalar
and vector copies. The IR recognizes each row as the existing pure intrinsic.

- `popcountg`, `stdc_count_ones`, `stdc_count_zeros` and `stdc_has_single_bit`
  use POPCNT where enabled. Targets without it retain the SWAR population
  count.
- Leading counts use LZCNT where enabled. Values below the instruction's
  width are zero-extended and their excess leading zeros subtracted. Baseline
  BSR reads `x | 1`, then computes `width - 1 - index + (x == 0)`; no zero
  reaches the undefined intrinsic, including when the other limb of a wide
  value is nonzero.
- Trailing counts use TZCNT where enabled and BSF otherwise. A narrow value
  receives a sentinel bit just above its width, giving that width for zero.
  A full-width baseline BSF receives the top bit and adds one for zero.
- Defined zero counts remain the source width. `clzg`/`ctzg` then select their
  supplied fallback, which is evaluated once even for a nonzero operand.
  `clrsb*` counts the leading zeros after complementing a negative operand and
  subtracts its sign bit. The first-one/first-zero and complemented counts
  retain their existing corrections.
- `stdc_bit_width`, `stdc_bit_floor` and `stdc_bit_ceil` use leading counts.
  Their variable shifts are masked to a count below 64, and a separate range
  test clears an absent or overflowing bit; the IR never receives an
  out-of-range shift. Byte swaps use BSWAP, shifted down when the value is
  narrower than its machine form.

Operands of 65–128 bits still split into two limbs. Counts combine the two
defined limb counts; population counts add them. Byte swaps reverse the two
64-bit limbs, exchange them and remove the bits beyond the declared width.
Argument evaluation, signed-result extension, rotations, carry chains and
bit reversals retain their existing recipes.

`test-checker --program-bits` checks generic/C23 instruction forms and results
at `x86-64`, `x86-64-v2`, `x86-64-v3` and `znver5`, on both paths. Its boundary
fixture includes bools, enums, bit-fields, widths around 32 and 64, 65/96/100/127
and 128-bit operands, all-zero and one-zero limbs, and both extreme `int`
fallbacks. The Tier 1 driver fixture compares both paths and rematerialization
choices with Clang, including the wide forms. The existing IR-source fixture
also exercises generic rows under per-function target feature overrides.
These checks establish semantics and instruction selection; no additional
throughput measurement is claimed here.

### The overflow builtins' instructions (#1385)

`__builtin_add_overflow`, `sub_overflow` and `mul_overflow`, and clang's typed
ones (`sadd_overflow` to `umulll_overflow`, which reach the same runtime
builtins), were two-limb recipes for every type: the exact result's low and high
64 bits from the operands' sign words and an unsigned compare's carry, a 64-bit
product from four 32-by-32 products corrected for the signs by two more, then
both limbs compared with the destination's extension of the low one (66 to 103
instructions by Tier 0 where clang -O0 has 15). Now:

| operands and destination | exact result | overflow |
| --- | --- | --- |
| signed 64-bit, one class (`long`, `long long`, a signed `_BitInt(64)`) | the operation, typed | an `X86` row of `add`, `cmp` or `imul` and `seto` (`check_lower_overflow_row`) |
| operands of at most 32 bits, any destination | one 64-bit operation of the extended operands (an unsigned 32-bit product unsigned) | the destination's extension of the low bits differs, or past 64 bits the result's sign (`cbuiltin_lower_overflow_narrow`) |
| a 64-bit sum or difference of operands of the destination's signedness | the 64-bit operation | its carry (`low < a`), borrow (`a < b`) or the sign of `(low ^ a) & (low ^ b)` (`(a ^ b) & (a ^ low)`), or the low bits' mismatch (`cbuiltin_lower_overflow_same`) |
| a product with a 64-bit operand | the 64-bit product, and the 128-bit product's high limb, one `mul` or `imul` where the operands share an extension | both limbs against the destination (`cbuiltin_lower_overflow_product`) |
| a 64-bit sum or difference of mixed signedness | two limbs, as before | both limbs (`cbuiltin_lower_overflow_mixed`) |

The row's operation adds the second operand into the first's register or
multiplies it in (which a row may write), or compares them, then `seto` sets the
first scratch register's low byte and `movzx` extends it into the int result's;
the value stored through the pointer is the typed operation's, computed beside
it. A function with such a row is one with vectors (`Tier0Function.vectors`), as
any function with an `X86` entry is. The IR takes each row as its intrinsic
(`saddo`, `ssubo`, `smulo`: [the IR guide](ir.md)'s "Intrinsics"), a pure
operation of the operands' type, 1 where the exact result leaves its signed range,
which Tier 1 selects as the operation and `seto` ([the selection
guide](tier1-selection.md)), and the typed stream's interpreter evaluates the
rows by the same semantics (`ir_intrinsic_overflow`). The recipes' typed
operations are the same everywhere; 128-bit operands and destinations, and
`_BitInt`s of 65 to 127 bits (#1447: their descriptor is their width, their
high limb extended from it, and a destination fits where its high limb extended
from the width's bits past 64 gives the result's back), keep their limbs' recipe
(`cbuiltin_lower_overflow_wide`). Where an operand or the destination is a
`_BitInt` past 128 bits (#1447), the lowering computes the exact result in the
signed `_BitInt` that holds it and every destination value
(`check_lower_overflow_encompassing`: a sum's or difference's one bit past the
widest operand's signed width, a product's the signed widths' sum), as clang's
code does: both operands extended into its limbs, their sum or difference a limb
at a time, their product by libgcc's `__mulbitint3`, as GCC -O0's code makes
one; the destination takes its low limbs extended from its width, and the flag
is whether the bits from its top bit up (past its width, unsigned) differ from
its sign's (`check_lower_overflow_limbs`). Otherwise the destination stores its
value before the recipe's last entry, the flag, which stays on the value stack.

`test-builtin-lowering` runs every recipe natively against clang's builtins:
every ordinary signed and unsigned 8/16/32/64-bit and `_Bool` operand and
destination, `_BitInt` destinations (of 65 to 127 bits too, with 100- and 127-bit
operands), and `_BitInt` operands narrower than their
slot class beside whole ones (each recipe's: at most 32 bits, past them, an
unsigned 32-bit product past 2^63 into a signed `_BitInt(63)`, whose extension
of the low bits alone would give them back). `test-checker`'s
`overflow-instruction-rows` program runs signed longs', ints', unsigned longs'
and mixed calls of boundary values by both paths against clang -O0's build and
checks that the signed longs' rows are `add`, `imul` and `cmp` with `seto`;
`test-checker`'s `bit-int-65-to-127-overflow` program runs `_BitInt`s of 65 to
127 bits' and hashes their destinations' bytes, and `bit-int-past-128-overflow`
those past 128 bits' (to 300 bits, beside 129-bit, 128-bit, narrower and `_Bool`
ones); `test-checker-driver`'s Tier 1 fixture `tier1-overflow` runs them by
`--tier1` (a `_BitInt(200)` difference and an unsigned product among them), its
default warnings' fixture checks the signed `mul_overflow` error against clang's,
and `tests/ir-source/overflow-builtins.c` through the IR's lowerings, both
interpreters and Tier 1 (`test-ssa-source --typed --tier1`).

On the 9700X (CPU 2 under the lock, best of three repetitions per process, the
median of three rounds), loops of 4,096 elements, each called 20,000 times from a
clang -O2 driver: the issue's signed longs' running sum went from 119.8 ms (Tier 0)
and 68.9 ms (Tier 1) to 59.2 ms and 30.9 ms (clang -O0 48.4 ms, -O2 15.0 ms), and
their products from 241.9 ms and 169.7 ms to 103.6 ms and 48.1 ms (clang -O0
75.3 ms, -O2 30.4 ms). Over those and ints' sums and products and unsigned longs'
products, Tier 1 now takes 0.64 to 0.80 times clang -O0's time (it took 1.4 to 2.5
times) and Tier 0 1.2 to 1.6 times (2.5 to 3.4), the rest Tier 0's loops' own code.
`mul_overflow(long, long, long*)` is 15 instructions by Tier 0 and 9 by Tier 1,
where it was 102 and 54 (clang -O0 15). sqlite3.c's `-c`, which has no overflow
builtin, writes the same object in the same time. [The
record](https://github.com/buster14a/buster-performance/tree/9615dfdc39342638c4269e1930a29fb7553d1c88/docs/measurements/overflow-builtins-1385-2026-11-07)
has the commands, receipts and limitations.

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
but refused names would remain zero (none is now), and so would builtins the
checker checks but no lowering takes, with `BUSTER_CBUILTIN_CAP_CHECKER`
alone (none is now: the masked memory builtins and
`__builtin_elementwise_clmul` have their code, #148). The matrix builtins are an error at every
call, as clang's are without `-fenable-matrix`, so they have the source
capability and `__has_builtin` answers 1 for them, as clang's does. clang's sized legacy atomics
(`__sync_fetch_and_add_4` and the others) aren't catalog entries: the catalog
resolves each to its family's builtin, with its capabilities
(`cbuiltin_sized_entry`).
`test-preprocessor` checks the exact catalog and the advertised list, including
raw macro names, unknown bounded names and guarded input boundaries. An
advertised name also needs clang's own conditions for the unit, as clang's
`__has_builtin` has them: its row's language modes and required target
features (the preprocessor guide's "feature queries"; `CBuiltinSourceScope`
keeps each scoped intrinsic's, and `cbuiltin_languages` the catalog's). The
preprocessor's own builtins it answers 1 for are clang's: the target queries,
`__is_target_variant_os` and `__is_target_variant_environment` among them,
which it evaluates as clang's do (0 on Linux).

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

### Microsoft and z/OS lists (#148)

`__builtin_ms_va_start`, `__builtin_ms_va_end` and `__builtin_ms_va_copy`
(0x459 to 0x45b) and `__builtin_zos_va_start`, `__builtin_zos_va_end` and
`__builtin_zos_va_copy` (0x45c to 0x45e) take the varargs actions, as clang
checks them on x86-64 Linux.

- **Microsoft lists.** `__builtin_ms_va_list` is `char *` here. Each ms
  builtin binds its list as clang's `char *&`: an lvalue of exactly `char *`,
  never converted. Another type is refused, and so are qualifiers on the
  pointer or its `char`, and an rvalue (`p + 0`).
- **Microsoft `va_arg`.** `va_arg` of such an lvalue reads it as a Microsoft
  list in any function, as clang's `BuildVAArgExpr` does
  (`check_varargs_microsoft_list`). `char *const` or `const char *` is no list
  of either kind.
- **Starts.** clang's `checkVAStartABI` comes first.
  `__builtin_ms_va_start` belongs to an `ms_abi` function alone ("used in
  System V ABI function"). Every other start, z/OS's included, belongs to any
  other function ("'va_start' used in Win64 ABI function"). Then the function
  must be variadic, and the last-parameter check applies as for `va_start`.
  The driver words the three errors as clang does, at the builtin's name
  (`check_diagnostic_varargs`).
- **z/OS lists.** z/OS's lists are `char **` parameters, converted as a
  prototype's.
- **Code.** A Microsoft list's operations take its `char *`'s address
  (`CheckVarargsEffect.microsoft`).
  - `va_arg` reads the 8-byte slot it points at and moves it 8 bytes on, as
    clang's `EmitMSVAArg` does. A value of 1, 2, 4 or 8 bytes is in the slot.
    Any other is where the slot's pointer points: a 3- or 16-byte struct, a
    `long double`, a `_Complex double`, an `__int128`. An aggregate's value is
    that place's address.
  - `__builtin_ms_va_copy` stores one `char *` in the other.
    `__builtin_ms_va_end` and `__builtin_zos_va_end` emit nothing, as clang's
    `va_end` does on x86-64.
  - `__builtin_ms_va_start` sits only in `ms_abi` definitions, which the
    backend refuses.
  - `-c` refuses `__builtin_zos_va_start` and `__builtin_zos_va_copy` by name.
    clang's x86-64 code for them is LLVM's 24-byte `va_start` and `va_copy`
    over the `char *` that z/OS's list is. The owner decided on 2026-11-08
    that they stay refused (#1785).
- **Input counts.** The reader counts calls of these names among its varargs
  spellings, to size its tables. A spelling the unit never interned takes no
  comparison in that count.

clang's `-Wvarargs` warnings, such as "second argument to 'va_start' is not
the last non-variadic parameter", are checked here (`BUSTER_CHECK_VARARGS_WARN_*`)
but not yet rendered, for any start.

## Hints, memory and frame checking

`c_check_builtin_misc.c` checks hints, memory/string builtins and frame queries
without reading source memory. The result records converted arguments,
evaluation masks, warnings, terminal effects, memory effects and explicit
unsupported requirements. `__builtin_expect` converts both runtime arguments
to `long` and evaluates each once, in order, unless clang's evaluator reads its
first argument as an integer constant without side effects, a const object's
constant initializer among them (`const long k = 9; __builtin_expect(k, f())`):
then, as clang's code generation emits a builtin call that evaluates to a
constant, the call is that constant and neither argument runs, the expected
value's effects (a call, a volatile read) dropped. Its separate
`constant_value` and `constant_evaluated_operands` describe the source constant
evaluator's first argument only. Neither argument has a general ICE
requirement.

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
infinities and NaNs exactly for float, double, target x87 long double,
`_Float16` and `__float128` ("Signaling NaNs and the `_Float16` and
`__float128` constants" below).
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

### The rest of clang's classifications (#148)

`isnormal`, `issubnormal`, `iszero`, `issignaling` and `isfpclass` take a real
floating argument unconverted, as clang's `BuiltinFPClassification` does.
`isfpclass` also takes a vector of them, whose result is the signed vector of
its elements' width (`check_vector_mask_type`), and a mask, unconverted and
never evaluated, that must be an integer constant expression. Clang's
`BuiltinConstantArgRange` takes the mask's value sign-extended from its own
type's width, whatever its signedness, and that must be in [0, 1023]:
`(unsigned char)200` and `(_Bool)1` are errors. The mask's bits are LLVM's
`FPClassTest`: 1 signaling NaN, 2 quiet NaN, 4 -infinity, 8 -normal,
16 -subnormal, 32 -0, 64 +0, 128 +subnormal, 256 +normal and 512 +infinity.
The other classifiers are `isfpclass` of their own masks (264, 144, 96 and 1),
as clang's code is (`createIsFPClass`). `signbitf` and `signbitl` convert
their argument to `float` and `long double`, as their prototypes say, then
take its sign. Each folds as clang's evaluator does (APFloat's `isNormal`,
`isDenormal`, `isZero`, `isSignaling`, `classify() & mask` and `isNegative`),
an integer constant expression; a vector never folds. `canonicalize`,
`canonicalizef` and `canonicalizel` take their prototypes' conversions and
never fold: clang's rows aren't constexpr, so a call in a static initializer
is an error. Under `FENV_ACCESS` they may raise invalid
(`check_builtin_float_strict`), as fpclassify's and isinf_sign's compares do.

The lowering tests a classification's mask by a table of 64 bits, which the
recipe indexes by six bits of the argument's representation
(`cbuiltin_float_lower_class_table`): the exponent zero, the exponent all
ones, the integer bit (a float's or double's implicit one, its exponent's
being nonzero), the fraction below it zero, the fraction's top bit, and the
sign. A float's or double's entry is its class's bit in the mask. An x87
one's is what clang 23's -O0 code gives, which isn't the classes' bits.
LLVM's `expandIS_FPCLASS` tests a mask that one quiet compare can test by the
x87's compare (NaNs alone, zeros or NaNs, one infinity or NaNs, zeros,
subnormals or NaNs, or those masks' inverses), unless `FENV_ACCESS` makes the
code strict. It tests any other mask by integer tests of the encoding's
fields. In those, an unsupported encoding or a pseudo-denormal is a NaN when
the mask holds both NaNs, a pseudo-denormal is a zero or subnormal when the
mask holds both of those, and either is in no class otherwise: of a
pseudo-denormal, `isfpclass(x, 0xf0)` is 1, and `isfpclass(x, 0x60)` and
`isfpclass(x, 0x90)` are 0. `cbuiltin_float_lower_x87_class` models that for
x86's rules (`SETOEQ` and `SETUNE` aren't legal, `fabs` isn't free), and
gives clang's answers for all 1024 masks on every class of encoding, under
`FENV_ACCESS` and not. `signbitf` and `signbitl` are `signbit`'s recipes of
their converted arguments. `canonicalize` is the argument times 1.0 of its
class, as clang's `mulss`, `mulsd` and `fld1`, `fmulp` are: a signaling NaN
comes out quiet, an unsupported x87 encoding the default NaN, and a
pseudo-denormal normalized. `isfpclass` of a vector of floats or doubles
stores the vector into a frame object and classifies each lane by the scalar
recipe, the lane loaded into the recipe's argument, storing its 1 or 0 into
the result's frame object as an `int` or a `long long`, as clang's zero
extension of `llvm.is.fpclass`'s lanes gives (`check_lower_floating_lanes`);
a vector of half-precision or `__bf16` lanes stays refused, as their scalars
are, and so does one of `long double` elements, whose 16-byte lanes clang
gives a vector of `__int128` results.

`test-check-builtins-float` checks each fold against the host compiler's own
builtins in each format (`isfpclass`'s with every mask), the mask's rules and
the signbits' conversions, and clang's acceptance of the sources behind those
checks. `test-builtin-float-lowering` runs each recipe by both paths on float,
double and x87 inputs: the classifiers', the signbits' and canonicalize's
against the host's builtins and the x87 fixture of clang -O0's results, and
`isfpclass`'s, with every mask LLVM treats apart and every 37th, against the
model. `test-checker` (`c_check_program_classify_test.c`) checks the rules'
sources by both paths against clang, and runs programs natively against clang
-O0's builds: `isfpclass` with all 1024 masks on every class of float, double
and x87 encoding, under `FENV_ACCESS` and not; the other classifiers, the
signbits and canonicalize's results; folds; operands evaluated once; and
vectors of 4 to 64 bytes of floats and doubles, GNU's and clang's extended
ones, at x86-64-v4. Clang -O3's code differs from -O0's for eight masks of x87
values, so -O0's builds are the oracle.

### Signaling NaNs and the `_Float16` and `__float128` constants (#148)

`__builtin_nans`, `__builtin_nansf` and `__builtin_nansl` (catalog codes 0x3e4
to 0x3e6), and the `_Float16` and `__float128` forms of `nan`, `nans`, `inf`
and `huge_val` (0x3e7 to 0x3ee: `nanf16`, `nansf16`, `inff16`, `huge_valf16`,
`nanf128`, `nansf128`, `inff128`, `huge_valf128`), are floating builtins of the
checker's, as `nan` and `inf` are (`cbuiltin_float_constant_extra`,
`cbuiltin_float_nan`, `cbuiltin_float_signaling`), and `__has_builtin` answers
them. Their payloads are read as `nan`'s are, and each value is APFloat
`makeNaN`'s (`check_builtin_float_special`): the payload masked below the quiet
bit, the quiet bit set or, for a signaling NaN, clear, and a signaling NaN
without a payload given the bit below the quiet one (`nans("")` is
`0x7ff4000000000000`, `nansf16("")` `0x7d00`).

The checker's long double holds every format's NaN as the x87's own loads
place it: the format's fraction at the significand's top, so that bit 62 is
every format's quiet bit (`check_action_nan_bits`). A `__float128` keeps its
own bits (#1250, `c_check_quad.c`), a NaN's whole 111-bit payload among them
(payloads are read modulo 2^128), and its long double mirror holds its
fraction's top 63 bits there. The value keeps its bits until a conversion to another format quiets it,
as APFloat's does (`check_action_convert_nan`, which also truncates the
payload to the narrower format's fraction, so that a NaN narrowed then widened
keeps only what the narrow format held), so a conversion to the value's own
format, the rounding of a NaN, and a unary `+` or `-` (a sign change alone, as
`changeSign` makes it) leave a signaling NaN signaling.

Static data, aggregates' initializers and function code write a NaN's bits from
that encoding, its payload and quiet bit among them (`check_data_float_bytes`,
`check_lower_float_bits`). Main wrote the canonical NaN, so `static double d =
__builtin_nan("5")` lost its payload (`0x7ff8000000000000` where clang writes
`0x7ff8000000000005`), and so did `nanf` and `nanl` in static data and `nanl`
in code. The classifications and comparisons fold of `_Float16` constants,
their least normal `0x1p-14`, and of every `__float128` constant, by its
mirror's class or its bits' order. They still refuse a run-time `_Float16` or
`__float128` argument by name, as before.

`test-check-builtins-float` checks the constructions and refusals, and
`test-checker --program-float-constants` (`c_check_program_constants_test.c`)
checks sources by both paths against clang (folds in constant expressions,
result types, static and aggregate initializers, refusals) and runs programs
natively against clang -O0's builds: each builtin's bytes in static data, in
an aggregate's initializer and in code, with payloads, signs, negations and the
conversions that quiet a signaling NaN, and `nan`'s, `nanf`'s and `nanl`'s
payloads in static data.

## Helper contracts and direct forms (#1059)

A helper is the fallback for an operation, not its representation. Behind a
helper's call the code generators see only an unknown call, which constrains
optimization, spills the caller-saved registers live across it, and costs a call
and a return. `c_check_program_helpers.c` publishes what each helper family's
operations are, so that a consumer can take one directly where the contract and
its tests support it, and must take the helper, or refuse, where they don't.

Each family's contract (`check_program_helper_contracts`) gives the effects every
one of its operations may have, which a caller keeps whatever code implements
it: memory read or written through an address it is given (`READS`, `WRITES`),
an access of the object's width atomic as other threads see it (`ATOMIC`), a
memory order (`ORDERS`; every helper strengthens its order to `seq_cst`), no
access moved across it or eliminated past it (`BARRIER`, a call's), the floating
environment read and its exceptions raised (`FP_ENVIRONMENT`, `FP_EXCEPTIONS`),
errno set (`ERRNO`), no return (`TERMINATES`), a return it may not make, which a
consumer assumes neither way (`MAY_TERMINATE`), and effects past these, which a
consumer keeps whole (`UNMODELED`). The two termination effects are the
operation's own control flow: a defined non-return, or code whose control flow
the contract can't bound. A fault is its access's or its operation's (an atomic
load through a bad pointer faults as a plain load does, and a floating
exception traps only where the program unmasks it), which a direct form keeps
by those instructions' own semantics (`x86_semantics.c`'s
`BUSTER_X86_EFFECT_FAULT` and `BUSTER_X86_EFFECT_FP_EXCEPTION` per form), not a
call's effect. Apart from them, its requirements are what a
direct form of the operation needs of the function it is placed in, which a
helper's call doesn't pass to its caller (the call's convention covers the
registers, and the helper is compiled for the unit's target): target features
its instructions need (`FEATURES`) and fixed or implicit registers past the
convention (`FIXED`). `check_program_helper_family` takes a helper's family by
what names it, and a helper no family names (a load or store of another id, a
read/modify/write without its rule, a new helper before its family has its row)
is `unmodeled`'s, with every effect as a bound (it may not return, never that it
doesn't), never a neighbour family's.
`check_program_helper_effects` narrows the family's set to a planned helper's
(`load_n` reads, `store_n` writes; only clang's IEEE maximum and minimum read
the floating environment among the read/modify/writes; a math builtin's facts).

| Family | Effects | Requirements | Direct form |
| --- | --- | --- | --- |
| `atomic-access` (`load_n`, `store_n`, C11's, an `_Atomic` object's) | reads or writes, atomic, orders, barrier | none | an aligned integer's, enumeration's or pointer's, of a constant order: the typed stream's marked `LOAD` or `SET`; a seq_cst store emits memory `xchg`; an implicit `_Atomic` object's aligned load or store, and an update's first load, seq_cst's |
| `atomic-rmw` (exchange, compare-exchange, fetch ops, test-and-set, clear, `__sync`) | reads and writes, atomic, orders, barrier; the IEEE maximum and minimum also the floating environment and exceptions | fixed registers | an aligned integer's or enumeration's exchange, or addition or subtraction returning the old or the new value (a pointer's exchange and a test-and-set's too), of a constant order: the typed stream's `ATOMIC_RMW` (`xchg` or `lock xadd`); an implicit `_Atomic` integer's increment, decrement, `+=` or `-=` |
| `fence` (thread, signal, `__sync_synchronize`) | orders, barrier | none | a constant order: the typed stream's `FENCE` |
| `terminal` (`trap`, `unreachable`) | terminates | none | none |
| `flt-rounds` | floating environment, barrier | none | none |
| `math` (sqrt to rint, copysign) | floating environment, exceptions, errno (sqrt), barrier | none | none |
| `x86-leaf` (`c_builtin_x86_helpers.c`) | memory, orders, floating environment, may not return, unmodeled, barrier | features, fixed registers | none |
| `asm` (`c_check_program_asm.c`) | memory, orders, floating environment and exceptions, may not return, unmodeled, barrier | fixed registers (a template's instructions are its author's: the assembler takes every form whatever the target, as GNU as and clang's do, so no feature the compiler gates) | a statement of register, memory and constant operands whose records name no register but its operands' and whose forms a Tier 0 row takes: an `X86` entry in its caller (`check_asm_row`, [the inline assembly guide](c-inline-assembly.md)'s "Placed in the caller") |
| `unmodeled` (a helper no other family names) | every one as a bound: memory, atomic, orders, floating environment and exceptions, errno, may not return, unmodeled, barrier | features, fixed registers | none |

A helper's physical effects are `x86_semantics.c`'s, read from its records
(`check_program_helper_physical`), never a list of this file's: the resources
they read and write (general and vector registers, flags, MXCSR, the x87's
control word), their forms' memory effects (a fence, a locked access) and
properties (a fault, a floating exception), whether they read or write an object
a pointer names (an access based on `rsp`, on `rbp` where the code has a frame
pointer, or on `r15` where it has a realigned frame's base is its frame's), and
whether the code returns or traps. A record of a form the
tables don't cover has only the conservative record, which says nothing of it,
so it is counted apart, and a consumer takes its helper as one of unknown
effects. `test-checker`'s contract check (`check_test_program_helper_contracts`)
makes every helper the emitters make (379: the atomic loads and stores of every
width, the three fences, every read/modify/write of every width and format, both
terminal builtins, FLT_ROUNDS, the 14 math builtins and the 112 x86 leaves, AMX's
among them; each family's exact count, which a helper an emitter adds or stops
making changes; and four unnamed helpers with a trap's, sqrt's, a fence's and a
store's code, which keep `unmodeled`'s bounds) and checks each against its
contract: no callee-saved register
written but those its published frame restores, no object written or read, no
fence or locked access, no floating environment read or exception raised, that
its contract doesn't have (but where it is `UNMODELED`, or the old-value read
of a store's exact width-matched `xchg [rdi], rsi; ret` body), no trap without a
return its contract doesn't allow, and each claim backed by its code but where
the contract is `UNMODELED`, whose effects are bounds (a load reads its object
and a store writes it with a locked exchange whose old value dies in caller-saved
`rsi`; a
read/modify/write writes its object with a locked access; a thread fence fences
and a signal fence only returns; a terminal builtin traps and doesn't return; a
claim of the floating environment reads MXCSR or the x87's control word; errno
is written). Its one gap the tables don't cover where the contract models every
effect, the IEEE maximum's and minimum's `btr` and `bts` of an immediate, the
test names for those helpers alone, and checks every uncovered record against
it, not the first alone; the x86 leaves' system instructions (`pause`, `rdtsc`, Key
Locker's) are uncovered too, under their family's `UNMODELED`.

**Direct fences.** `check_program_direct_fence` admits `__atomic_thread_fence`,
`__atomic_signal_fence`, `__c11_atomic_thread_fence` and
`__c11_atomic_signal_fence`, unscoped, of an order the checker knows that is one
of C's (0 to 5) and whose argument evaluates for no effect (no side effect, and
nothing of the floating environment: a conversion under `FENV_ACCESS`, which
clang evaluates at run time), and `__sync_synchronize`. The helper planner and the lowering ask it alike: the
planner plans no helper for the node, and the lowering writes the typed stream's
`FENCE` of its order and scope (`BUSTER_TYPED_FENCE_SIGNAL` for a signal fence)
in the helper's call's place, its constant argument evaluated for nothing. Every
consumer keeps it: Tier 0 emits `mfence` for a thread fence of `seq_cst` and
nothing for the others, as clang does ([the Tier 0 guide](tier0.md)), the typed
interpreter runs it as nothing, `ir_lower` makes it the IR's `fence`, which no
optimizer moves anything across, eliminates anything past or drops, and Tier 1
selects it to `mfence` or nothing ([the IR guide](ir.md)'s "Fences"). A fence of
a dynamic or out-of-range order (which the helper strengthens to `seq_cst`,
where clang emits a switch over the orders or nothing), of an argument with an
effect or the floating environment's, or of a scoped family keeps its helper. On SQLite 3.50.4,
`sqlite3MemoryBarrier`'s `__sync_synchronize` went from
`sub rsp, 8; call <helper>; add rsp, 8; ret` to `mfence; ret`, and the unit has
one helper fewer; c-testsuite's subset, which has no fence, compiles to the
same bytes as before.

**Direct atomic loads and stores.** `check_program_direct_access` admits
`__atomic_load_n`, `__atomic_store_n`, `__c11_atomic_load` and
`__c11_atomic_store`, unscoped, of an integer's, an enumeration's or a pointer's
object of 1, 2, 4 or 8 bytes, its value's size, known aligned to it (a native
one, for C11's), of an order the checker knows that its kind takes (a load's
relaxed, consume, acquire or seq_cst; a store's relaxed, release or seq_cst) and
whose argument evaluates for no effect, as a direct fence's. The lowering
(`check_lower_atomic_direct`) evaluates the pointer and, for a store, the value
converted to the object's type, and writes the typed stream's `LOAD` or `SET` of
that type's class with the atomic marker (`BUSTER_TYPED_ACCESS_ATOMIC` and the
order). An implicit `_Atomic`
object's load and store, and an update's first load, are direct too
(`check_lower_atomic_direct_plan`): its plan's address and buffers stay, and its
leaf's call becomes the marked access of its bits' class, of seq_cst. Loads and
relaxed/release stores compile as ordinary accesses (`mov`, `movzx`; atomic when
naturally aligned on x86-64, whose total store order gives their orders). A
seq_cst store emits a width-matched memory `xchg` without `mfence` in both native
tiers (#1396). The typed stream and IR keep its write-only store semantics, so
an initializing store does not read undefined bytes. An implicit assignment
continues to return its converted desired value, including float, bool and
pointer values, from its unchanged desired buffer. Tier 0's value-returning
`STORE` saves the desired result, and Tier 1 copies a live desired SSA value into
reserved `r11`, before the exchange destroys its value register. The IR keeps
the store as an atomic access
([the IR guide](ir.md)'s "Atomic accesses") that no pass splits, merges,
eliminates or moves past another access, a call or a fence. The memory exchange
supplies the full barrier; standalone seq_cst fences still emit `mfence`. No
performance measurement is claimed for this change. A dynamic or invalid order (which the helper strengthens
to seq_cst), a scoped family's, a floating object's and an unaligned or wider
one keep their helper or libatomic's call. Native store helpers likewise use
`xchg` and continue to strengthen every order they receive to seq_cst; this does
not strengthen the inline relaxed/release stores. The planner reserves no leaf for an
implicit object's access or update (its update loop's compare-exchange is
direct: "Direct compare-exchanges", below), and a width's load, store and
exchange leaves only where a C11 or generic load, store or exchange of a native
object takes its plan (a generic form's, or a representation's: a floating or
another object's builtin, that keeps one). The compiler's own `main.c`, every function lowered
(`--lower-all`), takes 91 atomic accesses directly and keeps 8 helper calls of
them (its tests' dynamic orders); SQLite 3.50.4's 38 are all direct, and the
unit plans no helper.

**Direct read/modify/writes.** `check_program_direct_rmw` admits an exchange
(`__atomic_exchange_n`, `__sync_lock_test_and_set`, `__sync_swap`,
`__c11_atomic_exchange`, and `__atomic_test_and_set` of its byte with 1) and an
addition or a subtraction returning the old value or the new one
(`__atomic_fetch_add` and `_sub`, `__atomic_add_fetch` and `_sub_fetch`, the
`__sync` ones, `__c11_atomic_fetch_add` and `_sub`), unscoped, of an integer's
or an enumeration's object (an exchange's of a pointer's or a bool's too) of 1,
2, 4 or 8 bytes, its value's size, known aligned to it (a native one, for C11's),
of an order the checker knows (any of C's: the locked access is a full barrier,
which every order takes; a `__sync` builtin's is seq_cst) whose argument evaluates
for no effect, its effect's shape its rule's. The planner and the lowering ask it
alike; the lowering (`check_lower_atomic_rmw_direct`) evaluates the pointer and
the operand converted to the object's type (negated for a subtraction), writes the
typed stream's `ATOMIC_RMW` of the object's class (`BUSTER_TYPED_RMW_ADD` for an
addition, and the order), and for a new value adds the operand to the old one in
`int` (a narrow object's) and converts it back. An implicit `_Atomic` integer's
or enumeration's increment, decrement, `+=` or `-=` whose operands are computed as
integers is one too (`check_program_atomic_update_direct`): its update's
compare-exchange loop becomes one `ATOMIC_RMW` addition of the operand narrowed to
the object's width, and its plan reserves no compare-exchange leaf. Tier 0 emits
`xchg` or `lock xadd` of the object's width with the operand in its register,
the old value extended as the class's load extends it ([the Tier 0
guide](tier0.md)); the IR keeps it as `rmw` ([the IR guide](ir.md)'s "Atomic
read/modify/writes"), an atomic access no pass moves, merges or drops, and Tier 1
selects the same instructions, the object's width and never its narrow value's
32-bit register's, as a barrier no load folds past. clang -O0's code is the same
(`xchg`, `lock xadd`); -O2's has a `lock add` where an addition's result is unused.
The bitwise, minimum and maximum operations are loops of compare-exchanges
("Direct fetch loops", below); the floating operations, a dynamic order and a
scoped family's keep their helper; an implicit update of another operation keeps
its loop, whose compare-exchange is direct. The compiler's own `main.c` calls 3
read/modify/write helpers where it called 19 (16 direct; 2 helpers planned where
7 were), and with every function lowered 305 where it called 403 (102 direct; 152
planned where 176 were); SQLite 3.50.4 has none, and its object is unchanged.

**Direct compare-exchanges.** `check_program_direct_cas` admits
`__atomic_compare_exchange_n`, `__c11_atomic_compare_exchange_strong` and
`_weak`, `__sync_bool_compare_and_swap` and `__sync_val_compare_and_swap`,
unscoped, without a `__sync` builtin's protected variables, of an integer's, an
enumeration's or a pointer's object of 1, 2, 4 or 8 bytes, its value's size,
known aligned to it (a native one, for C11's), of orders the checker knows (any
of C's for either, `lock cmpxchg` being a full barrier; a `__sync` builtin's is
seq_cst) whose arguments, and GNU's weak flag, evaluate for no effect. The
lowering (`check_lower_atomic_cas_direct`) puts the pointer in a temporary, which
the typed stream's `ATOMIC_CAS` names, and pushes the expected value and the
desired one, converted to the object's type; `ATOMIC_CAS` of the object's class
leaves the old value, which is the val form's result and whose equality with the
expected one (in `int`, for a narrow object's) is the others'. GNU's and C11's
expected value is where their second argument points: the lowering reads it
there after evaluating the desired value, as clang does, and writes the old
value back there only where the exchange failed, as their leaf does. Every
plan's compare-exchange is direct too (`check_lower_atomic_direct_plan`): a
generic `__atomic_compare_exchange`'s, a C11 one's of a representation, and the
loop of an implicit `_Atomic` update that isn't an integer's addition or
subtraction (`*=`, `<<=`, a floating object's `+=`, a bool's), which so calls
nothing; its expected buffer takes the old bits whether or not it succeeded, as
the leaf leaves it. Tier 0 swaps the expected value into `rax` where it isn't
there, loads the address from its local's home into the stack's first free
register, and emits `lock cmpxchg` of the object's width, swapping back and
extending the old value as the class's load does ([the Tier 0
guide](tier0.md)); the IR keeps it as `rmw ... cas` ([the IR guide](ir.md)'s
"Atomic read/modify/writes"), and Tier 1 selects `lock cmpxchg` around `rax` the
same way. A C11 compare-exchange of a floating object (whose bits its leaf
compares), a dynamic order and a scoped family's keep their helper. The
compiler's own `main.c` calls 1 read/modify/write helper where it called 3 (18
direct; 1 helper planned where 2 were), and with every function lowered 281
where it called 305 (126 direct; 144 planned where 152 were); SQLite 3.50.4 has
none, and its object is unchanged.

**Direct fetch loops.** `check_program_direct_fetch` admits the bitwise, minimum
and maximum read/modify/writes, returning the old value or the new one: GNU's
`__atomic_fetch_and`, `_or`, `_xor`, `_nand`, `_min` and `_max` and their
`_fetch` forms, the `__sync` ones (clang's `__sync_fetch_and_min`, `_max`,
`_umin` and `_umax` too) and C11's `__c11_atomic_fetch_and`, `_or`, `_xor`,
`_nand`, `_min` and `_max`, unscoped, of an integer's or an enumeration's object
(a bool's for and, or and xor alone, whose bits stay 0 or 1) of 1, 2, 4 or 8
bytes, its value's size, known aligned to it (a native one, for C11's), of an
order the checker knows (any of C's; a `__sync` builtin's is seq_cst) whose
argument evaluates for no effect, its effect's shape its rule's. x86 has no
instruction that leaves such an operation's old value, so that clang's code is
a `lock cmpxchg` loop, and so is this one (`check_lower_atomic_fetch_direct`):
the pointer and the operand converted to the object's type into temporaries, the
object read by a relaxed atomic `LOAD`, and a loop that computes the new value
from the old one (in `int` for a narrow object, converted back; nand's and
complemented; the minimum or the maximum by the value type's ordering, an arm
of each), runs `ATOMIC_CAS` of the old value and the new one, and goes round
again, the old value its result, until that result is the old value. Its result
is the old value or the kept new one. No helper is planned for one, and no new
typed kind or IR operation is needed. clang's wrapping increment and decrement,
the floating minimums and maximums, a pointer's, a dynamic order and a scoped
family's keep their helper. The compiler's own `main.c` calls no
read/modify/write helper where it called 1 (19 direct; none planned where 1
was), and with every function lowered 194 where it called 281 (213 direct; 112
planned where 144 were); SQLite 3.50.4 has none, and its object is unchanged.

**Coverage.** `--helper-report` prints on standard error, after the command, a
line of the totals over its units (`helpers`, with `units`, `refused`: units the
lowering refused an operation by name, `direct`: the lowered code's direct
forms, an atomic access's marked `LOAD`, `STORE` or `SET`, a read/modify/write's
`ATOMIC_RMW` or `ATOMIC_CAS`, a fence's `FENCE` and an
asm statement's `X86` entry in its caller, `helper`: the
lowered code's call sites of a helper,
an explicit builtin's, an implicit `_Atomic` access's, a generic form's and an
asm statement's alike, and `planned`: helpers emitted) and a line per family
(`helpers.fence`, `direct=`, `helper=`, `planned=`). A family without a direct
form counts none: the report reads only the entries of the families that have
one. The planner keeps no count of its own: its helper uses are its `uses`.

**Tests.** `test-checker`'s fence section (`c_check_program_fence_test.c`)
lowers fences of every order and scope and the helper's cases by both paths,
checks each function's `FENCE` entries and values, its calls, and Tier 0's
records by both twins as `x86_semantics.c` reads them (an `mfence` between a
store and a load where the source has a `seq_cst` fence there, none for the
other orders), the lowered code's direct fences and the plan's helper uses, then runs them natively against clang -O0's
build. A function with a statement expression has fences over a live value: its
lowering tracks its stack (`check_lower_track`, as every function of a program
with a terminal helper does), where a `FENCE` neither pops nor pushes; the
compiler's own `main.c` by `-c` and `--lower-all` is the larger case. `test-checker-driver`'s fence unit (`c_check_fence_driver_test.c`) is the
concurrent control a sequential interpreter can't be: a store-buffering litmus,
built by both paths, by Tier 0 and by `--tier1` (every function Tier 1's),
whose threads each store, fence and load, run 100,000 iterations without a
thread fence (a signal fence, a compiler barrier alone: the control) and with a
`seq_cst` one. The fenced runs must never show both loads reading 0; the
control's count shows the machine reorders without the fence. On a 4-CPU cloud
container, over two runs of the four builds, the control showed both loads
reading 0 in 58 to 41,194 of 100,000 iterations (clang -O0's build of the same
program: 53,347), and the fenced litmus in none. The control may show none on a
run whose threads never overlap, which leaves the fenced run's check weaker,
not wrong; the test prints both counts. It also reads each build's
`--helper-report` and its objects' fenced threads (one `mfence` each, the
control's none). The same litmus runs twice more with atomic accesses: a
seq_cst store and a seq_cst load (the direct marked `SET` and the
marked `LOAD`), which must never show both loads reading 0, and relaxed ones, the
second control; a seq_cst thread's object has one memory `xchg` and no `mfence`,
and the relaxed one's has neither. In the earlier #1059 baseline using
`mov`/`mfence`, the relaxed control on that container showed both loads reading 0 in
50 to 18,745 of 100,000 iterations, and the seq_cst runs in none.
`test-checker`'s atomic-access section (`c_check_program_atomic_access_test.c`)
lowers loads and stores of each width and order, C11's, a pointer's and an
implicit object's by both paths, checks each function's marked entries, its
fences, its calls and Tier 0's records by both twins, the lowered code's marked accesses and the plan's helper uses,
then runs every width and order, the implicit object's loads, stores and
updates and the helpers' cases natively against clang. The seq_cst store checks
require the exact exchange width and no separate fence, and a following explicit
fence must survive. `test-tier1-select` keeps the desired value live across each
store width and order, with direct and folded addresses and guard bytes;
`test-tier1-emit`'s certificate rejects a store weakened to `mov` or missing its
preserving copy. `test-tier0`'s depth fixtures keep live values below `SET` and
value-returning `STORE` at the register table's boundary and beyond it.
Its read/modify/write
section (`c_check_program_atomic_rmw_test.c`) lowers exchanges, additions and
subtractions of each width, the `__sync`, C11, pointer and test-and-set forms
and an implicit object's increment by both paths, checks each function's
`ATOMIC_RMW` entries and values, its calls and Tier 0's one locked access by both
twins, the helpers' cases (a compare-exchange, a bitwise operation, a dynamic
order, an implicit multiplication's loop), the lowered code's read/modify/writes and the plan's helper uses, then runs every
width, operation and form natively against clang. `test-checker-driver`'s counter
unit is the read/modify/writes' concurrent control: four threads add to shared
counters by every direct form (two of them a short's and a byte's, which wrap)
and take a spin lock by an exchange around a plain increment, by both paths, by
Tier 0 and by `--tier1`; every count must be exact, and a volatile counter's
increments, the control, show the threads overlap (on the 4-CPU container under
the other parts' load, 1,000,000 and 3,225,039 of 4,000,000 kept in two of the
four builds; a run whose threads take turns on one CPU keeps them all, which
leaves the exact counts weaker, not wrong). It found Tier 1 selecting a byte's
and a short's `xchg` and `lock xadd` at 32 bits, writing past the object, which
`test-tier1-select` now runs on guard bytes. `test-checker-driver`'s report unit
pins `--helper-report`'s direct forms and calls of a unit with every kind of
each, and Tier 1's selection tests keep a load from folding past a fence of any
order, an atomic load or a read/modify/write, and an atomic load from folding at
all.

**Paired measurements** (#1059). `--helper-forms=LIST` gives every operation
of the listed families (`fence`, `atomic-access`, `atomic-rmw`, `asm`, or
`all`) its helper's call where it has a direct form
(`CheckProgramOptions.helper_forms`, read by the `check_program_direct_*`
predicates the planner and the lowering ask alike, and by the asm planner): the
code the compiler made before #1059, of the same compiler, for paired runs. An
implicit `_Atomic` object's accesses have no helper to take and stay the typed
stream's. `measure-helpers` (`helpers_measure.c`) builds each program four
ways, by Tier 0 and by `--tier1`, each as it lowers it and with
`--helper-forms=all`. Its programs are the primitives' kernels, single-threaded
loops of a concurrent program's operations (reference counts by `lock xadd`, an
uncontended spin lock's acquire exchange and release store, a sequence lock's
acquire and release accesses and fences, flags by `fetch_or`, `fetch_and` and
`xor_fetch`, a running maximum by a weak compare-exchange, and inline asm's
`bswap`, `roll` and `leaq`), and speedtest1, whose SQLite's relaxed
`AtomicLoad` and `AtomicStore` (38 of them, its VDBE loop's interrupt test among
them) are its direct forms. For each object it reports the families' direct
forms and helper calls, its executable bytes, and Tier 1's spill and call stores
and loads (the save and reload traffic: Tier 0 saves the values live across
every call); every build's output must be the first's; then it times each
source's `-c` compile and each program's run in a Williams design's order, as
`measure-tier1-programs` times its programs. The 9700X job runs it where the
direct forms' admission, the helpers' planning or the measurement changes (the
`helpers` gate), and on the daily and dispatched runs.

On the 9700X ([buster-performance's
record](https://github.com/buster14a/buster-performance/tree/main/docs/measurements/helpers-direct-1059-2026-11-04),
#1264's run 41764205497, `znver5`, medians of 11 runs and 5 compiles), every
direct form is faster than its helper by both tiers. Run time, helpers / direct:

| Program | Tier 0 | `--tier1` |
| --- | ---: | ---: |
| `sequence` (fences, acquire and release accesses) | 46.8 | 101.6 |
| `lock` (acquire exchange, release store) | 4.37 | 4.84 |
| `swaps` (inline asm) | 3.06 | 1.00 (helpers either way) |
| `counts` (`lock xadd`) | 1.12 | 1.12 |
| `maximum` (weak compare-exchange) | 1.07 | 1.65 |
| `flags` (compare-exchange fetch loops) | 1.03 | 1.05 |
| speedtest1 (38 relaxed accesses) | 1.00 | 1.02 |

A helper strengthens every order to `seq_cst`, so the runs measure that as well
as the call: a release or acquire fence is no instruction where its helper is an
`mfence`, and a release store is a `mov` where its helper is an `xchg`. The
fetch loops gain least, since each iteration is a `lock cmpxchg` either way:
Tier 0's direct loop runs more instructions than the helper's leaf (510 M against
465 M) but takes 3% fewer cycles. So no family keeps its helper for want of a
gain. Every range of 11 runs is apart from its pair's, except two: speedtest1 by
Tier 0, and `swaps` by `--tier1`, which runs the same code either way.

The objects' facts:
- **Helper calls.** The primitives' helpers builds make 25 calls where the
  direct ones make none (Tier 1's make 4: the asm statements keep their helpers
  under `--tier1`). speedtest1's make 38 where the direct ones make none.
- **Code size.** The primitives' text is 2,275 bytes against 2,947 by Tier 0,
  and 1,875 against 2,579 by `--tier1`. speedtest1's is 687 bytes smaller by
  Tier 0 and 2,346 by `--tier1` (1,415,994).
- **Save and reload traffic.** The primitives' Tier 1 code has 5 call stores
  and 5 loads, where the helpers' has 27 of each. speedtest1's has 47 fewer of
  each, and 14 fewer spill stores and 16 fewer spill loads.
- **Run time.** speedtest1 by `--tier1` runs 1.6% faster, in time and in
  cycles (203.5 against 206.9 ms), and by Tier 0 the same.
- **Compile time.** speedtest1's `-c` is unchanged: 112.4 against 113.7 ms by
  Tier 0, and 464.7 against 464.8 by `--tier1`. Instructions are equal to
  within 0.03%.
- **The primitives' compile.** At 2 ms its times follow the rotation's order,
  not the compile's cost: a helpers build runs right after its direct build in
  4 of the 5 rounds, and takes the same time in the round where it runs first.

**Pending** (#1059): C's volatile accesses carried to the IR as such
([the IR's guide](ir.md)'s "Effects through the consumers"), asm statements of
fixed registers, labels, the x87's operands or flag outputs in their callers
(each keeps its helper), and Tier 1's IR for an asm statement's row.

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
return zero-extended raw bits; width-specific memory XCHG stores and MOV loads
provide conservative sequential consistency. The store helper discards the old
bits in caller-saved RSI, preserving its void result and raw-value ABI. Thread/sync fences
use MFENCE. Signal fences require no machine instruction. A fence of a constant
order is no helper's call at all, but the typed stream's `FENCE` ("Helper
contracts and direct forms", above). Every helper CALL,
including a signal fence's, must remain an unknown-effect compiler barrier;
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
  lowering, which takes the answer of its function's emission, from the size and
  the pointer's type that the query read after their value conversion (an
  `_Atomic` pointer's lvalue answers by its pointer's pointee)
  (`CheckBuiltinAtomicResult.late_completion`, `query_size` and `query_pointer`,
  and `CheckProgram.body_eager`, which the selection makes for such a unit;
  #932's reviews). The C11 query follows the same width now,
  and the declaration-time evaluator checks every family
  (`check_builtin_atomic_family`), so the queries fold in enumerators and bounds.
- **GNU's generic forms**, `__atomic_load`, `__atomic_store`, `__atomic_exchange`
  and `__atomic_compare_exchange` (`check_builtin_atomic_generic_one`): any
  complete object of a size but an `_Atomic` or `_BitInt` one (records, unions,
  arrays and long double among them), whose values are pointers converting to
  pointers to its unqualified type, but a store's value and a compare-exchange's
  desired one (and an exchange's result) to the first argument's own pointer
  type, qualifiers kept, as clang's ByValType. One of those in an address space
  the object's doesn't include is clang's "passing 'int *' to parameter of type
  '__attribute__((address_space(256))) int *' changes address space of
  pointer", at the argument (#2233); the others, pointers to the value type,
  drop the space, as clang's do. The lowering copies the values into C11's representation
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
  them) but long double ("must be a pointer to floating point type"). A
  float's, a double's, a `_Float16`'s and a `__bf16`'s have leaves; the other
  types are valid and refused by name at the lowering. clang's code computes them through LLVM's x86 expansion of
  `llvm.maximum` and the others on every target without AVX10.2 (baseline to
  znver5 alike, at -O0 and -O2): `maxss` or `minss` of the old value and the
  operand, the result's sign cleared where the old value's is clear (maximum) or
  set where it is set (minimum), the old value where it is a NaN, and in the
  `_num` forms the operand where only the old value is one. So their NaN results
  aren't IEEE's (for one, `fmaximum(+0, sNaN)` is the signaling NaN, unquieted),
  and the leaves (`cbuiltin_atomic_rmw_ieee_body`) compute the same sequence.
  With AVX10.2 clang's code takes `vminmaxss` instead, whose NaN results differ,
  so a function whose target has AVX10.2 refuses them by name.
- Of a `_Float16` or a `__bf16` (#1843), clang's code converts both values to
  float, computes that sequence and converts the result back (a `_Float16`
  with AVX512-FP16 computes it in half precision with `vmaxsh`). Every result is
  one of the two values, its sign or quiet bit changed, so the leaves compute it
  in the integer registers (`cbuiltin_atomic_rmw_ieee_half_body`): `maxss`'s or
  `minss`'s choice by the values' order (a signed key of the magnitude), the
  same sign and NaN steps, then what the conversion back does on the unit's
  target, a leaf format each (`check_program_helper_half_format`):
  - a NaN result quieted, as `__truncsfhf2`, `vcvtps2ph`, `__truncsfbf2` and
    `vcvtneps2bf16` quiet it, but not with AVX512-FP16, which converts none;
  - a `__bf16` denormal result flushed to its sign's zero with AVX512-BF16 or
    AVX-NE-CONVERT, whose `vcvtneps2bf16` flushes it.

  A function whose own target differs from its unit's in those features is
  refused by name. The leaves raise no floating-point exception, where clang's
  code raises its conversions' and `maxss`'s. And clang's `__bf16` code keeps
  the old value as a float and converts it back for its compare-exchange, so
  where that changes it (an object holding a signaling NaN, or a denormal where
  `vcvtneps2bf16` flushes it) the compare fails forever and clang's program
  hangs; the leaves compare the bits they loaded and return.

`test-check-builtins-atomic` checks the queries' constants against clang's
evaluator's rules, the generic forms' conversions and constraints and the scoped
families' models (`check_builtin_atomic_test_families`);
`test-check-builtins-atomic-rmw` the added rules
(`check_builtin_atomic_rmw_test_added`); `test-builtin-atomic-rmw-helpers` the new
leaves against the host compiler's own builtins at every width (the IEEE
maximum's and minimum's on every pair of 14 values of each width and type, the
2-byte types' in the host's own format, but the old values its `__bf16` code
hangs on);
`test-preprocessor` the sized names' resolution, also at a guard page; and
`test-checker` their acceptance and first errors against clang's (35 fixtures
of the IEEE family's among them) and five programs of them against clang -O0's
builds (`c_check_program_atomic_test.c`: sized legacy forms, wrapping, flags and
lock-free queries, generic forms, scoped families, and the IEEE maximum and
minimum on every pair of 13 values, signaling NaNs among them, of a float, a
double, a `_Float16` and a `__bf16`).

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

`c_check_builtin_typed_overflow.c` implements clang's 18 typed overflow
builtins, `sadd_overflow` to `umulll_overflow` (`umull_overflow` among them): each has
the ordinary `T`, `T`, `T*` prototype of its signedness and width (int, long or
long long), whose parameter conversions precede the generic add, sub or
multiply-overflow action (`check_builtin_typed_overflows` names each one's).
Its formal destination is unqualified; discarded pointer qualifiers remain
diagnostics. A successful action always retains the destination write. Wider or
complex conversion payloads without a real implementation remain explicit
unsupported cases.

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

## Bit builtins

`c_check_builtin_bits.c` checks clang's other bit builtins as the graph's
miscellaneous builtins (`cbuiltin_bits`, catalog codes 0x3a0 to 0x3c7):
`bitreverse8` to `bitreverse64` and `bitreverseg`, `bswapg`, `clrsb`, `clrsbl`
and `clrsbll`, `clzs` and `ctzs`, `popcountg`, `clzg` and `ctzg`, C23's 16
`__builtin_stdc_` builtins, and the carry chains `addcb` to `addcll` and
`subcb` to `subcll`. Their rules are clang 23's Sema:

- a fixed prototype (`bitreverse8` to 64, `clrsb`, `clzs`, `ctzs`, the carry
  chains) converts each argument as if by assignment, with the ordinary pointer
  rules for a carry's pointer: another signedness or a lost qualifier warns,
  another pointee is an error, as clang 23's `-Wincompatible-pointer-types` is;
- `popcountg` and `clzg`'s and `ctzg`'s first argument take an unsigned
  integer type unconverted (bool, plain char where it is unsigned, an
  enumeration of an unsigned type and unsigned `_BitInt`s among them);
  `clzg`'s and `ctzg`'s optional second argument, the result for zero, takes
  its integer promotions (a bit-field's by its width), which must give `int`;
- `bitreverseg` and `bswapg` take any integer type and give it, `bswapg`'s
  `_BitInt`s of a multiple of 16 bits, of 8 or of 1;
- the stdc ones take an unsigned integer type that is no bool or enumeration
  and give `unsigned int`, `bool` (`has_single_bit`) or the argument's type
  (`bit_floor`, `bit_ceil`); `rotate_left` and `rotate_right` take an unsigned
  integer type (bools and enumerations admitted) and a count of any integer
  type, which counts modulo the width, a negative one's mathematical remainder.

Each folds as clang's evaluator does, in its operand's width of up to 128 bits,
where every argument it reads is a constant: `clzs`, `ctzs`, and `clzg` and
`ctzg` without a second argument, of zero are undefined (a fault, as
`check_builtins` gives `clz` of zero), and a carry chain, whose store of its
carry is an effect, never folds. A constant of a `_BitInt` past 128 bits
(#1447) folds by its limbs (`check_builtin_bits_fold_wide`, c_check_wide.c's
values): set bits by `vpopcntq` (with a scalar twin, compared in
`test-check-builtins-bits`), leading and trailing counts from its significant
bits and first set limb, a reversal of the limbs then a shift by the padding,
a byte swap of its width's bytes, `bit_floor`'s and `bit_ceil`'s one bit, and a
rotation by the shift kernels; a rotation's count of any width reduces modulo
the width from its limbs (`check_builtin_bits_wide_amount`), which a narrow
value's rotation by a wide count takes too. One constant a call: only the set
bits are a kernel worth a vector.

`c_builtin_bits_lower.c`'s recipes compute them on U64 temporaries, a value
zero-extended from its width, as clang's code computes on its `iN`: the
target-selected counts and byte swaps described above, SWAR population counts
where POPCNT is absent, reversals by masked swaps of halves,
selections by multiplying with a compare's 0 or 1, without branches. A rotation
masks its count for a width that is a power of two and takes a remainder for
another `_BitInt`'s. A carry chain adds or subtracts twice, each carry a
compare of the wrapped sum, and stores their OR at its pointer before its
result, as clang's code does.

An operand of 65 to 128 bits (`__int128`, a `_BitInt`) is a 16-byte frame
object whose two limbs the recipe loads, as the overflow builtins' are: it
counts limb by limb (the second limb's count added where the first is zero),
reverses and swaps bytes limb by limb, exchanging the limbs and shifting the
128 bits down past its width, and rotates by 128-bit shifts of a variable
count (each limb shifted by the count's part below 64, the crossing bits
carried, a whole limb moved where the count is 64 or more). A wide result is
written to a frame object of its own, its high limb extended from its width,
and read back as one I128 or U128 value. A wide rotation count's remainder
comes from its limbs': 2^64's and 2^128's remainders are constants of the
width.

A first operand past 128 bits (#1447) goes limb by limb in the lowering
(`check_lower_bits_limbs`), as clang's code computes on its `iN`, without
branches: a count sums its limbs' population counts, or adds each limb's
leading (trailing) zeros while the limbs before it are zero, from the top limb
down (the bottom one up), less the padding past the width; a limb's counts are
POPCNT's, LZCNT's and TZCNT's where the target has them, and otherwise SWAR's (a
smeared limb's population count for its leading zeros, `~x & (x - 1)`'s for its
trailing ones), all 64 for zero. A reversal or a byte swap reverses each limb
(`bswap`, then masked swaps for the bits) into the other end of a buffer, then
shifts the limbs down by the padding; `bit_floor`'s and `bit_ceil`'s bit `k`
is limb `k / 64`'s bit `k % 64`; a rotation by `k` is the limbs' left shift by
`k` ORed with their right shift by `(w - k) % w` (`check_lower_limb_shift`).
A rotation's count, of any width, reduces modulo the width from its limbs'
remainders, each times `2^(64 i)`'s, a negative one's plus the width less
`2^(64 n)`'s; a narrower value's rotation by a count past 128 bits takes that
remainder into the recipe. Clang's code generator can't compile a rotation of
a width past 128 bits that is no power of two (its urem of the count); its
Sema takes one, and so does this compiler, which `test-checker-driver`'s
`tier1-bits` checks against a reference written with shifts.
`test-checker`'s `bit-int-past-128-bit-builtins` program runs the rest against
clang -O0's build.

clang 23's code generation, and LLVM's main, evaluates `bitreverseg`'s
argument twice, and `bswapg`'s for widths other than 1 and 8: `EmitScalarExpr`
reads its width, then `emitBuiltinWithOneOverloadedType` emits the argument
again, which repeats its side effects and volatile reads. This compiler
evaluates it once, as a call does. test-checker's
`bits-reversal-and-swap-arguments-evaluated-once` agrees with clang's build of
a model that evaluates each argument first, and
`bits-clang-evaluates-reversal-and-swap-arguments-twice` differs from clang's
build of the same source, so that a fix in clang shows.

`test-check-builtins-bits` checks every fold, result type and conversion
against the host compiler's own builtins on boundary and random values of 27
operand types and 7 count types, the refusals and the carry chains' pointer
rules; `test-checker --program-bits` checks source fixtures by both paths
against clang and runs programs of every builtin natively against clang -O0's
build, 128-bit and wide `_BitInt` operands, results and counts among them. No
unit of the corpora calls these builtins, so the folds stay scalar: the checker
asks once per call, in source order, as for the other miscellaneous builtins.

## Alignment builtins and `expect_with_probability`

The checker takes `__builtin_align_up`, `__builtin_align_down`,
`__builtin_is_aligned` (`cbuiltin_align`) and
`__builtin_expect_with_probability` (catalog codes 0x3e0 to 0x3e3, the table's
last rows) as miscellaneous builtins, by clang 23's Sema:

- an alignment builtin's source is an integer type but bool and the
  enumerations (`_BitInt`s and 128-bit ones among them), or an object pointer,
  an array decayed to its element's pointer; a function, a function pointer, an
  `_Atomic` type and `nullptr_t` aren't. Its alignment is such an integer type.
  Both are checked by their types before their lvalue conversions;
- a constant alignment, which clang evaluates past its side effects and through
  const objects' constant initializers wherever the expression holds them
  (`const int c = 3;` makes `__builtin_align_up(x, c + 0)` an error), must be at
  least 1, at most 2 to the source's width less 1 (a pointer's 64), and a power
  of 2, in that order; 1 draws clang's `-Wtautological-compare`, which the
  checker records but doesn't report. The checker's runtime view of the argument
  reads no const object, so the source adapter evaluates it as a constant
  context would (`check_input_misc_evidence`), and the call's checks and
  folds take that value from its evidence (`CheckBuiltinCall.alignment_evaluated`)
  while the argument itself is checked, and runs, as before. An unknown
  alignment is unchecked, and so is a modification's value, on which clang's
  evaluator fails past side effects or not (`(s = 3)`, `(s = 3) + 0`, `s++`:
  `BUSTER_CHECK_MODIFICATION`, which assignments and increments set and a
  comma's discarded left operand doesn't pass on, so `(s = 4, 3)` is checked);
- `align_up` and `align_down` are of the source's type, its qualifiers kept (a
  `const int` source's `__typeof__` is a `const int`), and `is_aligned` is a
  bool;
- `expect_with_probability` takes `expect`'s two longs and a probability that,
  converted to `double`, must be a constant floating expression without side
  effects in [0, 1]: a NaN isn't. Clang evaluates it as a constant expression,
  which reads a const object's constant initializer wherever it holds one
  (`one ? 0.5 : 0.25` of a `const int one = 1`), so its subtree is checked as a
  constant context's (`BUSTER_CHECK_CONTEXT_VALUE` on its node, without a
  constraint report of its own), which the same helper marks from the call's
  evidence record. A constant expression's evaluation also fails on a discarded
  operand it can't evaluate, where a fold goes on: a comma's left operand or a
  cast to void's that is no constant, as a variable's read or a modification
  (`(s, 0.75)`, `((void)s, 0.75)`), marks the value
  `BUSTER_CHECK_DISCARDED_UNEVALUABLE`, which the probability refuses, while
  `((void)0, 0.75)`, `(one, 0.75)` and `(&s, 0.75)` are constants. It never
  runs. Its call folds as `expect`'s, and otherwise its recipe is `expect`'s.

The alignment builtins fold, as integer constant expressions, where clang's
evaluator does: of constants without side effects, in the source's width
(`(x + (a - 1)) & ~(a - 1)`, `x & ~(a - 1)` and `(x & (a - 1)) == 0`), and of an
address constant by its object's declaration alignment as clang gives it
(`check_input_declaration_alignment`, which the call's evidence carries with
its object, `CheckBuiltinCall.base_object`): the same address, or 1, where the
alignment at its offset is at least the requested one; the offset rounded, or
0, where the object's is; nothing otherwise, so a static initializer of
`__builtin_align_up(&small[1], 8)` for a `char small[8]` is no constant, as in
clang. An alignment of 1 folds any address. A folded integer evaluates neither
argument, as clang's code doesn't; a folded address still evaluates both, as
clang computes it at run time. A local array's address is no constant: the
call runs, and gives what the fold would, since a frame places a declaration at
least as aligned as clang declares it (#978's large arrays). A source past 128
bits (#1447) folds by its limbs (`check_wide_align_fold`), its constant
alignment checked as any source's (at least 1, at most 2 to its width less 1,
a power of 2). An alignment past 128 bits is checked by its constant's shape
(`check_wide_alignment_shape`: its sign, zero, significant bits and whether it
is a power of 2), as clang checks it, through a const object's initializer too
(the call's evidence keeps its limbs, `CheckBuiltinCall.alignment_limbs`).

`c_builtin_misc_lower.c`'s `cbuiltin_misc_align` computes them as clang's code
does, from the bit builtins' descriptors (a pointer's 64 bits): the source
zero-extended from its width, the alignment zero-extended or truncated to it,
the mask the alignment less 1 in that width (all ones for an alignment that
truncates to 0, as a char's runtime 256 does), and a source of 65 to 128 bits
limb by limb, with the mask's borrow and the sum's carry crossing between the
limbs.

A source past 128 bits goes limb by limb in the lowering
(`check_lower_align_limbs`): its bits zero-extended from its width, the
alignment's zero-extended or truncated to it, the mask its value less 1 in
that width by a borrow chain, `align_up` `(x + mask) & ~mask` by a carry chain,
`align_down` `x & ~mask`, each a frame object of the source's type, and
`is_aligned` whether `(x & mask)`'s limbs are all zero. `test-checker
--program-align`'s `align-bit-int-sources-past-128-bits` program runs them
against clang -O0's build, and its source fixtures check the constant
alignments of such sources and the refusals of wider alignments.

`test-check-builtins-misc` checks the folds, the alignments' rules, the
operands' types, the result types and the address folds, and
`test-checker --program-align` checks source fixtures by both paths against
clang and runs programs natively against clang -O0's build: every integer
width by alignments of every width, 128-bit and wide `_BitInt` sources,
addresses in static data and in frames, and `expect`'s and
`expect_with_probability`'s arguments evaluated once, in order, or not at all.

## Generic builtins of clang's own checks and of an instruction or two

The checker takes `__builtin_classify_type`, `__builtin_eh_return_data_regno`,
`__builtin_dwarf_cfa` and `__builtin_launder` (catalog codes 0x3f0 to 0x3f3,
after the NaN constants) as miscellaneous builtins, by clang 23's Sema and
evaluator (`check_builtin_misc_generic`):

- `classify_type(e)` is its operand's GCC type class, as clang's evaluator gives
  it in C: `void` 0, an integer or an enumeration 1, `bool` 4, a pointer, an
  array or a function 5, a floating type 8, a complex one 9, a struct 12, a union
  13, a `_BitInt` 18, a vector 19, and `nullptr_t` -1. An `_Atomic` type is its
  value type's class. The operand is any expression, of an incomplete type too,
  read by its type before any conversion, and never runs (`classify_type(i++)`
  leaves `i`). The call is an int constant expression;
- `eh_return_data_regno(n)` is x86-64's DWARF register of the n-th exception
  data register, rax's 0 and rdx's 1, and -1 for any other n, which must be an
  integer constant expression. The call is an int constant expression;
- `dwarf_cfa()` is the function's canonical frame address, which clang -O0's
  code computes as rbp + 16: Tier 0's `FRAME_ADDRESS` plus 16, which gives the
  function a frame pointer as `__builtin_frame_address` does;
- `launder(p)` is p, of its type, which must be a pointer to a complete object
  type: not `void`'s, not a function's, and not an incomplete struct's. An
  address constant stays one (`int* p = __builtin_launder(&g);` at file scope),
  so `__has_constexpr_builtin` answers 1 for it, as clang's does.

Eight more are miscellaneous builtins of clang's Sema too, catalog codes 0x3f4
to 0x3fb (`check_builtin_misc_more`):

- `alloca_uninitialized(n)` is `alloca(n)`, whose bytes clang never
  initializes anyway;
- `alloca_with_align(n, a)` and `alloca_with_align_uninitialized(n, a)` take n
  bytes at an alignment of a bits, which must be an integer constant expression,
  a power of 2, 8 or more and at most `INT32_MAX`, as clang's Sema checks it.
  The alignment never runs. Past 16 bytes the size grows by the alignment less
  16 and the address is rounded up after Tier 0's `ALLOCA`, as an over-aligned
  variable-length array's is; clang -O0's code rounds rsp down instead, and
  makes a constant size in the entry block a frame object, which only moves the
  bytes;
- `allow_runtime_check(s)` is true and `allow_sanitize_check(s)` false, as
  clang -O0's code has them with no sanitizer on, of a string literal (the
  call's evidence, `CheckBuiltinCall.nan_payload`, as `nan`'s payload is read),
  parenthesized at most: as clang's, an explicit cast of it is refused
  (`CheckBuiltinCall.nan_payload_cast`), where annotation's string takes one;
  `allow_sanitize_check`'s must name one of the seven sanitizers clang takes
  there (`address`, `thread`, `memory`, `hwaddress` and the kernel's three). The
  string never runs, and neither call is a constant expression;
- `annotation(x, s)` is x, of its integer type, s a narrow string literal (the
  second argument's evidence); no constant expression;
- `addressof(e)` is `&e`, as the operator takes it: an lvalue or a function
  designator, not a bit-field, a register variable or a vector's element, an
  address constant where e has one (`int* p = __builtin_addressof(g);`). The
  lowering takes the operand's address, which makes a local addressed;
- `function_start(f)` is f's address, a `void*`, an address constant, of an
  operand that evaluates to a function at offset 0, as clang's
  `getAsBuiltinConstantDeclRef` reads it: a function designator that names a
  function, or an address constant at offset 0 of one of the functions the
  operand names (`&f`, `(void*)&f`, `(void (*)(int))f`, `1 ? f : g`, `f + 0`;
  `CheckBuiltinCall.function_objects`). A pointer variable, a conditional that
  doesn't fold, an object's or a string's address, a null pointer and an offset
  are refused.

`__builtin_complex(re, im)`, catalog code 0x3fc (`check_builtin_misc_complex`),
is the complex number re + im·i, of type `_Complex T`, where re and im are of
one real floating type T once lvalue-converted (`float` to `long double`, and
`_Float16`, `__bf16` and `__float128`, as clang 23 takes them), and neither is
converted otherwise. As clang's Sema, it checks each argument's type before its
conversion first, so that an `_Atomic double` is refused, and an integer or a
complex value, then the two types against each other. Its errors are clang's
(`check_semantic_expression`): "argument type 'T' is not a real floating point
type" at the first such argument, its type as written (an array's, a function's,
a qualified one's, its typedef's with its `aka`), and "arguments are of different
types ('R' vs 'I')" at re, by the converted types, their qualifiers dropped and
their typedefs kept. It folds where both are
floating constants, as clang's evaluator folds it, so that glibc's `CMPLX` makes
static data and an enumerator's `__imag__` an integer constant: the folds keep
signed zeros, infinities and NaNs, a `static const` object's value, and long
double's, `_Float16`'s and `__float128`'s parts (a `__float128`'s by their bits,
#1250, `check_quad_complex_set`). At run
time the lowering evaluates both arguments in order into temporaries and stores
them into a frame object of the two parts, whose address is its value, as every
complex value's is (`check_lower_complex_builtin`: a `__float128` part's 16
bytes copied 8 at a time).

The last of them, catalog codes 0x3fd to 0x3ff and 0x440 to 0x44e
(`check_builtin_misc_last`):

- `__arithmetic_fence(x)` is x, lvalue-converted, of a floating type, a complex
  one or a vector of either, by its type before the conversion (an `_Atomic
  double` is refused, and a complex integer); a constant where x is, as clang's
  evaluator folds it. At -O0 clang's code computes x as it is, which the
  lowering does;
- `__xray_customevent(p, n)` and `__xray_typedevent(t, p, n)` are nothing, their
  arguments converted to `const char *` and `size_t` as a call's and never
  evaluated, as clang's code has them without `-fxray-instrument`, and
  `__warn_memset_zero_len()` is nothing;
- `__builtin_trivially_relocate(d, s, n)` is `memmove(d, s, n * sizeof *d)`,
  whose value is d (the miscellaneous builtins' memmove recipe, its size
  multiplied), where d is a pointer, as written, to a type C's relocation takes
  (an element, through arrays, of a scalar, complex or vector type: no struct,
  union, `_Atomic` or function), not const, s has d's type as written (an array
  isn't its pointer), and n converts to `size_t`;
- `__builtin_operator_new` and `__builtin_operator_delete` are C++'s alone, and
  the ten pointer authentication builtins (`__builtin_ptrauth_auth` to
  `__builtin_ptrauth_strip`) need a target x86 isn't: every call is an error, as
  clang's is ("is only available in C++", "this target does not support pointer
  authentication"), the latter's after their arity. `__has_builtin` answers 1
  for the ten, and `201802L` for `__builtin_operator_new` and
  `__builtin_operator_delete` in every language, as clang's does (the date of
  the change that let them call any usual allocation function, which libc++
  asks about); `__has_constexpr_builtin` answers as clang's.

`__builtin_preserve_access_index` and `__builtin_hlsl_resource_getpointer_typed`
(0x452 and 0x453, #148) are checked as clang's Sema checks them in C, and `-c`
refuses each by name, as clang's code generation does for x86:

- `__builtin_preserve_access_index(x)`, BPF's relocation marker, is x's value,
  converted, of its type: an rvalue and never a constant. Clang's code
  generation refuses it without `-g`, and with `-g` its member accesses fail
  to select for x86. With `-g`, clang compiles a plain value as itself, which
  the refusal here doesn't follow.
- `__builtin_hlsl_resource_getpointer_typed(...)` is `void(...)` in C. Its
  arguments are a variadic call's, any but void ones ("argument type 'void' is
  incomplete"), and its code is "cannot compile this builtin function yet".

`__has_builtin` answers 1 for both, as clang's does.

`__builtin_clear_padding(p)` (0x454, #148) zeroes the padding bits of the
object p points to, as clang's `PaddingClearer` does.

- **Sema.** p is a pointer as written (an array isn't one) to a complete object
  type that isn't const. Volatile and `_Atomic` pointees are fine. Each refusal
  is clang's error at the argument's first token (#2414,
  `check_semantic_clear_padding`): "passing 'T' to parameter of incompatible
  type pointer: type mismatch at 1st parameter ('T' vs pointer)", a function
  pointer's "must be a pointer to a trivially-copyable type", "variable has
  incomplete type 'T'" with a tag's note, and "read-only variable is not
  assignable".
- **Bits.** The bits no scalar, bit-field or vector of the object holds are
  padding:
  - a record's holes and tail, and an unnamed bit-field;
  - a union's bits that none of its members holds;
  - an x87 long double's last 48;
  - a `_BitInt`'s bits past its width;
  - an atomic type's bits past its value's;
  - a vector's past its elements (a bool ext vector's past one bit each).

  `check_lower_padding_entries` (`c_check_program_padding.c`) walks the
  element's type with an explicit stack before the function lanes run, once a
  call, each held piece an interval of bits; an inner array's first element is
  walked and its intervals copied for the others, or merged into one where the
  element has no padding (#1842). The intervals are sorted and merged.
- **Stores.** A byte with no bit held is a zero, in runs stored 8, 4, 2 and 1
  bytes at a time. A byte with some is its load ANDed with their mask. An
  array's elements, through arrays of arrays, are cleared in a loop by their
  element's stores. A variable length array is left as it is, as clang's code
  leaves it. Where an element's stores would pass about 460 of a builtin's
  entries (some 40 runs), its entries (offset, bytes and mask, 8 bytes each)
  are a read-only pool of the unit's, which the recipe's loop reads
  (`cbuiltin_misc_lower_padding_table`): the same bytes cleared, by a few
  dozen typed entries whatever the element's size (#1842).
- **Limits.** An element past 4 GiB, whose entries keep 32-bit offsets, or
  of more than 1,048,576 intervals or entries is refused by name. The walk's
  stack grows with the type's nesting, which no limit of its own bounds
  (#1842; before, a type nested past 64 levels was refused, and its frame
  written past the stack).
- **Sugared arrays** (#1842's 1b). Clang's walk doesn't look through an
  array type's sugar (`dyn_cast<ConstantArrayType>` of the sugared type). So
  it treats as one scalar, and clears nothing inside:
  - an array of a typedef's type or of `typeof`'s;
  - one that a pointer declared with parentheses points to
    (`struct S (*p)[2]`, `ParenType` sugar), a parameter's and a typedef's of
    such a pointer too;
  - an element that is such an array (`A d[2]`), and such a member, whose
    record's other padding is cleared.

  A complex typedef's padding and an array of a typedef's struct are
  cleared. The plan does the same with the declarations' sugar.
  - **The argument** (`check_lower_padding_argument`): the call's argument is
    followed through implicit conversions, `&`, `*` and subscripts to the
    declaration it points into, at the level of that declaration's type.
  - **The sugar** (`check_lower_padding_sugared`):
    - a group's `PAREN` node, which the reader makes over an array in a unit
      that names the builtin (`docs/c-types.md`'s "Type sugar");
    - a typedef or `typeof` specifier that writes the level, below which
      the typedef's own declaration answers.
  - **Members:** found through their record's body
    (`check_lower_padding_member`).
  - **The walk:** a frame's `opaque` count makes the array it reaches all
    occupied (`check_lower_padding_entries`).

  **Still cleared:** an array that a cast's type name, a member access, or a
  call's result points to, which clang keeps.

`__builtin_invoke(f, a...)` (0x455, #148) is the call `f(a...)` in C, as
clang's Sema builds it.

- The reader drops the name and its parentheses (`check_input_declaration`'s
  walk), so the call's first operand is its callee and the rest are its
  arguments. A function, a pointer to one, a member of one, `*p` and `&g` are
  all fine. The checks are the call's: a prototype's conversions, a variadic
  function's promotions, and argument counts.
- With no arguments, clang's error "too few arguments to function call,
  expected at least 1, have 0" is at the name
  (`CHECK_INPUT_INVOKE_ARGUMENTS`). The constant evaluator takes the name as
  the call's syntax too (`check_constant_invoke`), so the call's type is
  known in `_Generic` and `sizeof`.
- Clang reports the call's other errors (a callee that isn't a function, a
  call without a prototype's warning) at the name. Here they are at the
  callee, in the checker's wording for calls.
- It is never a constant, and its name alone is refused as other builtins'
  are.
- `__has_builtin` answers 1.

### Checked, refused in code (#148)

`__has_builtin` answers 1 for some builtins whose source checks are clang's
but whose code `-c` refuses by name (#1785). Some are refused because clang
can't compile them for x86-64 either: `__builtin_preserve_access_index` (BPF
alone: "using builtin_preserve_access_index() without -g", and with `-g` the
backend's "Cannot select: intrinsic %llvm.preserve.struct.access.index") and
`__builtin_hlsl_resource_getpointer_typed` ("cannot compile this builtin
function yet"). The others clang does compile, but its code for them isn't
sound, and this compiler keeps refusing them:

- `__builtin___NSStringMakeConstantString` of no string literal, whose
  object clang's code makes from what isn't one (below, "Of no
  literal");
- `__builtin_zos_va_start` and `__builtin_zos_va_copy`, whose code clang
  writes over a `char *` that its 24-byte list overruns. The owner decided on
  2026-11-08 to keep refusing them rather than copy clang's overrunning code
  (#1785).

`__builtin_available` with an empty version (`0.0.0`) is refused too. Clang
23.1.5's code generation refuses it where it reaches the expression ("cannot
compile this scalar expression yet": an if's condition, an operand of `+`),
but where the invalid expression is a whole return value, a declaration's
initializer or a conditional's condition, Sema drops that statement or
declaration silently: `return __builtin_available(macos 0.0.0, *);` compiles
to a function that returns an uninitialized value, with only
`-Wreturn-type`'s warning. `__builtin_counted_by_ref` of a bit-field count
crashes clang's code generation, and so does `os_log` data of a 32- or
64-byte vector or of a `_BitInt` past 128 bits. Each was rechecked against
clang 23.1.5 on 2026-11-09.

A unit that only checks them (`-fsyntax-only`) gets clang's answers. A unit
that compiles one gets the refusal's message, at the call.

`__builtin_eh_return(offset, handler)` (0x456, #148) is `void(size_t,
void *)` and doesn't return, as clang's Sema takes it: each argument converts
as a prototype's parameter. Its code unwinds to the handler as LLVM's
`EH_RETURN` does, for libgcc's unwinder (`uw_install_context`):

- **The frame.** A function that calls it (`Tier0Function.eh_return`) has a
  frame pointer. It saves every callee-saved register, then rdx and rax,
  LLVM's `CSR_64EHRet` set. Its CFI says where each is: rdx at the CFA less
  64 and rax less 72 with five callee-saved pushes, as clang's frame has
  them after `__builtin_unwind_init`. The fixed descriptor can't say it, so
  the frame is a row program (`emitted_tier0_eh_rows`).
- **The unwinding.** The call is a TAIL_CALL of a row of its own
  (`BUSTER_TIER0_TAIL_EH`). The row is indirect, its callee the handler in
  `r11`, and the offset is its one argument, in `rcx`. Its teardown:
  - `lea rcx, [rbp + rcx + 16]`, the CFA plus the offset;
  - the release, then pops of rax, rdx, the callee-saved registers and rbp;
  - `mov rsp, rcx` and `jmp r11`.

  The handler runs with the stack LLVM's `ret` from `rcx - 8` gives it. The
  offset is evaluated first, then the handler, as clang's arguments are.
- **Ordinary returns** step over rax's and rdx's slots and keep the result.
  LLVM's pop the saved rax over it (`movl $42, %eax; popq %rax`), as GCC's
  epilogue doesn't.
- **Where it may be:**
  - **A variadic function.** Its register save area is stored after the
    pushes, so rax's and rdx's slots stay where the CFI says, as in clang's
    frame. The unwinding passes none of the function's arguments on
    (`tier0_tail_call_valid`).
  - **Beside musttail calls.** Each TAIL_CALL row has its own kind in
    `Tier0Function.call_tails`. The lowering tells an eh_return's row by its
    argument's ABI type, a `const void`. A musttail call's teardown steps
    over rax's and rdx's slots, as an ordinary return does
    (`tier0_count_tail_finish` and `tier0_emit_tail`, per row).
    `unwind_tier0_frame_tails` reads each tail by its kind: an eh_return's
    jump comes right after `mov rsp, rcx`. `emitted_tier0_eh_rows` restores
    rax's and rdx's rules after a musttail call's release, as it does after
    the epilogue's.
  - **Within an expression** (`c + (__builtin_eh_return(o, h), 1)`). The
    offset and the handler go into temporaries, the expression's pending
    values are discarded, and the TAIL_CALL then has nothing under its
    operands (`check_lower_eh_unwind`). The rest of the expression is dead.
    Dead stand-ins of the discarded values' classes take their places, as for
    a terminal helper's call (`check_lower_terminate`), both by
    `check_lower_dead_value`:
    - a vector's is a zero constant of its bytes;
    - a mask's is an integer's zero converted to its bytes;
    - any other's is a constant of its class.

    The tracked stack keeps each vector's and mask's bytes
    (`check_lower_track_bytes`): an X86 row's result's, a constant's, a local
    object's, or the entry's own. Before #1436's review this was missing, and a
    terminal helper's call under a pending vector
    (`v * (__builtin_trap(), w)`) broke Tier 0's rule on vector constants.

    The tracked emitter's adjacency filler makes its constants the same way
    (`check_lower_emit_tracked`, #1438's review). That filler is the
    cancelling pair it writes where a pending value's class must precede its
    consumer. Before, it wrote `CONST MASK`, which no mask entry takes, under
    a pending mask (`__builtin_ia32_kandhi(k, (__builtin_trap(), k))`).
    `test-checker-driver`'s dead vectors' program builds such masks and 64-
    and 32-byte vectors for x86-64-v4 with `--verify`, under the trap and
    under `__builtin_eh_return`.
    A unit that calls `__builtin_eh_return` tracks its typed stacks for this
    (`CheckLowerShared.terminal`). The pending values are evaluated before
    the offset and the handler, as clang evaluates them.
- **Its consumers:**
  - `unwind_tier0_frame_tails` reads the frame.
  - `emitted_verify_frames` proves its rows up to the `mov rsp, rcx`. At the
    jump, the handler's stack is one no rule describes, and LLVM's CFI
    doesn't describe it either.
  - Tier 1 falls back to Tier 0 for the function, as for
    `__builtin_unwind_init`'s frame.
- **Tests.** `test-checker-driver`'s eh_return program writes rax's and
  rdx's slots, then unwinds at offsets 0, 16 and 32 into a handler that
  records the registers and rsp. It runs six functions:
  - one with two unwinding sites;
  - a variadic one that reads its variadic argument first;
  - one that unwinds within expressions after a call;
  - one that unwinds within a `?:`, an `&&`, a statement expression and a
    comma, with a `double`, a `long double`, a struct's member and a vector
    pending;
  - one that makes a musttail call where it doesn't unwind;
  - one whose musttail call copies a 32-byte struct to the stack's incoming
    area.

  For the musttail calls the runner also records the callee's result, the
  callee-saved registers and rsp. A vector function holds a value under
  `__builtin_trap`. The program is built on both paths at Tier 0 and with
  `--tier1`, all with `--verify`, and prints what clang `-O0`'s build prints.
- **A clang bug.** Clang can't be the oracle for a musttail call with a third
  integer argument in such a function. LLVM's teardown of a `CSR_64EHRet`
  frame pops the saved rdx over the argument already loaded into rdx, so its
  callee gets the slot's value. This compiler passes the argument.

`__builtin___CFStringMakeConstantString(s)` and
`__builtin___NSStringMakeConstantString(s)` (0x457 and 0x458, #148) are
`const __NSConstantString *`, clang's predeclared record (`const int *isa;
int flags; const char *str; long length;`, 32 bytes), which the reader makes
the first time a call needs it, at its claim or where an operand's evaluation
reaches the call first (`check_input_ns_constant_string`: a `typeof`'s, a file
scope `sizeof`'s).

- **Sema.** s converts to `const char *` as a prototype's parameter. CFString's
  is, past its parentheses and casts, an ordinary string literal
  (`CheckObjCString`): a pointer, a wide literal, `__func__` or a UTF-8 one
  (`u8"x"`, or `"a" u8"b"`, `CheckStringLiteral.utf8`) is refused, with clang's
  "CFString literal is not a string constant" at the argument (#2189). An
  argument that doesn't convert keeps the generic error, where clang gives its
  conversion error and then that one. NSString's
  is any `const char *`: clang checks its literal in Objective-C alone.
- **Warning.** A CFString literal that isn't UTF-8 as LLVM's strict conversion
  reads it (an overlong form, a surrogate, past U+10FFFF, or cut short;
  `pp_utf8_sequence`) is clang's `-WCFString-literal`, "input conversion
  stopped due to an input byte that does not belong to the input codeset
  UTF-8", at the literal (`check_input_cfstring_literal`).
- **The value.** Of a literal, either call is an address constant, as clang's
  evaluator takes it (`IsOpaqueConstantCall`): the address of its call's own
  object (its `)`'s token plus one, `CheckBuiltinMiscAction.cfstring_object`),
  a static one. So a static initializer of one (`static const void *p =
  __builtin___CFStringMakeConstantString("x");`, cast to `long` or plus an
  offset) is accepted, and its code is the object's address: no operand runs.
- **The objects.** `check_data_cfstring_keys` (`c_check_data_input.c`) makes what
  clang's `GetAddrOfConstantCFString` makes:
  - **The record:** one `__NSConstantString` record of 32 bytes of each
    literal's characters, writable data in the section `cfstring`, 8-aligned.
    Its fields are `__CFConstantStringClassReference`'s address, its flags
    (`0x7c8`, or `0x7d0` for UTF-16), its characters' address and their
    length.
  - **Its characters:** an ASCII literal's (no byte past `0x7f` and no NUL) are
    its own string literal. Any other's are the UTF-16 units of LLVM's strict
    conversion, which stops at the first byte that begins no sequence, then a
    NUL unit. They go in a constant array of their own, 2-aligned (at the
    call's `(`), and the length counts the units.
  - **One per literal:** each call's object takes the symbol of the lowest
    object of its characters, as weakrefs take their targets' at numbering
    (`check_program_alias_number`). So two calls of one literal, a CFString's
    and an NSString's among them, have one address, as clang's.
  - **The class:** the unit's file-scope declaration of
    `__CFConstantStringClassReference` (`check_input_cfstring_class`), or else
    an undefined global of that name, the program's own.
- **Of no literal.** Clang's evaluator takes an NSString of what isn't a
  literal as an address constant too (`IsOpaqueConstantCall` looks at no
  argument), so it is one here as well, of its call's own object: a static
  initializer of one is accepted, `(long)` of it plus an offset too, and two
  compared aren't an initializer's constant, as clang's. But clang's code
  makes its object from the literal its argument isn't (a `cast<StringLiteral>`
  of another expression), so `-c` refuses the object by name, wherever the call
  is, in the static data (`check_data_cfstring_keys`, at the call's `)`):
  "unsupported: an NSString of no string literal, whose object clang's code
  makes from the literal its argument isn't" (#1785).
  - What clang's object holds is another node's bytes read as a literal's,
    so it isn't defined, and an assertion-enabled clang aborts at the cast.
    Clang 23.1.5's release build (#1785) makes an empty 8-bit string (flags
    `0x7c8`, length 0) for a pointer variable, an array, `s + 1`, a
    conditional, `(char *)0` and a compound literal, and for `__func__` an
    array of 8 zero UTF-16 units (flags `0x7d0`, length 8). Clang looks
    through casts first, so `(const char *)(void *)"zz"` makes the literal's
    own string.
- **Differences.**
  - A static `_Bool` of one is refused, as one of any address is
    (`static _Bool b = &x;`).
  - Clang's ELF object also has an `objc_imageinfo` section where it makes a
    CFString, and keeps the characters in `.rodata`, which this compiler's
    string pool (`.rodata.str1.1`) holds.

A miscellaneous builtin's argument of the other signedness of a `char`
pointer (`__builtin_strlen(u8"x")` in C23) gets the pointer-sign warning, as
an ordinary call's.

`__builtin_call_with_static_chain(call, chain)` (0x4b0, #148) is the call's
value, as clang's `BuiltinCallWithStaticChain` checks it.

- **The call.** The first argument is a call as written: not a name, and not
  a call in parentheses (its node is then a grouping's). It may not call a
  builtin. That means a catalog builtin, a header builtin's leaf, or the
  unit's external declaration of a library builtin's name (`abs` or `strlen`
  declared), found through the callee's parentheses, as `getBuiltinCallee`
  finds it.
- **The chain.** The second argument is a pointer, of any type.
- **Code.** As clang's code does, `-c` evaluates the callee, then the
  chain, then the arguments, and passes the chain in `r10`, SysV's static
  chain register (LLVM's `nest`). The call's own arguments and result stay
  as they were.
- **A musttail return of it.** Clang's Sema gives the builtin the type
  `R(R, C)`, where R is the call's result and C the chain's type after the
  usual unary conversions (`BuiltinCallWithStaticChain`), and checks the
  return's musttail against that type.
  - So the caller must be prototyped, not variadic and of the default
    convention, return R and take exactly R and C, as similar types. The
    messages name `__builtin_call_with_static_chain`
    (`check_musttail_static_chain_rule`).
  - Its code is the call, then the return. Clang's code generation emits no
    musttail for a builtin's call, so the return is no tail call
    (`check_control_static_chain`).
- **The call's record.** The chain is the call's argument 0, of class `U64`.
  Its ABI type is `void`, which no other argument has and `abi_assign`
  ignores, so it takes no register of the convention's. After the batch,
  `check_lower_chain_location` puts it in `r10`. Its consumers take it as
  follows:
  - **Tier 0.** `r10` is one of the argument registers that its locations
    may name (`BUSTER_TIER0_ARGUMENT_REGISTERS`), and every call emitter's
    moves take it as any register argument's. An indirect callee still goes
    through `r11`.
  - **The typed IR.** A call with an argument in `r10` keeps its ABI records
    (`ir_typed_aggregate_call`).
  - **Tier 1.** A record in `r10` is general place 6, past the six argument
    registers (`tier1_select_parameter_registers`). A chained indirect call
    calls through `rax` (`tier1_select_callee_register`), since its chain
    takes `r10` and the moves' scratch `r11`. The code certificate checks the
    same sequence. A chained indirect variadic call, whose `al` takes `rax`,
    falls back to Tier 0 (`ir_lower`'s `IR_ERROR_UNSUPPORTED`).
  - **The interpreters.** Both take the call without its chain
    (`tier0_call_chain`, `ir_interpreter_unchained`). A native callee gets
    the chain in `r10` from the bridge; an interpreted one, whose C
    parameters don't include it, doesn't. The IR interpreter keeps the other
    arguments' ABI records past the chain's (their sizes, places and extension
    flags) and checks the chain's definedness as an argument's.
  - **SSA promotion** sees calls as opaque values and needs nothing.
- **Tests.** `test-checker-driver`'s static chain program runs naked
  callees that read `r10`, direct and indirect, of aggregate, floating and
  variadic calls, past six arguments, in a loop and as a musttail return's
  value. Its musttail error fixtures compare each rule's message with
  clang's. It is built on both
  paths at Tier 0 and with `--tier1 --verify`, and each build prints what
  clang `-O0`'s does. `test-interpreter` and `test-ir-lowering` pass a chain
  to native and interpreted callees.
- **Identities.** The miscellaneous builtins past 0x45f start at 0x4b0, past
  the `<stdbit.h>` library builtins' identities (0x460 to 0x4a5). A static
  assertion keeps them apart.

`__builtin_os_log_format_buffer_size(format, data...)` and
`__builtin_os_log_format(buffer, format, data...)` (0x4b1 and 0x4b2, #148) are
clang's os_log buffers, as `BuiltinOSLogFormat`, `OSLog.cpp` and
`emitBuiltinOSLogFormat` make them.

- **The format.** It must be an ordinary or UTF-8 string literal past its
  parentheses and casts ("os_log() format argument is not a string
  constant"). The reader reads it as clang's `ParsePrintfSpecifier` does,
  with os_log's annotations (`c_check_os_log_input.c`).
  - Annotations are `%{public}`, `%{private}`, `%{sensitive}` and
    `%{mask.NAME}`, comma-separated. A privacy one counts where it is at
    least as strict as those before it.
  - Positions are `%N$`, and amounts are `*` and `*N$`.
  - Length modifiers include C23's `H`, `D`, `DD`, `wN` and `wfN`.
- **The layout.** The layout pass stops where clang's handler stops: at a
  value without its data, or after a `%P` without its length. Each
  specifier gives up to four items, in this order: its mask (8 bytes of its
  name), its field width when it is an argument, a string's or pointer's
  count (from the precision's argument, or its constant as 4 bytes) or any
  other's precision when it is an argument, and its value (none for `%m`).
  A data argument's size is its promoted type's: arrays and functions as
  pointers, float and `__fp16` as double. `_Float16` and `__bf16` aren't
  promoted, and take 2 bytes.
  - The pass pushes each item on the checker's arena as it finds it, 16
    bytes each and nothing else (`check_os_log_format`, #1329). Escaped
    `%%`, bytes past the format's NUL and specifiers of fewer items take no
    room. An arena that can't hold the items is a resource failure, and the
    arena is left as it was.
  - `test-check-builtins-misc` checks the items of fixed formats of each
    kind, 64 KiB formats of escaped percent signs and of percent signs past
    a NUL in arenas of no room, and generated formats. Each runs in arenas
    exactly as large as its items and a byte short of them.
- **The checks.** The checking pass goes on past a `%P` without its length,
  as clang's `CheckFormatArguments` does. Each data argument is under 256
  bytes, promoted ("argument N is too big"). For `__builtin_os_log_format`,
  a mask of other than 1 to 8 bytes is refused, and so is a struct or
  complex value that a specifier or an amount takes. Clang's `-Wformat`
  warnings aren't given, as they aren't for printf here.
- **The size.** `__builtin_os_log_format_buffer_size` is a `size_t` integer
  constant expression whose data never runs: 2 header bytes, then 2 bytes
  plus the data of each item. `__has_constexpr_builtin` answers 1 for it.
- **The buffer.** `__builtin_os_log_format` writes what clang's helper
  function writes, inline (`check_lower_os_log`), and its value is the
  buffer.
  - It writes the summary byte (0x1 for private items, 0x2 for items other
    than scalars), the items' count, then each item's descriptor
    (`kind << 4 | privacy`), its size and its data.
  - As clang's `emitBuiltinOSLogFormat`, each item's data is evaluated when
    the item is collected, in the items' order, into a temporary of the
    item's: an argument that several items take is evaluated again for each,
    so positions reorder and repeat the data's side effects, and data no item
    takes isn't evaluated. The stores follow, after every item's data.
  - Each item's data is an unsigned integer of its size, as clang's helper
    takes it. A signed `_BitInt` narrower than its class is zero-extended
    from its width, and a `long double`'s 10 bytes are followed by 6 zeros.
    Data of 16 bytes (a `long double`, an `__int128`, a `_BitInt(65)` to
    `_BitInt(128)`, a 16-byte vector, a `__float128`) and of 2 (`_Float16`,
    `__bf16`, a 2-byte vector) is stored. A `__float128`'s and a 2-byte
    vector's value is its object's address here, whose bytes are read into
    the item's temporaries as it is evaluated.
  - A 32- or 64-byte vector and a `_BitInt` past 128 bits are refused by
    name: clang's code generation crashes on them.
- **Limits.** Up to 255 data arguments are taken, as clang's Sema takes
  them (the required arguments plus 0xff): "too many arguments to function
  call, expected at most 256" (257 for `__builtin_os_log_format`) past them.
  Those past the operands' 32-bit mask are evaluated and reached through its
  full mask (#1785). A format of more than 255 items, whose count byte clang's
  code truncates (300 items store 0x2c), is refused by name.

`__builtin_dump_struct(pointer, printer, rest...)` (0x4b3, #148) is clang's
record dump, as `BuiltinDumpStruct` checks it.

- **Its arguments.** The pointer, lvalue-converted, points to a complete
  struct or union. The printer is a function or a function pointer.
- **The calls.** clang then calls the printer once per line, as
  `printer(rest..., format, operands...)`: no operands for a brace's line,
  and up to five for a field's. A prototyped printer must take every such
  call. It is variadic, with no more parameters than the rest and the
  format, and the rest and the format (a `char *`) convert to those
  parameters. An unprototyped printer takes any calls. A rest argument that
  doesn't convert is clang's conversion error, as a call's argument's, with
  its notes "in call to printing function with arguments '(rest..., "%s",
  "struct S")' while dumping struct" at the builtin and "passing argument to
  parameter 'x' here" (#2189, `check_driver_dump_struct`).
- **Bit-fields no format prints.** A named bit-field of a `_BitInt` or a
  128-bit integer type, which clang's dump would print by its address, is
  refused as clang's Sema refuses it ("address of bit-field requested").
- **Code.** The calls are planned before the function lanes run
  (`c_check_program_dump.c`, `check_lower_dump_prepare`), as clang's
  `BuiltinDumpStructGenerator` builds them, and made by `check_lower_dump_struct`:
  - the record's canonical name (`"%s"`), then `" {\n"`; an unnamed record's
    is `struct (unnamed)`, its typedef's name, or with the records whose
    member's type it is, its scope (`struct O::(unnamed)`,
    `(unnamed struct)::`, `(anonymous struct)::`);
  - a line per field, `"%s%s %s "` of its indent (two spaces a level), its
    type's name and its name, `": %zu "` and its width for a bit-field, then
    `"="`; a struct or union field's fields follow one level deeper, between
    `" {\n"` and `"%s}\n"` of its indent, and any other's value: `" "`, its
    format and `"\n"`, or `" *%p\n"` of its address where no format fits;
  - unnamed bit-fields are skipped, anonymous members' fields flattened, and
    the record's last line is `"}\n"`.
- **Type names.** A field's type prints as its declaration spells it: the
  type table has no typedef sugar, so the plan finds each member's declarator
  in its record's body (`check_lower_dump_member`), from the token past the
  body's `}` (`CheckGraph.tag_completions`, which the deferred reader's merge
  maps for records its lanes complete), and its typedef specifier
  (`check_input_typedef_spelling`) gives the alias `type_print` prints. A
  typedef's name covers its own qualifiers but those its declaration writes
  with it, which print before it as clang prints them (`CI`, `const CI`,
  `volatile CI` of a `const int`'s `CI`: `TypeNames.alias_covered`,
  `check_lower_dump_alias`). Each typedef names the place it is written
  alone: the declaration's specifier sequences, its own then each nested
  parameter's, are listed in source order with the typedef among each
  (`check_lower_dump_places`), the order `type_print` prints its specifiers
  in, which takes the k-th one's alias for the k-th it prints
  (`TypeNames.alias_order`), so `int (*)(I)` keeps its `int`; a level an
  alias names has no suffix of its own, which its AFTER and POINTER_END frames
  ask of that level alone (`TypePrinter.aliased`, by each TYPE frame's id and
  depth), so `int (*(*)(FP))(int)` keeps its return's `(int)`. Where the
  counts differ (a declaration its walk doesn't model), the typedefs apply by
  type, as before. A member's address space prints by the macro its
  declaration writes it with, as clang's `MacroQualifiedType` names it
  (#1447 item 2, `check_lower_dump_space_macro`): `AS1 int * f`, an
  invocation's `ASN(3) char * i`. That holds where the declaration writes one
  `address_space` list, which one macro's expansion wrote, and the member's
  type has one target space. The macro comes from the unit's expansion
  history, which the plan ensures once, before the lanes, for a unit that
  writes `address_space` at all. Any other space is spelled out. A typedef's
  name covers its own address space too (`T1 * t` of `typedef AS1 int T1`). An
  unnamed record's canonical name has its scope, as clang's
  `printNestedNameSpecifier` prints it: the unnamed records whose bodies hold
  it, whatever declarator it is part of (an array's, a pointer's, a
  function's: `check_lower_dump_parent`), up to a named one (C's named tags belong to no record), then the
  function whose body holds the outermost, with its parameters' types where
  it has a written prototype, each as its declaration spells it
  (`check_lower_dump_parameter`: its declarator's declared type, qualifiers
  kept, the first identifier outside a record body its specifiers define that
  declares itself, with every typedef its tokens name; an array's or a
  function's adjusted type, which keeps none) (`struct main()::L::(unnamed)`,
  `struct old_style()::O::(unnamed)`); an unnamed record whose own scope is a
  function has none.
- **Formats.** `PrintfSpecifier::fixType`'s, after clang's own cases: bool
  `%d`, plain char `%hhd` (`%hhu` with `-funsigned-char`), signed char
  `%hhd`, unsigned char `%hhu`; a pointer to a character type
  `"\"%.32s\""`, any other pointer `%p`; an enum its underlying type's; the
  integer and floating types with their length modifiers, a `size_t`,
  `ssize_t`, `ptrdiff_t`, `intmax_t` or `uintmax_t` typedef of file scope
  `z`, `t` or `j` from C99; a vector of a builtin type with its count
  (`%v4d`, `%v4hlf`, `%v16c`). `__int128`, the half-precision types,
  `__float128`, `_BitInt`, arrays, complex and `_Atomic` types have none.
- **Operands.** The printer and the rest of the arguments are evaluated
  again for each call, as clang's are, the rest converted to the printer's
  parameters or promoted; the pointer is evaluated once, after the first
  call. The strings are a read-only pool of the call's
  (`CheckLowerInitializers.dumps`), each string once. A field's value is
  read through the record's address (a bit-field's from its first byte) and
  promoted as a variadic argument; a vector passes as a call's does. A
  call's result is discarded. A printer that returns or takes a struct, a
  union, a long double, a 128-bit integer, a half-precision value or a
  vector, or returns a complex value, passes and returns them as a call's do
  (`check_lower_dump_argument`, a result in an object of the call's, #1842).
- **The rest's conversions** are a call's (`check_action_call`'s, #1842): a
  complex value converts to a complex or a real parameter and passes through
  the variadic part as itself; an `_Atomic` aggregate's parameter takes the
  argument's value, a plain or an `_Atomic` one, in an object of the atomic
  type's padded bytes; a transparent union's parameter takes a member's value
  (or the union's), passed as its first member is
  (`check_lower_transparent_value`). Each line's call evaluates the rest again,
  so an `_Atomic` object's read among them takes an atomic plan a line, which
  the lowering's room counts (`CheckLowerInitializers.extra_atomics`, and
  os_log's items alike); before, a dump of an `_Atomic` argument wrote past the
  plans' table, over the function's long double constants.
- **Refusals.** An array or a function for a transparent union's parameter,
  rest arguments with a statement expression or more than 4096 nodes, and
  records of more than 1 GiB of strings or 64 levels are refused by name. A record's lines and strings grow as they need (the strings are
  found again through a hash table of their offsets), so a record of any
  number of fields is dumped (#1842).

`__builtin_available(spec, ...)` (0x4b4, #148) is clang's availability check,
a keyword whose specs are no expressions: each a `*`, or a platform's name and
its version (`macos 10.12`, `iOS 10_3`). It is a `_Bool` that is no constant
expression. On an x86-64 Linux target, whose platform name is empty, the
`*`'s, clang's code is the constant 1.

- **The parser.** The scalar parser reads the specs in their own modes
  (`PARSE_MODE_AVAILABILITY_SPEC`, `_VERSION`, `_NEXT`): a `*` or an
  identifier, whatever the ordinary names mean (a typedef's name too), then
  a number, then `,` or `)`. Its errors are clang's where the kinds don't fit:
  "expected '('" just past the keyword, "expected a platform name here",
  "expected a version of the form 'major[.minor[.subminor]]'", and the
  balanced delimiters' "expected ')'". The vector path's checks never take the
  keyword (`PARSE_CLASS_OPERAND_KEYWORD`), so its declarations fall back.
- **The specs' checks.** `c_check_availability_input.c` makes the checks of
  clang's parser past the kinds, at the keyword, in the walk's source order:
  - `ParseVersionTuple`'s reading of the number's spelling: a numeric
    constant (not `true` or `__func__`), its components separated by `.` or
    `_`, their values wrapping as unsigned ones, a zero version's error at the
    token after the number, and -Wavailability's warning where the
    separators differ (`1.2_3`);
  - the platform, canonicalized as `canonicalizePlatformName` does, one of
    the 19 `getPrettyPlatformName` names, and none with `xros` in it;
  - an anyAppleOS version of 26 at least;
  - and then, where each spec is valid, the list's: a second `*`, a
    platform's second version, and no `*`, just past the last version.

  A version whose kept components are all 0 (`0.0.0`, `0.2147483648`) is no
  spec of clang's, without a diagnostic: clang's expression is invalid, and
  its code generation refuses it ("cannot compile this scalar expression
  yet") or, where it is a whole return value or initializer, drops the
  statement silently ("Checked, refused in code"). `-c` refuses it here by
  name.
- **The value.** The call takes no operand (its specs' tokens are ignored).
  The walk and the constant evaluator, in its raw and its bound modes, give it
  the builtin's id at its `)`, so `sizeof`, `typeof`, `_Generic` and a VLA's
  bound read its type, and an integer constant expression refuses it.
- **-Wunsupported-availability-guard.** clang warns at a check in a function's
  body that isn't an if's condition, as written through parentheses and `!`,
  nor a case label's value, once it has read the body: the warning completes
  after the body's `}`. A check whose specs aren't each valid is no
  expression of clang's, which no warning visits. A file-scope check gets
  none. The walk and the constant evaluator (a `typeof`'s operand, which the
  walk doesn't visit) each ask, once.
- **Differences.** clang reads the rest of a list after a spec's error, and
  its errors before a missing `)` include the list's; this parser stops at
  its first. `__builtin_available` in a constant context gets the checker's
  messages of a call that is no constant, as `__builtin_cpu_supports` does:
  "initializer is not a constant expression" where clang says "initializer
  element is not a compile-time constant", and a static assertion's message
  at the call's `)`.

`__builtin_counted_by_ref(member)` (0x4b5, #148) is clang's address of a
counted_by count, as `BuiltinCountedByRef` checks it and `GetCountedByFieldExprGEP`
computes it.

- **Sema.** Its argument, past parentheses, is a pointer or an array without side
  effects and a member access (`.` or `->`), else clang's errors at it ("argument
  must reference a member with the 'counted_by' attribute", "cannot have
  side-effects"). A member of a `counted_by` count (not `sized_by`'s or an
  `_or_null` one's) makes it a pointer to the count's member, of its declared type,
  found by its name in the access's record (`check_graph_counted_member` in the
  walk, `check_counted_lookup` in the constant evaluator, which keeps an access's
  base type at its operator); any other such member a `void *`.
- **Its value's uses.** clang's `CheckInvalidBuiltinCountedByRef` refuses it where it
  would leak the count's address, by where the call stands as written, past
  parentheses (`check_input_counted_ref_context`): an assignment's operand or a
  declaration's initializer (not an initializer list's element), a call's argument,
  a return's value, a subscript's base and a binary operator's operand, the comma
  operator's too, each with clang's message at the call.
- **Code.** The access's record's address (an `->`'s pointer, a `.`'s object's)
  plus the count's offset; a null pointer for a `void *` one. Its argument doesn't
  run otherwise. A bit-field count, which crashes clang's code generation, is
  refused by name.

`__builtin_dynamic_object_size` of clang's counted_by forms (#148,
`emitCountedBySize` and `findStructFieldAccess`) counts at run time
(`check_graph_counted_size`, `check_lower_counted_size`): past casts, `&`, `*`, a
comma's right operand and one subscript, to a member access. A counted flexible array
member (or its element's address) is the count times its element's bytes, less the
index's bytes; a counted pointer read as a value the same, by its pointee's bytes (1
for `void`); another member's address, through a pointer, the record's size less the
member's offset (and its index's bytes) plus the flexible array member's bytes.
The count converts to the size's width by its signedness, and a negative result, or
a negative index, is 0. clang's evaluator leaves a counted flexible array member to
the code, so no bound of the object folds it here either; mode 3, an argument with
side effects and a bound the object gives (another member of a named object) keep the
ordinary query's answer. `__builtin_object_size` ignores the count, as clang's does.

`__builtin_infer_alloc_token(args...)` (0x45f, #148) is a `size_t` whose
arguments never run. It is clang 23's allocation token: the reader works it
out from the arguments' syntax, in the walk and in the constant evaluator
alike (`c_check_alloc_token_input.c`).

- **The type.** clang's `inferPossibleType` gives it. The first argument
  that gives one decides.
  - The argument's first `sizeof` (not `_Alignof`) in source order that
    nothing but groupings and binary `+ - * / << >>` hold gives its
    operand's type. That is the leftmost one clang's walk reaches.
  - A variable's name alone gives its initializer's.
  - An explicit cast stops the search, as clang's `IgnoreParenImpCasts`
    keeps casts.
- **The name.** The type's canonical name as clang prints it with
  `SuppressTagKeyword`: `S`, `char *`, `int[4]`, `int (*)(int)`, `bool` in
  C23. A typedef-named record prints the typedef's name. An unnamed record is
  `(unnamed struct at FILE:LINE:COL)`, qualified by the records around it
  (`O::(unnamed struct at …)`), as clang qualifies one but not a named one.
  The type printer's `BUSTER_TYPE_PRINT_NO_TAG_KEYWORD` drops the keywords.
- **The token.** LLVM's `getStableSipHash` of the name, SipHash-2-4 under
  LLVM's fixed key, modulo half of the 64-bit token space. A type holding a
  pointer takes the upper half, as `TypeHashPointerSplit`, the default mode,
  gives it. Holding means a pointer member, array element, atomic value or
  nested record, or a `uintptr_t` through typedefs. For `uintptr_t`, the type
  table keeps no typedef sugar, so the reader looks at the `sizeof`'s operand
  and at each record body's members.
- **Constness.** With a type, the token is an integer constant expression.
  Without one, or for an incomplete record without a pointer, there is no
  constant, as clang's evaluator gives none, and the code's value is 0, the
  AllocToken pass's fallback token.
- **Arguments.** They may be anything but `void`. `__has_constexpr_builtin`
  answers 1, as clang's does.
- **Limits.** `-falloc-token-max` and `-falloc-token-mode` aren't options
  here. An unnamed enumeration's name, and more than 31 arguments, are
  refused. A body on a deferred lane that names an unnamed record is read
  again in source order, as source-location builtins' bodies are.

`__builtin_cpu_init`, `__builtin_cpu_is` and `__builtin_cpu_supports` (0x44f
to 0x451) are libgcc's CPU model's, as clang's code reads it: `cpu_init()`
calls `__cpu_indicator_init`, which is also the model's constructor;
`cpu_supports(s)`, a `bool`, tests the bit of the feature the string literal s
(parenthesized at most, never cast, as `allow_runtime_check`'s)
names in its word (`__cpu_model.features[0]` at 12, or `__cpu_features2`'s),
and is false for a feature clang doesn't know, where clang also warns
("invalid cpu feature string for builtin", of no option, at the builtin's name:
`check_input_cpu_supports_literal`, #1843); `cpu_is(s)` compares the model's vendor, type or subtype with
the value of the name s, which clang must know (an error otherwise). The names
and their words, bits, fields and values are `c_builtin_cpu.inc`, derived
LLVM data that `tools/generate_x86_cpu_model.c` generates: the strings and ABI
values of LLVM 23.1.5's `X86TargetParser.def` rows that clang's
`validateCpuSupports` and `validateCpuIs` take. `THIRD_PARTY_NOTICES.md`
records the input, its digest and the command. The lowering reads
the model through the runtime's symbols `__cpu_model` and `__cpu_features2`
(`check_lower_runtime`, data symbols: `check_lower_runtime_object`), which
libgcc.a's `cpuinfo.o` defines, and calls `__cpu_indicator_init` as a runtime
function. `test-checker-driver` builds a program that defines the model itself
and tests every name over each bit of each word and each value from 0 to 63 of
each field, by clang and by both paths, which must print the same
(`check_test_cpu_model_runs`), and `test-checker` runs `cpu_init` and some
tests of this host's model against clang's build.

`__builtin_infer_alloc_token` stays out: clang's value is a hash of the type it
infers from a `sizeof` among its arguments (0 without one), which its front end
folds.

`__builtin_stdarg_start` is the old name of `__builtin_va_start`, which clang's
Sema checks as that: the catalogue resolves it to `va_start`'s entry
(`cbuiltin_alias_entry`). Both take two arguments in every standard, as clang
23's do. C23's `va_start` with one argument is `<stdarg.h>`'s, by
`__builtin_c23_va_start`.

Sixteen others are leaves, per-unit helpers of clang -O0's instructions
(`c_builtin_x86_leaves.inc`), named in the supplementary scope with no
features. Their prototypes are clang's metadata's:

- `__builtin_debugtrap`: `int3`, whose trap returns past it;
- `__builtin_readcyclecounter`: `rdtsc`, as `__builtin_ia32_rdtsc`;
- `__builtin_readsteadycounter`: 0, since x86's code reads no counter for it;
- `__builtin_thread_pointer`: `fs:[0]`;
- `__builtin_stack_address`: the caller's rsp at the call, past the leaf's
  return address;
- `__builtin_dwarf_sp_column`: 7, rsp's DWARF register;
- `__builtin___clear_cache`, `__builtin_assume_separate_storage` and
  `__builtin_assume_dereferenceable`: nothing past their arguments, which the
  call evaluates as clang's does;
- `__builtin_memcpy_inline` and `__builtin_memset_inline`: their constant
  size's bytes, by `rep movsb` and `rep stosb`, where clang's code stores them
  itself;
- `__builtin_unwind_init`: a leaf that does nothing, whose caller's frame
  saves every callee-saved register (rbx, r12 to r15, and rbp where it isn't the
  frame pointer) and restores it, with its unwind information, as LLVM's
  `llvm.eh.unwind.init` makes it (`Tier0Function.unwind_init`, #1126's review);
  ordinary Tier 1 leaves such a function to Tier 0;
- `__builtin_init_dwarf_reg_size_table(p)`: 8 in each of the 17 bytes at p, the
  sizes of the DWARF registers rax to rip, as clang's code stores them;
- `__builtin_set_flt_rounds(v)`: the rounding mode `FLT_ROUNDS`'s value v names,
  in the x87's control word and in MXCSR, by LLVM's mapping (0xc9 shifted by
  2v + 4, whose bits 10 and 11 are the control word's and, 3 places up,
  MXCSR's 13 and 14). Both go through the red zone below the leaf's return
  address;
- `__builtin_extend_pointer(p)`: p as an `unsigned long long`;
- `__builtin_verbose_trap(category, message)`: `ud2`, as `__builtin_trap`'s;
- `__builtin_setjmp(buf)` and `__builtin_longjmp(buf, 1)`: LLVM's SjLj lowering
  (X86's `emitEHSjLjSetJmp` and `emitEHSjLjLongJmp`) as leaves. setjmp stores
  the caller's frame pointer (rbp, its frame's address where it has one, as
  clang's `llvm.frameaddress`), the resume address (the leaf's own return
  address, after the caller's call) and the caller's stack pointer (clang's
  `llvm.stacksave`) in the buffer's words 0 to 2, and returns 0. longjmp loads
  rbp and rsp back, sets eax to 1 and jumps to the resume address, where the
  call of setjmp returns again with 1; a `ud2` ends it, which no path reaches.
  Since a longjmp from a deeper frame leaves every other register as that frame
  left it, the function that calls setjmp has the frame LLVM's setjmp, which
  clobbers every register, gives it (`Tier0Function.returns_twice`): it saves
  every callee-saved register and restores it, as `unwind_init`'s does, keeps no
  local in one and no value of its stacks either (its integer depths are the
  nine caller-saved registers'), and a realigned frame whose base is r15 (an
  over-aligned object beside `alloca` or a VLA) is refused. A value of an
  enclosing expression that a call holds (`100 + __builtin_setjmp(buf)`'s 100)
  is saved in a region of the call's own in such a function
  (`tier0_layout_frame`), so the second return finds it whatever calls ran
  before the longjmp. Before #1242's review the regions were shared, so a later
  call could overwrite the value. A long double's, an `__int128`'s or a
  vector's value lives in its depth's frame slot, which later expressions
  share: the call copies the slots below its arguments into a snapshot of its
  own and back after it (`tier0_returns_twice_snapshot`, #1843), 8 bytes at a
  time through the x87 stack's FILD and FISTP, which keep every bit pattern and
  touch no register the call's moves hold. The snapshot also takes the mask
  slots below the call (the masks it saves from their registers and those past
  them, #2465), and a deep call's integer and floating values below its
  arguments in their deep slots, which other deep entries share; so no value
  held across such a call is refused. C's contexts for setjmp (a condition,
  compared with a constant) hold none. Ordinary Tier 1
  leaves such a function to Tier 0.
  longjmp's second argument must be a constant 1, wherever the call is, with
  clang's two errors at the call (`check_input_longjmp_argument`).

A call's size of `memcpy_inline` and `memset_inline` must be an integer
constant expression (the prototype's `I`). A non-constant one is clang's error
"argument to '__builtin_memcpy_inline' must be a constant integer", at the call,
as for the x86 builtins (`test-checker-driver`'s generic builtins' fixtures).

`verbose_trap`'s two arguments must each be a constant string without a `$`,
as clang's Sema reads them (`check_input_verbose_trap_arguments`, in the source
adapter), each error at the argument's first token with clang's message. A
narrow string literal or `__func__`'s name, under parentheses and casts, is read
whole; an argument that is no address constant of an object (an automatic
variable's value, an integer, a null pointer) is no constant string. Another
address constant, a `static const char[]`'s or a literal's past its start, clang
reads through its constant evaluator: the adapter reads its chars from the
unit's constant objects (`check_input_verbose_trap_object`, #1843) up to the
first NUL, of an array of plain char, with clang's errors where the string has a
`$` or the read leaves the array (past its end, or no NUL in it). The address
of a const record's member array reads its string to the member's end, and a
const pointer object's value its pointee's.

Casts on the way are clang's evaluator's (`check_input_verbose_trap_designator`,
#1843). A narrow literal's bytes it reads at the address whatever the casts
(`tryEvaluateString`'s fast path: `(const char *)(const void *)"m" + 1`,
`(const char *)(const unsigned char *)p` of a pointer to a literal), up to its
terminator: one past that is no string. Any other object's chars, `__func__`'s
and a wide literal's among them, it reads through the lvalue's designator, which
a conversion between pointers to different types loses (ExprConstant's
`CK_BitCast`). So the argument's own type, each cast to a pointer and each such
cast's operand must point to plain char (or be an array of it), qualifiers
aside: `(const char *)(char *)m` reads `m`, but `(const char *)(const void *)m`,
`(const char *)u` of an `unsigned char` array, `(const char *)&s` of a record
and `(const char *)&m` of the array itself are clang's "must be a pointer to a
constant string". A const pointer object's own initializer's casts count too
(#2189): `static const char *const q = (const char *)(const void *)m;` lost
the designator it stores. An object whose bytes the reader doesn't keep is
still refused by name.

Differences left: `-Wstring-plus-int` at such an argument isn't given here (#2190). A
builtin's argument's `-Wpointer-sign` and qualifier warnings are clang's
words, "passing 'const unsigned char[5]' to parameter of type 'const char *'
converts between pointers to integer types where one is of the unique plain
'char' type and the other is not", at the argument (#2189,
`check_driver_pointer_arguments`, for builtins whose arguments are a list's
or their operands); beside `__builtin_verbose_trap`'s own error at the same
argument, clang gives the warning first and this gives it after.

Two costs are accepted for now:
- A call of `memcpy_inline` or `memset_inline` is a call of its leaf, where
  clang's code makes the constant size's stores itself. Lowering them through
  the misc builtins' straight-line copy and fill recipes
  (`cbuiltin_misc_lower_copy_small`) is a follow-up.
- `__has_constexpr_builtin` answers 0 for `__builtin_assume_separate_storage`,
  which clang answers 1 for. A leaf has no catalogue row for the query to read,
  and since the builtin returns `void`, no C constant expression can call it.

`test-checker --program-align` checks source fixtures of their classes,
registers, types and refusals by both paths against clang, and runs programs
natively against clang -O0's build: the folds, launder's pointers, a callee's
CFA against its caller's stack address, debugtrap's trap counted by a SIGTRAP
handler, the counters, the thread and stack pointers, the no-ops' arguments
evaluated once in order, the inline copy's and fill's bytes, the unwinder's
table of register sizes, each rounding mode in the control word and MXCSR and a
division rounded by it, the extended pointer, the values `unwind_init` leaves,
`verbose_trap`'s SIGILL in a child process, and the alloca variants' bytes and
alignments in a loop whose allocations stay apart, the checks' values, the
annotation's integer evaluated once, `addressof`'s and `function_start`'s
addresses of a local, a member, an array, a function and in static data, and
`complex`'s folds in static data and an enumerator and its values at run time of
every real floating type, from qualified, aligned and volatile operands, each
evaluated once in order, `__arithmetic_fence`'s values of each floating type, a
complex one and a vector, the xray events' arguments, which never run, and
`trivially_relocate`'s moves of ints, doubles and arrays of ints, overlapping
ones among them (`c_check_program_generic_builtins_test.c`). `test-checker-driver` compares
`verbose_trap`'s errors, their places and messages, with clang's by both paths
(`c_check_generic_builtin_driver_test.c`).

## The builtins of a place and of a type's shape (#148)

`__builtin_LINE`, `__builtin_COLUMN`, `__builtin_FILE`, `__builtin_FILE_NAME`,
`__builtin_FUNCTION`, `__builtin_vectorelements` and
`__builtin_omp_required_simd_align` are keywords, as in clang, and the parser
reads them so. The checker's walk in source order works each one out where it
reads it (`check_input_source_builtin`, `check_input_builtin_constant_prepare`,
`c_check_constant.c`'s `check_constant_source_builtin`), as it does
`__builtin_offsetof`: a constant cached at the call's `)`, which every context
takes, code, static initializers, enumerators and `_Static_assert` alike.

- `__builtin_LINE()` and `__builtin_COLUMN()` are `unsigned int` constants of the
  call's place as clang's `SourceLocExpr` gives it. That place is the end of its
  outermost macro expansion, as `getExpansionRange(...).getEnd()` resolves it:
  a function-like invocation's closing parenthesis, an object-like macro's
  name, and for a macro argument the end of the invocation it was passed to.
  A call written outside any macro is placed where it was written. The line is
  presumed after `#line`, and the column counts from 1
  (`check_input_source_place`: `pp_output_expansion_end` over the expansion
  history, which a unit that deferred it records then, `pp_presumed` and
  `pp_line_of`).
- `__builtin_FILE()`, `__builtin_FILE_NAME()` and `__builtin_FUNCTION()` are
  `char *` constants, the addresses of strings the walk makes
  (`check_input_source_string`): the presumed file's name as written (after
  `#line`), its last component as clang's `llvm::sys::path::filename` takes it
  (`pp_path_file_name`, which `__FILE_NAME__` shares: `"/"` for a root, `"."`
  for a name that ends in `/`, a `//net` root name whole, the whole name where
  that is empty), and the enclosing function's name, `""` outside
  one. Each is a private string object of its keyword's token, as `__func__`'s
  array is, which the emission names `.Lbuster_string_N`.
- `__builtin_vectorelements(T)`, of a vector type or of an expression of one,
  which isn't evaluated, is its elements' count. Its parentheses are read as
  `sizeof`'s, so a comma expression is an operand, of its last operand's type.
- `__builtin_omp_required_simd_align(T)` is the unit's target's SIMD alignment
  in bytes whatever the type (16, 32 with AVX, 64 with AVX-512F, as clang's
  `getOpenMPDefaultSimdAlign` gives it). Its operand is `alignof`'s: an array's
  base element type, which must be a complete object type, not `void`, an
  incomplete record or a function type.
- Each is an `unsigned long`. An operand that isn't what clang takes is an
  error, as clang's is.

clang's type traits of C, `__is_destructible(T)`, `__is_nothrow_destructible(T)`
and `__is_bitwise_cloneable(T)`, are keywords of the same span and constants
of the same walk, each an `int` (Sema's `getLogicalOperationType`), as
`EvaluateUnaryTypeTrait` gives them where no type has a destructor:

- the destructible ones are 1 for every complete type, a variably modified one
  among them, and 0 for `void`, an array of no bound and a function type;
- `__is_bitwise_cloneable` is its base element type's (`isBitwiseCloneableType`):
  1 for a complete one and a function type, 0 for `void`;
- `CheckUnaryTypeTraitTypeCompleteness` first requires the destructible ones'
  base element type to be `void` or complete, and `__is_bitwise_cloneable`'s
  type to be `void`, an array of no bound or complete: an incomplete record or
  enumeration is an error at the keyword.

`__builtin_ptrauth_type_discriminator(T)`, of the same span, is no constant:
every use is an error at its keyword, where clang says "this target does not
support pointer authentication".

A deferred body's lane refuses a body that uses one of the first five (the
preprocessor's places change as they are read), which source order then reads.
`__has_builtin` answers 1 for all eleven (`pp_implemented_builtin`), and
`__has_constexpr_builtin` 0, as clang's. `test-checker`'s
`generic-source-places-and-type-queries` runs them natively against clang -O0's
build by both paths, after a `#line` that names the file, and `generic-type-traits`
the traits of complete, incomplete and variably modified types; their source
fixtures compare every kind of type and context, and the errors, with clang.

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

`c_check_builtin_x86.c` adds 562 audited SemaX86 constraint facts for 534
affected names in the frozen 1,492-name own-main target-builtin scope and the
supplementary scope: 127
rounding sets, 91 SAE sets, 64 gather/scatter scale sets (1, 2, 4 or 8, a hard
error as clang's `CheckBuiltinGatherScatterScale` has it) and 280 direct ranges,
`_mm_prefetch`'s hint (0 to 7) the last of them.
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

#799's report and its follow-up name the immediate intrinsics that stb_image,
xxHash and zstd call through `<immintrin.h>`: `_mm_shuffle_epi32`, the word
shuffles, the byte shifts, `_mm_shuffle_ps` and `_mm_shuffle_pd`, the word
insert and extract, `_mm_alignr_epi8` and `_mm_blend_epi16`, and AVX2's
`_mm256_permute4x64_epi64`, `_mm256_shuffle_epi32`,
`_mm256_permute2x128_si256`, `_mm256_alignr_epi8`, `_mm256_blend_epi32`, the
byte shifts, `_mm256_extracti128_si256`, the word shuffles,
`_mm256_permute_ps` and `_mm256_permute4x64_pd`. Each has its builtin's row
and its SemaX86 range. The x86 programs' `x86-immediate-header-intrinsics` runs
every one of them through the header at znver5, at its immediate's edges (0,
the fields' widths and one past them, the lanes' and elements' counts, 255) and
with its operands both ways, against clang -O0's build.
`check_test_x86_header_ranges` gives each one past its range, below it or not a
constant, and all of them at their ranges' tops. It requires clang's lines from
both of the driver's paths: each error at the macro's use in the source, and the
note that names the macro. At targets without AVX-512F and AVX-512VL, these
intrinsics stop at #775's limits.

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
- AVX-512's arithmetic by a rounding control: the 64-byte vectors' sums,
  differences, products, quotients and square roots (`addpd512` to `divps512`,
  `sqrtpd512`, `sqrtps512`), the scalar ones into w under a mask
  (`addsd_round_mask` to `divss_round_mask`), the conversions of integers to
  floats and of floats to doubles under a mask (`cvtdq2ps512_mask`,
  `cvtudq2ps512_mask`, `cvtqq2pd512_mask`, `cvtuqq2pd512_mask`,
  `cvtps2pd512_mask`, which takes the exceptions' suppression), the scalar
  conversions to integers (`vcvtsd2si32` to `vcvtss2si64`) and from them into a
  vector's low element (`cvtsi2sd64` to `cvtusi2ss64`, a family of their own,
  `CHECK_X86_LOWER_INT_CONVERT`), and the 64-byte FMAs merged into a, zeroed or
  merged into c (`vfmaddpd512_mask` to `vfmsubps512_mask3`). The current mode
  (4) takes the plain form; 8 to 11 take the embedded rounding's form with its
  direction. `x86_semantics.c` gained the effects of the forms these reach that
  it lacked: VSUBSS, VSUBSD, VDIVSS and VDIVSD's EVEX forms, the 231 FMAs' and
  FMSUBs' 64-byte forms and the EVEX integer-to-float conversions.
- the rest of the gathers and scatters: AVX-512's of floats and doubles at 64
  bytes (`gathersiv16sf` to `gatherdiv8df`, `scatterdiv16sf` to
  `scattersiv8di`) and of every element at 16 and 32 bytes (`gather3div2df` to
  `gather3siv8si`, `scatterdiv2df` to `scattersiv8si`), the existing families'
  with their forms; and AVX2's under a vector's signs (`gatherd_d` to
  `gatherq_q256`), a family of their own (`CHECK_X86_LOWER_GATHER_VECTOR`:
  VEX's VPGATHER with the mask in vvvv, which the instruction zeroes). Each
  takes clang's scale check (1, 2, 4 or 8). `x86_semantics.c` gained the effects
  of the float gathers and scatters, VPGATHERQD's and VPSCATTERDQ's.
- SSE4.1's roundings by an immediate (`roundps`, `roundpd`, their 256-bit
  forms, and `roundss` and `roundsd` of b's first element into a: 0 to 15), its
  dot products (`dpps`, `dpps256`, `dppd`), AES's key assist
  (`aeskeygenassist128`), SHA's four rounds (`sha1rnds4`, 0 to 3, a legacy form
  without vvvv, as BINARY's SHA forms are) and F16C's conversions to halves
  (`vcvtps2ph`, `vcvtps2ph256`, 0 to 255), each the immediate's whole byte, by
  VEX forms that require AVX. `x86_semantics.c` gained their VEX forms'
  effects, and Tier 0's legacy list SHA1RNDS4.
- the system builtins whose instructions take fixed registers or memory, as
  per-unit leaves (`c_builtin_x86_helpers.c`): `rdtsc` (`__rdtsc`'s records),
  `lzcnt_u16` (BSR's index xored with 15, 16 for 0), the fences (`lfence`,
  `mfence`, `sfence`), `clflush`, `ldmxcsr` and `stmxcsr` (through the leaf's
  red zone) and `xsetbv`, which only the kernel may run and whose fixture only
  compiles and links.
- the header builtins, leaves too: the names `<xmmintrin.h>`, `<emmintrin.h>`
  and `<prfchwintrin.h>` declare (`_mm_lfence`, `_mm_mfence`, `_mm_sfence`,
  `_mm_pause`, `_mm_clflush`, `_mm_getcsr`, `_mm_setcsr`, `_m_prefetch` and
  `_m_prefetchw`), whose calls were calls of undefined functions. An x86 builtin
  of the intrinsic catalogue's rows and leaves (`cbuiltin_source_intrinsic`)
  stays the builtin under a declaration of it with external linkage, at file
  scope or at block scope, as clang keeps it (`check_input_intrinsic_prepare`,
  each catalogue row's verdict remembered in `CheckInputReader.intrinsic_source`);
  a definition with internal linkage is the program's function, and a local
  object of the name hides it. A declaration with external linkage of a
  builtin's name (an x86 builtin of those rows and leaves, or a catalog builtin
  such as `__builtin_bswap32` or `__builtin_clz`; not a library builtin, which a
  program may define) is checked as clang's Sema checks it
  (`check_input_builtin_declaration`, #1843): a type that isn't compatible with
  the builtin's is clang's "conflicting types", with its note of the builtin's
  type, and a definition, C99's inline one too, is clang's "definition of builtin
  function". A compatible declaration keeps a catalog builtin the builtin, its
  calls the builtin's, an integer constant expression's too
  (`check_input_builtin_kept`), as clang keeps it.
  `_mm_prefetch` is `<xmmintrin.h>`'s macro over `__builtin_prefetch` where
  the header is included, and without it clang's builtin (a
  `TARGET_HEADER_BUILTIN` of no features), which is a leaf too: its hint a
  constant of 0 to 7, SemaX86's range (`c_check_builtin_x86.c`'s last rule,
  whose error clang defers to reachability), and its code no prefetch, as
  `__builtin_prefetch`'s has none: a prefetch has no effect a program sees,
  past its address's evaluation.

`test-checker`'s programs of them (`c_check_program_x86_rest_test.c`) call each
at its immediate's edges, and past them, where a pragma turns off clang's
`-Wargument-outside-range` and both compilers' code takes the immediate's low
bits (an index's, a predicate's), against clang -O0's code for znver5, run
where the host has the features. `test-check-builtins-x86` compiles every
supplementary fact's edges with clang (`check_builtin_x86_test_clang_supplementary`):
a function per value of the constrained argument, the others at values their
facts accept, declared at the prototype's types as clang spells them. Clang
must refuse exactly the values the fact refuses, with the fact's bounds.

#1029 adds the remaining 21 x86 names from the znver5 `__has_builtin` gap to
the supplementary scope, with their existing Clang 23.1.1 catalogue
prototypes and target-feature expressions. The implementation and fixtures
are original. The additional semantic facts are audited from
[`llvmorg-23.1.1`'s `SemaX86.cpp`](https://github.com/llvm/llvm-project/blob/llvmorg-23.1.1/clang/lib/Sema/SemaX86.cpp):

- The fourteen `pcmpestr*128` and `pcmpistr*128` names use SSE4.2 leaves
  specialized by the converted immediate byte. Explicit lengths move from
  the helper ABI's `edi`/`esi` into `eax`/`edx`; index results move `ecx` to
  `eax`, masks return in `xmm0`, and A/C/O/S/Z flag results use `setcc` and
  zero extension. The planner deduplicates by name and immediate, with a
  separate bounded key table allocated only for units using these leaves.
  The ordinary call boundary accounts for every implicit register clobber.
  Their prototype's `I` marker requires an integer constant expression;
  SemaX86 adds no direct range check for these names. Controls outside the
  byte are converted to the prototype's `char` before specialization.
- SSE4a's `extrqi` and `insertqi` use their two-immediate instruction forms.
  The row retains each operand's separate ICE check and packs the converted
  length and index bytes into the encoder's two-immediate field. These
  names likewise have no extra SemaX86 range rule. Native fixtures compare
  the defined low quadword, with lengths and indices whose sum is at most
  64 after the instruction's six-bit truncation; they do not inspect
  EXTRQ's unspecified upper quadword.
- `vcvtps2ph_mask`, `vcvtps2ph256_mask` and `vcvtps2ph512_mask` use masked
  narrowing rows with source in ModRM.reg and destination in r/m. The
  128/256-bit forms require AVX-512VL and the 512-bit form AVX-512F.
  SemaX86's cases at lines 761, 763 and 764 constrain argument 1 to 0–255.
  The 512-bit form selects its SAE encoding only for controls 8–12 and
  encodes them as 0–4. Other controls, including 13–15 and 255, keep their
  ordinary encoding; there is no extra rounding operand or separate
  SAE-set check. A pinned Clang 23.1.4 assembly oracle confirms this
  distinction, and the native fixture resets MXCSR between controls and
  width groups so that exception status cannot conceal it.
- `readeflags_u32` and `writeeflags_u32` are recognized by `__has_builtin`
  even on x86-64. Calls receive `this builtin is only available on 32-bit
  targets`, following `isX86_32Builtin` and its early check at lines
  523–540. This is a semantic error in dead and unevaluated code too, before
  lowering. Prototype errors still take precedence, and a failed call has
  no valid expression type, preventing cascading operator diagnostics.

`test-checker` adds native Clang comparisons for all fourteen string results,
both SSE4a forms, and all three masked conversions. They exercise repeated
and distinct controls in one unit, signed and saturated lengths, live input
vectors across fixed-register calls, both mask formats, zero/sparse/full
conversion masks, the full low rounding nibble, all MXCSR rounding modes,
ties and exceptional floating values. The MXCSR is restored after each
fixture. Independent byte checks cover all 256 controls of each string
leaf through both encoders, including its fixed-register ABI and bounded
output failure. Diagnostic fixtures compare both SSE4a ICE positions,
string controls and the 32-bit-only errors against Clang. The existing
supplementary-range oracle covers the three new range facts; catalogue and
preprocessor tests cover exact names, bounded slices and target features.
The lowering-to-prototype coverage check also requires both SSE4a immediate
arguments to be ICEs, and checks VCVTPS2PH512's shared immediate/SAE control
at its actual parameter index while leaving the runtime mask unrestricted.

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
`-Wargument-outside-range`'s default-error policy. Its message ends in
` [-Wargument-outside-range]` wherever the driver reports it, as clang's does
under every mapping: clang's printer adds `-Werror,` only to a warning that
isn't an error by default, so the option is alone whether the diagnostic is a
warning, an error or a fatal error, by pragma or with `-Werror` (#1079). The
ordered command-line mappings start from that error baseline (`-Wno-error=`,
`-Wno-`, `-Werror=`, re-enabling it after `-Wno-`, and
`-Weverything`/`-Wno-everything`), and diagnostic pragmas then downgrade or
suppress it at their locations; `-w` suppresses it over any pragma, as clang
does. The driver's general `-Wfatal-errors` is #1081. A failed or incomplete
program leaves unknown events for conservative reporting rather than dropping
them.

Only an actual range event allocates the temporary reachability worklist.
It uses one visited byte and one 64-bit stack slot per graph node/control,
each queued at most once, and restores scratch on success and failure. The
scalar walk handles the dependent edges of this exceptional diagnostic path;
ordinary valid inputs retain the existing scalar/vector checking kernels.
`test-checker` compares independent source fixtures with Clang, one translation
unit per fixture, through both production driver schedules. It checks
acceptance, severity, complete range messages (their option's suffix too) and
source locations, including negative immediates, dead control flow,
unevaluated operands, pragma state and the command-line mappings above.
`check_test_x86_range_mappings` holds clang 23's text, each diagnostic and note
line for line, for the default, `-Werror`, `-Werror=argument-outside-range`,
clang's and GCC's warning, error and fatal pragmas, a push and a pop, and
deferred reports, which clang and both paths must give. The command-line
mapping fixtures compare status, severity, location and range message for the
ordered mappings and their pragma combinations.

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

For #1066, a call whose hard immediate constraint fails is reported as clang
reports it. A nonconstant immediate, one of the prototype's `I` arguments or
one a SemaX86 rule constrains, gives `argument to 'NAME' must be a constant
integer`, where clang's `BuiltinConstantArg` says it. An invalid rounding or
SAE control gives `invalid rounding argument`, and an invalid gather or
scatter scale `scale argument must be 1, 2, 4, or 8`. Each is placed at the
call's begin, the builtin's name, with the notes of the macros it comes from.
The checker still records them as `CHECK_DIAG_INCOMPATIBLE` on the call's node,
and its effect keeps the failed constraint. The call's value has no type, so
that an enclosing call or operator inherits the error rather than reports
another, as clang checks no call whose arguments hold an error. The driver and the corpus check
find that effect by node (`check_diagnostic_x86_hard`), a search per error
reported, since the stream keeps the effects in their nodes' order. The report
keeps the call's `)` as its token, so its order among the unit's other
diagnostics, and the cutoffs that read it, are as before: clang reports the call
after its arguments' own diagnostics. `-fsyntax-only` and `-c` print it alike.
`test-checker` (`check_test_x86_hard_errors`) compares hand-written fixtures
with clang, line for line: the k-shifts, a call over two lines, two calls on one
line, nested calls, macros, constants that are integer constant expressions and
ones that aren't, rounding, SAE and scale controls, and errors in the arguments
(an undeclared one, failed calls inside calls and operators, and failed calls in
a conditional's arms, which each report). Each runs by both paths, with
`-fsyntax-only` and `-c`. `check_test_x86_hard_corpus` runs the corpus check on
a call over two lines, in a unit that parses and in one whose later syntax
error sends it through the repair. Its first error must be clang's line,
column and message. `check_test_x86_hard_every` generates a unit with one
call for every in-scope x86 builtin and each of its integer constant arguments,
that argument a parameter, and compares it with clang the same four ways. It
also checks that every lowering row's immediate and rounding control is such
an argument, so the lowering never refuses a nonconstant one that the checker
let through. It also checks that every x86 builtin's message fits
`BUSTER_CHECK_DIAGNOSTIC_X86_TEXT`, and that it generated every call it counted.
Its coverage may not fall below the tree's: 588 calls of 555 builtins, and 555
lowering rows.

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
  `-fenable-matrix`, which buster hasn't: no unit has a matrix type. Since
  nothing more could compile, they are implemented as clang's are, and
  `__has_builtin` answers 1 for them.

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

## Library builtins

#148's library builtins are clang 23's rows with attribute `F`, `__builtin_X`
for a C library function X, and its rows with attribute `f`, X itself where
clang makes the name a builtin of the unit. `c_builtin_library.c` holds both
lists, `c_check_builtin_library.c` their folds.

**Calls.** `c_builtin_library_calls.inc` lists the 393 `F` rows that compile as
calls of their functions: the math library's (`sin` to `tgammaf128`, the complex
functions, `powi`'s aside, and the `__fp16` forms below), the string and memory functions, the formatted
input and output (`printf`, `snprintf`, `vsscanf`, and the `FILE` builtins below), the
allocation functions, `abort`, the wide-character functions and
`__get_unsafe_stack_*`. The checker gives each call its row's ordinary
prototype (`c_builtin_intrinsic_types.c` decodes it) and checks it as a call
of that prototype, through `check_x86_action`'s ordinary path, as the
fortified builtins are: the arguments convert to the parameters, a variadic
one's take the default promotions, and its diagnostics are a call's. The
lowering calls X by name (`check_lower_call_to` with a runtime function of the
lowering, `CHECK_LOWER_RUNTIME_LIBRARY` and the list's index), through the
unit's own declaration's symbol where the unit declares X with linkage, as
clang's call shares the module's one function of that name, or through the
lowering's own runtime function of that name where it has one (`fmax`,
`memcpy`, `strcpy`, `sprintf`, ...: `check_lower_library_runtime`), so that a
unit has one symbol of each name. `__builtin_powi`, `powif` and `powil` are
`llvm.powi`'s, whose code calls libgcc's `__powidf2`, `__powisf2` and
`__powixf2` (x raised to an `int` by repeated squaring, not `pow`'s value), and
so do theirs.

**The `__fp16` forms** (`__builtin_sinf16` and 34 others, #148). Their rows'
prototypes take and return `__fp16`, which no function type may (clang's Sema
refuses such a declaration, and so does `type_function`), so
`c_builtin_intrinsic_types.c` decodes them with `_Float16` in its place
(`BuiltinIntrinsicTypeInput.half_prototypes`, for the rows
`cbuiltin_library_half` names): binary16's values pass alike, and pointers keep
their `__fp16` (`sincosf16`'s `__fp16 *`). `check_x86_action` then gives the
call's arguments `__fp16` conversions (the rounding is the same) and its value
`__fp16`'s type, which C promotes to `float` where it computes:
`__builtin_fmaxf16(x, y) + 1` is a `float`. clang -O0 calls 21 of them by their
own names, as this does: `sinf16`, `acosf16`, `sqrtf16`, `powf16`, `ldexpf16`,
`sincosf16` and their kin, which glibc doesn't define, so that a program links
them from elsewhere. It computes the other 14 in `float`, which the lowering
does too (`check_lower_library_promoted`): `ceilf16`, `floorf16`, `truncf16`,
`rintf16`, `roundevenf16`, `roundf16`, `fmaxf16`, `fminf16`,
`fmaximum_numf16`, `fminimum_numf16` and `frexpf16` call the `float` function
of their arguments converted to `float` (clang's baseline code calls the same;
its `roundss` and `maxss` at znver5 give the same values), `fmaf16` calls `fma`
of them converted to `double`, its result rounded once to `__fp16` as clang's
`__truncdfhf2` rounds it, and `fabsf16` and `copysignf16` are their 16 bits'
sign cleared or copied. `fmaxf16`, `fminf16` and their `_num` kin fold, as
clang's `E` rows do. `__builtin_canonicalizef16`, no `F` row, is the floating
builtins' (0x3da): its `__fp16` argument converted to `float` and back, which
quiets a signaling NaN as clang's code does.

**`__builtin_char_memchr`**, no `F` row either, is `memchr`'s call under its
own prototype (`char *(const char *, int, size_t)`), as clang's code calls it
(`cbuiltin_library_alias`): its arguments convert to that prototype and its
value is a `char *`.

**Where clang's code is no call.** clang -O0 compiles many of these rows to
LLVM intrinsics rather than calls: `llvm.maxnum` and `llvm.minnum` for `fmax`
and `fmin`, `llvm.maximumnum` and `llvm.minimumnum` for the `_num` ones,
`llvm.floor`, `ceil`, `trunc`, `round`, `roundeven`, `rint` and `nearbyint`
for the `long double` and `_Float128` roundings, `llvm.copysign` and
`llvm.fabs`, `llvm.fma`, `llvm.frexp`, `llvm.modf` and `llvm.abs`. On x86-64
most come back as calls of the same library functions; the rest (`fabs`,
`copysign`, `abs`, `maxnum` and `minnum` of `float` and `double`) are computed
inline, and so does this lowering compute `fmax`, `fmin`, `fmaximum_num` and
`fminimum_num` of `float` and `double` (#967), as `__builtin_` calls and as
plain calls of the unit's declarations that clang makes the builtin (the
reader's mark, `cbuiltin_library_plain_folds`' entries of no fold, which
`check_lower_call` takes, a musttail return's too, as clang's code computes
it inline and returns it; such a call references no function, so that a
program links without libm as clang's does; but a declaration with an asm
label, which clang's code calls by its label, `check_program_call_extreme`;
`-fno-builtin` and `-fno-builtin-NAME` keep the call, and
`test-checker-driver`'s library runs check all three; a definition's
`no_builtin` keeps it too, below): `__builtin_elementwise_maxnum`'s
rows and its kin's
(`check_vector_floating_scalar_row`), which glibc's functions would not match.
clang's code of `llvm.maxnum` and `llvm.minnum` gives its first operand of two
zeros and the other operand of a signaling NaN, where glibc's `fmax` and `fmin`
give the second zero and a quiet NaN; and its code of `llvm.maximumnum` and
`llvm.minimumnum` gives a signaling first operand unquieted where the other is a
NaN too, where glibc's give a quiet NaN (C allows both: F.10.9.2 leaves the
zero's sign open, and a signaling NaN's result). The rows are clang's
instructions at each target where a row holds them: AVX-512's masked moves
(`VCMPSD` into k1, `VMOVSD`), AVX's `VBLENDVPD` by `VCMPUNORDSD`, and SSE2's
`maxnum` select (`MAXSD`, `CMPUNORDSD`, `PAND`, `PANDN`, `POR`), x86-64-v2's
too (its `BLENDVPD` reads xmm0, which no row names). SSE2's `maximumnum` takes
more records than a row has, so it is two rows of the same bits
(`check_vector_extreme_scalar_rows`): `MAXSD` of x and y, its sign's mask all
ones where x is a NaN (so that `MAXSD`'s y passes), then x where y is a NaN.
`test-checker`'s programs compare them and the rest bit for bit, on the
harness's signaling and quiet NaNs, zeros of both signs, infinities and
subnormals among them, at x86-64, x86-64-v2, x86-64-v3 and x86-64-v4
(`library-extremes-of-special-values`), and `test-checker-driver`'s
`tier1-extremes` fixture runs them by `--tier1` at x86-64, x86-64-v3 and
x86-64-v4.

**Folds.** The rows with attribute `E` that clang's constant evaluator folds,
this folds too, in constant contexts (static initializers, `_Static_assert`,
array bounds, enumerators and case labels) and in code, where clang folds a
builtin call whose operands evaluate (its CodeGen's `EmitBuiltinExpr` tries
`EvaluateAsRValue` first): `strcmp`, `strncmp`, `memcmp`, `bcmp` and their wide
kin (`wcscmp`, `wcsncmp`, `wmemcmp`, which compare `wchar_t`s as `int`s) to
clang's -1, 0 or 1, not the library's differences; `strlen` and `wcslen`;
`abs`, `labs` and `llabs`; the `fmax`, `fmin`, `fmaximum_num` and
`fminimum_num` families, their `f128` forms among them (of a `__float128`'s bits, #1250); and
`copysignl` and `copysignf128`, and `fabsf128`. Strings are literals, at constant
offsets, and `const` arrays whose initializers are constant (clang's GNU
folding, for which its `-pedantic` warns; this doesn't). Pointer results
(`strchr`, `memchr`) and the other `_Float128` functions aren't folded, and calls with
side effects never are. `__has_constexpr_builtin` answers 1 for these.

**Plain names.** `c_builtin_library_plain.inc` lists the 446 `f` rows of C's
modes, `<stdarg.h>`'s `va_start`, `va_end` and `va_copy` among them, whose
prototypes take a `va_list` by reference (`A`, spelled as its decayed
`struct __va_list_tag *`): clang's code calls functions of those names, which
no C library has. Each is a builtin of the unit unless `-fno-builtin`,
`-ffreestanding` or `-fno-builtin-NAME` says otherwise, one of
`ALL_GNU_LANGUAGES` (`alloca`, `bzero`, `strdup`, `index`) in GNU's modes alone,
and one of `C23_LANG` (`<stdbit.h>`'s 70 functions) in C23 and later
(`pp_library_builtin`, from `PPOptions.no_builtin`, `freestanding` and
`no_builtin_functions`, which the driver's options set), which `__has_builtin`
answers by. In a function whose definition has clang's `no_builtin`
attribute (#1958), with no arguments or with a string naming the function
(`no_builtin("strlen")`), a call of the unit's declaration of one is that
declaration's call, as clang's code makes it (`EmitDirectCallee`'s
`no-builtin-NAME` and `no-builtins`): no fold, no inline `fmax` and no inline
`<stdbit.h>` function (`check_input_no_builtin`, which the reader asks where it
marks such a call); a string clang warns names no builtin (`"*"`) covers
nothing, and the `__builtin_` forms are builtins still. clang's constant
contexts still fold such a call (its evaluator doesn't read the attribute),
which this doesn't in the function's own static initializers. The list is
pinned to Clang 23.1.4's official build, CI's
reference: its GNU rows include `strlcat` and `strlcpy`, as every upstream 23.x
release's do (the `llvmorg-23.1.4` and `llvmorg-23.1.5` tags' `Builtins.td` and
`release/23.x` keep them). Debian's and Ubuntu's clang packages, apt.llvm.org's
included, comment both rows out with
`debian/patches/23-strlcpy_strlcat_warning_removed.diff`, whatever the release,
so their `__has_builtin` answers 0 and a call is an ordinary implicit
declaration. The oracles compare `clang --version` with the reference, print
`expected clang 23.1.4, found …` (`found Ubuntu 23.1.6` for such a package's
build) when another build is found, and skip only the plain names that build's
vendor is known to classify differently (`cbuiltin_library_table_differences`:
Debian's and Ubuntu's `strlcat` and `strlcpy`), a difference no named build
explains still failing.
Neither `strlcat` nor `strlcpy` is folded: a call
of either stays a call of the library's function in every mode. A declaration
of such a name is the library's function, which calls
reach as any function's; where it has external linkage and the row's
prototype, its calls fold as `__builtin_X`'s do (Sema's `BuiltinAttr`:
`check_input_library_call` marks them, `check_builtin_library_fold_plain` folds
them). In units with overload tables, each ordinary declaration retains this
permission in `CheckOverloadCandidate.library_call` after attribute inheritance
(`#1234`, `check_input_library_declaration`). `check_overload_call` folds the
selected candidate's call after resolving and converting its arguments, for
both the expression graph and source-time constant evaluation. A newer
overload sharing the name cannot grant or remove the selected declaration's
permission; an `overloadable` declaration never has it, even with the library
prototype. Linkage, prototype and `-fno-builtin`/freestanding policy use the
same checks as calls in units without overload tables. An undeclared one's
use declares it with its catalog row's prototype
(#927's `check_input_implicit_function`, `c_check_implicit_input.c`, clang's
`LazilyCreateBuiltin`), which the driver reports as clang does
(`check_driver_implicit_library`): from C99 on, an error, `call to undeclared
library function 'sin' with type 'double (double)'; ISO C99 and later do not
support implicit function declarations`, and before C99 a warning, `implicitly
declaring library function 'sin' with type 'double (double)'`, each with
clang's note `include the header <math.h> or explicitly provide a declaration
for 'sin'`, both under `-Wimplicit-function-declaration`, and a prototype with
`size_t` spelled as clang's (`'__size_t (const char *)' (aka 'unsigned long
(const char *)')`). The call then converts its arguments to the prototype's
parameters (`sin(1)` passes 1.0), in C89 as clang's does.

A prototype that names the unit's types takes them from its file-scope
typedefs at the call, as clang's `ASTContext` keeps them (`check_input_file_type`):
`FILE` (`P`), `jmp_buf` and `sigjmp_buf` (`J` and `SJ`, decayed as parameters)
and `ucontext_t` (`K`); `pid_t` (`p`) is clang's `int`. The diagnostics spell
them as clang does (`CheckInput.implicit_library_types`, `CBuiltinLibraryNamed`:
`'int (ucontext_t *)' (aka 'int (struct U *)')`, `'int (long *)'`). Without the
type, the use declares the function as any undeclared one, with clang's
warning that it needs its header, but for the rows clang declares without
checking their prototypes (attribute `T`: `setjmp` and its kin, `getcontext`,
`savectx`, `vfork`, Sema's `allowTypeMismatch`), which warn of nothing. `umask`
and `pthread_create` have no prototype: their uses are any undeclared
function's, as clang's are.

**C23's `<stdbit.h>` functions.** `stdc_leading_zeros_uc` to
`stdc_bit_ceil_ull`, the 14 operations of `__builtin_stdc_leading_zeros` to
`__builtin_stdc_bit_ceil` (clang's `BIstdc_*`) of a parameter of `unsigned
char`, `short`, `int`, `long` or `long long` each, are computed inline, as
clang's code computes them: a call of the unit's declaration of one, where
the name is a builtin of the unit (C23's, `pp_library_builtin`), the
declaration has external linkage and the row's prototype (`unsigned int`,
`bool` for `has_single_bit` or the parameter's type for `bit_floor` and
`bit_ceil`, of the parameter's type), and the call has one argument, is the
builtin (`check_input_stdc_library_call`, in the walk and in the reader's
constant evaluator for an enumerator's value). It has an identity of its own
(`CBUILTIN_STDC_LIBRARY_FIRST` + the operation × 5 + the type), which the bit
builtins check as a fixed prototype's (`check_builtin_bits_parameter`: the
argument converted as by assignment, a pointer's or a record's refused) and
fold, and lower, as their generic builtin's of the parameter's type. So
`stdc_leading_zeros_ui(1)` is 31 in an `_Static_assert`, and a call computes it
even where the unit defines the function itself, whose address the name still
is. A call of another count is the declaration's, with its count's error; a
declaration of another prototype or of internal linkage, and every other mode,
make the call the function's. `__has_constexpr_builtin` answers 1 for them in
C23. Their implicit declarations' diagnostics spell `unsigned short` and C23's
`bool`.

**Refused.** The classification builtins of custom checking
(`__builtin_isnormal` and its kin) and `__builtin_signbitf` and `signbitl`,
which clang tests inline, are the floating builtins' ("Floating checking"),
not calls. The alloca variants are the catalogue's miscellaneous builtins
("Generic builtins of clang's own checks and of an instruction or two"). The
table's other `F` rows stay refused, and `__has_builtin` answers 0 for them.
The signaling NaNs and the `_Float128` and
`_Float16` NaNs, which clang folds, are the floating builtins' constants
("Signaling NaNs and the `_Float16` and `__float128` constants"). The prototypes of the rest don't decode: the matrix builtins.

**The `FILE` builtins.** `__builtin_fprintf`, `__builtin_fscanf`,
`__builtin_vfprintf`, `__builtin_vfscanf` and the fortified
`__builtin___fprintf_chk` and `__builtin___vfprintf_chk` are calls of their
functions too, whose prototypes take the unit's `FILE` (clang's prototype code
`P`): the type of the last typedef named `FILE` at file scope before the call,
as clang's `ASTContext` keeps it (`setFILEDecl`), whatever its type
(`check_input_file_type`, `BuiltinIntrinsicTypeInput.file_type`). A local
object named `FILE` doesn't hide it, and a block-scope typedef of the name is
none; a unit without one refuses the call, as clang does ("use of unknown
builtin"). A deferred body's lane leaves a call of one to source order, as its
bindings hold only the names the body spells. Their plain names (`fopen`,
`fprintf`, `fread`, `fscanf`, `fwrite`, `vfprintf` and `vfscanf`) are plain
library builtins too, of the implicit declarations' rules: undeclared, a call
declares the function with its row's prototype where the unit has a `FILE`
(C99's error, C89's warning), and warns that it needs `<stdio.h>` where it
hasn't, as clang does. `test-checker-driver` compares the errors of calls
without a `FILE` with clang's (`c_check_generic_builtin_driver_test.c`). A
library builtin named outside a call before any call declares it
(`int *q = malloc;`, #2348) is declared as clang's lookup declares it, as
its name is resolved: its error and note come before the error of the
expression around it, which completes at the same place
(`c_check_implicit_value_driver_test.c`).

`test-checker` (`c_check_program_library_test.c`, alone with
`--program-library`) checks the lists against clang's table and accounts for
every `F` and `f` row of C's modes; checks the folds' constant contexts by both
paths against clang's acceptance; and runs programs of each family (`double`,
`float`, `long double`, `_Float128` and `__fp16` math, `powi`, complex, strings, wide
characters, formatted output and input, allocation, the folds in code, special
values and `errno`) by both paths against clang -O0's builds.
`test-checker-driver` (`c_check_library_driver_test.c`) compares the implicit
declarations' diagnostics of every plain name with clang's, line for line, in
twelve configurations of language modes and the options above; runs programs
of C89's implicit library declarations and of `strcmp`'s fold under each
option against clang's builds; and checks each refusal by name at `-c`.
`test-preprocessor --clang` compares `__has_builtin` and
`__has_constexpr_builtin` with clang's (the preprocessor guide's "feature
queries").

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
./build/compiler test-check-builtins-bits
./build/compiler test-check-builtins-rounds
./build/compiler test-builtin-rounds-helper
./build/compiler test-check-builtins-math
./build/compiler test-builtin-math-helpers
./build/compiler test-checker --program-library
./build/compiler test-checker-driver --executable ./build/compiler
./build/compiler test-preprocessor --clang
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
