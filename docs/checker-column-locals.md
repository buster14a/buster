# Checker column locals

Issue #889 concerns repeated loads of the walks' state, not a change to the
checking algorithm. The host build keeps `-fno-strict-aliasing`. No new
`restrict` or disjointness assertion is introduced.

## Snapshot boundaries

- `check_input_resolve_range` takes the parser's immutable token count and
  token columns, the reader's fixed token-column bases, and its type-table and
  output pointers at entry. The source reader allocates those columns before
  resolution. Deferred readers bind their own columns before entering the
  walk; `check_deferred_cut` records a cut, not a new set of columns.
- `check_input_expressions` takes its token-column bases after classification.
  Region setup and column allocation have already completed. Appending graph
  nodes changes counts and contents, not these token-column allocations.
- `check_control_children` takes the fixed per-body column bases. Its label
  helper changes label contents/counts and may set the program's error; it
  cannot replace the columns.
- `check_program` takes completed input columns next to their consuming loops.
  Its control writer uses the scratch allocations already held in locals.
  Label counts are reacquired **after each** `check_control_build`, and a
  body's reference count is captured **after** `check_program_references`.
- `check_graph_run` and its region schedulers take immutable graph columns and
  already allocated stream-column bases before their node loops. The leaf
  writer receives those bases by value, so inlining it does not reintroduce
  stream-descriptor loads between stores.

Every snapshot borrows existing storage; there are no new allocations or
copies of whole reader/writer structs. Array **elements** still load normally:
a stable base is not a claim that its contents are immutable.

## Deliberately live state

Reader resource/report state, diagnostic cutoffs, bindings, scope depth,
graph append counts, writer counts, stream effect/report counts, and the type
table's interned-type count remain live. In particular, `overload_heads` is
allocated lazily during declaration resolution and is not an entry snapshot.
The leaf writer tests type validity at the node where it executes; earlier
scalar nodes may have interned types.

The traversal order, classification, diagnostic checks, scalar twins, stored
values and error publication are unchanged. This does not implement #529's
reader algorithms or #892's branch-removal proposals.

## Checks and performance acceptance

`test-checker` includes focused control-column tests across label allocation,
capacity exhaustion and writer reuse, plus empty and invalid-capacity graphs
on all three scheduler choices. Its existing type-order, graph-capacity,
source diagnostics, overload and deferred-reader cases exercise the mutable
state that must not be frozen.

Source changes alone are not a speedup measurement. Qualification for #889
also requires optimized assembly with the project flags, scalar/vector object
and diagnostic identity on the frozen corpora and c-testsuite, and the issue's
9700X IBS, stage-time and whole-compile comparisons across code placements.
Raw captures and their harnesses belong in `buster-performance`, not here.
