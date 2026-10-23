# Independent body binding views

`c_check_body_bindings.c` consumes the real source-history handoff from
`check_input` for #113. It resolves a body's retained names independently,
using private binding state and the immutable file-position index. It keeps
declaration identities and source-order output slots across worker schedules.
Source declaration/type elaboration and semantic graph checking still run in
the existing sequential reader; this helper supplies the next binding consumer.

## Entry points and ownership

Before scheduling, call `c_body_bindings_spans` once for all source-order body
slots. Two position searches locate each interval; scalar/vector classification
counts declarations, uses and preceding file activations once across the unit.
Spans are independent jobs. Each stores the original event range, its undo-base,
use/declaration counts and source-order slot. `c_body_bindings_span` is a one-off
convenience that scans the preceding prefix for a single body.

The producer reactivates parameters and labels at the opening body. Preceding
file activations give the difference between original and private undo marks.
Exit restores may occur at the closing token or immediately afterward, or be
omitted when empty. A final translation-unit cleanup stays outside the slice.
The function's own file declaration at `OPEN_BODY` remains in the private view,
with its historical type identity; an earlier activation uses the file index.
Nested function definitions are rejected by the current validated parser.

Each worker calls `c_body_bindings_resolve` with private output/scratch arenas.
For the scalar path, initialize a private `CBindingReplay` once with the
environment's name capacity and the largest span event count. The replay
returns with empty heads after each body, so later bodies reuse the allocation
without clearing the process-wide name universe again. The batch path uses
the existing kernel and its bounded-walk fallback.

The private event view borrows `key`, `point`, `operation`, `kind` and `depth`.
Only `declaration` and `end` are materialized: restores subtract the undo-base,
and declaration-end indexes subtract the span's first event index. Ends at or
beyond the slice become unbounded within this view. A scalar
twin and masked SIMD translation implement exactly these operations. Restore
and use events' unspecified end entries are never read.

A use resolved locally maps its private activation handle to its original
source activation handle. An unresolved local use queries the file index
at that use's original position. The vector path flushes up to 16 such queries
together, including partial batches; original output destinations preserve
use-event order. A missing file declaration fails closed, because the
successful source reader had resolved all retained uses already. Handles map
through the source declaration and kind columns. Source-less builtin typedefs
retain a nonzero handle even though their declaration payload is `UINT32_MAX`;
the semantic consumer must supply their type instead of indexing that payload.
Source declarations borrow historical types through
`CheckEnvironment.declaration_types`.

## Memory and failure contract

Input columns are contiguous u32 or byte arrays of their logical event count;
no padding is required. A successful span must be used with its exact unchanged
environment. All borrowed arrays, file index and declaration types remain
alive until readers join. The caller keeps the span array and environment
stable too. Replay storage precedes the worker's scratch entry mark.

Output owns two u32 columns, `tokens` and `handles`, with exactly `count`
elements in original use-event order. Empty use sets permit null columns and
need no output storage. `retained_bytes` counts those columns and alignment.
`file_queries` records fallback demand; binding statistics include the private
view's declarations/uses, including parameter reactivations and the function's
own declaration. They count events rather than distinct source names.

Each call releases its temporary view and kernel results. Failure rolls back
new output, returning an empty result with `INVALID` or `RESOURCE` status.
The output and scratch arenas are distinct and private. Existing input/replay
storage may share their arenas below entry marks; new allocations must not
overlap it. Workers never append to shared bindings, change shared types or
publish linkage effects. Join before merging source-order result slots.

## Qualification boundary

`test-checker` compares the helper with the real whole-unit declaration column
through both parser/producer paths. Scalar and batch body resolution run in
reverse order on one/eight lanes with private replay and arenas. Fixtures cover
parameter/function-name shadowing, nested local scopes, a closed block
prototype followed by a file lookup, later prototypes and array bounds,
ordinary/tag namespaces, and macro-generated bodies. A 32-sibling-scope case
forces the existing batch kernel's bounded-walk fallback.

Guarded event columns cover lengths 0–129, nonzero slice offsets and undo
bases, partial vector tails and actual file fallback. Translation output
columns also end at guard pages. Whole-stream scalar replay supplies exact
activation-handle references. Synthetic traces cover both exit conventions,
omitted empty restores, translation-unit cleanup and source-less builtins.
Empty results, exhausted arenas,
undersized replay capacity, invalid slots and undo underflow are covered.

The [source-pipeline harness](measurements/c-body-bindings-2026-10-23/README.md)
includes the available producer, handoff and body-binding consumer on frozen
small/large source inputs. Its target-host results are pending. Independent
name resolution alone does not satisfy #113: complete top-level initializer
checking, private body types, source-order effects/output reconciliation and
actual deferred semantic/IR/corpus equivalence remain required.
