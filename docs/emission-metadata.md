# Emission metadata protocol — prerequisite for #1056

## Status and scope

`src/buster/compiler/emission_metadata.c` is a standalone, tier-neutral protocol
for source and frame events and their resolution against final record layout.
It is an implementation prerequisite, **not the completed emitted-function
redesign**. The C driver, `Tier0Code`, the Tier 1 frame adapter, the fixed-frame
unwind writer and the driver's Tier 1 `-g` restriction are unchanged.

The module neither produces machine code nor proves that a supplied unwind rule
matches that code. Its checker establishes bounded, ordered, canonical metadata
and valid layout references. Instruction effects, control-flow joins, saves,
restores and return-path correctness need a separate producer/consumer proof
before any of these events can replace the existing unwind publication path.
A balanced remember/restore stack is not such a proof.

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
`EmissionCheck` reports the failing part and first index; an unbalanced final
frame state reports `frame_count`. The scalar and vector layout checkers return
the same result, including the first malformed record.

The module does not allocate. The tests therefore establish capacity-failure
atomicity, not whole-driver arena-exhaustion rollback. A future adapter must
reserve all code and metadata storage before publication and restore its arena
marks on every failure.

## Scalar and vector paths

Layout validation has a contiguous 16-record AVX-512 path and an exact scalar
path. Masked loads cover only the valid tail; a shifted end vector checks each
record against its predecessor, including across batch boundaries. Include the
module after `host_isa.h` in a compiler translation unit. The standalone vector
test enables `BUSTER_HOST_VECTOR` explicitly.

Sparse event resolution and frame-state protocol checking currently use scalar
code. There is no locked-9700X measurement demonstrating whether further
vectorization pays, and this prerequisite makes no speedup claim. Integration
must measure the retained storage and total compile cost, including the layout
arrays, rather than treating this module's allocation-free implementation as
proof that the complete path is free.

## Reproducible correctness checks

Run from the repository root. The vector commands require an AVX-512F-capable
host. These commands are correctness tests, not performance measurements.

Baseline:

```sh
clang -std=c17 -O2 -g -march=x86-64 -Wall -Wextra -Werror -Wno-unused-function -fno-strict-aliasing -fwrapv -funsigned-char tests/emission-metadata/test.c -o /tmp/buster-emission-scalar && /tmp/buster-emission-scalar
```

Vector and scalar-twin comparisons:

```sh
clang -std=c17 -O2 -g -mavx512f -DBUSTER_HOST_VECTOR=1 -Wall -Wextra -Werror -Wno-unused-function -fno-strict-aliasing -fwrapv -funsigned-char tests/emission-metadata/test.c -o /tmp/buster-emission-vector && /tmp/buster-emission-vector
```

Address and undefined-behavior sanitizers, baseline:

```sh
clang -std=c17 -O1 -g -march=x86-64 -Wall -Wextra -Werror -Wno-unused-function -fno-strict-aliasing -fwrapv -funsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all tests/emission-metadata/test.c -o /tmp/buster-emission-scalar-sanitize && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/buster-emission-scalar-sanitize
```

The same sanitizers with the vector checker enabled:

```sh
clang -std=c17 -O1 -g -mavx512f -DBUSTER_HOST_VECTOR=1 -Wall -Wextra -Werror -Wno-unused-function -fno-strict-aliasing -fwrapv -funsigned-char -fsanitize=address,undefined -fno-sanitize-recover=all tests/emission-metadata/test.c -o /tmp/buster-emission-vector-sanitize && ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 /tmp/buster-emission-vector-sanitize
```

Each run performs 2,447,306 assertions. Coverage includes empty/null inputs,
leading/internal/trailing padding, zero-width records, changed branch widths,
generated origins, end boundaries, frame-rule vocabulary and nested state
stacks, signed and unsigned limit values, invalid metadata and capacity rollback.
Guard pages test exact input/output lengths and every layout count from zero
through 65. Seeded property tests compare 4,096 layouts against independently
calculated offsets and compare both implementation paths.

The fixtures describing frame pointers, early-return state and repeated probe
allocation changes are protocol fixtures. They do not execute generated code,
check a native unwind reader or demonstrate a working driver debug subset.

## Remaining implementation and acceptance

The following remain necessary before #1056 is complete:

- Introduce the common emitted-function record/reference/label/branch ownership
  result and adapt both producers, without requiring Tier 1 to publish a Tier 0
  frame. Keep failure classification coordinated with #1054.
- Publish real source provenance and frame events from the producers. Validate
  them against instruction effects and control flow, including joins, multiple
  returns, open frames, realignment, generated code and native probe sequences.
- Integrate final layout resolution with common debug/unwind consumers and the
  unwind writer. Preserve existing code/object/unwind output where required and
  verify changed frame shapes at instruction boundaries with independent tools.
- Enable and document the bounded Tier 1 `-g` subset, test full arena-failure
  rollback, and check scalar/vector and deterministic lane configurations.
- Measure no-debug compile cost and storage under the locked 9700X policy. New
  raw evidence and measurement harnesses belong in `buster-performance`, not in
  this repository.

Neither the full compiler build nor these acceptance requirements are replaced
by the standalone protocol test.
