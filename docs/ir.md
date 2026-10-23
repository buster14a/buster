# Per-function IR

`ir.c` implements the representation, verifier, compact derived arrays and stable
printer of [#131](https://github.com/buster14a/buster/issues/131). `ir_lower.c`
adapts the existing `Tier0Function` typed stream to an all-in-memory form.
The implementation is original project code; no external implementation or
test corpus is imported.

## Status and consumers

The current producers are `ir_lower_typed` / `ir_lower_typed_scalar`, the
handwritten IR fixtures and the synthetic benchmark. Consumers are the two
verifiers, CFG and use-list builders, printer, and the lowering tests' independent
executor. The default `compiler file.c` driver remains the parser's driver.
The merged `check` command produces checked expression graphs and supported
scalar static data, but it does not yet produce executable per-function streams.
This IR currently consumes the generated or explicitly constructed
`Tier0Function` contract.

The remaining acceptance of #131 is explicit:

- Complete the executable source-function handoff from #129/#133 to this IR.
  The merged checker retains checked expression graphs, explicit conversions,
  declaration/object identities and function prototypes. Per-function body
  membership, ordered parameters and locals, executable control-flow targets,
  and backend call descriptors still require integration. Lowering every corpus
  function, including SQLite, also needs the required frontend language coverage.
- #134 and #138 must use the verifier/printer after SSA construction and Tier 1
  passes. They can consume the columns below without an instruction-list adapter.
- #174 must provide dominator-tree intervals for complete cross-block SSA
  verification. Same-block ordering is checked today, and the optional interval
  interface is exercised by independently specified graph fixtures.
- SQLite's **IR bytes per source token**, including `sqlite3VdbeExec`, still
  needs that complete frontend path and an approved 9700X run. Synthetic bytes
  per IR instruction or per typed entry are different measurements.
- The stable printer exists; parsing its output remains #175.

## Lifetime and stage contract

`ir_create(Arena *, const IrSizes *, IrFunction *)` allocates exact-sized columns
in one caller-owned function arena. `IrSizes` accepts 64-bit counts so limits are
diagnosed before narrowing. Each logical count must fit u32. An instruction ID is
its zero-based index; `UINT32_MAX` is reserved for a missing operand and is never
a valid instruction ID. Overflow returns `IR_ERROR_LIMIT`, allocation failure
`IR_ERROR_MEMORY`. Failure preserves both the result view and the arena length.

The producer writes disjoint columns directly and publishes a complete function.
The byte columns have stride 1 and the operand columns stride 4; allocation
aligns their bases to 64 bytes. There is **no readable or writable tail padding**.
Logical length, allocation capacity and the kernel's 16-lane width are distinct.
The vector verifier uses masked final loads and bounded indexed byte reads;
callers may also supply unaligned, exact-length views. Empty functions are valid
only with zero blocks and a void result. Empty side arrays may have null bases.
Raw pointers must actually cover their declared counts; no verifier can establish
the mapping ownership of an arbitrary caller-supplied pointer.

The producer and consumers preserve instruction order, block IDs and side-effect
order. The IR has no hidden allocation or automatic mutation of use lists. A
rewrite that changes instruction numbering or block membership builds fresh
columns and explicitly rebuilds the affected derived arrays. Allocations and
views belong to the caller; releasing the function arena releases its IR.

Verifiers restore scratch on success and failure. Derived-array builders and
lowering require disjoint input, output and scratch storage, and commit their
output only on success. A consumer can keep CFG or use arrays in a separate
shorter-lived arena and discard them immediately after its pass. No full-vector
buffer must be retained across a semantic dependency: complete functions and
contiguous block ranges are the logical batches, with every partial 16-row batch
processed immediately.

## Instruction columns

| Column | Width | Meaning |
| --- | ---: | --- |
| `op` | u8 | `IrOp`; below 64, for one-register byte tables |
| `type` | u8 | Result type; `void` instructions cannot be value operands |
| `flags` | u8 | Currently `BUSTER_IR_VOLATILE` on loads/stores |
| `alignment_log2` | u8 | Load/store alignment as a power of two, 0 through 31 |
| `a`, `b`, `c` | 3 × u32 | Value IDs or opcode-specific indices |

These columns use **16 bytes per instruction**, before block offsets and sparse
tables. `arena_bytes` reports the entire allocation charged to the arena, including
alignment gaps. It is not just the sum of payload widths.

Types are `void`, `i1`, `i8`, `i16`, `i32`, `i64`, `i128`, `f32`, `f64`, `f80`
and `ptr`. Signedness is in operations such as `sdiv` / `udiv` and `sext` /
`zext`, rather than duplicated in integer type IDs. Aggregates remain in memory.
Integer arithmetic wraps at the result width. Division and shift validity are
the language/lowering pass's responsibility; the structural verifier does not
prove dynamic divisors or shift counts. Floating predicates follow C: ordered
comparisons, except `ne`, which also succeeds for unordered operands.

All unused operands are `BUSTER_IR_NONE`; unused flags/alignment are zero.
This canonicalization catches accidental metadata reuse and makes printing and
column comparisons deterministic.

| Operations | Operand contract |
| --- | --- |
| `const` | `a` indexes a two-u64 bit-pattern constant; `b,c` absent |
| `undef` | No operands; an unspecified value of its result type |
| `param` | `a` indexes `parameter_types` |
| Unary / conversion | `a` is the source value |
| Binary arithmetic | `a,b` are values; result/operand types agree |
| Shifts | `a` matches the result; `b` has an integer type |
| `icmp`, `fcmp` | `a,b` are equally typed; `c` is `IrPredicate`; result is `i1` |
| `select` | `a:i1`, `b,c` match the result type |
| `stack_addr` | `a` indexes `slots`; result is `ptr` |
| `symbol_addr` | `a` is a link `SymbolId`; result is `ptr` |
| `address` | `a:ptr`, optional `b:i64`, `c` indexes `addresses` |
| `load` | `a:ptr`; result is the loaded type |
| `store` | `a:ptr`, `b` is the stored value; result is `void` |
| `call`, `call_indirect` | `a` is a symbol or a pointer value; `b` indexes `calls` |
| `phi` | `a` is an extra-word offset, `b` the incoming count; pairs are `(predecessor, value)` |
| `br` | `a` is a block ID |
| `cbr` | `a:i1`, `b` true block, `c` false block |
| `switch` | `a` integer value, `b` extra-word offset, `c` case count; words are `default_block, constant_id, target, ...` |
| `ret` | Optional value in `a`, matching the function result; the instruction itself is `void` |
| `unreachable` | No operands; a terminator |

Constants preserve exact low/high bits, including NaNs, negative zero and x87
payloads. Unused bits above the type's width must be zero. The printer uses fixed
hexadecimal payloads, independent of locale or decimal rounding.

`IrSlot` records size, power-of-two alignment, optional scalar type, and flags for
address-taken, volatile or temporary storage. A `void` slot is untyped memory;
scalar slots must be large enough for their type. `IrAddress` holds a signed
64-bit displacement and scale 1, 2, 4 or 8. Other scales lower into a multiply
and an address with scale 1. The IR does not promise that every displacement
fits an x86 immediate; selection must materialize larger ones.

`IrCall` records an argument-ID slice in `extra`, a fixed parameter-type slice in
`call_types`, result type and variadic flag. Calls retain C-level values, rather
than assigned ABI registers or stack offsets. Actual value types must match
fixed parameters; additional variadic arguments must be nonvoid. `symbol_count`
is an exclusive bound supplied by the translation unit; `UINT32_MAX` permits any
nonzero, nonsentinel link ID. Zero is reserved by `link.c` and is rejected for
both direct calls and symbol addresses. Complete C aggregate/prototype lowering belongs to the
checker/backend integration; this adapter's existing stream only exposes scalar
classes and loses the named/variadic parameter boundary, as described below.

## Blocks, CFG and use lists

`block_offsets[block_count + 1]` partitions all instructions into nonempty,
contiguous ranges. Every range ends in exactly one terminator; phis are a prefix
of their block. Broad rewrites rebuild these ranges. There are no linked ranges.
Block zero is the function's entry and must contain no phis: initial entry has
no predecessor edge selecting an incoming value. A producer introducing phis
into an entry loop must create a real preheader with explicit initial values.
Edges back to block zero remain valid for phi-free input; no implicit `undef`
incoming edge or value is invented by the verifier.

`ir_build_cfg` validates shape, instruction rules and terminator placement, then
builds CSR successors/predecessors and `instruction_blocks`. Its topology build
does not substitute for the full verifier's phi or dominance checks. Multiple
targets from the same source to the same destination become **one CFG edge**.
Successors retain first occurrence order in the terminator (true before false;
default before switch cases); predecessors are in ascending source-block order.
The successor columns directly match `dataflow.c`'s existing CFG interface.

Each phi names every distinct predecessor exactly once, with no extra entries.
Its value is used at the **end of that predecessor**, so a loop backedge can
legitimately name a later instruction. Self-referencing phis outside the entry
block are structurally valid; removing trivial phis is an SSA construction/cleanup policy, not an
assumption imposed by this representation.

`ir_build_uses` fully verifies the function, then counts uses, prefix-sums the
counts and fills `IrUses`. `offsets` has `instruction_count + 1` entries;
`uses` contains `(instruction, operand)` pairs. Positions 0/1/2 denote a/b/c,
and 3 plus the argument/incoming index denotes a call or phi side operand.
Each slice is stable in instruction/operand order, and repeated operands produce
repeated uses. Void-result instructions have empty slices. Block IDs, symbols,
slots, constants and side-table offsets never count as value uses. Derived counts
are accumulated in u64 and checked before becoming u32 offsets.

## Verification

`ir_verify` checks common instruction rules 16 rows at a time. Byte-permute tables
classify operand roles and semantic rules; type-set membership, operand/result
agreement and terminator masks are vector comparisons. Indexed type reads use
bounded scalar byte loads into the batch, avoiding hardware gathers and accidental
four-byte reads from byte columns. Constants, calls, phis, switches and conversions
use exact scalar handling; a failing vector batch also uses the scalar rule to
produce the same diagnostic. `ir_verify_scalar` checks one instruction at a time.
Both return the same `IrCheck` fields, including the first error location.

The graph phase checks phi coverage/types, duplicate switch constants and use
ordering. Duplicate switch detection uses stable byte-radix sorting with at most
16 passes, so adversarial i128 keys cannot cause a quadratic hash-probe scan.

An optional `IrDominance` view supplies trusted half-open dominator-tree intervals.
Both endpoints are zero for an unreachable block. The verifier checks use order
within a block regardless of this view. With it, cross-block definitions must
enclose the use block's interval; cross-block uses involving unreachable blocks
are rejected. Phi uses use their predecessor's position and interval. The vector
path compares batches of 16 uses; the scalar twin performs the same comparisons
one at a time. `dominance_checked` distinguishes complete interval checking from
structural verification. The verifier does not construct or prove the supplied
dominator tree: #174 owns that analysis.

## Typed-stream lowering

`ir_lower_typed(input, result_type, symbol_count, output, scratch, result)` and
its scalar twin accept the current `Tier0Function` contract after validating it.
The semantic result type is explicit because the stream's `AbiReturn` describes
locations, not its source type. The adapter doesn't model #148's incoming
variadic frames: a non-null `incoming_variadic` (compared, never read) and a
local flag other than `ADDRESS_TAKEN` (`BUSTER_TYPED_LOCAL_ADDRESS_ONLY`'s
anchors and `va_list` objects) are refused with `IR_ERROR_METADATA`, the local
in `detail` (`BUSTER_IR_NONE` for the frame), after the count and column checks
and before validation or allocation, rather than lowered as ordinary slots. So
are #136's frame objects (a local with a size in `local_sizes`: an array, a
struct or a union, of class VOID), whose ordinary slot would take VOID's bytes
and overlap its neighbours. A kind past `RETURN` (#136's `COPY`, and those after
it) is refused with `IR_ERROR_OPCODE` at its entry, after validation and before
allocation: the plan and the emitter would take it for a binary operation.
Lowering objects (an `IrSlot` has their size and alignment) and copies remains
open.

