# Text IR and execution oracles

`ir_parse.c` reads the stable `ir_print` format of [the shared IR](ir.md).
`interpreter.c` executes the existing `Tier0Function` typed stack stream;
`ir_interpreter.c` executes shared IR. These are the consumers for
[#175](https://github.com/buster14a/buster/issues/175) and
[#176](https://github.com/buster14a/buster/issues/176).

The implementation, fixtures and tests are original project material under
Apache-2.0 WITH LLVM-exception. The host C library is called at run time; no
external implementation or test corpus is incorporated.

## Commands

```sh
./build/compiler test-ir-parse
./build/compiler test-interpreter
./build/compiler test-interpreter-native
./build/compiler test-interpreter-link
./build/compiler test-ir-interpreter
./build/compiler interpret-ir --print tests/ir/arithmetic.ir
./build/compiler interpret-ir --result tests/ir/arithmetic.ir 5 7
./build/compiler interpret-ir --bind 100=printf tests/ir/hello.ir
```

Every build and test runs on the approved 9700X through the shared per-run lock.
The new test commands are also in the existing protected workflow.

`interpreter.c` runs Tier 0's masks (#179, [Tier 0's guide](tier0.md)'s "Masks"):
conversions to and from integers, `LOAD`, `STORE` and `SET` of a mask's bytes,
`AND`, `OR`, `XOR` and `NOT` of one width, and every class's `DUP`, `DISCARD` and
`JUMP_VALUE`, each mask's bits in its value's low word (zeros past its width) and
its bytes in the high word; a width other than an entry's is `INTERPRETER_INVALID`.
Like the other classes past `F80`, a mask's frame object and machine operations
(`X86` rows) aren't modelled. `test-tier0`'s masks check runs its random programs
without rows or objects through it against their native code.

Dynamic-stack text uses `alloca.ptr %size`, `stack_save.i64` and
`stack_restore.void %position` with no extra metadata. Both interpreter fronts
execute the [shared dynamic-stack contract](ir.md#dynamic-stack-contract).
`test-ir-lowering` includes their frame/lifetime, overflow/resource, transformation
and native-refusal regressions; `test-ir-parse` includes a canonical stack fixture.
The execution oracle bounds storage by its supplied arena and live memory limit.
Restore handle values that do not match a retained capture in the current
invocation fail with `INTERPRETER_INVALID`. Numeric equality cannot establish
provenance: a fabricated integer with identical bits passes this check, so
producers must preserve captured-handle provenance. Undefined or released storage
uses the existing undefined/memory diagnostics. The verifier checks static
structure and types; execution checks live-capture membership and memory access.

`interpret-ir` reads a sequence of functions from a regular file. Their symbols
are their one-based order in that file; `--entry ID` selects one, defaulting to
1. Following arguments are integer spellings of the parameter bit patterns,
with decimal, hexadecimal and negative spellings accepted. The function's
parameter types determine their interpretation. `--result` prints the exact
two-word result as hexadecimal and returns success. Otherwise the low eight
bits of the result are the process exit status, leaving stdout for the program.
`--print` parses and prints the canonical representation without execution.

`--bind ID=HOST_SYMBOL` explicitly resolves a native function in the host process,
for example `--bind 100=printf`. Native IDs must differ from the file's function
IDs. `malloc`, `calloc`, `realloc` and `free` bindings carry allocation bookkeeping.
These commands run trusted test programs in the current process, like other
native execution tests; memory diagnostics are an oracle aid, not isolation.

## Text grammar

The grammar follows the printer, with a complete instruction on each line:

```text
function (i64, ptr) -> i64 {
  slot $0 size=8 align=8 type=i64 flags=0x00
b0:
  %0 = param.i64 0
  %1 = const.i64 0x00000000000000000000000000000001
  %2 = add.i64 %0, %1
  %3 = ret.void %2
}
```

Function parameters and results use the IR type names `void`, `i1`, `i8`,
`i16`, `i32`, `i64`, `i128`, `f32`, `f64`, `f80` and `ptr`. Value IDs are
`%N`, block IDs `bN`, stack slots `$N`, and symbols `@N`. Instruction, block
and slot definitions are numbered consecutively from zero. Forward value uses
on phi backedges are permitted by the verifier's phi rules. Constants are
unsigned hexadecimal bit payloads of at most 128 bits; printing always uses
32 hexadecimal digits. Empty lines and semicolon comments are accepted.

Each instruction begins `%ID = OP.TYPE`. Unary and binary operands are value
IDs, separated by commas. Compare instructions add a predicate (`eq`, `ne`,
`lt`, `le`, `gt`, `ge`, `ult`, `ule`, `ugt`, `uge` as applicable).
`param` takes its parameter index; `stack_addr` a slot; `symbol_addr` a symbol.
`address` takes `%base + %index * SCALE offset=DISPLACEMENT`, with the index
term optional. Loads and stores carry `align=N` and optional `volatile`.
Calls take `@SYMBOL(%ARGS) fixed=(TYPES) variadic=0|1`; indirect calls use
`%TARGET` instead. A phi lists `[%VALUE, bPREDECESSOR]` pairs. Branches name
blocks, conditional branches take `%CONDITION, bTRUE, bFALSE`, and switches
take `%VALUE, default bN, [HEX_CONSTANT, bTARGET], ...`. `ret` optionally
names a value; `undef` and `unreachable` have no operands.

The printer is authoritative for opcode-specific spellings. Parser syntax
diagnostics retain a byte offset and one-based line/column. Structural errors
carry the shared verifier's diagnostic and source instruction location.

## Parser contract

`ir_parse(source, output, scratch, result)` consumes one function and returns
`IrParseResult`; `ir_parse_scalar` is its exact scalar twin. `consumed` lets a
caller advance through a sequence. Whitespace-only input returns `IR_PARSE_END`.
The vector path classifies bounded 64-byte newline chunks with masked tails;
the ordered grammar uses an explicit state and no recursion. Geometric scratch
builders retain parsed rows and metadata during the single text pass. Final
publication uses the same exact-size `ir_create` constructor as lowering.

Source, output and scratch must be disjoint; each points to its declared logical
extent and requires no padding. Failure preserves the output view and arena
length. Scratch is restored on every return. The constructed function is checked
with the shared verifier before publication. `ir_validate.c` then builds CFG and
tree-only intervals through the shared dominator analysis. It reuses the core
verifier's scalar/vector use kernel to check reachable SSA uses, including phi
uses at predecessor ends. A live use of a dead definition is rejected. Dead use
blocks retain shape, type, phi-coverage and same-block ordering checks without
an execution-relevant cross-block dominance requirement. This preserves the
lowering producer's unreachable diagnostic regions without inventing intervals.
Two aligned, disjoint views over unused scratch hold derived columns and
temporary work; the original arena prefix/length is preserved. Matrix work is
bounded by available scratch and 16 MiB, with the existing scalar fallback.

Printing a canonical fixture after parsing reproduces its text. Structural
roundtrips preserve instruction/block/slot order, operand meanings, constant
bits, call signatures and observable attributes. The printer does not serialize
unused sparse entries, sparse-table sharing, the translation unit's symbol
capacity, or an address scale when no index exists. Parsing normalizes those
details by occurrence and infers the smallest exclusive symbol bound; physical
side-table identity and arena addresses are not text-format properties.

## Pass fixtures in the text form

`tests/ir/optimize/*.ir` are [#175](https://github.com/buster14a/buster/issues/175)'s
first [#139](https://github.com/buster14a/buster/issues/139) tests written in
this text form: a hand-written `<name>-before.ir` and `<name>-after.ir` pair per
pass, one function a file, both written as the printer writes them.
`test-ir-optimize` runs them after its constructed fixtures; no other command or
CI step is needed.

For each pair, the test reads both files, requires that they differ (a pair that
tests no transformation fails), and requires each file to be exactly one
function that both parser paths accept, consume the same bytes of and agree on
column for column (`ir_parse` and `ir_parse_scalar`), and whose canonical print
is the file's bytes. It then runs the pass on the parsed before
function by both optimizer twins, requires the vector twin's non-zero
transformation counter, prints the vector result and requires its bytes to equal
the after file's, parses that text again and requires it to be the after file's
function, prints the reparsed function and requires the after bytes again, and
runs both the before function and the transformed ones through
`ir_optimize_test.c`'s independent interpreter on eighteen inputs, which must
give the same returns, statuses and stores. The scalar twin's result must be
structurally the vector result's, except for value numbering: its scalar twin is
the available-expression oracle, which may find fewer congruences than the
partition refinement and may never find more (the same rule as `test-ir-gvn`).

The pairs, and what each records:

- `sccp-constant-branch` (SCCP alone): a constant fold through an integer
  addition and comparison retargets the conditional branch to its single
  executable successor, and the arm the constant condition excludes is
  unreachable and goes. DCE is not in this pass set, so live-but-unused
  instructions, including the other constant, stay.
- `dce-roots` (DCE alone): a dead multiply chain goes; the store, the load and
  the copy that the return reads stay. Loads remain roots: there is no
  dereferenceability proof.
- `cfg-threading` (CFG cleanup alone): a chain of empty jump blocks is threaded
  to its target and its blocks compact, leaving the entry block, the block with
  the computation and the return block.
- `combined-switch` (the driver's `--tier1` set, SCCP with DCE and CFG): a
  switch on a constant operand collapses to the taken edge, the case it selects
  branches straight to, and the default and other case blocks go with their
  computations and constants.
- `gvn-congruence` (value numbering): two additions that repeat a dominating
  expression, one in each arm of a diamond, become that expression's value, and
  their operands renumber with them.
- `licm-hoist` (loop-invariant motion): a loop with an existing preheader, a
  header, a latch and one exit; the header's two invariant computations move
  into the preheader before its branch, in dependency order, and every value
  id renumbers by its position.
- `sroa-private-fields` (private-field replacement): a 16-byte slot whose only
  uses are an address, constant-offset addresses, stores and loads splits into
  two 8-byte slots at offsets 0 and 8, the original slot compacts away, and
  each address becomes a stack address of its field's slot. This pass's stores
  legitimately move to another slot's address, so this pair compares values and
  statuses but not the executor's address-keyed store trace.

The text form carries no unused sparse rows: the passes keep the original
constant, slot, address and call-type indices (only new folded constants append,
and CFG cleanup compacts blocks), while a parsed file's tables hold exactly what
its text names. So the canonical bytes, not a whole-struct comparison, are the
structural contract with an after file; the reparsed text is compared with the
parsed after file as well, which checks the same structure through two parses of
the same bytes.

`./build/compiler test-ir-optimize --print-text` prints each before file's
vector result instead of running the checks, for maintaining the pairs: a review
of the printed text is what turns it into an after file; the command writes no
file and runs no transformation the normal test would not.

## Execution contract

Both fronts use `InterpreterValue` with low/high 64-bit words, explicit frame
stacks, a caller-owned scratch arena, symbol bindings and resource limits.
Program recursion creates interpreter frames without recursive C calls. The IR
front handles integer widths through 128 bits and host x87 extended precision;
the current typed input only expresses its existing integer widths and f32/f64.
Phi operands are snapshotted before any phi destination is written. The IR
front verifies each function once per execution, at its first frame, since its
IR can't change while it runs (#134: a recursion's every call verified its
callee twice, 45% of a c-testsuite program's time).
Callable program mappings share one symbol namespace: IDs zero and UINT32_MAX
are reserved, and function, data and native IDs must be distinct. Different IDs
may intentionally refer to the same address.

`InterpreterOptions` bounds steps, frames, values, locals and registered memory.
Optional checks diagnose reads of undefined values/bytes and accesses outside
known writable or readable allocations. Results identify the status, function,
instruction and executed step count. Scratch allocations are reclaimed when
execution ends. Caller-provided global memory retains the program's writes.

`interpreter_native.c` marshals C scalar values into the host SysV x86-64
integer, SSE and stack argument locations, including mixed variadic calls and
wide scalar arguments/results, and with a call's ABI records its aggregates too
(below). Native function addresses are explicit bindings.
Calls accept at most 256 arguments. Variadic operands must already have
their default promotions. Because IR integers have no signedness, native i8/i16
arguments require explicit extension metadata in `native_extensions`; the typed
front supplies it from machine classes. Ambiguous narrow native calls fail with
`INTERPRETER_NATIVE`. The text command can express a promoted i32 signature.
The IR lowered from a typed stream has the stream's pointers as `i64`s: the IR
front takes a flagged native's pointer operands and result (the allocators',
the copies' and the fill's) as pointers, as the typed front takes its `U64`s.
Nor does that IR keep which of a variadic call's arguments are named (its
`IrCall` takes them all as named): such a call's named arguments are its
interpreted callee's parameters, its first, and the rest are unnamed, which a
variadic definition's frame reads, and a callee without one ignores (a variadic
function that never calls `va_start`, or a function C calls without a
prototype, which passes its arguments as a variadic call does).
`interpreter_link.c` materializes Link data atoms and symbol addresses, applies
supported data fixups, resolves global names across units and preserves local
symbol identity. Unsupported data forms are reported rather than guessed.

Heap definedness uses a byte map per allocation. Native free/realloc maintain it;
execution teardown releases those maps while preserving any unfreed program
allocation, whose ownership remains with the caller. Memory bindings remain
caller-owned. The generated-program comparison disables undefined-byte checks
to match that generator's established zero-initialization convention; the
handwritten undefined-memory cases use the enabled checks.
Flagged `memcpy`/`memmove` calls transport byte-definedness alongside the copied
representation, including overlapping moves, without consuming a scalar value.
The typed stream's `COPY` is the same representation transfer without a scalar
value: it carries the source's definedness, or defines the destination when the
source has no tracked map, accepts identical or disjoint ranges only, refuses
partial overlap, and leaves both bytes and metadata unchanged when either range
fails its bound or permission check. Later typed reads retain undefined-byte
diagnostics; `memset` defines its written bytes. This is the oracle's checking policy, with the language-level questions
about indeterminate values and padding discussed separately in
[WG14 DR451](https://open-std.org/jtc1/sc22/wg14/www/docs/dr_451.htm).

### Aggregates by value and variadic definitions

A call with ABI records (#131,
[#788](https://github.com/buster14a/buster/issues/788)) runs as the IR's records say.
Between interpreted functions, each named aggregate argument's bytes, definedness and
all, are copied into an object the callee's frame owns, aligned to 64 or, where it
says more, as the call's argument area is (its records' `stack_alignment_log2`, or
the callee's incoming frame's), so that an aggregate aligned past 64 on the stack
keeps its alignment; its address is the callee's parameter. An `IGNORE`d one, an
empty record's 1-byte object, gets a fresh undefined object whatever the call passes.
A callee that returns through a hidden pointer takes the call's destination as
parameter 0; one that returns in registers returns its bytes' address, from which
`ret` copies at most the call's room to the destination while the callee's memory
still holds them. A call that doesn't use such a result has no room for it and drops
it. The call's and the callee's records must agree (each named aggregate's size, the
result's kind), an entry can't have records of its own, since no call places them,
and a call's argument area can't be aligned past 2^30, Tier 0's and Tier 1's bound:
each is `INTERPRETER_INVALID`.

A variadic definition (#148) is entered from a call, which lays out its anchors:
the register save area holds the argument registers as the psABI saves them (rdi to
r9 at 0 to 40, xmm0 to xmm7's low eightbytes from 48), and the overflow area is the
call's argument area past the named arguments' bytes (its anchor at
`incoming.stack_bytes`, the area aligned as rsp is at the call: to 2^`stack_alignment_log2`
of the call's records, at least 16, so that `va_arg`'s rounding of an argument
aligned past 16 finds its start). Each argument's
place is the call's records' or, without records, LLVM's assignment of a scalar: the
next register of its kind (two general ones for an `i128`), or else an 8-byte stack
slot (16 bytes aligned to 16 for an `i128` and a long double). Only the bytes the
arguments write are defined. The named arguments' registers must be the
definition's `incoming` counts, which `va_start`'s offsets take, and the unnamed
ones must have their default promotions; a variadic entry, which no call lays out,
is refused.

A native called with records gets each argument where its location says (its
registers, its stack slot, a `SPLIT`'s two, nothing for `IGNORE`), an aggregate's
eightbytes from its bytes, a hidden pointer in rdi and `al` the records' vector
count. A `STACK` location flagged `HIGH` or `LOW` holds one eightbyte alone in its
8-byte slot, the aggregate's high or low one (the convention's eightbyte at a time,
#765), with that eightbyte's definedness; a variadic definition's overflow area
takes it so too. An aggregate result comes from rax, rdx, xmm0's and xmm1's low
eightbytes and st0's ten bytes, or through the hidden pointer, and its
destination's bytes are then defined. What the bridge can't place is
`INTERPRETER_NATIVE`, before the call: a register no argument goes in, a part past
its location's registers (a vector register's upper half, as an `SSEUP` part takes),
a stack part both `HIGH` and `LOW` or with no bytes, an argument area aligned past 16
or larger than 4096 bytes, and a result in st1 or in parts of other sizes.

`test-ir-interpreter` checks each against values worked out in C: calls with
records between interpreted functions (a struct in registers whose callee writes its
copy, one through a hidden pointer, an empty record's), their refusals, and a result
room smaller than its result; a variadic definition reading back eighteen unnamed
scalars from its anchors, and another two unnamed structs placed by records; an
unnamed struct aligned to 32, read as `va_arg` reads it, and a named one aligned to
128 whose callee's copy keeps that alignment, each from frames at every 16 bytes of
128 in the arena, and calls aligned past 2^30; a 16-byte object's high or low
eightbyte alone in a stack slot before a following long, taken by a native of plain
longs and by a variadic definition's overflow area, with its definedness, and both
flags, refused; and six
natives clang compiled, which take and return structs in general and vector
registers, on the stack, through a hidden pointer and in st0, against calls of them
from C, with what the bridge refuses. `test-ir-lowering` runs Tier 0's generated
programs with records (without floating values) and its variadic callees through the
interpreter against their native code ([the IR guide](ir.md)). Copying an aggregate
for its callee, the result's bytes, the hidden pointer, the overflow anchor's
offset, the named registers' check, an `IGNORE`d parameter's fresh object, a native's
result parts and the vector registers' count were each checked by mutation.

Inline assembly, threads and signals are outside this oracle's scope. Program
callbacks from libc into interpreted functions and setjmp/longjmp are not an
implemented host boundary.

## Metamorphic IR testing

`./build/compiler test-ir-metamorphic` runs the versioned, deterministic IR
campaign for [#731](https://github.com/buster14a/buster/issues/731). It consumes
the existing public IR, parser, verifier, interpreter and optimizer interfaces;
it does not replace their implementations or the C source fuzzing campaign.

```sh
./build/compiler test-ir-metamorphic --seed 731 --count 6
./build/compiler test-ir-metamorphic --seed 17 --first 24 --count 1000 \
  --max-steps 100000 --max-ms 600000 --artifacts /tmp/ir-failures \
  --revision EXACT_SOURCE_REVISION
./build/compiler test-ir-metamorphic --replay /tmp/ir-failures/CASE_DIRECTORY
```

The initial grammar subset has one `i64` parameter and result, integer bitvector
operations, diamonds and phis, bounded single/nested loops, switches, and ordinary
or volatile stack/global loads and stores. Each case has an independently stated
unsigned-C model of its result and final global bytes. Input cohorts include zero,
both branch directions, every switch arm, the maximum unsigned value and the sign
bit. Loops run at most seven outer and five inner iterations. Calls, floating
point, dynamic stack operations and native Tier1 execution are not admitted by
this initial generator. They are not counted as successful unsupported cases.

The representation variants are an identity control, instruction/block ID
renaming, independent constant reordering, unused pure-value insertion, CFG edge
splitting with phi predecessor repair, commutative operand exchange, and an
unreachable block. Rebuilding validates the original and replacement. Its
bounded SIMD ID-remap kernel has a scalar twin; canonical texts must match
exactly. Both complete verifiers check reachable dominance. Both text-parser
paths must stabilize print/parse/print. For each variant, SCCP, DCE, CFG cleanup,
their combination, GVN, LICM and scalar replacement run through their scalar and
vector paths. Optimized arrays may differ; checked execution must preserve the
independent result and guarded global-memory observations.

The default six cases cover every shape and are in the shared correctness
inventory. Larger runs have explicit case, step, time, instruction and arena
bounds: at most 100,000 cases, 128 instructions and 16 blocks per function, eight
8MiB arenas, four interpreter frames and bounded memory registrations. Time or
execution limits, unsupported consumers, undefined values, resource exhaustion and semantic
differences have distinct categories. Invalid opcode/type/value/phi/terminator
and grammar controls must be rejected with matching complete diagnostics; no
invalid IR is interpreted. Allocation failures must preserve unpublished views
and arena prefixes.

`--max-ms` is checked between bounded consumer calls, rather than interrupting
an individual public API call. Interpretation has the separate `--max-steps`
limit; the enclosing CI command also has its process timeout. Resource limits
from rebuilding, printing, either parser, verification and optimization stay
resource outcomes. Constrained-arena controls check those boundaries, and an
independent control requires rejection when a correct return value accompanies
different final global memory.

Artifact capture requires `--revision` (or `GITHUB_SHA`) to identify the source
used to build the tool. The caller is responsible for that association; it is
not inferred from a possibly newer working checkout. The record also stores
the host compiler, executable size/FNV fingerprint, generator version, seed,
case, transformation, optimizer path, input, limits, exact failure category and
observations. FNV is an identifier aid, not a correctness or collision oracle.
Fresh case directories contain `original.ir`, `variant.ir` (the optimizer
input), and `transformed.ir` (the consumer output) when those views are valid,
plus `run.txt` and its one-line replay command.
If construction failed before a valid view could be published, the artifact
states that boundary and retains the original generated text and regeneration
recipe. Existing directories/files are never overwritten. Capture failures are
reported separately from the original failure.

Replay regenerates the recorded version/seed/case under the same limits and
requires the same failure category, transformation, optimizer path, input and
complete retained canonical bytes. A trailing-byte difference fails replay too.
A different error, timeout or unsupported result is not the recorded failure.
The explicit `--inject semantics|verify|step-limit` option tests this protocol
using an intentional control in the first unoptimized candidate; manifests and
summaries identify the injection, and replay preserves it. These controls are
not enabled in ordinary campaigns. The runner's own temporary-file contracts
check capture, exact replay, overwrite refusal, malformed metadata and cleanup.
Exit0 means the campaign passed (or the recorded failure no longer reproduces),
exit1 is a classified correctness failure, and exit2 is a usage, budget,
unsupported, resource, changed-replay or artifact failure. Deterministic summary
fingerprints retain canonical generated/variant text; wall time and addresses
do not enter them.

## Acceptance boundaries

The default source driver remains syntax-only. The checker currently publishes
checked expressions and supported static data, and its owner is implementing
the executable source-body/control-flow handoff in #133. No frontend work is
replaced by generated streams or handwritten IR here.

c-testsuite's programs run from checked C through the IR front: #134's
`test-ssa-source` runs each program of the declared subset in three IR forms,
every local in memory and each promotion path's SSA, against its expected
output ([the SSA guide](ssa.md)'s "Real and generated C"). Running them from
their typed streams (the typed front), whole-source-corpus structural roundtrips
and real Csmith/YARPGen interpreter campaigns remain acceptance work. The
callable oracles and text command are available for Tier 0, IR passes and the
configurable #142 harness; passing synthetic execution tests does not establish
them.

No interpreter speed claim is made: #176 deliberately specifies a scalar oracle
off the compiler path. Parser throughput also requires separate target-host
measurements before any performance conclusion.
