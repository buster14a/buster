# Verified IR analysis handoff

This is the implementation contract for the first production handoff in #1055,
not a performance acceptance record. The baseline inspected for this change is
`a074713a47797f90ba899f59fadd0c9ac4da3a56`.

## Boundary and verification

`ir_optimize_verified` in `src/buster/compiler/ir_optimize_verified.c` uses the
existing optimizer preparation, SCCP, DCE and rewrite kernels. Its final
verification retains the CFG and dominators in a separate caller-owned arena.
It does not allocate a final use list, constant lattice or optimizer worklist.
The primary optimizer workspace is retired after the complete rewrite and
before final verification.

Final verification still uses the shared IR verifier's implementation:

1. Check the descriptor shape and all instruction rows.
2. Build one CFG and check its graph, phis, switches and same-block ordering.
3. Construct dominators from that CFG.
4. Check the graph with full dominance, including cross-block uses.

The scalar/vector verifier path and dominance path follow the resolved optimizer
choice. No weaker validation predicate, trusted boolean or skipped final check
is introduced. `check_tier1_compile` consumes those exact dominance intervals;
it no longer immediately builds another CFG and dominator analysis.

The ordinary `ir_optimize`, `ir_build_cfg`, `ir_verify` and `ir_verify_scalar`
entry points are unchanged and remain independently validating. The ordinary
optimizer is also the rebuild comparison control in the new regression tests.
The two optimizer entry points currently mirror only orchestration; they share
all transformation kernels. Consolidating that orchestration after qualification
must preserve the independent rebuild mode and the tests, not silently make both
controls reuse the same retained product.

### Scoped construction counts

The successful final-verification-to-selection boundary has this source-level
call trace at the baseline, excluding initial optimizer preparation:

| Construction | Baseline | Retained handoff |
| --- | ---: | ---: |
| CFG | 5 | 1 |
| Dominators | 2 | 1 |
| Use list | 1 | 0 |

The baseline final verifier's preparation invokes the public verifier, CFG
builder and use builder; the use builder invokes the verifier again. Final
verification invokes the verifier once more. These account for four CFG builds;
the driver's following CFG build is the fifth. This table is an audited call
trace, not a measured whole-compilation counter report.

`IrVerifiedAnalysis` records actual successful-path calls in the new final
verifier: one shape check, one row check, two graph checks, one CFG construction
and one dominator construction. `arena_bytes` measures retained analysis
storage, including alignment, from its caller's entry mark. It is **not** peak
scratch, total per-function storage or resident memory. The optimizer's existing
`scratch_bytes` remains the workspace length after rewrite, not a true nested
allocation high-water mark. Whole-pipeline counters and peak-memory accounting
remain required for #1055 acceptance.

## Revision and borrowing contract

An `IrVerifiedRevision` is caller-owned, initialized once for its scope. A
successful handoff binds its generation to the exact published `IrFunction`
descriptor, not to a temporary descriptor in the optimizer. A handle also names
that scope and the analysis arena. The driver checks this capability before
selection and invalidates it before retiring storage.

Call `ir_verified_invalidate` **before** any mutation of the function, its
columns or its side tables, or before retiring/recycling their storage. This
includes an in-place permutation that preserves every address and count. None
of the current mutating passes preserves this product. A later producer must
verify the new revision and publish a new handle.

The scope must not be moved, copied or reinitialized while an old handle can be
consulted. Generation exhaustion fails closed rather than wrapping. A copied
function descriptor is not the bound function; another lane's scope is not the
owner even if it contains equal fields. These are per-function lifetime rules,
not a global cache or pass manager.

`ir_verified_current` is an O(1) capability check. It does **not** detect an
unannounced raw-pointer write. Such a write violates the explicit mutation
protocol; address/count comparisons or a claimed verification flag would not
make it safe. Both the optimized IR arena and analysis arena must remain live
and unchanged while a consumer borrows their columns. Keep the CFG alive with
the dominator product conservatively rather than assuming no borrowed graph
storage.

