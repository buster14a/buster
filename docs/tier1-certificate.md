# Tier 1 allocation certificates

This is [#939](https://github.com/buster14a/buster/issues/939)'s certificate
checker and the producers that feed it. It certifies **allocations**: the
source program, the allocation's stream of stores, reloads, moves and
recomputations, and its block-entry assertions; and, for the CFG consumer, the
**code** emitted from them, decoded from its records (see "The emitted code's
certificate"). There are four entry points:

- `tier1_allocation_certificate_check` checks the interval allocator's existing
  fragment assignment. It cannot prove source uses, CFG edges or actual emitted
  spill/reload instructions, which that interface does not contain.
- `tier1_certificate_check` proves a normalized source/machine-stream
  correspondence using block-entry assertions and actual sequential edge edits.
  `tier1_certificate_oracle` independently infers byte/value facts to a CFG
  fixpoint and cross-checks small cases.
- `tier1_certificate_cfg` publishes that program for an allocation
  `tier1_cfg.c` made over a CFG: the source from the preallocation
  `Tier1CfgInput`, the stream, entry assertions and edges from the untrusted
  `Tier1CfgAllocation`. `tier1_certificate_cfg_check` publishes and checks.
- `tier1_certificate_emitted` publishes that program for the code the CFG
  consumer emitted: its stream decoded from the records themselves, each
  node's records regenerated and compared, the control flow walked.
  `tier1_certificate_emitted_check` publishes and checks.

All implementation and fixtures in this change are original project material
under Apache-2.0 WITH LLVM-exception. No external checker code or corpus is
imported.

## Trust boundary

`Tier1CertProgram` borrows immutable bounded arrays. The producer supplies the
verified **preallocation** source operations, source value widths/classes,
original CFG and simultaneous phi maps, target storage-unit layout, ABI
entry/return requirements and semantically justified rematerialization recipes.
Those facts must not be reconstructed from the allocator's output.

The rewritten instruction records must be independently decoded from the
**actual emitted stream**. `opcode`, `immediate` and `effect` together identify
every non-location semantic attribute; they are not a probabilistic hash.
Operands include implicit operands. Original instructions' early clobbers,
post-use clobbers and definitions are explicit. A producer must account for
memory addressing, flags, control-flow, implicit writes and every original
memory effect when creating these records. Physical aliases share byte-unit
indices. A private stack home aliases any actual write to the same bytes.

The checker itself does **not** decode x86 bytes or derive these target facts
from `X86Instruction`. `tier1_certificate_emitted.c` is the independent bridge
that does, for the CFG consumer's code: it decodes each record by its own form
and operands, and takes no allocator-provided opcode label, effect label,
location layout, recipe or swap annotation as proof. Its trust boundary is in
"The emitted code's certificate".

The untrusted proof consists of each block's entry facts and the rewritten
block/edge streams. A missing source instruction/use, changed opcode/immediate/
effect, illegal operand mask/tie, unknown home or unproved edge fact fails.
Kill flags and a claimed valid-home bit are not proof inputs.

## Fast proof

Block zero's assertion is first proved from real ABI inputs. All other storage
starts unknown. Every block is then simulated from its own assertion, checking
source operations in order, and every outgoing edge must establish the
successor's assertion after its **sequential** edits and simultaneous source
phi renaming. All blocks must be reachable; disconnected circular assertions
are rejected rather than treated as proofs.

An assertion names at most one location per value. Different equal-width names
may share the same byte span, provided the ABI or every incoming edge proves
each name. Partial overlapping assertions are refused. Within a block, copies
may establish any number of register and stack homes. The exit requirements
share the same assertion discipline; they are proved on every zero-edge
(sink) block, and a program with exit requirements but no sink block is
invalid.

Each byte carries a value identity, byte lane and width. A definition gives its
source name a fresh identity. Old homes therefore stop matching without a
scan over every register or stack slot. This is important when a loop executes
the same source definition again. A semantic source copy preserves content
identity and may introduce another name for it.

The base state uses block epochs. Each successor starts from the same
predecessor exit through a separately stamped sparse overlay; no full state
copy is made per edge. Epoch bounds are derived from the bounded record counts.
The function's initial scratch setup is cleared once, not once per block.

`Tier1CertLocation.extent` describes all bytes overwritten, not just the value
width. In particular a 32-bit GPR write invalidates its 64-bit alias; later
64-bit uses cannot rely on the previous upper half. Original early clobbers
happen before uses; post-use clobbers happen before explicit definitions.
Implicit outputs belong in the original operand/effect description.

### Transitions

The bounded edit vocabulary supports same-class moves through registers or
private homes, disjoint GPR exchanges, three-instruction XOR exchanges, integer
constant materializations and stable frame-address materializations.
Memory-to-memory moves and unrecognized recipes fail closed.

A move reads its source's first byte known, which refuses a move out of
uninitialized storage; its other bytes are copied as they are, unknown ones as
unknown, which no fact relies on. That is a 4-byte value's register moved whole
by a 64-bit move, or a scalar float's by `movaps`, as the emitted code does. An
exchange reads every byte of both sides known. A vector location may be a
register's whole width or a scalar view of its low 4 or 8 bytes (`movss`'s and
`movsd`'s), whose write extent is the register's. The oracle applies the same
rules.

A scratch-free GPR exchange is legal. The fast XOR transfer checks all three
concrete instructions, operand relationships, classes, widths, write extents,
target features and disjointness before applying an exchange summary.
GPR XORs invalidate live flags; vector/mask recipes do not acquire that GPR
side effect. The oracle instead executes the XORs as GF(2) expressions over
incoming bytes and proves their resulting permutation. A damaged middle
instruction is rejected.

The target vocabulary deliberately does not model EVEX 128/256-bit edits into
registers 16–31, arbitrary XOR networks, symbol/TLS recipes or cross-class
register transfers. The decoder must refuse a target form whose full effects
cannot be represented; it must not silently approximate it by a supported edit.

Integer recipes belong to the value's own register class. A general register's
constant is its payload. A vector or opmask constant is materialized within its
class, the payload identifying the producer's justified constant. A
`CONSTANT` edit writes a register of that class, never a slot or another class.

An operand the source marks `BUSTER_CERT_RECIPE` may be its value's integer
recipe itself rather than a location: location `BUSTER_CERT_NONE` in the
rewritten instruction. A use is then an immediate, which reads no storage. A
definition is then kept nowhere, and its value is left to its recipe. Such an
operand can't be tied, tied to, or a copy's, and only a value with an integer
recipe may have one. A frame recipe needs its base, so it is never an operand.

A fact with location `BUSTER_CERT_NONE` says only that a source-certified recipe
is available. It never gives a register contents. A register-required use
needs a checked materializing instruction first. Frame-address recipes also
require the correct currently available base value and the source's justified
stability premise.

That base value is an incarnation, not just the name. A frame recipe binds the
base incarnation current when the dependent value's incarnation was
established: at an entry or block-entry assertion, at the dependent's own
definition, or at its first materialization. Every `FRAME_ADDRESS` edit for
that incarnation must name a base location still holding the recorded
incarnation; a later definition of the base cannot retarget the recipe, while
a location that preserves the recorded incarnation still proves the edit.
`stable` remains the producer's premise that the recipe is justified for that
incarnation; it does not make the recipe content-stable across later
definitions of the base. The oracle erases a value globally on redefinition,
so it accepts only while the recorded base is still the base's current
incarnation, a strict subset of the fast checker's surviving-base cases.

## CFG allocations

`tier1_certificate_cfg.c` publishes the certificate of an allocation
`tier1_cfg.c` made over a CFG (#140). Include it after `tier1_cfg.c` and
`tier1_certificate.c`. Its two inputs stay apart:

- **The source is `Tier1CfgInput` alone.** At each position it holds the uses
  (a non-phi value's use at its own definition position is the definition's
  requirement, not an operand), the registers of the fixed ranges that start
  there (clobbered after the uses), and the non-phi definitions. These become
  source records of at most `BUSTER_CERT_MAX_OPERANDS` operands or clobbers
  each, in that order.
  - Each operand's allowed registers are its value's (a definition's
    `definition_registers` where given).
  - A use or definition that isn't required may be in a slot
    (`BUSTER_CERT_MEMORY`), or, for a rematerializable value, its recipe
    itself (`BUSTER_CERT_RECIPE`).
  - Each position's first record clobbers the general class's scratch
    register: the selector's expansions use it (`BUSTER_TIER1_SELECT_SCRATCH`),
    so no value may stay there between moves.
  - The phis are the edges' simultaneous renames. An undefined operand
    (`BUSTER_TIER1_CFG_NONE`, the IR's undef that #141's blocks' lifetimes
    give promotion) is a rename from `BUSTER_CERT_NONE`: any bits are the
    phi's value along that edge, so its fact needs no proof there, which is
    what `tier1_cfg_verify` checks of it too. The oracle makes every unit hold
    such a destination after the edge.
  - A rematerializable value is an integer recipe of its class, with its own
    id as the payload.
- **The allocation is untrusted.** Its stream is what the consumers run. Each
  position runs:
  1. the stores of the fragments that leave a register for their value's slot;
  2. the moves of a split at that position;
  3. the reloads, register moves and recomputations of the fragments that
     enter a register;
  4. the position's records, at their fragments' locations.

  The other parts:
  - **Edges.** Each edge carries its resolved moves in order. A slot-to-slot
    move, or a recomputation into a slot (a phi's home), goes through the
    class's scratch register, as a consumer makes it.
  - **Block entries.** Each block's certificate is where the allocation keeps
    each value its intervals carry into the block, at
    `tier1_cfg_entry_location`. A value recomputed there gets a recipe fact.
  - **Shape.** Every column is in range, intervals and fragments are in order,
    and each edge's placement agrees with its moves and degrees. Each offset is
    bounded before it bounds a read. A fragment carries a register only if it
    is a register's (`UINT8_MAX` otherwise), and a slot only where the
    allocation has slots. Publication validates only this shape; whether the
    values arrive is the checker's to prove.

Storage is what the source declares, for each class that has values: its
allocatable, scratch and fixed-range registers, at the class's width (8 bytes
for a general register, 16 for a vector one, or 64 where one past xmm15 is
declared, 8 for an opmask register). Then come a slot per spilled value and
each class's temporary slot. A class without values holds none, so its
registers and the fixed ranges' clobbers of them are left out. On c-testsuite's
subset this cut the checker's storage units from 199,184 to 67,624. An
allocation naming other storage, or a transition the stream can't express (a
store of a recomputed value, which has no register to store from), is refused
as `TIER1_CERT_TRANSITION`. A use or definition outside its value's intervals
is `TIER1_CERT_VALUE`. Blocks that block 0 doesn't reach never run, so the
program leaves them out and counts them (`Tier1CertCfgStats.unreachable_blocks`).

`Tier1CertCfgStats` reports the source's positions, records and operands, the
stream's transitions, split moves and edge moves (each after expansion), the
certificate's facts (and recipe facts), renames, reachable and unreachable
blocks, edges, locations, units and the published program's bytes. On a
refusal it names the block, value and position. Publication is linear in the
input's uses, definitions and fixed registers, the allocation's intervals,
fragments and moves, and the blocks each interval spans, with a binary search
per operand and fact. It runs on the caller's output arena (restored on
failure) and scratch arena (always restored).

`tier1_cfg_verify` remains the independent structural checker. It also
enforces the allocator's own constraints, which the certificate judges only
by their effect on values. Two kinds of mutant show the difference. A value
moved into k0 for a fragment no instruction reads is harmless, since no
instruction reads it there. Two SSA names in one register at a block's start
are harmless where they hold the same contents on every incoming edge, as a
phi and its own operand can. Every other mutant `test-tier1-cfg` makes fails
both checkers. That includes the malformed shapes: an interval map or an
interval's fragments forged past their columns, a spill where the allocation
has no slot columns, a register left on a slot's fragment, slots counted
without their columns or past the values, a fragment column, the edges'
placements or a move column absent, and fragments no interval owns. Both
checkers refuse each before reading through it: `tier1_cfg_verify` checks the
allocation's columns as the certificate's shape does
(`tier1_cfg_verify_columns`).

## Slow oracle

`tier1_certificate_oracle.c` uses a different representation: dense sets of
`(source value, byte lane)` facts for each byte. It initializes nonentry blocks
to lattice top, intersects incoming states to a fixpoint, performs global name
kills on definitions and full edge copies, and applies simultaneous phi
renaming from a saved state. Certificates never seed that inference.

After inference proves every assertion, a separate dense local pass checks
that the assertion is **sufficient** for its block and successors. Otherwise
an oracle could infer a useful register omitted from the certificate and
incorrectly accept a certificate the fast checker rightly rejects.

The oracle is deliberately limited to 64 values, 512 byte units and 64 blocks.
Exceeding those bounds returns `TIER1_CERT_CAPACITY`, not success.
`tier1_certificate_oracle_scratch_size` gives a program's exact scratch need,
or 0 past the bounds. A move saves only its two locations' rows before writing,
and a source instruction saves the whole state only for a semantic copy: the
other instructions read nothing they write. General
structural and target-form validation is shared; symbolic transfer, storage,
loop inference and XOR evaluation are independent.

## APIs and cost reporting

Include `tier1_certificate.c` after the normal standard headers; it has no
allocator or arena dependency. Include `tier1_certificate_oracle.c` after it
for small-case qualification. All arrays remain borrowed and immutable.
Caller scratch and the result report must be disjoint from all input storage
and from each other.

```c
size_t bytes = tier1_certificate_scratch_size(&program);
Tier1CertReport report = { 0 };
Tier1CertError error = tier1_certificate_check(&program, scratch, bytes, &report);
```

The size query includes alignment slack for an unaligned buffer; the caller
must still allocate the returned capacity. The checker performs complete
structural validation. A zero size query denotes invalid bounded domain sizes.
The oracle uses a separately supplied, larger buffer. Errors are explicit;
neither capacity exhaustion nor an unsupported edit is a valid allocation.

`Tier1CertReport` exposes source instructions/operands, edits, checked facts,
edge obligations, phi renames, storage units, values, blocks, byte reads/writes,
assertion bytes and used scratch bytes. Location-free assertions count toward
assertion size. Edge edit and phi counts are separate, avoiding a misleading
claim that all input storage is just the assertion array. The slow report also
counts fixpoint rounds and cumulative work.

Fast scratch is bounded by
`24*units + 24*values + 5*blocks + 96` bytes with the current record layouts.
Checking costs linear work in the bounded declarations, source instructions/
operands, rewritten edits, certificate assertions, edge/rename obligations and
byte-unit updates. Width is bounded by 64 bytes. There are no per-store scans
of stack homes, per-block clears of the value domain or per-edge state copies.

The fragment adapter includes `tier1_allocate.c`'s input contract and independently
sweeps start/end events. Ends precede starts at a position. Fixed reservations
are reference-counted, allowing overlapping fixed intervals without allowing
an allocated register to overlap them. Required uses are checked with a
monotonic fragment cursor. The adapter also requires every interval of one
value to carry the same class, a rule `tier1_allocation_input_valid` and
`tier1_allocation_verify` do not impose; the sweep indexes register occupancy
per value, so mixed-class values fail closed rather than being misattributed.
Its cost is
`O(positions + intervals + fragments + uses + fixed-register updates)`;
numeric position buckets are explicit in its report and are not called
machine instructions. `Tier1IntervalReport` separately reports existing
fragment-certificate size and checker scratch. Existing allocator preparation/
publication costs remain part of allocator measurements.

This is the scalar correctness implementation. No SIMD speedup or target-host
throughput result is claimed. The contiguous byte updates and bounded record
validation are candidates for measurement-driven vectorization; target-host
evidence is still required before selecting a production fast-path policy.

## Where the checker runs

- **`test-tier1-allocation`** wraps every successful `tier1_allocate` and
  `tier1_allocate_scalar` call of its fixtures with the fragment checker
  (below), and runs the stream fixtures.
- **`test-tier1-cfg`** checks every allocation both paths make with
  `tier1_certificate_cfg` and `tier1_certificate_check`:
  - the eleven fixtures (loops, a critical edge, phi swaps, slot cycles through
    the temporary slot, values and masks across calls) and the random programs
    (2,000 by default: loops, irreducible regions, rematerialization, fixed
    ranges, all three classes);
  - the slow oracle on every fixture and every 16th program
    (`--oracle-every N`), which must give the same answer;
  - the mutants of every 8th program (and, on every fourth oracle case, the
    oracle's answer on each). Every order, definition and split-destination
    mutant must fail the certificate, and the certificate must refuse no mutant
    `tier1_cfg_verify` accepts.

  On the default seed: 3,632 certificates, 281,318 facts, 201,918 edits. The
  certificate refused 1,681 of 1,701 interference mutants and 1,537 of 1,551 k0
  mutants; every other refusal of the 34 is one of the structural constraints
  above.
- **`test-tier1-cfg-source`** certifies every allocation both paths make of
  real C (c-testsuite's subset, the frozen `sqlite3.c`, musl's units and the
  SIMD fixtures in the correctness list) and reports the certificates' sizes. A
  refused certificate fails the run.
- **The emitter** checks each allocation before emitting from it when
  `Tier1EmitOptions.certify` asks: the CFG consumer's final allocation by
  `tier1_certificate_cfg_check`, the straight-line consumer's fragments by the
  fragment checker. A refusal is `TIER1_EMIT_CERTIFICATE`, and
  `Tier1EmitResult.certified` says it passed.
  - Every emitter test sets the flag: `test-tier1-emit`, `test-tier1-pipeline`,
    `test-tier1-masks` and the call, division, label, narrow, terminal,
    dropped-definition and variadic suites they include.
  - `test-tier1-pipeline` requires certified emissions with facts.
  - The C driver's `--verify` sets it with `--tier1`. The emitter's
    `Tier1EmitDiagnostic` keeps the checker's `Tier1CertError` as the nested
    cause `certificate` of the emission stage ([the outcomes
    guide](tier1-outcomes.md)). A refusal is an internal failure and capacity a
    resource failure, so either fails the compile rather than leaving the body
    to Tier 0, which would hide it.
- **#938's harness** allocates every problem it adapts by both of buster's
  allocator paths, and reports an allocation only past its certificate. This
  goes through `tier1_problem_allocate`, whose gate is `tier1_problem_certify`
  (`tier1_problem_intervals.c`, the fragment checker on the adapter's one-block
  problems). A refusal is `certificate-rejected` in the tally.
  - `tests/tier1-problem/report.c` reports problem files so.
  - Its integration build's fuzzer certifies every problem it decodes.
  - Its tests certify the fixtures and 400 generated imports, 251 of which
    spill.

  [The machine-problem guide](tier1-machine-problems.md) has the outcomes.

## Tests and current integration

The existing `tier1_allocate_test.c` is now a coordinator; its original fixtures
are retained **byte-for-byte** in `tier1_allocate_test_cases.c`. Test-only
wrappers validate every successful `tier1_allocate` and
`tier1_allocate_scalar` call made by those fixtures and their included suites.
They preserve allocator return statuses, output memory, arena state and
capacity-test boundaries. They are not production allocator wrappers and do
not automatically intercept unrelated Tier 1 emitter/pipeline test suites.

`test-tier1-allocation`, already in the normal correctness manifest, also runs
the normalized-stream checker/oracle and fragment mutation suites. The stream
fixtures cover ABI entry, initialized/uninitialized cycles, critical edges,
phi cycles, sibling-edge isolation, forged homes, partial writes, omitted
uses, tied-source copies, call clobbers/restricted masks, callee-saved exits,
exit-assertion shape and sink requirements, stack reuse, stale definition
incarnations, frame-recipe base redefinitions, legal exchanges, damaged XORs
and missing materializations. There are 1,000 generated valid move sequences
and 1,000 independently checked mutations, plus structural/capacity tests and
a guard-page pass that copies every borrowed array so it ends at `PROT_NONE`.

The stream suite can also run without the compiler's unity build:

```sh
clang -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -DBUSTER_CERTIFICATE_STANDALONE src/buster/compiler/tier1_certificate_test.c -o /tmp/buster-certificate-test && /tmp/buster-certificate-test
```

```sh
clang -std=c17 -O3 -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char -DBUSTER_CERTIFICATE_STANDALONE src/buster/compiler/tier1_certificate_test.c -o /tmp/buster-certificate-test && /tmp/buster-certificate-test
```

Standalone qualification: **2,087 stream checks, zero failures** in an
address/undefined-sanitized build, including the guard-page pass and the recipe
operands' and class constants' pairs (`tier1_cert_test_recipe_operands`).

## Cost on real code

`benchmark-tier1-certificate [--only VARIANT] [preprocessor options] file...`
builds `benchmark-tier1-cfg-source`'s corpus of every function `-c` compiles of
the files, keeping each function's vector-path allocation. Every allocation must
pass `tier1_cfg_verify`, and its certificate is published and proved once
outside the time; a refusal fails the benchmark, which then reports no time.

It reports the corpus's IR instructions, values, blocks and edges. It reports
the certificates' sizes: source positions, records and operands, the stream's
edits by kind, facts, recipe facts, renames, locations, units, the programs'
bytes and the checker's scratch (sums and the largest), and the units the
checker read and wrote.

It then times five variants over every function, after a warm-up, in 21 passes
whose order rotates:

- `allocate`: `tier1_cfg_allocate`, the allocation certified;
- `publish`: `tier1_certificate_cfg`, the allocator's share of the cost;
- `check`: `tier1_certificate_check` alone, on programs published before;
- `certify`: both, as `Tier1EmitOptions.certify` runs them;
- `verify`: `tier1_cfg_verify`, for comparison.

Each is given per IR instruction (time, cycles, instructions, IPC) and in
cycles per certificate item: the operands, edits, facts and renames the
checker's work is linear in. The 9700X job runs it on the frozen `sqlite3.c`
where its sources change (`changes`' `tier1-certificate`).

On the 9700X at `fee05dd6`, the frozen `sqlite3.c`'s 2,600 functions and
486,651 IR instructions gave these medians per IR instruction:

| Variant | ns | Cycles | IPC |
| --- | ---: | ---: | ---: |
| `allocate` | 143.5 | 795 | 2.72 |
| `publish` | 89.5 | 496 | 2.85 |
| `check` | 86.6 | 480 | 4.59 |
| `certify` | 171.2 | 949 | 3.81 |
| `verify` | 125.5 | 695 | 3.14 |

Certifying an allocation costs 1.19 times making it.

Per IR instruction, a certificate holds 0.86 facts, 0.16 edits and 1.8 source
operands, in 470.8 bytes. 92% of those bytes are fixed-width records: a source
record reserves eight operand slots and eight clobber slots on each side, but
the records average 1.13 operands. A packed layout would make certificates
several times smaller. The checker's scratch is 44 bytes per IR instruction.

[The frozen record](https://github.com/buster14a/buster-performance/tree/b468d3c8a50ef36de1a56bc622af4098e3f26ed5/docs/measurements/tier1-certificate-939-2026-11-02)
has the run, the sizes by record and the limitations. It is one process in one
job.

### Why the checker is scalar

The checker and the publication have no vector path or twin. This is a
recorded decision, not a measured loss: no vector candidate has been built or
timed. The reasons it waits:

- **It is off the compile path.** Only `--verify` and the tests run it
  (`Tier1EmitOptions.certify`). A default `-c` or link never pays its 171 ns
  per IR instruction.
- **Within a block, its work is a dependent walk.** Each stream instruction
  reads the storage state the one before it wrote. The checker runs at 4.59 IPC
  on the 9700X, with 10.7 million unit reads and 17.7 million unit writes for
  `sqlite3.c`'s 778 thousand stream instructions. The independent work lies
  across blocks, not across one block's instructions.
- **Its data-parallel axis is the block.** The design needs no fixpoint: each
  block is checked from its own entry facts, and each edge from its
  predecessor's exit state. So blocks, and functions, can be checked
  independently: on lanes, or as batches of blocks of one shape.

If certification moves onto the default path, a candidate along that axis must
be measured against this scalar baseline on the 9700X. Until then, the scalar
checker is the only path, and the oracle is its independent check.

## What the certificate proves

The CFG certificate proves the allocation: every use reads its value, along
every path, through every edge's moves, phi renames, fixed-range clobbers,
spill slots and recomputations. It proves this for every allocation the Tier 1
tests produce, and, with `--verify`, for every one the driver's `--tier1`
emits from.

Its stream is the allocation's own account of its transitions. The code
emitted from it is proved by the emitted code's certificate, below, wherever
the decoding takes the function's shape.

## The emitted code's certificate

`tier1_certificate_emitted.c` publishes the certificate of the code the CFG
consumer wrote, decoded from its records. `Tier1EmitOptions.certify`, and so
the tests and the driver's `--verify`, run it after every CFG function's
emission. A refusal fails the function (`TIER1_EMIT_CERTIFICATE`, the
diagnostic's error its `Tier1CertEmittedReason` plus
`BUSTER_TIER1_EMIT_CODE_CERTIFICATE`), the checker's capacity is
`TIER1_EMIT_MEMORY`, and a shape the decoding doesn't take leaves the code
uncertified (`Tier1EmitResult.code_reason`), counted by `--tier1-report`'s
`code_certified` and `code_uncertified`.

**Trusted, as the source:** the IR and its selection (what a node's sequence
computes from the registers it is given: `tier1_emit_cfg_kernel`,
`tier1_select_fixed`), the ABI's argument registers, the rematerialization
recipes, the frame's displacements from its base register, the CFG and its
phis, and `x86_semantics.c`'s effects of each record.

**Untrusted:** the allocation, every emitted record, and the emitter's
account of them: each record's origin (`Tier1Origin`, marked by
`tier1_emit_mark`), each block's and stub's first record, the references, and
which recomputed definitions it left out. An origin only says which decoding
applies; every record is decoded by its own bytes.

The decoding:

- **Storage.** Each general register is an 8-byte and a 4-byte location, each
  xmm register its 16 bytes and its scalar 8 and 4, each opmask register its 1,
  2, 4 and 8. The frame base register's displacements over the spill, save and
  temporary rows, the calls' outgoing areas and the stack parameters' incoming
  slots are byte units, so slots that values share over their lifetimes are the
  same bytes. Values have their own widths: a general one 8 bytes or 4 (every
  narrower one is in its canonical 32-bit form), a float 4 or 8, a mask its own.
- **Moves.** `mov` (32- and 64-bit), `movss`, `movsd`, `movaps` and `kmov`
  between registers and frame bytes are the edits their forms say, at their
  forms' widths: a 32-bit store of an 8-byte value truncates it, and its later
  8-byte use fails. A recomputation is a constant's edit only where
  `tier1_rematerialize` writes exactly that record, with its reference, for the
  value its origin names.
- **Nodes.** Each node's records are regenerated from the allocation's
  registers at its use phase and must equal the emitted ones byte for byte. They
  are one instruction: its operands read where the allocation has them, every
  register and frame byte its records write (by their effects) killed, its
  definition written where its interval starts. A call's sequence is checked
  against the IR (its target's reference, `al`, its result's extension), its
  arguments read in their ABI registers and outgoing slots, the outgoing area
  killed after it (its callee owns it). A copy that vanishes in its own register
  is its operand's value. A return's epilogue may come before its `ret`.
- **Entry.** The parameters are entry facts in their ABI registers and incoming
  slots. The prologue may write none of them; a narrow parameter's extension
  into its canonical form is the parameter, and a stack parameter's load must
  be its type's form.
- **The frame protocol.** The prologue and each epilogue are decoded record by
  record by rsp's offset from its value at entry, each record exactly its form's
  (`push`, the frame pointer's `mov`, `sub`; `add`, the reset from rbp, `pop`),
  nothing else in them. The prologue must build the frame the layout's offsets
  are of: its saved registers pushed, its frame pointer set where it has one,
  its allocation exactly; and with calls, rsp a multiple of 16 at them (the ABI's
  alignment, rsp 8 past one at entry). A push is a store of its register into
  its slot, a pop the load back, the frame pointer's set a kill of rbp. Each
  callee-saved register's value at entry and the return address are values of
  the certificate: entry facts, asserted at every other block's entry (in the
  push's slot, or in the register where the prologue doesn't push it), and read
  by each return's `ret`, where they were at entry, with rsp back at the return
  address. A register the function doesn't give back, a slot something
  overwrote, an epilogue that pops the wrong slot or leaves rsp elsewhere fails.
- **Control.** Each block's tail, its jumps and edges' moves, is walked for each
  successor, by its condition's value for a conditional branch's: the path must
  reach the successor's first record through that edge's moves alone, or reach
  a successor whose only predecessor it is and whose first records are the
  edge's moves. Every branch target is a block's or a stub's start, or the
  prologue's own probes'.
- **Frame.** No record but the prologue and the epilogues writes the frame base
  register or the frame pointer, nor rsp but a call's and a `ret`'s own push and
  pop. A store through
  any pointer but the frame base register's is the program's own, assumed not to
  reach the frame's rows, as C's object model says.

**Not decoded yet**, so not certified, each counted by its reason: by-value
aggregates (#789's copies, parts and hidden pointers), dynamic and realigned
frames (`alloca`, a stack restore), a prologue's stack probes, a variadic
definition's register save area,
an indirect jump to several labels, a call's stack arguments or a stack
parameter where the frame base register isn't rsp's (or the incoming area's),
and an entry block that is a loop's. The allocation's certificate still covers
them, and the straight-line consumer's functions keep the interval
certificate. Canonical 32-bit storage (a 4-byte value's upper half zero) is the
selection's contract and isn't checked: a 4-byte value is its low 4 bytes.

**Tests.** The emitter's tests, the mask tests and the pipeline test run it on
every CFG function they emit. `test-tier1-pipeline` also corrupts each
certified function's records: every prologue and epilogue record, and a sample
of its moves and nodes' (another register, a frame access 8 bytes off, an
immediate 8 off, two adjacent records of one origin swapped). A mutant the
certificate refuses counts, and one it accepts runs natively and must still
return the interpreter's results. At this change, 879 of 890 mutants are
refused, all 76 of the prologues' and 72 of the epilogues' among them, and the
11 accepted (independent moves swapped, and the like) are harmless. With `--tier1 --verify`, the
compiler's `main.c` certifies 562 of its 813 CFG functions' code and the
c-testsuite subset 231 of 288, none refused; most of the rest are aggregates'.

**Cost.** `benchmark-tier1-code-certificate [--only emit|decode|check|certify]
[-E's options] file...` times the decoding, the check and both per IR
instruction and per record, beside the emission they certify, on every
function whose code it certifies, with the certificates' sizes and the
uncertified functions by reason. The 9700X job runs it on the frozen
`sqlite3.c` where `changes`' `tier1-code-certificate` entry says, and its
numbers go to buster-performance.
