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

## Execution contract

Both fronts use `InterpreterValue` with low/high 64-bit words, explicit frame
stacks, a caller-owned scratch arena, symbol bindings and resource limits.
Program recursion creates interpreter frames without recursive C calls. The IR
front handles integer widths through 128 bits and host x87 extended precision;
the current typed input only expresses its existing integer widths and f32/f64.
Phi operands are snapshotted before any phi destination is written.
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
wide scalar arguments/results. Native function addresses are explicit bindings.
Calls accept at most 256 scalar arguments. Variadic operands must already have
their default promotions. Because IR integers have no signedness, native i8/i16
arguments require explicit extension metadata in `native_extensions`; the typed
front supplies it from machine classes. Ambiguous narrow native calls fail with
`INTERPRETER_NATIVE`. The text command can express a promoted i32 signature.
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
Later typed reads retain undefined-byte diagnostics; `memset` defines its written
bytes. This is the oracle's checking policy, with the language-level questions
about indeterminate values and padding discussed separately in
[WG14 DR451](https://open-std.org/jtc1/sc22/wg14/www/docs/dr_451.htm).

Inline assembly, threads and signals are outside this oracle's scope. Program
callbacks from libc into interpreted functions and setjmp/longjmp are not an
implemented host boundary.

## Acceptance boundaries

The default source driver remains syntax-only. The checker currently publishes
checked expressions and supported static data, and its owner is implementing
the executable source-body/control-flow handoff in #133. No frontend work is
replaced by generated streams or handwritten IR here.

Whole-source-corpus structural roundtrips, c-testsuite execution from checked C,
and real Csmith/YARPGen interpreter campaigns require that producer. The callable
oracles and text command are available for Tier 0, IR passes and the configurable
#142 harness as those source inputs become available. These dependencies remain
acceptance work; passing synthetic execution tests does not establish them.

No interpreter speed claim is made: #176 deliberately specifies a scalar oracle
off the compiler path. Parser throughput also requires separate target-host
measurements before any performance conclusion.
