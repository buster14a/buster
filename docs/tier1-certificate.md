# Tier 1 allocation certificates

This is the checker core for [#939](https://github.com/buster14a/buster/issues/939),
not a claim that the current native emitter has a complete machine-code
certificate producer. There are two deliberately different entry points:

- `tier1_allocation_certificate_check` checks the interval allocator's existing
  fragment assignment. It cannot prove source uses, CFG edges or actual emitted
  spill/reload instructions, which that interface does not contain.
- `tier1_certificate_check` proves a normalized source/machine-stream
  correspondence using block-entry assertions and actual sequential edge edits.
  `tier1_certificate_oracle` independently infers byte/value facts to a CFG
  fixpoint and cross-checks small cases.

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

This module does **not** decode x86 bytes or derive these target facts from
`X86Instruction`. It must not be advertised as verifying native output until
that independent bridge is implemented. An allocator-provided opcode label,
effect label, location layout, recipe or swap annotation is not such a bridge.

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
may establish any number of register and stack homes.

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

A fact with location `BUSTER_CERT_NONE` says only that a source-certified recipe
is available. It never gives a register contents. A register-required use
needs a checked materializing instruction first. Frame-address recipes also
require the correct currently available base value and the source's justified
stability premise.

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
Exceeding those bounds returns `TIER1_CERT_CAPACITY`, not success. General
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
`24*units + 16*values + 5*blocks + 96` bytes with the current record layouts.
Checking costs linear work in the bounded declarations, source instructions/
operands, rewritten edits, certificate assertions, edge/rename obligations and
byte-unit updates. Width is bounded by 64 bytes. There are no per-store scans
of stack homes, per-block clears of the value domain or per-edge state copies.

The fragment adapter includes `tier1_allocate.c`'s input contract and independently
sweeps start/end events. Ends precede starts at a position. Fixed reservations
are reference-counted, allowing overlapping fixed intervals without allowing
an allocated register to overlap them. Required uses are checked with a
monotonic fragment cursor. Its cost is
`O(positions + intervals + fragments + uses + fixed-register updates)`;
numeric position buckets are explicit in its report and are not called
machine instructions. `Tier1IntervalReport` separately reports existing
fragment-certificate size and checker scratch. Existing allocator preparation/
publication costs remain part of allocator measurements.

This is the scalar correctness implementation. No SIMD speedup or target-host
throughput result is claimed. The contiguous byte updates and bounded record
validation are candidates for measurement-driven vectorization; target-host
evidence is still required before selecting a production fast-path policy.

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
stack reuse, stale definition incarnations, legal exchanges, damaged XORs and
missing materializations. There are 1,000 generated valid move sequences and
1,000 independently checked mutations, plus structural/capacity tests.

The stream suite can also run without the compiler's unity build:

```sh
clang -std=c17 -O1 -g -Wall -Wextra -Werror -fno-strict-aliasing -fsanitize=address,undefined -DBUSTER_CERTIFICATE_STANDALONE src/buster/compiler/tier1_certificate_test.c -o /tmp/buster-certificate-test && /tmp/buster-certificate-test
```

```sh
clang -std=c17 -O3 -Wall -Wextra -Werror -fno-strict-aliasing -fwrapv -funsigned-char -DBUSTER_CERTIFICATE_STANDALONE src/buster/compiler/tier1_certificate_test.c -o /tmp/buster-certificate-test && /tmp/buster-certificate-test
```

Initial local standalone qualification: **2,064 stream checks, zero failures**
in both optimized and address/undefined-sanitized builds. The 16 fragment
mutations also passed both builds in an isolated shim using the fetched
allocator input contract and original quadratic oracle. That shim result is
not a full compiler build or an approved-host measurement. The PR's CI is the
integration qualification.

## Remaining #939 acceptance

This change deliberately does not close #939. Remaining work is:

1. Independently bridge the real selector/native emitted records to this
   source/stream contract, and have the allocator publish entry assertions as
   its machine-view/CFG producer lands. Do not synthesize a passing emitted
   stream from fragment assignments.
2. Connect #938's snapshot/quality harness to the applicable checker. Interval
   validation alone must not be labeled machine-code correctness certification.
3. Qualify the integrated compiler and measure preparation, assertion
   publication and check-only cost per actual source instruction on the locked
   Ryzen 7 9700X, including certificate sizes, edits, edges, units and memory.
   Keep raw logs, measurement harnesses and campaign artifacts in
   `buster14a/buster-performance`, and link the pinned report here.

Unsupported forms, missing producer evidence and unavailable target-host
measurements are outstanding work, not passing acceptance gates.
