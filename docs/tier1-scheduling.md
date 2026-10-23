# Experimental Tier 1 scheduling

`tier1_schedule.c` supplies a bounded postallocation scheduling experiment for
[#172](https://github.com/buster14a/buster/issues/172). It consumes the actual
`X86Instruction` records of a block, the operand-resolved effects from
`x86_semantics.c`, and the original 9700X observations in `x86_zen5.c`. It returns
a permutation for a caller to apply before branch sizing and encoding.
Scheduling remains opt-in. This implementation does not establish that
scheduling improves a C program or that its compilation cost is justified.

## Producer and consumer contract

Initialize the encoder, semantics and Zen 5 tables before worker threads.
`Tier1ScheduleInput` supplies `instructions`, an optional byte column `pinned`,
and `count`. These are physical-register records after allocation, validated
for ordinary 64-bit encoding by the emitter. They are the entire block; labels
or source anchors within it require the corresponding caller pin. The API does
not infer block boundaries, calling-convention clobbers or volatile information.

`tier1_schedule_block(input, scratch, order, capacity, vector, statistics)`
returns `TIER1_SCHEDULE_OK`, `TIER1_SCHEDULE_INVALID_ARGUMENT` or
`TIER1_SCHEDULE_CAPACITY`. Its scratch is one caller-owned, reusable
`Tier1ScheduleGraph`; no allocations occur inside it. At the current bound of
256 records, the scratch is 33,024 bytes. The output is
`order[new_position] = old_position`, with one `uint16_t` per record. The emitter
must permute every parallel record column with it, including node attribution
and branch targets, then calculate branch displacements and relocations.

All slices, scratch and optional statistics are disjoint. Only `count`
records/pin bytes and `capacity` output elements are accessible. Their natural C
alignment is sufficient; no readable tail padding is required. Empty input
permits null pointers. Invalid arguments, insufficient capacity and blocks
above 256 records are rejected before input access and preserve output and
statistics. The caller retains the original order on rejection. No large block
is silently truncated or split into incorrectly independent fragments.

The stage consumes a complete block at a time. Its eight-record dependency
comparison and 16-height reduction widths are kernel widths, not semantic
batch boundaries. All final loads are masked by actual records or successor
bits. Inactive bits are zero, including the padding in fixed-stride graph rows.
The result and optional graph/statistics belong to the caller and remain valid
until the scratch is reused; the input is immutable throughout.

## Dependencies and priorities

The graph retains every original-order RAW, WAR and WAW dependency over the
semantics module's GPR, vector-register, flag and memory resources. Partial
register writes, zero idioms, implicit registers and addressing-register uses
come from those shared operand-resolved effects, rather than a scheduler copy
of instruction semantics. An unknown semantic form reads and writes everything.

The following records are full ordering barriers:

- Any record without measured GPR-result latency for its exact operand case.
- Memory accesses, including read/read pairs; calls, control transfers and
  unknown semantics; possible arithmetic or floating-point faults; MXCSR
  effects; decorations and caller pins.
- Flags consumers, whose result-path latency is unmeasured, and a compare or
  test immediately preceding a conditional branch.

Pinning both sides of an adjacent compare/test and conditional branch retains
their adjacency. This protects an existing fusion opportunity without asserting
that every preserved pair fuses on every encoding or layout. Memory barriers
also preserve potentially faulting and volatile accesses. There is no alias
speculation, movement across calls, division speculation, FP reassociation,
false-dependency rewrite or use of unmeasured pipe data.

Only GPR RAW edges contribute to weighted priority heights. A movable node's
priority is its measured GPR latency plus the largest priority among those
successors; a pinned node ends that path with priority zero. That zero is an
ordering priority, not a claim that its unknown latency is zero. Flags-only,
WAR and WAW edges constrain legality without receiving a GPR-latency weight.
The heights are relative priorities, not cycle-exact performance predictions.
The current conservative graph can retain redundant physical-register edges.

The vector implementation obtains effects in eight-record batches and model
latencies by the existing 32-ID in-register lookup. It compares each record's
resource masks against eight prior records together, producing dependency
bitsets. Reverse ready frontiers assign dependency levels; contiguous masked
height loads reduce the already completed successor levels. The scalar twin
uses scalar effects and costs, pairwise hazards and a backwards height pass.
Both select a ready instruction of greatest priority and resolve ties by its
original position. This final selection loop is shared and scalar; independent
blocks per SIMD lane and a before-allocation alternative remain experiments.

`tier1_schedule_verify(input, order)` independently reconstructs scalar
effects from the original records. It first checks that the order is a complete
permutation, then checks every required original-order pair, barrier and fusion
pair. It does not trust the graph or heights produced by either scheduler.

## Model coverage

`Tier1ScheduleStats` records the actual input count, pins, unknown operand cases,
missing counts for every model column, dependency edges, dependency levels and
positions moved. The tests compare those missing counts with
`x86_zen5_missing` on the independent input records. A recognized instruction
with unknown flag latency or pipes stays missing for those columns. The
existing measurement rows and their retained provenance are unchanged.

The Tier 1 selector carries the actual records' `x86_zen5_id` values and reports
coverage separately. These consumers do not turn a test generator into a
complete Tier 0/Tier 1 emitter manifest, and a nonempty manifest with missing
cases does not satisfy [#181](https://github.com/buster14a/buster/issues/181).
Full emitted operand coverage, flags/load increments, executed micro-ops,
pipes, and generated-program evidence remain required.

## Tests and measurements

`test-tier1-schedule [--seed N]` checks both graph matrices, GPR priority edges,
resolved effects, latencies, heights, levels, order and coverage. It covers
handwritten RAW/WAR/WAW cases, partial registers, explicit pins, faulting
instructions, unknown immediates, known L1 loads, compare/branch adjacency,
malformed permutations, rejected capacities and null/empty slices. Every
length from zero through 256 ends at guarded input, pin and output boundaries.
Random mixed and movable-register blocks exercise many legal topological
orders. These checks operate on machine records, not on a source-language
corpus.

`benchmark-tier1-schedule [--blocks N] [--records N] [--only
encode|scalar|vector] [--after-scalar MS]` freezes original machine-record
blocks, verifies both schedules and encodings, and reports 11-pass median
nanoseconds and available core cycles per record. It measures unscheduled
encoding beside the complete effects/model/graph/height/order construction,
record copy and encoding. It reports scratch storage, movement and missing
latency cases. The generator is outside timing, so this is a measured machine
stage boundary, not a source-to-code compiler benchmark.

The command also builds and executes an original 64-record integer loop in
both orders. Both encoders must agree, both programs must match an independent
integer reference for 64 inputs, code pages change from writable to executable,
and execution preserves SysV callee-saved registers. Alternating measurements
report bytes and runtime per loop iteration. It is a synthetic fixture with
one layout, not a representative runtime result or a reason to enable
scheduling. The external generated-code ABI call disables only Clang's
function-type metadata probe under UBSan.

Run each build, test and measurement separately through the shared 9700X lock;
pin measurements to CPU 2. For example:

```sh
EXCLUSIVE_OWNER=issue-172 ~/buster-fast-bench/exclusive.sh ./build/compiler test-tier1-schedule
EXCLUSIVE_OWNER=issue-172 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-tier1-schedule
EXCLUSIVE_OWNER=issue-172 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-tier1-schedule --only scalar
EXCLUSIVE_OWNER=issue-172 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-tier1-schedule --only vector
EXCLUSIVE_OWNER=issue-172 ~/buster-fast-bench/exclusive.sh taskset -c 2 ./build/compiler benchmark-tier1-schedule --only vector --after-scalar 200
```

Local focused Clang builds and sanitizer checks are development diagnostics.
They do not substitute for a controlled 9700X run. Final acceptance still needs
source-to-Tier-1 lowering and `speedtest1`/corpus executions, with scheduling
off, before allocation and after allocation, measuring compile cost as well as
runtime across repeated processes. That source/corpus path is absent from this
bounded work. Neither #172 nor #181 can be closed from these fixtures, and
missing measurements are not evidence that scheduling does not pay.

The implementation, tests, generator and benchmark helpers are original C
project contributions. No third-party scheduling implementation or timing
dataset is imported.

## Boundary audit #298

Audit pin: `42630114aeb0553c3e2fabfd7fe9bc84b0ce6126`;
[scope and evidence](simd-boundary-audit.md).
[The implemented scheduler](https://github.com/buster14a/buster/blob/42630114aeb0553c3e2fabfd7fe9bc84b0ce6126/src/buster/compiler/tier1_schedule.c#L4)
consumes physical-register `X86Instruction` records and caller pins. Effects
from `x86_instructions_effects` and operand-sensitive Zen 5 cost rows feed
hazard edges and weighted priorities; `tier1_schedule_block` produces a u16
`order[new] = old` permutation; the test/benchmark applies it and encodes/executes
records. `tier1_schedule_verify` is an independent scalar consumer. A general
C/IR emitter invoking this opt-in path is missing at the pin.

Retain the contract above: complete blocks of at most 256 records, disjoint
natural-aligned counted input/output and reusable 33,024-byte scratch, no
readable input padding, inactive graph bits zero. Rejection preserves outputs
and statistics and requires the caller to retain original order; do not split
an oversized block into arbitrary independent fragments. Internal eight-record
comparisons and height reductions are not scheduling independence boundaries.
Memory/fault/unknown/flags/call barriers and adjacent compare/branch constraints
preserve semantic order; missing latency is not zero cost.

Producer obligations are block boundaries, label/source pins and physical
operands after allocation. After the permutation, carry every attribution,
reference and target side table through the mapping **before** branch sizing
or encoding. A scheduler cannot reorder already resolved branch displacements
without updating them. Preallocation scheduling needs virtual-register pressure
and allocator feedback and is a separate missing edge, not this same API moved
earlier.

Disposition: keep opt-in conservative scheduling. Rebuilding effects and
hazards may be avoidable if an emitter retains them, but invalidation after form
choice/spills must be measured with construction/remapping/scheduling/encoding.
`test-tier1-schedule` and its native loop verify the current crossing. [#172](https://github.com/buster14a/buster/issues/172)/[#181](https://github.com/buster14a/buster/issues/181)
retain integrated source-corpus compile cost, runtime, unknown coverage and
code-size acceptance; synthetic weighted priorities are not cycle predictions
or proof that source programs improve.
