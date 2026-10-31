# Tier 1 machine-problem interchange

This is an independently usable prerequisite of [#938](https://github.com/buster14a/buster/issues/938),
not completion of its LLVM quality campaign. `tier1_problem.c` supplies a
versioned, bounded interchange and structural validator;
`tier1_problem_intervals.c` bridges a deliberately narrow subset into the
**existing** `Tier1AllocationInput`. It does not invent the proposed streaming
allocator's contract, replace the selector, or implement the independently
owned [#939](https://github.com/buster14a/buster/issues/939) certificate checker.

No LLVM exporter, real LLVM snapshot, greedy comparison, quality result,
preference replay, exact solver result or performance measurement is included.
The fixtures are original synthetic records and are marked as such. Successful
import, and even `tier1_allocation_verify`, are **not** certificate validation.
Nothing in this change publishes an allocation as a certified quality result.

## Storage and identities

A problem describes one function. Row indices are its stable normalized block,
instruction, value, register, class, operand and edge identities. Source IDs are
retained for diagnostics; they are not array indices and their uniqueness is not
assumed. The original MIR is an external artifact identified by a SHA-256.
Opaque operand payloads refer back to that artifact. The snapshot is an
allocation-problem view, not another executable IR or a lossless MIR replacement.

The wire encoding is explicitly little-endian u32 words, without C structs or
pointers. `tier1_problem_decode` borrows the supplied immutable bytes, including
its metadata and rectangular tables, without allocating or transposing them.
Native producers supply the same tables as native u32 arrays; `wire=false`
selects their representation. Table rows may be unaligned. There are no padding
or sentinel read requirements. `tier1_problem_encode` canonicalizes either view.

The caller owns every native table's complete declared storage. Structural
validation cannot establish the size of an arbitrary C allocation. The input,
its descriptor and all borrowed buffers must remain valid and immutable for the
whole operation. Source values and table indices are not interpreted as trusted
addresses.

### Header: 24 words / 96 bytes

| Word | Meaning |
| --- | --- |
| 0 | Magic `0x50414d42` (bytes `BMAP`) |
| 1 | Version 1; other versions refuse |
| 2 | Exact complete byte length; trailing bytes refuse |
| 3 | Flags: pre-PHI=1, greedy-frequency provenance=2, frozen order=4, synthetic=8 |
| 4 | Original operand count, before alternative-form operands |
| 5 | Metadata byte count including its final zero padding |
| 6..16 | Counts for the eleven tables below, in order |
| 17 | Alias-unit ID domain size |
| 18..23 | Reserved zero words |

The pre-PHI flag is mandatory. Unknown flags and nonzero reserved words refuse.
A flag states the producer's claim, not proof that the capture ran the claimed
passes. The exporter and campaign receipts must substantiate those claims.
Version 1 limits each table and the alias-unit domain to 2^20 rows/units, each
metadata string to 2^20 bytes, and the complete file to 1 GiB. No integer count
is silently truncated. Extensions requiring different semantics need a version
change, not an ignored field.

### Metadata: twelve length-prefixed byte strings

The order is LLVM revision, exporter revision, source SHA-256, MIR SHA-256,
target triple, target CPU, exact target features, exact pass options, ABI,
frequency provenance, workload, function. Each string has a u32 byte length
followed immediately by its bytes. Only the feature string may be empty. The
first two are 40 lowercase hexadecimal characters, the next two 64. Control
bytes and embedded NUL refuse; other bytes are retained without Unicode
normalization. The complete metadata region, not each string, is padded to four
bytes with at most three zero bytes.

The digest fields are syntax-checked, not authenticated or recomputed: the
external artifact/campaign reader must verify the actual files. A synthetic
fixture's repeated-letter digests identify no real LLVM revision. Frequency
provenance is mandatory even for synthetic or unavailable data. The
`greedy-frequency provenance` flag must remain clear unless an instrumented,
pinned exporter records the block/edge frequencies actually used by greedy.
Pre-allocation frequencies must not be relabeled as that evidence.

### Tables

Every entry is a u32; `NONE` is `UINT32_MAX`. Frequency pairs are the low then
high words of an unsigned 64-bit value. They are estimates in the producer's
stated scale, not observed execution counts. Version 1 contains no execution
counter table.

| Table | Words per row, in order |
| --- | --- |
| block (8) | source ID, instruction begin/count, outgoing-edge begin/count, frequency low/high, reserved flags=0 |
| edge (8) | source block, destination block, phi begin/count, frequency low/high, probability numerator/denominator |
| instruction (8) | source ID, owning block, LLVM opcode, original operand begin/count, flags, alternative-form begin/count |
| value (4) | source virtual-register ID, class, width in bits, flags |
| operand (10) | kind, referenced ID, phase, flags, class constraint, tied operand or NONE, width, LLVM subregister index, target flags, opaque payload index |
| register (8) | LLVM physical-register ID, bank, architectural encoding, width, alias-unit begin/count, flags, reserved=0 |
| class (5) | LLVM class/mask ID, bank, width, member begin/count |
| member (1) | register row index |
| unit (1) | alias-unit ID |
| phi (3) | incoming value, destination value, owning edge |
| form (4) | owning instruction, alternative LLVM opcode, alternative operand begin/count |

Bank IDs are GPR=0, vector=1, mask=2, other=3. Register flags are reserved=1,
scratch=2 and callee-saved=4; scratch must also be reserved. A class row may
also describe a call-clobber mask, with its members explicitly listing the
clobbered registers (not LLVM's complementary preserved-mask interpretation).
An empty member list is representable. Register alias lists are nonempty.
Members and each register's alias-unit list are sorted and unique; two
registers may intentionally share alias units. Register, value and class
widths are nonzero.

Operand kinds are virtual register=0, physical register=1, clobber mask=2 and
opaque=3. Their IDs address the value, register, class, or external MIR payload
respectively. Only virtual operands have a class constraint; the others use
NONE. Phases are early read=0, early write=1, late read=2, late write=3. Flags are
read=1, write=2, implicit=4, undef=8, early-clobber=16, kill=32, dead=64. Ties are
reciprocal global operand indices within the same original instruction or form.
Virtual and physical operands carry a nonzero width. The codec retains
subregisters, implicit flags, masks, ties and target flags even where the
current adapter refuses them.

Instruction flags reserve four defined indications: call=1, inline assembly=2,
unmodeled side effects=4, terminator=8. Value flags are external entry value=1
and rematerializable=2. These are information, not proof of a legal target
instruction, ABI entry state, or safe rematerialization recipe. The codec
checks structural references and flags, not LLVM opcode semantics or SSA
dominance. It never authorizes an opaque operand, alternative form or recipe
for execution merely because its record is well formed.

Every original instruction owns a contiguous operand slice. Alternative
operands follow **all** original operands. Blocks/instructions/edges/forms,
class members and register units use canonical CSR ownership: no holes,
overlapping slices, duplicate ownership or orphan rows. Every phi is owned by
its source edge. Probabilities have a nonzero denominator and numerator no
larger than it. Phi widths must match. Mutually consistent ownership is checked
in both directions. There is no silent function or constraint filtering.

## Adapter to the existing allocator

`tier1_problem_intervals` accepts one block with no edges, phis or alternative
forms, under exactly `x86_64-unknown-linux-gnu` and `sysv-x86_64`. It supports
64-bit GPR and 128-bit vector classes whose allocatable physical members have
disjoint alias units, unique valid encodings, and the exact class width.
Reserved and scratch registers are excluded; an active register sharing a
reserved register's alias units or bank/encoding refuses, even when its own
alias-unit list is disjoint. RSP is never allocatable. Target classes, not host AVX-512,
determine the available register masks.

Each value must have exactly one definition. Only explicit virtual-register
reads in phase 0 and definitions in phase 3 are accepted. No boundary values,
implicit/fixed operands, masks, subregisters, tied operands, undef/kill/dead
annotations, early clobbers, target-specific operand flags, opaque operands or
unmodeled instruction effects are silently erased. A differing per-use class
mask refuses rather than intersecting or unioning it away.

Positions are `4 * instruction + phase`. A value begins at its definition and
ends just past its last use, or just past the definition when dead. Required
positions include the definition itself, so an initial spill cannot masquerade
as a definition produced directly in memory. The input uses the existing
allocator's CSR columns, with stable normalized value IDs. It contains no
fabricated hints, rematerialization eligibility, fixed reservations or edge
moves. It does not allocate or rewrite instructions itself.

The adapter owns its output columns in the caller's arena and discards temporary
class/unit/cursor maps on success. Descriptor counts and table bases are stable
for the invocation; no `restrict` or strict-aliasing assumption is introduced.
Failure restores the entry length and leaves output and statistics untouched;
unused bytes may have been written, as with the existing arena APIs. The arena
prefix is preserved. Source/result/statistics and the arena descriptor cannot
overlap unused capacity. The three mutable descriptors must also be mutually
disjoint and disjoint from the source descriptor, tables and metadata. Source
storage in the existing arena prefix is allowed.

Statistics report original instruction/operand denominators and **arena**
retained and temporary bytes, not a complete compiler stack/RSS measurement.
The fixed 64-entry physical register map and other stack locals are outside
those arena counters. No timing or throughput claim is made. The codec and
adapter are unoptimized scalar reference/ingestion paths; SIMD implementation
and approved-host producer-to-consumer measurements remain outstanding.

## Rejections and publication

The reason enum names malformed metadata/layout/references, unsupported target,
CFG, phi, class, alias, phase, tie, implicit operand, subregister, form, boundary,
SSA, per-use mask changes and capacity exhaustion. `tier1_problem_tally` records
**one terminal outcome per attempted function**: functions = accepted +
rejected, and reason counts reconcile. This is a deterministic first-reason
histogram, not a count of every construct inside a rejected function. Call it
once after the final stage of a function; do not count decode and adaptation as
two independent functions. Decode failure is itself a reported function outcome.

`accepted` means accepted by the stage being tallied, never certified allocation.
`certificate-unavailable` is a distinct reason for a campaign whose prerequisite
checker is absent. No checker callback or success stub is supplied here. A
future quality publisher must invoke the real checker against original and
rewritten programs and refuse publication on absence or failure. Import and
occupancy checks cannot stand in for that gate.

## Tests and reproduction

Run from the repository root. These commands are correctness tests, not
performance measurements; use the approved shared host lock for target runs.

```sh
clang -std=c17 -O2 -g -march=x86-64 -Wall -Wextra -Werror -Wno-unused-function -Wno-psabi -fno-strict-aliasing -fwrapv -funsigned-char -pthread -DBUSTER_TP_INTEGRATION tests/tier1-problem/test.c -o /tmp/tier1-problem && /tmp/tier1-problem
```

Without `-DBUSTER_TP_INTEGRATION`, only the standalone codec is tested. With it,
the test includes the real arena, real `tier1_allocate`, scalar twin and actual
input validator, never substitute implementations. The focused hosted workflow
runs both baseline/native builds, ASan/UBSan, and seeded libFuzzer tests. These
are separate from the repository's required full qualification.

Fixtures cover native/wire round trips, canonical byte identity, all truncated
prefixes, every input alignment modulo 64, guard pages on input, destination
and arena tails, deterministic mutations, CSR/reference errors, GPR and vector
class membership, phase/tie/alias/mask retention, refusals by reason,
capacity rollback, unchanged publication and the current allocator bridge.
The allocator smoke checks occupancy only; it is not #939 acceptance.
`seed.c` emits explicitly synthetic straight/CFG/form seeds; `fuzz.c` verifies
that every successfully decoded mutation canonically re-encodes byte for byte.

## Remaining #938 acceptance

The next work needs a pinned LLVM exporter that extracts target constraints
from LLVM's machine APIs (not inferred assembly annotations), a documented
streaming-machine-view integration as #140 lands, and the real #939 checker.
The frozen problem must fix the common order and disable subsequent scheduling;
the complete-pipeline track must charge both sides' preparation, SSA work,
allocation, preservation, move resolution and rewriting. Merely replaying an
interval assignment is not either complete track.

Quality records still need exact edit origins, block/edge ownership, folded
forms, callee-save/frame/code-size consequences, separate estimated/observed
counts, policy-specific preference replay and per-function regressions.
Throughput still needs input-to-rewritten-record timing, median/tails and full
memory accounting. Exact-model optimum/incumbent/timeout are not implemented.
Real SQLite/frozen-corpus captures, results, measurement harnesses and raw logs
belong in [buster-performance](https://github.com/buster14a/buster-performance),
with immutable reviewed pins. No synthetic result closes those gates.

## Provenance

These source files, tests and schema documentation are original project work
under Apache-2.0 WITH LLVM-exception. No LLVM implementation, generated target
metadata, upstream MIR fixture or corpus input is imported. The synthetic rows
are deliberately not represented as actual LLVM captures. Future exporters or
incorporated captures must record their own exact input/toolchain provenance
and applicable licenses.