The adapter finds block boundaries, compares incoming stack shapes exactly,
classifies 16 entries at once (one at a time in the twin), counts output, then
emits into final-sized columns. Stack replay and irregular calls/control flow
are shared ordered work. Locals begin in typed slots. Only expression-stack
depths live across block boundaries receive temporary slots. Values are spilled
before outgoing edges and reloaded in the receiving block; `JUMP_VALUE` therefore
preserves conditional-expression arms even with an outer expression still live.

A separate preheader materializes slot addresses and initializes parameters.
Backedges to source entry zero skip that initialization. The typed stream's U64
pointer representation becomes explicit `inttoptr` / `ptrtoint` at memory and
address operations; ordinary U64 arithmetic remains integer arithmetic. Integer
comparisons produce `i1`, explicitly extended to the stream's `i32` result.

The typed endpoint becomes a void return for a void function and an `undef`
return for nonvoid fallthrough, whose value the existing stream leaves unspecified.
The caller must diagnose source-language constraints before lowering. The adapter
handles all currently defined typed kinds and classes; it does not invent missing
i128/f80, indirect-call, volatile, aggregate or prototype information. In particular,
the legacy stream provides argument classes and a variadic bit but no
named-parameter count: this compatibility adapter checks all supplied classes as
fixed parameters. A source producer must preserve the effective call-site
prototype and conversions already retained by the checker when constructing
`IrCall` descriptors.

