# Emission metadata and the emitted function (#1056)

## Status and scope

The C driver's emission (`check_program_emit`) consumes one tier-neutral result,
`EmittedCode` (`src/buster/compiler/emitted_code.c`): instruction records, the
branch columns, symbolic references, labels, optional source events and the
frame's events. Tier 0's code (its functions, its helpers and the assembly
templates' code) adapts to it (`emitted_from_tier0`), and Tier 1's publishes it
itself (`check_tier1_compile`): no consumer reads a `Tier0Code`, and Tier 1 no
longer describes its frame as Tier 0's prologue/epilogue protocol. The frame's
events are the producers' own (Tier 1's frame emitter writes them with the
records that make them), resolved through the final layout after branch sizing,
written as the fixed-frame CFI where they spell a fixed frame (the same bytes as
before) and as a row program otherwise, and with `--verify` proved against the
records and their control flow. Tier 1 compiles with `-g`, at the function's
granularity ("Tier 1's `-g`" below).

`src/buster/compiler/emission_metadata.c` is the protocol underneath: source and
frame events at record boundaries and their resolution against a final record
layout. `./build/compiler test-emission-metadata [--seed N]` tests it alone (its
translation unit, `emission_metadata_test.c`), and `./build/compiler
test-emitted-code [--seed N] [--count N] [--cross-check] [--keep]` the emitted
function's producers, consumer and verifier ("Tests" below); both are in
`tools/ci/correctness.tests`. Compilation without `-g` runs no source protocol.

The protocol module neither produces machine code nor proves that a supplied
unwind rule matches that code: its checker establishes bounded, ordered,
canonical metadata and valid layout references. The proof is the verifier's
(`emitted_verify_frames`, "The frame verifier" below). A balanced
remember/restore stack is not such a proof.

No imports or external implementation are incorporated. The code and original
C fixtures use the project's Apache-2.0 WITH LLVM-exception terms.

## Record boundaries and padding

`EmissionLayout` borrows two exact-length arrays: each record's byte start and
byte end after branch sizing and final layout, plus the function's byte size.
The layout owner supplies them once; metadata does not independently estimate
instruction widths. Spans must be ordered, nonoverlapping and within the size.
Zero-width records and gaps for leading, internal or trailing padding are
representable. Empty layouts need no array storage.

An `EmissionAnchor` contains a record boundary in `[0, count]` and a side:

| Anchor | Byte offset | Intended use |
| --- | --- | --- |
| `LEFT` at boundary zero | Zero | Initial generated/prologue region |
| `LEFT` at boundary `b > 0` | `ends[b - 1]` | A frame change immediately after an instruction |
| `RIGHT` at boundary `b < count` | `starts[b]` | Source attribution beginning at an instruction |
| `RIGHT` at boundary `count` | Function byte size | The end after trailing padding |

For example, a push ends at byte 3 and the next instruction starts at byte 8.
Its CFA change belongs at `(1, LEFT)`, byte 3, not `(1, RIGHT)`, byte 8. The
updated unwind rule must already hold throughout that padding. A generated
source marker at the left boundary and a source origin at the right boundary
can describe the padding without assigning it to a statement.

Anchors are ordered by boundary, then side. Several distinct anchors can resolve
to the same byte offset when records are adjacent or zero width. Resolution
preserves their order and does not deduplicate them. A future source-row
consumer must use the last event at a byte offset for the following nonempty
range. An end event describes no instruction after the function. Frame rules
at equal offsets execute in their original order.

## Optional source origins

`EmissionSourceEvent` selects either an origin ID in a producer-owned origin
table or `EMISSION_SOURCE_GENERATED`, with `BUSTER_EMISSION_NO_ORIGIN` as its
explicit sentinel. No missing origin is silently replaced by a source
statement. A present source map begins at `(0, LEFT)`, so the prologue or leading
padding is explicitly attributed or marked generated. Source anchors are
strictly ordered; duplicate anchors are rejected.

`source_count == 0` means no source metadata. The module allocates nothing and
requires no per-entry source array. A caller compiling without metadata should
skip metadata production and this path entirely. That is an interface property,
not a measured claim about the current driver's compile cost.

## Frame rule vocabulary

`EmissionFrameEvent` describes a rule change at an anchor rather than a saved
register mask, one stack allocation or a fixed epilogue convention. It can
express a CFA register/offset, a CFA-offset change, saved-at-CFA-offset and
saved-in-register rules, restoration of the initial rule, same-value and
undefined rules, and ordered remember/restore-state operations. Offsets are
signed bytes; no eight-byte slot convention is embedded in the protocol.
Register IDs belong to the target's unwind-register namespace, not the encoder's
register numbering. The target also owns the initial CIE rules.

