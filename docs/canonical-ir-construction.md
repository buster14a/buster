# Canonical IR block-row construction protocol

This contract complements [validation boundaries](ir-validation-boundaries.md)
and [CFG publication](canonical-cfg-publication.md). It turns one family of
canonical invariants from properties that a whole-module scan discovers after
construction into properties that construction cannot violate. The canonical
validator stays the independent oracle for the same family.

## The family

For every lowered function:

1. **Ownership.** Every row belongs to exactly one block chain. Chains are
   acyclic, every chain id is in range, and `last_instruction` is the chain's
   tail.
2. **Termination.** No row follows a terminator, and every block ends in one.
   `IrBlock.terminated` agrees with the tail.
3. **Result binding.** A row's result value names exactly that row as its
   `definition` and has the row's type. A value has one definition: a row or
   one block parameter, never both.
4. **Reference range.** Operands and targets name values and blocks that
   already exist when the row is committed.

`ir_instruction_is_terminator` is the one terminator predicate. The commit,
CFG publication and the validator all use it: `BRANCH`, `BRANCH_IF`, `SWITCH`,
`INDIRECT_BRANCH`, `RETURN`, `UNREACHABLE`, and inline assembly with targets.

## Block states and operations

A block is **open** until a terminator is committed to it and **closed**
afterwards. `IrBlock.terminated` is that state. Only the operations below write
it, so producers never set it themselves.

| Operation | Preconditions (a refusal changes nothing) | Effect |
| --- | --- | --- |
| `ir_block_append_instruction`; `ir_block_commit_trusted` is the inline fast path for a fresh, unpublished function | Block exists and is open. Every nonzero count has storage. Operands are `< value_count`, targets are `< block_count`. The result is `INVALID`, or an existing value with no definition and the row's type. | Appends the row as the tail and binds its result. A terminator closes the block. |
| `ir_block_insert_instruction_after` | Row checks as above. Not a terminator. `after` is `INVALID` (head) or an existing non-terminator row. Caller obligation: `after` belongs to `block`. | Links the row mid-chain and binds its result. A closed block keeps its terminator last. |
| `ir_block_retract_tail` | The row is the function's newest row and the block's tail. `previous` is its chain predecessor. The block is open, or closed by exactly that row as an edge-less `UNREACHABLE`. | Unlinks the row, unbinds its result and reopens the block. |
| `ir_block_truncate_after` | Block open. The rows after `keep` in its chain are exactly the function's newest rows, in ascending order, and none is a terminator. | Removes them and unbinds their results. |
| `ir_function_first_open_block` | None. | Explicit finalization: the first open block, or `INVALID`. O(blocks). |

Checks cost O(operands + targets) per row, over data the producer already holds.
Truncation walks only the suffix it removes. Nothing here scans the function.

### Why the preconditions establish the invariants

Consider a function in which only these operations and whole-function remaps
change chains, `terminated` and definitions. The invariants hold by induction.

- **Append.** The new id `n` is fresh. It becomes the tail of one open chain,
  whose tail's `next` was `INVALID`. Ownership stays a partition. The block had
  no terminator, so the chain's only terminator, if any, is `n` at its end.
  The result was unbound, so after binding it has exactly one defining row of
  its type. Blocks and values are never renumbered while construction is open,
  so a reference that is in range at commit time stays in range.
- **Insertion.** Inserting a non-terminator ahead of or between
  non-terminators leaves every chain's terminator last.
- **Retraction** removes the newest row from its own tail. No other chain can
  reach that row, and its result loses its only definition. `UNREACHABLE` has
  no successors, so no edge, predecessor list or block argument refers to it.
  Reopening its block therefore invalidates nothing else. Every other
  terminator is final.
- **Truncation.** A suffix whose ids are exactly `keep + 1 .. count - 1` in
  ascending order contains every row the truncation frees. No other chain holds
  one of them.
- **Finalization.** "Every block closed" is the only property of the family
  that a commit cannot establish, because it depends on the whole function
  being lowered. `c_ir_finish_construction` checks it once per function.

Whole-function rewrites keep the family by construction of their maps:
direct-SSA value compaction, local promotion's `ir_rewrite_compact`, FAST
compaction, publication and `ir_function_invalidate_cfg`. The validator and
publication (below) check their output.

## Producers

- **C frontend (`c_gen.c`).** Every row goes through `c_ir_append_instruction`.
  That funnel commits the row, turns a refusal into a sticky
  `construction_refused` flag with a message, and never writes links,
  definitions or `terminated` itself. The seven hand-written
  `terminated = true` stores (`c_ir_terminate` and six direct terminator sites)
  and all 69 hand-written definition stores (63 back-patch statements, five
  inline assignments and one in SSA restoration) are gone. The two place-recovery retractions use `ir_block_retract_tail` and
  `ir_block_truncate_after`. Direct-SSA memory restoration uses
  `ir_block_insert_instruction_after`. `c_ir_finish_construction` runs after SSA
  finish: a refused row or an open block rejects the function with a structured
  diagnostic, so the module is never certified.
- **Tail after a `noreturn` operand.** A noreturn call or
  `__builtin_unreachable` inside a larger expression closes its block with an
  `UNREACHABLE` marker, and the rest of the expression still emits rows. Before
  this protocol those rows were committed after the terminator on the certified
  default path. `c_ir_reopen_unreachable_marker` retracts the marker while it is
  still the newest row, so the tail follows the call in the same block, where
  its operands still dominate it. A disconnected continuation block would
  satisfy the canonical validator but fail selected-MIR dominance
  verification. SSA events and reads recorded after the marker move to its
  position (`c_ir_ssa_follow_retracted_row`). Statements after a closed block
  are skipped before they emit anything, so functions without such a tail lower
  byte-for-byte as before.
- **Hand-built IR (tests, fixtures).** This IR still uses raw
  `ir_function_add_instruction` and writes links itself, so it can represent
  every invalid state of the family. That is deliberate: negative oracle tests
  need it.

## Checks at each boundary

| Boundary | Family checks | When |
| --- | --- | --- |
| Commit and edit primitives | Preconditions above | Every build, every row the frontend emits |
| Frontend finalization | Refused row, open block | Every build, once per function |
| `ir_function_publish_cfg` | Each tail is a terminator. No terminator has a successor. Ownership (existing permutation walk). | Every preparation, every producer, including optimized production and certified input |
| `ir_validate_canonical_module` | The whole family, independently. It now also rejects a value defined by both a row and a block parameter. | Unchanged boundaries: uncertified input, transform outputs in debug/test/sanitizer builds, `-fverify-codegen`, Wasm/eBPF/LLVM entry points |

Publication reports `UNTERMINATED_BLOCK` for an empty block, where it used to
report `INVALID_ID`. It also rejects a non-empty block whose tail is not a
terminator; before this protocol it published such a block as one that falls
off its end. Both checks read the row that publication already loads, which is
one predicate per block and per row.

## Counters

`BUSTER_BENCH_ALLOCATIONS` builds report these counters under `ir_construction`
in `-fsource-metrics`:

- `commit_checks`, `commit_refusals`, `commit_operand_checks`,
  `commit_target_checks`
- `commit_result_binds`, `commit_closes`, `commit_insertions`,
  `commit_retractions`, `commit_truncated_rows`
- `commit_reopened_markers`, `commit_finalized_blocks`
- `cfg_terminator_checks`

They count exact events, not time. Compare them with the existing
`validation_*` counters to see what the oracle scans for the same family.