The returned IR is verified before publication. All count, allocation and invalid
input failures preserve the caller's result and reclaim temporary memory. Scalar
and vector classification produce identical columns, side arrays and block ranges.

## Tests and measurements

```sh
./build/compiler test-ir
./build/compiler test-ir --seed 81985529216486895 --count 1024
./build/compiler test-ir-lowering
./build/compiler benchmark-ir
./build/compiler benchmark-ir --only vector --after-scalar 200
./build/compiler benchmark-ir-lowering
./build/compiler benchmark-ir-lowering --only vector --after-scalar 200
```

`test-ir` has independently expected CFG/use lists and rejection fixtures,
conversion and constant coverage, loop/diamond/self phis, duplicate targets,
8,192 adversarial i128 switch cases, guarded columns of lengths 0 through 33,
dominance tails, overflow, arena rollback, exact printer text and random
integer DAG mutations. Agreement alone is insufficient: malformed fixtures must
be rejected by both paths.

`test-ir-lowering` lowers generated typed programs through both paths, verifies
every function, compares their columns, and executes the resulting IR in a
test-only explicit-frame executor against the existing typed-stream interpreter.
Calls and global-memory effects are compared, with targeted fixtures for values
across conditional joins, backedges to entry zero, narrow shifts and wide pointer
scales. This is adapter testing, not the production interpreter of #176.
The current generator can still produce out-of-range floating-to-integer
conversions with its `defined` option. For those undefined results only, the
test executor chooses the existing Tier 0 interpreter's CVTT convention; its
host casts are range-checked. Representable conversions retain the IR operation's
signed or unsigned semantics, and F32 arithmetic/conversion rounds at F32 precision.

