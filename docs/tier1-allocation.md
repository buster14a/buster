# Tier 1 interval allocation and rematerialization

`tier1_allocate.c` implements the interval assignment and move-resolution part
of [#140](https://github.com/buster14a/buster/issues/140).
`tier1_rematerialize.c` implements the eligibility column and flags-safe copies
for [#178](https://github.com/buster14a/buster/issues/178). These are original
implementations. Wimmer and Franz's
[Linear Scan Register Allocation on SSA Form](https://doi.org/10.1145/1772954.1772979)
is the algorithmic reference; no implementation or external fixture corpus is
imported.

The allocation interface is a view of machine lifetimes. It neither defines
another IR nor changes the concurrently owned IR/SSA implementations. The
selector must publish its effective uses **after** folding: folding an address
or shift into a later instruction extends the original operands' lifetimes.
Allocating the unfused uses and then folding is unsound.

## Producer and consumer

`Tier1AllocationInput` borrows contiguous columns for `count` input intervals:

| Column | Width | Meaning |
| --- | --- | --- |
| `starts`, `ends` | u32 | Half-open machine lifetime `[start,end)` |
| `values` | u32 | Stable value id, below `value_count` |
| `classes` | u8 | GPR=0, vector=1 |
| `allowed_registers` | u32 | Allowed physical-register bit mask |
| `flags` | optional u8 | `REQUIRED` pins the whole interval in a register |
| `hints` | optional u8 | Preferred allowed register, or `UINT8_MAX` |
| `rematerializable` | optional u8 | Certified 0/1 eligibility from selection |
| `use_offsets` | optional u32 | `count+1` CSR offsets into use columns |
| `use_positions` | optional u32 | Nondecreasing positions inside each range |
| `use_required` | optional u8 | 1 requires a register at that use; 0 permits memory |

`registers[2]` contains the allocatable physical masks. GPR bits are 0–15;
vector bits are 0–31. The caller restricts the vector mask to the generated-code
target, independently of the host's AVX-512 capability. The caller excludes
RSP, reserved scratch registers, and frame bases selected by the frame planner.
In particular, it applies the frame planner's RBP/R13 reservation before
allocation. Masks of zero are valid when memory operands are permitted.

The selector/liveness adapter uses input position `2*i` and definition position
`2*i+1` for node `i`. Ending a source interval at the output phase permits a
destination to reuse a dying source register. Positions are simultaneous
phases: all required uses at one position are pinned before allocation makes
any eviction decision. Too many simultaneous required operands return
`TIER1_ALLOCATION_CONSTRAINT`, not a memory operand masquerading as a register.

Distinct input intervals of one value may describe lifetime holes or machine
constraint splits. They must not overlap or change register class. Assignment
can change between such intervals; the producer must supply the required
control-flow transitions. Extending ranges over loops and choosing a
loop-contiguous order are the liveness/block-order producer's responsibilities.
The allocator does not infer either from numeric instruction ids.

`Tier1FixedIntervals` supplies half-open reservations/clobbers with physical
masks and classes. For example, a call phase reserves its caller-saved masks;
values crossing it can then use only unreserved registers or memory. An
instruction's fixed operand uses a one-bit `allowed_registers` mask, with
`REQUIRED` when needed. A fixed reservation describes an *other* use/clobber;
the producer does not reserve that same register against the operand itself.
The current implementation removes intersecting reservations for the whole
input interval. Producers may split around constraints to recover registers.
It does not yet search for optimal split positions around fixed ranges.

Fixed filtering reuses the position-sorted end events and the dead event buckets.
It sweeps fixed starts strictly before each value interval's end and keeps the
maximum fixed end for each of the two classes' 32 physical registers. Two
16-register vector comparisons identify the reservations whose maximum end is
strictly greater than the value interval's start. This preserves half-open
boundaries and removes the same union of masks from the entire interval,
including unsorted, nested and duplicate fixed ranges and multibit clobbers.

Preparation costs O(positions + fixed ranges + events), including required-use
events, with four temporary bytes per fixed range and a 256-byte register table.
The existing sixteen-interval vector cross-product handles pair counts no larger
than `positions + 16*(intervals + fixed ranges)`. This keeps tiny functions and
small fixed sets on the simple path while bounding its work linearly as well.
The scalar allocator keeps its independent cross-product reference. Empty fixed
sets allocate no additional scratch.

The current native caller receives its input from `tier1_liveness_build`, whose
allocation initializer leaves `fixed` empty. `tier1_emit_prepare` copies that
input and adds allowed masks and required uses; calls save and restore live
caller registers separately. Thus this change addresses the allocator API's
fixed-range scaling; it does not establish a speedup in the current native
pipeline or the default Tier 0 compiler.

`tier1_allocate` and `tier1_allocate_scalar` return an explicit status and an
arena-owned `Tier1Allocation`. Each input interval owns the compact range
`fragment_offsets[i] .. fragment_offsets[i+1]` in the output's start/end,
location-kind and register columns. Fragments cover the input lifetime
exactly. A fragment is REGISTER, SPILL, or REMATERIALIZE. Nonregister fragments
have register `UINT8_MAX`.

`value_registers[value]` is a convenient selector view: it is a physical
register only when the value's fragments all stay in one register without
splitting; otherwise it is `UINT8_MAX`. The initial leaf selector rejects
values without that uniform view. It does not silently ignore spill plans.
`tier1_allocation_location` queries the precise location for a machine position.

Actual spill values have dense `spill_ids[value]` and the inverse compact
`spill_values[spill_count]`. Values requiring no slot have id `UINT32_MAX`.
`Tier1FrameSlots.indices` can borrow `spill_values` alongside the producer's
value-indexed size, alignment and conservative lifetime columns. Rematerialized
values allocate no spill slot unless another interval of the same value really
spills. Output statistics count vector and scalar intervals, transitions,
maximum pressure, retained arena bytes and peak temporary bytes.

## Assignment

Both paths validate and count-sort position events. End events precede starts
at the same position. The common vector path applies to a class whose ranges
all have its full register mask and whose maximum pressure fits. An AVX-512
inclusive prefix sum of event deltas gives pressure/depth. Ends open reuse
chains at that depth, and starts consume them. Fresh roots take distinct
physical registers; simultaneous pointer-jumping rounds resolve inherited
registers. This uses at most maximum pressure registers, including arbitrary
input order and noncontiguous physical masks.

The counting-sort placement and depth matching currently use scalar indexed
stores. Pointer jumping uses scalar indirect loads into contiguous lane
buffers, then vector change detection and publication, avoiding Zen 5's costly
gather. This is a hybrid implementation, not a claim that the entire matching
step implements the proposed nearest-smaller-values vector kernel. The whole
preparation/assignment benchmark charges these phases and their scratch space.

Constrained or overfull classes defer to a scalar scan bounded by their
physical-register count. It honors available-register hints, then evicts an
unpinned active interval whose next use is farther away. Without use columns,
the end position is the conservative next-use estimate. A value without a
register gets a spill/rematerialization fragment. An eviction closes its
register prefix. With `use_required`, it reenters a register at its next
required use, potentially evicting another value, and can be split again.
The output capacity is bounded by `2*intervals + 2*uses`; the simple path needs
at most `2*intervals`. Dense spill ids are assigned in input-interval order.

This is not yet the complete Wimmer/Franz implementation: it lacks inactive
interval lookahead across holes, loop-aware split placement, vector spill
region selection/recoloring, and automatic machine-constraint interval
construction. Input holes are independent ranges, and the scalar twin is a
validity oracle, not a proof of optimum constrained coloring.

## Transitions and parallel moves

A register-to-spill boundary requires a store before the register is reused.
A spill-to-register boundary requires a reload; a rematerialization-to-register
boundary requires a fresh copy of its certified definition. An initial spill
requires producing the definition in an appropriate temporary and storing it.
At a simultaneous transition phase, stores precede reloads/copies that reuse
their source registers. Required-use fragments make these transitions explicit;
the allocator does not emit machine code for them.

`tier1_resolve_moves` sequentializes a same-class, same-width parallel assignment.
The locations are opaque caller ids and must not alias. Destinations must be
unique. It emits assignments whose destination is no longer an outstanding
source, and breaks a cycle by saving one old destination in a caller-reserved
scratch location. Identity moves disappear. The scratch must not appear in
the input. Registers and stack slots may use disjoint id ranges, but a machine
consumer still needs a legal lowering for a memory-to-memory copy. CFG edge
placement and critical-edge splitting are not done by this routine.

## Rematerialization

`Tier1RematerializationInput` is a borrowed column view of selection facts,
not an alternate value representation. The scalar and vector mark producers
accept only:

- Integer constants of width 4 or 8, with width-4 payloads normalized to u32.
- Symbol addresses explicitly certified stable for the function. TLS and
  dynamic binding recipes are not implicitly treated as ordinary addresses.
- Final signed-i32 frame displacements from a frame base explicitly certified
  stable at every copy position. A changing RSP is not stable merely because
  the original operation names a stack slot.

An operation-table lookup classifies 64 candidate bytes at a time. Sparse
payload checks confirm width, stability, displacement range and optional
comparable measured costs. Unknown costs prefer recomputation. Known
rematerialization costs greater than the reload cost suppress the mark.
The current Zen 5 model does not automatically populate these optional costs.

`tier1_rematerialize` emits one baseline x86 record. Zero uses `xor r32,r32`
only when the caller reports no live flags; otherwise it uses `mov r32,0`.
Other constants use a u32 move, sign-extended imm32 move, or imm64 move as
needed. Stack addresses use LEA from the final stable base. Each symbolic LEA
has its own encoder fixup mark plus returned symbol and addend; the consumer
must publish a PC32 fixup for every copy through `link_add_code_fixups`.
This module does not silently drop fixups or reuse the original copy's offset.

## Memory and failure contract

Columns need no padding or alignment beyond their element types. Logical
counts authorize exactly those elements; all vector tails use masked loads
and stores. Inputs are immutable and disjoint from the destination arena's
unused capacity. The result and its live allocations may precede a later call
in the same arena. Scratch is returned on success; errors restore the entry
arena length and preserve the result object. Limits are 2^20 intervals/values
and 2^24 positions/uses; larger requests fail before reading the columns.
Rematerialization recipes have the same 2^20-value limit, checked by both
marking paths and the individual copy builder before reading any column.
The mark output has `count` writable bytes and must be disjoint from every
borrowed recipe column; the individual copy result must also be disjoint.
Empty intervals are invalid; an empty function is valid. Optional absent use
columns require zero use count. Capacity failure is distinct from invalid
input and impossible register constraints.

## Validation and measurements

`test-tier1-allocation [--seed N] [--count N]` checks arbitrary input order,
both classes including vector register 31, pressure boundaries, required
operands, fixed clobbers, hints, deterministic output, repeated reload/resplit,
compact spill identity, malformed/overflow inputs and arena rollback. One
oracle compares interval fragments pairwise; another expands them into a
per-position register occupancy map. A move oracle executes sequential output
against an independent snapshot of the parallel assignment. Guard-page tests
end every input column at an inaccessible page through 65 elements.
Fixed filtering also compares exact masks with an independent per-position
bitmap, compares complete allocations with explicitly prefiltered inputs, and
exercises indexed capacity rollback after an existing arena prefix. The 8,192
nonoverlapping fixed/value interval fixture checks every resulting register
without invoking the deliberately quadratic allocation verifier.

Original native straight-line reductions execute the assigned register/spill
plans through the existing encoder with rematerialization enabled and disabled,
using both allocation paths. Results must match an independent integer sum.
They exercise actual spill stores, memory reads, repeated register-required
reload/resplit and recomputed constants. Additional native fixtures cover the
constant encoding boundaries, live flags, stable stack addresses and a resolved
PC-relative symbol copy. Repeated reload/resplit also has a location-state simulation.
These bounded tests are not the #142 C-program differential corpus.

`benchmark-tier1-allocation [--only vector|scalar] [--after-scalar MS]` times
complete interval validation/preparation, assignment, output publication and
arena reset. It reports nanoseconds and available hardware cycles per interval,
sample ranges, fallback intervals, spill slots and peak arena bytes. Both plans
are verified before timing. Input generation and source/IR liveness are outside
this benchmark. Default samples alternate variant order; isolated and
scalar-conditioned modes expose different host power states. Run repeated
processes through the per-run 9700X lock, pinned to CPU 2, before retaining a
performance claim. No performance acceptance is asserted by adding the tool.

Both issues remain open until source-corpus interval production and native
spill/reload/phi/call lowering are integrated and qualified. Required remaining
evidence includes SQLite and the slice's tests, #142, per-function assignment
validity on those corpora, spill/frame counts with and without rematerialization,
and controlled 9700X compile/runtime comparisons against Tier 0 and Clang -O2.
