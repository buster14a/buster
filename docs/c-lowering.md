# Selecting definitions for lowering

`c_lowering.c` supplies the emission planner for #129, part of parser tracker
#126. It consumes resolved definitions and references, finds the definitions
reachable from the unit's roots, and returns them in source order. It has a
vector path and an independent scalar worklist. Neither path recurses.

This is a graph consumer. The source checker must supply the complete graph
before this planner can select actual C bodies and initializers. The current
source checker work in #133/#312 does not yet supply complete body references,
inline-emission decisions, or the complete checker-to-Tier-0 bridge. Graph
tests and manually modeled C fixtures do not establish source-corpus lowering
or time saved compiling `main.c`. Issue #129 remains open for that integration.

## Producer and consumer

The semantic producer numbers definitions in source order after resolving
redeclarations and tentative definitions. One definition identity represents
one possible emitted function body or object, even when several declarations
refer to it. Its flags describe the already resolved emission policy; this
module does not reinterpret storage-class tokens or look up identifier names.

Roots include externally emitted definitions and definitions retained by
`used`, `constructor`, `destructor`, or an alias. An alias-target root means
the target body or object; publishing the alias symbol remains the link
producer's responsibility. Plain C99 inline-only definitions have no local
out-of-line emission. Their calls and addresses refer to the external symbol,
and their bodies do not add local dependencies merely by existing. The
producer must account for all declarations of a function and GNU inline
semantics before setting the flags. Clang documents the distinction in its
[C99 inline compatibility guide](https://clang.llvm.org/compatibility.html#inline)
and [language-mode guide](https://clang.llvm.org/docs/UsersManual.html#differences-between-various-standard-modes).

References record dependencies of emitted bodies and initializers. Local
evaluated references can reach another definition; unevaluated references and
references resolved to an external symbol do not. A blanket rule that drops
everything written inside `sizeof` would be wrong: evaluated variable-length
array bounds still contribute dependencies. The checker also selects
`_Generic` associations, handles constant-folded expressions, and resolves
calls and addresses before producing these columns. Static objects reachable
only from an unreferenced function remain unselected.

The output is a reached bitset and a compact array of selected definition
IDs in source order. A downstream function producer maps those IDs to checked
bodies for Tier 0 or IR lowering; an object producer maps them to checked
initializers and link definitions. Discovery order never becomes emission
order. Diagnostic checking is independent: default compilation still checks
every body, as #155 requires.
Checked inline-only bodies must also remain available to a future Tier 1
inliner. If inlining introduces local emission dependencies, that producer
must publish them; this out-of-line selection is not a list of checked bodies
that may be discarded.

## Graph preparation and traversal

`c_lowering_prepare` validates the bounded input columns and builds a stable
counting-sort adjacency index. Preparation belongs to the cost of using the
planner. The caller owns the arena and the prepared workspace; selections
reuse that workspace. No input column needs readable padding. Empty inputs
and partial vector batches are valid, and allocation failure is explicit.

`CLoweringInput` contains `definition_count` flags and `reference_count`
entries in each of the source, target and kind columns. All four columns use
`uint32_t` elements with their natural alignment. Source and local target IDs
are below `definition_count`; an external or unevaluated target may instead be
`BUSTER_C_LOWERING_NONE`. Neither count may be `UINT32_MAX`. Zero-length
columns may be null. Preparation copies the retained information, so the
producer can release its input columns after success. Failure preserves both
the arena length and the previous result.

The prepared `CLowering` owns a flags column, `definition_count + 1` adjacency
offsets, retained targets, a queue, a scalar visited column, the reached bitset
and selected IDs. Each solver reuses these buffers without allocating. The
bitset has `ceil(definition_count / 64)` words, with high unused bits zero;
only the first `selected_count` output IDs are valid. A workspace belongs to
one worker and must outlive every consumer of its selected IDs. The reported
`workspace_bytes` includes both paths' reusable storage and alignment;
`scratch_bytes` identifies its mutable traversal/output portion. These are
allocated capacities, not a count of bytes touched by every invocation.

`c_lowering_vector` keeps a compact frontier and a visited bitset. It batches
target membership checks and compacts the final selection in source order.
Sparse bitset writes serialize conflicts so duplicate targets cannot lose a
bit. A long dependency chain does not cause a full-edge scan at every round.
`c_lowering_scalar` uses an independent byte-visited worklist and supplies the
exact reference result. A select-all diagnostic path includes every eligible
local definition; it still excludes inline-only definitions.

## Validation and measurement

`./build/compiler test-lowering --clang` checks graph selection against an
independent oracle and checks manually modeled C emission fixtures against
Clang's object symbols. It exercises source ordering, cycles, duplicate
edges, empty inputs, vector tails, invalid contracts and resource limits.
The 9700X CI test list runs the Clang comparison.

`./build/compiler benchmark-lowering` measures deterministic synthetic graphs,
including an intrinsic-shaped small live set, broad fanout, chains and cycles.
Run each build, test and measurement through the shared 9700X lock; pin timings
to CPU 2. Selection-only measurements reuse the prepared graph; the complete
planner measurement additionally charges graph preparation and first touches.
Conditioned runs use a separate prepared plan immediately before a vector
sample, either running the scalar path or warming the vector path explicitly.

Options are `--shape all|intrinsic|fanout|chain|cycle|duplicates|large`,
`--definitions N`, `--only vector|scalar|prepare` and either `--before-vector MS`
or `--after-scalar MS`.
Each path has one warm-up and 21 samples. `prepare` means arena reservation,
first touches, validation, copying, adjacency preparation and vector selection;
result verification and arena teardown are excluded. Nanoseconds time the
operation, while the perf cycles/instructions also include the two clock reads.
Unavailable or multiplexed counters are reported as unavailable. Every measured
result is compared to a separate oracle outside the timed interval.
The default samples are unconditioned single selections; the checks between
samples do not establish sustained vector execution. Use separate processes
with `--before-vector 100` and `--after-scalar 100` for the two conditioned
states. There is no prepare-plus-scalar timing path, so the prepare result does
not establish a whole-planner improvement over scalar selection.

These measurements describe the planner, not backend lowering or a complete
compiler. The required SQLite/libc/c-testsuite emission comparison and
`main.c` counts and time saved require the source producer and downstream
lowering path described above.

## Existing source handoff and remaining integration

The checker implementation from [PR #312](https://github.com/buster14a/buster/pull/312),
inspected at `991f9ba756800c4b78b47bc983ae3ec7416000e4`, already has a real
source-to-static-data path. `CheckInput` retains resolved declaration uses,
stable object identities across redeclarations, types, linkage, static storage
duration, definition/tentative flags and checked initializer nodes.
`check_data_input` coalesces object definitions and initializers;
`check_data_emit` produces link atoms and fixups. That path currently selects
every static object definition, including block statics. Static storage
duration alone does not make an object an emission root.

An adapter must map those token-derived identities to dense definition IDs in
source order and map the selected IDs back to the emitter's object records.
Checked address constants already identify initializer targets. Supported
typed body expressions also retain object references, but the graph does not
yet publish their containing definition or the evaluated-emission dependencies.
Persistent inline policy and retention/alias metadata, complete body dependency
selection, and the function bridge to Tier 0 remain missing. Filtering objects
while dropping those body references would be unsound.

A separately scoped data-only adapter could accept successfully checked units
with no function definitions and derive edges from checked address initializers.
It would not establish #129's function/body or corpus acceptance. Wiring any
selection into the active checker's data path requires coordination with its
owner; this planner leaves that path unchanged.