On failure the result and analysis descriptors are unchanged, both destination
arenas return to their entry marks, and scratch is restored. The old revision
is invalidated even when a replacement fails, so an unchanged old descriptor
cannot accidentally authorize an old result as the replacement. These APIs
require distinct input/output/analysis/scratch storage and live, nonoverlapping
caller-owned descriptors; they are not concurrent mutation APIs.

## Driver lifetime graph

The lane's existing six arena reservations are retained; four are used for
this path. No reservation policy or unrelated lane structure changes here.

| Storage | Initial contents | Last consumer / subsequent use |
| --- | --- | --- |
| `steps[0]` | Lowered IR | Keep through optimization; then reuse for selection plan |
| `steps[1]` | Promoted IR | Keep through optimization; then reuse for emitted code |
| `steps[2]` | Optimized IR | Keep through selection and emission |
| `steps[3]` | Verified CFG and dominators | Keep through selection and emission |
| Caller output arena | Copied native records and metadata | Escapes the function call |

Do not retire lowered IR immediately after promotion: that would need a
separate audit of promotion's borrowed side tables. Instead, retire lowered and
promoted storage together only after the optimizer rewrite succeeds. That
rewrite allocates its own instruction and block columns, constants, slots,
addresses, calls, call types, parameter types, labels, aggregate-call records
and passed-value records. Incoming frame and return-ABI metadata are copied by
value. Vector-valued functions remain outside this optimizer's admitted subset;
mask values use its existing supported representation.

Selection and emission may continue borrowing optimized IR and derived
analysis. Their owners therefore survive until native instruction records,
branch columns, references, labels and return tails have been copied into the
caller's output arena and frame publication has completed. Then the revision
is invalidated and every lane stage is reset, on success or refusal. Reusing
`steps[0]` and `steps[1]`, rather than merely resetting separate arenas, makes
last-use retirement affect subsequent allocation placement. Actual resident
memory and throughput effects still require measurement.

## Regression coverage and qualification

`ir_optimize_verified_test.c` is called by the existing `test-ir-optimize`
command through `ir_optimize_text_test.c`. It adds comparison cases for all eight
pass masks and AUTO/vector/scalar selection, forced sweep-limit fallback,
diamonds, loops, empty IR, indirect labels, aggregate-call metadata and generated
DAG lengths around batch boundaries. It compares every CFG column and every
block-pair dominance relation with independently rebuilt analyses, and runs the
public full SSA verifier with freshly rebuilt intervals.

The tests overwrite retired optimizer scratch and input storage, reject copied
function descriptors and foreign scopes, recycle the exact arena addresses to
check generation-based ABA rejection, and invalidate before same-size in-place
edits. They exercise every smaller analysis capacity after a successful run,
output and scratch allocation failure, invalid options, generation exhaustion,
nondominating uses and invalid opcodes. The final verifier is tested directly
as well as through optimizer preparation. Adding these tests does not mean they
have been executed successfully; the PR records actual validation separately.

Before closing #1055, retain successful CI and sanitizer results, broaden the
rebuild/reuse comparison to emitted code and representative full-source corpora,
collect whole-function/source counters and true peak arena usage, and run paired,
placement-aware end-to-end Tier 1 timing and memory qualification on the locked
9700X. Store raw evidence in `buster14a/buster-performance/docs/measurements`, not
in this repository. No throughput or RSS improvement is claimed here.

The existing driver outcome taxonomy is unchanged. Review the handoff's
invariant-failure handling together with #1054; an invalid product must not be
presented as ordinary unsupported language coverage in the completed design.
This candidate is not sufficient by itself to close either issue.

## Provenance

The new code, tests and this document are original project material under
Apache-2.0 WITH LLVM-exception. The orchestration is adapted from this project's
own `ir_optimize.c` at the baseline above; fixtures and verification primitives
are existing first-party project interfaces. No external implementation,
generated metadata, corpus data or third-party fixture is incorporated.