`benchmark-ir` measures the complete verifier and derived-array entry points on
32-instruction and larger synthetic integer/control-flow functions. Setup,
independent accounting and calibration happen outside retained samples. Rebuild
timings include allocation, clearing and filling. Each sample lasts at least
2 ms, variants rotate order, and the default is 21 passes. `--only` isolates a
path; `--after-scalar` conditions the vector path with lighter scalar verification.
It reports ns/function, ns/instruction, useful instructions/s, TSC ticks and
actual core/CFG/use arena charges. TSC ticks are not core cycles.

`benchmark-ir-lowering` measures the available producer, handoff and consumer
together: input validation, planning, classification, count/emission and output
verification. Its units are typed entries, not source tokens, and its inputs
are generated programs, not SQLite. Target conclusions require the optimized
repository build on the approved 9700X, pinned to one CPU and under the shared
lock; cloud correctness runs do not establish Zen 5 throughput.

### Initial 9700X qualification

[Protected run 39934441798](https://github.com/buster14a/buster/actions/runs/39934441798)
passed the production build, complete tests and benchmarks for head
`7a6ed05745a6b522209005d2423103caedb6c99a` against main `635434f`, with
Clang 23.1.1 on the Ryzen 7 9700X. The workflow held the shared host lock and
pinned each benchmark to one allowed CPU. Each row below is a 21-pass median;
the after-scalar command runs 200 ms of untimed scalar work before every vector
sample in a separate process.

| Workload | Unit | Scalar | Vector | Vector after scalar |
| --- | --- | ---: | ---: | ---: |
| Core verifier, 32 instructions | ns/IR instruction | 9.029 | 5.718 | 5.744 |
| Core verifier, 262,144 instructions | ns/IR instruction | 8.726 | 5.539 | 5.550 |
| Lowering, 4 functions / 136 typed entries | ns/typed entry | 32.084 | 28.151 | 28.260 |
| Lowering, 64 functions / 20,617 typed entries | ns/typed entry | 84.115 | 75.861 | 76.660 |

The large core fixture used 4,227,137 bytes for its function (16.125 bytes per
instruction including side metadata and arena alignment), 1,179,772 bytes for
the CFG and 4,653,116 bytes for uses. The larger lowering corpus produced 47,783
IR instructions and charged 944,733 core bytes in total (45.823 per typed entry),
with a maximum function allocation of 43,779 bytes.

These are observations from one controlled CI run on generated inputs. They do
not establish source-corpus throughput, multi-process reproducibility or SQLite
bytes per source token. This recorded revision precedes the entry-phi rejection
regression; its timings are not relabeled as measurements of later revisions.