Frame events can share an anchor. The checker rejects out-of-order anchors,
unknown kinds, invalid register IDs, noncanonical unused fields, state-stack
underflow and an unbalanced final state stack. This permits multiple return
regions and repeated allocation/probe rule changes as protocol data; it does
**not** establish that a native probe loop or return epilogue has those rules.
Expressions and target-specific rule opcodes are not implicitly accepted. An
actual target writer must refuse any representable rule it cannot encode.

## Ownership and transactional publication

The entry points implement count/place/fill without owning an allocator:

1. `emission_metadata_plan` validates and returns exact source/frame row counts.
2. The caller reserves the two output slices in its own region or arena.
3. `emission_metadata_resolve` revalidates inputs and both capacities before the
   first output store, fills the slices, and publishes `EmissionResolved` last.

All input descriptors and slices remain live and unchanged during a call. They
must not overlap destination slices, destination descriptors or result storage.
The two destinations are disjoint. These are explicit ownership preconditions,
not assumptions introduced through `restrict`. Exact logical lengths suffice:
no input sentinel or output slack is required.

Any invalid-input or insufficient-capacity result preserves both destinations
and the result descriptor. A planning failure preserves its plan descriptor.
`EmissionCheck` reports the failing part and first index; success is
`EMISSION_NONE` with index zero, descriptor and destination errors also report
index zero, and an unbalanced final frame state reports `frame_count`. The
scalar and vector layout checkers return the same result, including the first
malformed record.

The module does not allocate. The tests therefore establish capacity-failure
atomicity, not whole-driver arena-exhaustion rollback. A future adapter must
reserve all code and metadata storage before publication and restore its arena
marks on every failure.

## Scalar and vector paths

Layout validation has a contiguous 16-record AVX-512 path and an exact scalar
path. Masked loads cover only the valid tail; a shifted end vector checks each
record against its predecessor, including across batch boundaries. The module is
included by `emission_metadata_test.c` and by `c_check_program.c` before
`c_check_debug.c`, after the host ISA is selected. The standalone wrapper's
vector command enables `BUSTER_HOST_VECTOR` through
`BUSTER_EMISSION_METADATA_TEST_VECTOR`.

Sparse event resolution and frame-state protocol checking currently use scalar
code. There is no locked-9700X measurement demonstrating whether further
vectorization pays, and this prerequisite makes no speedup claim. Integration
must measure the retained storage and total compile cost, including the layout
arrays, rather than treating this module's allocation-free implementation as
proof that the complete path is free.

## The emitted function

`EmittedCode` borrows every slice from its producer's arenas, which outlive the
emission's reading of them:

