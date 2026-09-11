# Canonical CFG publication

`ir_prepare_canonical_module` publishes `IrFunction.published_cfg` after
canonical transforms and their validation boundaries. `ir_function_publish_cfg`
is the only topology/edge-argument producer. It consumes validated input or an
explicit trusted producer/pass contract; the const publication pointer does not
extend an input semantic certificate to arbitrary later mutations.

Publication checks exact construction-list extents, tails, incoming order/types,
topology agreement and bounded IDs/counts. It proves instruction ownership while
building one instruction permutation, then publishes these immutable slices:

- Instructions occupy one contiguous span per block, in block ID order.
- Successor edges are grouped by source, retaining first terminator-target order.
- Predecessor slices contain edge indices sorted by source block ID.
- Parameters are grouped by block; argument `i` on an edge supplies parameter
  `i` of its destination.
- Variable operands, targets and immediates occupy shared pools. Already
  contiguous pools are reused, including shared promotion's operand pool.

Repeated conditional/switch/computed-goto destinations collapse to one edge.
Parameter-free destinations are included even if their builder omitted its
optional predecessor list. `ir_function_cfg_edge` searches a destination's
sorted predecessor slice without searching parameter incoming lists.
Construction transposes ordered builder incoming columns once, with linear
work in blocks, terminator targets, parameters and incoming values.

When instruction order changes, `instruction_remap` is the one explicit map
from prepublication IDs to published IDs. The row permutation is in place;
canonical source rows move with it, value definitions are remapped, and sparse
instruction extras are remapped and sorted again. Block, value, local, symbol
and label IDs remain stable. Computed-goto address identity and relocation
ownership therefore do not change. The declared entry remains independent of
numeric block order, and backend layout still emits it first where required.
The authoritative instruction row remains 64 bytes.

Native selectors, native canonical emission, Wasm, eBPF and LLVM consume the
same instruction spans and dense parameter/argument arrays. Both selectors'
private row-order reconstruction and their canonical edge reconstruction are
removed. Selected machine IR still owns its graph: AArch64 legalization can
split canonical blocks and i128 joins expand a value into two registers. That
is a target lowering result, not a duplicate canonical CFG.

Publication clears every per-instruction `next` and every builder
parameter/predecessor first/last pointer. The linked construction graph is no
longer retained as a second authoritative representation. Its arena allocations
become unreachable; arena high-water memory is reclaimed when the translation
unit is released, not by freeing individual nodes at publication. This cost
must be included in peak-memory measurements.

`ir_function_invalidate_cfg` explicitly reopens construction: it reconstructs
mutable links from the published slices, then clears the publication pointer.
Call it **before** mutating published rows or CFG/value identities, and reacquire
builder pointers afterward. Old construction-node pointers are stale. Builder
appends and shared-promotion entry perform this transition automatically.
Uncertified preparation validates the current representation; passing false is
not a replacement for reopening construction before a topology mutation.
Repeated unchanged preparation reuses its publication without reconstruction.

The canonical verifier reads either mutable construction lists or published
slices. Published shape checks verify dense coverage and topology agreement;
parameter type/label-provenance checks share the same semantic rules through
an indexed incoming-value view. Bootstrap tracing similarly reads published
slices after finalization and preserves its construction path beforehand.

`allocated_bytes` reports new persistent allocation performed by this
publication, excluding reused pools, original builder storage, arena padding
and temporary scratch. Pool counts and instruction-remap presence describe the
additional retained data. The existing optional `CFG_*` counters describe
publication work; `CFG_COPY_SOURCES` counts actual machine copy translation.
These counts are not compiler throughput results.

`ir_cfg_publication_tests` checks complete edge/argument streams for degrees
0/1/2/17/4096 and parameter widths 0/1/2/32/33, reversed builder order,
duplicate destinations, optional predecessor lists, invalid IDs,
truncated/overlong/cyclic lists and wrong tails. Instruction-span tests cover
interleaving, explicit remaps, source/extras/value-definition preservation,
nonzero entry identity, link discard and reconstruction. Existing native/mode,
Wasm, eBPF and bitcode tests cover downstream behavior. Performance acceptance
requires paired measurements and must account for finalization cost and peak
arena memory as well as saved backend traversal.