| Slice | Contents |
| --- | --- |
| Code | `records`/`count`; `branches`/`targets`/`branch_count` (each branch's record and its target's, ascending, which branch sizing reads); `references` (`LinkReference`, by marked instruction); `label_records` (each label's first record, where the emission defines its symbol) |
| Source events | `-g` only. `source_count` events, event *e* at the RIGHT side of boundary `source_records[e]` (nondecreasing), its origin `source_origins[e]` (an index into the body's typed entries, the lowering's entry tokens), or *e* itself where the column is null (Tier 0's: an event per typed entry), or `BUSTER_EMISSION_NO_ORIGIN` for generated code; and `source_records[source_count]`, where the epilogue's closing row starts (the record count for none) |
| Frame events | `frames`/`frame_count` (`EmissionFrameEvent`, the x86-64 namespace below) and `frame_status`: described (none means the CIE's rules throughout), undescribed (the producer's frame isn't one its events describe: the emission's backend failure at the function), no room (a resource failure, which the emission's lanes retry on one), or none asked for |
| Producer | `assembly` (a template's code: its events describe the frame the compiler built around the templates, which the verifier doesn't read); `error`/`node` (Tier 0's failure, which the emission reports); `deferred` (statistics) |

The x86-64 unwind-register namespace is the System V psABI's DWARF numbering
(rax 0, rdx 1, rcx 2, rbx 3, rsi 4, rdi 5, rbp 6, rsp 7, r8 to r15, the return
address 16; `emission_x86_registers` maps the encoder's numbering to it). The
CIE's initial rules are the CFA at rsp + 8, the return address at CFA − 8 and
the six callee-saved registers their own.

### Producers

Tier 0 (`emitted_from_tier0`): its records and columns as they are; with `-g`
its entries' first records as source events; with unwind tables its frame's
events from the frame it published (`Tier0Code.frame_*` and its tail calls'
records), which `unwind_tier0_frame_tails` checks are the records' (each
transition at its record's boundary: the reader takes no final offsets for
this), in the fixed writer's order (`unwind_function_rows`). A frame whose CFA
would pass `INT32_MAX` (within 56 bytes of 2 GB) has no events: the events'
values are signed 32-bit, so the emission refuses it where the old descriptor
took it.

Tier 1 (`tier1_frame_emit`, `tier1_frame_emit.c`): the frame emitter appends the
events with the records it writes, at the boundary after the instruction that
makes each change, tracking the CFA (`Tier1FrameCfa`): a push's CFA offset
(while the CFA is rsp's) and its slot; rbp's pointing; an allocation's CFA
offset; a release's, a pop's and the register restored; rbp's pop's CFA back to
rsp + 8. A probe loop without a frame pointer has its CFA in r11 from the
subtraction that sets r11 to the loop's bound, which nothing in the loop
changes, and back in rsp at the loop's exit, where rsp equals r11 — the shape
the fixed descriptor (pushes, one allocation, a frame pointer) couldn't express.
With a frame pointer the CFA is rbp's and the probes move no rule. Each epilogue's
events are bracketed by a remembered state at its first record and a restored
one after the ret that follows it (both consumers append the ret right after),
so that the body's rules hold again after an early return. `check_tier1_compile`
checks the brackets against its recorded exits (each a ret, each restoring right
after one) and publishes `EmittedCode`, with `-g`'s source events: the body's
start (generated code: the declaration's line) and the last epilogue's first
record where the frame is closed.

### Consumer

`check_program_emit_frame` (`c_check_program_emit.c`), for each function after
branch sizing, with `offsets` each record's start and `offsets[count]` the
length: `emitted_unwind` resolves the events through that layout
(`emission_metadata_resolve`, which checks the protocol and the layout, the
record ends `offsets + 1`), puts the rows in canonical form
(`unwind_rows_canonical`: rows at the function's end describe no instruction,
and a state restored there goes with its remembering, so that a closed frame's
last epilogue has no bracket) and lowers them (`unwind_rows_function`): to the
fixed descriptor where they spell its simple shape (pushes, one allocation, its
release and pops), which the vector writer takes, and otherwise to a row
program (`UnwindFunction.open == BUSTER_UNWIND_ROWS`), which the exact writer
writes (`unwind_rows_scalar`). For every fixed shape's rows a row program is the
fixed writer's bytes exactly: advances are always `DW_CFA_advance_loc4`, a
state's remembering takes no advance (the rules in force there are those in
force since the last row), and a CFA's register or offset alone changes by
`DW_CFA_def_cfa_register` or `DW_CFA_def_cfa_offset`. With `--verify`, the
events are proved (`emitted_verify_frames`), and where Tier 0's fixed-frame
analyzer (`unwind_tier0`, which reads the records alone) takes the function,
its frame must be the same CFI (`emitted_unwind_same`). A row program's rows live
on the lane's scratch (a helper's in the emission's output arena), which the
tables read once the lanes are done.

### The frame verifier

`emitted_verify_frames` proves a function's frame events against its records
and their control flow, as an unwinder reads them: at every record the code
reaches, the rules in force there (the events anchored at or before its start,
in order, from the CIE's) must hold on every path that reaches it. A forward
dataflow over the code's blocks (each branch to its target and on, each
indirect jump to every label, a ret, a trap or a tail call's jump ending its
path) follows each general register's value as the CFA plus an offset, which
register's entry value each holds, and where in the frame each entry value is
saved, to its fixpoint, the paths' meet at each block. Pushes, pops, register
moves, `lea`, additions and subtractions of immediates, realignments (an
unknown value), calls (rsp and the callee-saved registers kept, the others
unknown) and stores into the frame (which overwrite the slots they reach) have
their effects; any other register write (by `x86_semantics.c`'s resolved
effects) leaves the register unknown. A compare of two registers and the
`jne`/`je` after it give their equality on the equal edge: the probe loop's
exit. Then each reached record's state is checked against the rules: the CFA
register's value must be the CFA less the rule's offset, a callee-saved register
whose rule keeps it must hold its entry value, a saved one must be at its slot
or in its register, and the return address at its slot.

Its trust boundary: a store through a register it doesn't follow (an index, a
pointer) is trusted not to reach the save area, which the frame's layout keeps
apart; code nothing reaches isn't checked; and `jmp r11` is a tail call (Tier 0's
and Tier 1's through a pointer), any other indirect jump a jump within the
function to its labels. An assembly template's code is the user's and isn't
verified. A form `x86_semantics.c` doesn't cover gets its conservative record,
which may write any register, rsp among them, and any byte: a rule that fails on
a path through such a record is unproven (`EMITTED_VERIFY_UNPROVEN`), neither
proved nor refuted, and the driver keeps the frame, as Tier 0's fixed-frame
analyzer leaves such code (`UNWIND_ERROR_UNSUPPORTED`). An x86 builtin's
per-unit leaf of such a form is one: `monitorx`'s and `mwaitx`'s, which the
compiler's own `main.c` compiled with `--verify -c -march=znver5` emits. The
check goes on past an unproven rule, so that a refutation anywhere in the code,
on a path no such record is on, still wins. It is scalar and runs with
`--verify` and in the tests only, off the compile's path; its memory (a state
per block leader, two words per record) is given back. It rejects malformed
events (`EMITTED_VERIFY_INVALID`), refutes rules the code doesn't establish
(`EMITTED_VERIFY_REFUTED`, with the record and the last event applied), leaves
rules past an uncovered form unproven (`EMITTED_VERIFY_UNPROVEN`, the first
one's record) and refuses states remembered deeper than 16
(`EMITTED_VERIFY_UNSUPPORTED`).

### Tier 1's `-g`

The driver compiles `--tier1 -g`. Tier 1's IR keeps no source positions yet, so
its rows are the function's alone: its DWARF has the function's name and range,
every instruction from the entry to its last epilogue at the declaration's line
(the body's start a generated event, marked as the body's row, where a
debugger's breakpoint on the function stops), and the closing brace's line from
the last epilogue's first record where the frame is closed. No statement lines
inside a Tier 1 function and no variable locations: a debugger steps over a Tier
1 function's body as one line, and its backtraces place each Tier 1 frame at its
function's declaration (Tier 0's functions keep their statement rows). The rows
go through the same raw-row passes and `check_debug_rows_protocol` as Tier 0's
(an origin column takes the passes' exact path).

## Tier 0 source-row consumer

For `-g`, `c_check_debug.c` builds a function's declaration/origin events and
its source events (Tier 0's: an event per kept entry) from the emitted code's
`source_records` and `source_origins` and the lowering's entry tokens, uses the
records' exact starts and ends after branch sizing as the layout, and resolves
them through `emission_metadata_plan`/`resolve`. The resolved offsets and origins
must equal the raw rows that the existing scalar/vector builders produce; when
the layout, an anchor or a resolved row disagrees, the emission fails as an
internal backend error rather than publishing rows the protocol cannot justify.
A row's room running out is a resource failure (the emission retries a lane's
on one lane). Only `-g` pays for this validation. `test-check-debug`'s random
raw-row cases exercise the positive and negative paths, and its `--cross-check`
compiles and runs the resulting DWARF.

## Tests

`./build/compiler test-emitted-code [--seed N] [--count N] [--cross-check]
[--keep]` (`emitted_code_test.c`):

- row programs: every fixed descriptor `test-unwind` writes (each save mask, with
  frames at every LEB width, frame pointers', tail calls' and open ones) as its
  rows, written by the row writer into the fixed writer's bytes, as pairs and
  with one CIE, by the exact and the batch paths, and lowered back to the same
  descriptor where it is the simple shape (a row program of the same bytes
  otherwise); canonical rows (a closed frame's last epilogue's bracket and rows at
  the end dropped); and the row checker's refusals, a break at a time;
- Tier 0: generated programs of `test-unwind`'s shapes (calls, loops, alternate
  returns, objects, frame pointers, realigned frames, deep values), each
  function's events resolved after sizing into the descriptor the fixed reader
  gives (the previous path), byte for byte, by both paths; proved; and each CFA
  and slot event's value moved by 8, and each anchored one record early, refuted;
- Tier 1: the frame planner's every flag (variadic, dynamic stack, kept pointer,
  no red zone, probes) at alignments to 8192, sizes past two pages and no or all
  callee-saved registers, with a call, an early return and a closed or open end;
  proved; without probes, the previous adapter's descriptor (its frame read as
  Tier 0's protocol by the fixed reader), byte for byte; and with probes past a
  page without a frame pointer, a row program with its CFA in r11 that the fixed
  reader refuses; with the same mutants refuted;
- uncovered forms: `monitorx`'s leaf's return unproven, not refuted, and proved
  without it; a push before `monitorx` that its events don't describe refuted; a
  return that one path through `monitorx` reaches unproven; a push off its paths
  refuted after an unproven return; and the driver's `--verify -c
  -march=znver5` of `monitorx`'s and `mwaitx`'s leaves, by both paths;
- with `--cross-check` (GNU as and ld, gdb): that probe frame natively, its CFI
  the row writer's, gdb stopped at every instruction of it (the loop's each time
  it runs), each backtrace reaching the caller, whose frame must have its
  callee-saved registers and its rsp back.

`test-check-debug --cross-check` compiles its Tier 1 unwind fixture with `-g`
too (`c_check_unwind_test.c`): by each path and by `--lanes 1` and 8 into the
same bytes, the program runs and walks as without `-g`, `llvm-dwarfdump --verify`
passes, and gdb stopped at a Tier 1 function each of the 14 times it runs places
it at its declaration's line and each Tier 1 caller at its own (main, Tier 0's,
at its call's line). Every existing unwind check (`test-unwind`,
`test-check-debug`'s gdb at every instruction of each Tier 1 shape) runs on the
new path, and the driver's objects and executables of c-testsuite's subset
(plain, `-g`, `--tier1`, `--tier1 --verify`, `--scalar -g`) were compared byte for
byte against the previous head's.

## Reproducible correctness checks

Run from the repository root. The normal commands are:

```sh
./build/compiler test-emission-metadata [--seed N]
./build/compiler test-emitted-code --cross-check
```

`build.sh` builds them and `tools/ci/correctness.tests` runs them. The standalone
commands below compile the protocol's tests without the compiler, which is
useful for bisecting a protocol change; the vector ones require an
AVX-512F-capable host. These are correctness tests, not performance measurements.

Baseline:

```sh
clang -std=c17 -O2 -g -march=x86-64 -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char tests/emission-metadata/test.c -o /tmp/buster-emission-scalar && /tmp/buster-emission-scalar
```

Vector and scalar-twin comparisons:

```sh
clang -std=c17 -O2 -g -mavx512f -DBUSTER_EMISSION_METADATA_TEST_VECTOR -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char tests/emission-metadata/test.c -o /tmp/buster-emission-vector && /tmp/buster-emission-vector
```

Address and undefined-behavior sanitizers, baseline:

```sh
clang -std=c17 -O1 -g -march=x86-64 -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all tests/emission-metadata/test.c -o /tmp/buster-emission-scalar-sanitize && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/buster-emission-scalar-sanitize
```

The same sanitizers with the vector checker enabled:

```sh
clang -std=c17 -O1 -g -mavx512f -DBUSTER_EMISSION_METADATA_TEST_VECTOR -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all tests/emission-metadata/test.c -o /tmp/buster-emission-vector-sanitize && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/buster-emission-vector-sanitize
```

Each run prints its check and failure counts; the correctness inventory records
them. Coverage includes empty/null inputs, leading/internal/trailing padding,
zero-width records, changed branch widths, generated origins, end boundaries,
frame-rule vocabulary and nested state stacks, signed and unsigned limit values,
invalid metadata, capacity rollback and a destination with spare capacity whose
tail must stay untouched. Guard pages test exact input/output lengths and every
layout count from zero through 65; the malformed-record rules are compared across
SIMD batch boundaries, not only at the first record. Seeded property tests
compare 4,096 layouts against independently calculated offsets and compare both
implementation paths.

## Cost and what remains

The default compile now builds each Tier 0 function's frame events (a handful:
two per push, one per allocation, the epilogue's and each tail call's), resolves
them through the final layout and lowers them, where it built the fixed
descriptor directly before: work per function proportional to its frame's
transitions, plus the protocol's layout check over its records (16 at a time on
AVX-512 hosts). The events live on the lane's scratch until the emission ends.
No locked-9700X measurement of that cost or storage is in this change; the
9700X CI job's `measure-compile` and `benchmark-check-lanes` steps run where
their selectors say, and their numbers are the first evidence. New raw evidence
belongs in `buster-performance`.

Remaining beyond this change:

- Statement-level rows and variable locations for Tier 1's code need source
  positions carried through the IR (lowering, SSA, the optimizer and selection),
  which it doesn't have.
- The driver requests no probes (`BUSTER_TIER1_FRAME_PROBE_STACK`); the probe
  frame's unwinding is proved and checked natively in isolation only, as the
  issue asked, before any production option.
- The verifier's trust boundary (stores it doesn't follow, unreached code, rules
  past a form `x86_semantics.c` doesn't cover, which it leaves unproven) and its
  scalar implementation, which runs with `--verify` and in the tests alone.
