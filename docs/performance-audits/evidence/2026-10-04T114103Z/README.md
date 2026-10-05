# Conservative root-marking DCE: pinned Buster research evidence

## Disposition

Candidate for a bounded current-source census and controlled experiment. **Not
approved for default adoption.** No Buster speedup, peak-RSS reduction, generated
code reduction, or full compiler correctness is established by this package.
No repository branch, production source, workflow, PR, or default was changed.

Buster inspection pin: `f34cc3a56296be1b1244647775e1a40a67dae3ef` (2026-10-04).
Execution: this session's Linux x86-64 cloud container, not a user machine or
Ryzen 9700X. See `environment.txt` for compiler and environment identity.

## Files and results

- `dce_probe.c`: independently written C11 dependency-graph solvers and tests.
- `fixture.c`: independent actual C source witnesses for a subsequent Buster
  reproduction. Clang and GCC syntax checks pass; Buster execution is UNRUN.
- `results-clang-sanitized.txt`: Clang 17 -O2, AddressSanitizer + UBSan result.
- `results-gcc.txt`: GCC 14.2 -O3 result. Byte-identical to the Clang transcript.
- `SHA256SUMS`: source/results/environment identities.

A = reference-count deletion with all block parameters retained, modeling the
pinned canonical DCE's use/retention rule. B = simplest plausible extension:
allow zero-use parameters onto the same deletion queue. C = backward marking
from mandatory roots across both ordinary definitions and parameters.

32,768 exhaustive three-node graph/root/parameter assignments and 20,000 seeded
random multigraphs match an independently scheduled dense fixed-point oracle.
The tests also check mandatory-root preservation, retained-operand closure, and
C-retained subset of B-retained subset of A-retained. The abstract graph inputs
include cycles, self-edges and duplicate operands; they are NOT all valid SSA
control-flow graphs. This distinction is intentional and limits the claim.

An unused acyclic parameter retains 4/1/1 nodes in A/B/C. A loop with a dead
phi/multiply/add recurrence retains 14/14/10 nodes: C deletes the recurrence and
its private multiplier constant. Returning the accumulator instead retains all
14 nodes in each arm. There are 394,752 result comparisons for the two small
hand-built loop variants across n=0..256 and seed=0..255 and three solvers.
The loop evaluator is a bounded semantic control, not a general IR interpreter.

Three 262,144-node stress graphs test live chains, dead chains and a dead ring
with parameter nodes. All-live C enqueues every node. C's zero operand visits on
a dead ring do NOT mean zero total work: initialization, root classification
and sweeping still examine the graph. There are no latency/RSS measurements.

## Reproduce using existing C compilers

```sh
clang -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  dce_probe.c -o /tmp/dce-probe-asan
/tmp/dce-probe-asan

gcc -std=c11 -O3 -g -Wall -Wextra -Wpedantic -Werror \
  -fwrapv -fno-strict-aliasing -funsigned-char \
  dce_probe.c -o /tmp/dce-probe-gcc
/tmp/dce-probe-gcc

clang -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only fixture.c
gcc   -std=c11 -Wall -Wextra -Wpedantic -Werror -fsyntax-only fixture.c
```

The solvers take already-resolved definition IDs and already-classified roots.
They do NOT implement Buster's purity predicate, aliases, debug state, arena
policy, CFG validation, remapping, instruction selection, or machine allocation.
In particular, C must classify many more ordinary rows for purity than Buster's
current zero-use-filtered implementation. The model intentionally does not hide
that missing cost behind a claim of faster compilation.

## Pinned source observations

`src/buster/lib/compiler/ir/ir_fast.c`, blob
`560809307b2d8eb1307bcb605a5fc64203eb52ee`:

- `ir_instruction_is_pure` is the existing, conservative semantic authority.
  Do not widen it. Calls, loads/stores, FP arithmetic, division/remainder,
  atomics, exact SIMD and other excluded operations must remain roots.
- `ir_fast_dce` (around lines 282-333) counts all incoming parameter operands,
  queues zero-use pure ordinary definitions and propagates deletion. It has no
  transition deleting a parameter, and a closed use-cycle has no zero-use seed.
- `ir_fast_parameters` only folds agreeing non-self incoming values, with four
  sweeps. A phi(seed, add(mul(phi,33),1)) is not that trivial-parameter case.
- `ir_fast_function` selects bounded FOLD/ADDRESS/DCE/PARAMETERS and one final
  compaction. Keep its phase order unchanged in the initial candidate.

`src/buster/lib/compiler/ir/ir_promote.c`, blob
`3a0bf7fd859afeb9c3d34d2db49368717740903a`, `ir_rewrite_compact` at line 485:
only surviving rows/parameter lists get value IDs. The helper remaps operands,
source rows, extras and local maps. Dead parameters must be unlinked BEFORE
compaction or their incoming values remain required. The compactor also loops
over `block_count * local_count` when per-block `local_values` arrays are
present; whole-pass cost must not be described as merely the marking walk.

`src/buster/lib/compiler/ir/ir_cfg.c`: published CFG invalidation reconstructs
parameter/incoming/predecessor arrays. The default documented schedule performs
FAST before first publication; doing a late read/modify/write of published CFG
would add avoidable retained allocation. Later CFG publication remains a real
cost even when existing optional-pass payload counters exclude it.

The pinned `docs/middle-end-pass-map.md` describes frontend parameter-cycle
pruning from ordinary instruction-operand roots. It is not whole-IR marking
from effects. Its historical source mapping is not a fresh body-level audit of
`c_ir_ssa_finish`. The large `c_gen.c` body could not be retrieved in full;
exact current source-to-witness reachability is an explicit reproduction gate.

Historical owner #49 remains `status/needs-census`. PR #590's earlier SIMD
research required identical deletion sets and therefore excluded mark-live
replacement. This candidate changes optimization power, not merely the speed
of an identical algorithm. PR #1973's phase-order experiment found no benefit
on its representative corpus; it is relevant negative prior evidence, not a
current census of nontrivial phi/arithmetic cycles. Neither result is erased.

## Concrete adaptation

Operate at the current DCE slot on already-validated mutable canonical IR.
Freeze prior-pass aliases during the analysis. Assign transient node IDs to all
ordinary rows followed by block parameters. Existing value definitions identify
ordinary producers; a transient value-to-parameter map identifies parameter
producers. A root is any retained row that the EXISTING purity predicate refuses
to delete. Every terminator is a root, including unreachable terminators.
Externally required ABI/debug/metadata references need their existing treatment;
unclassified semantic references must not silently disappear.

Mark roots at enqueue. Pop a node, follow ordinary operands or every incoming
argument of a live parameter through resolved aliases, and enqueue each unseen
producer. Do not traverse unrelated parameters as unconditional roots. Once the
worklist empties, mark unvisited pure rows removed, unlink unvisited block
parameters while maintaining first/last/count, and use the shared compactor.
Run the remaining existing parameter stage and normal certification/publication.
No CFG topology, branch target, instruction scheduling or side-effect policy
changes. All code remains C and no dependency or vendor source is needed.

Proof obligations: all roots survive; the retained set is closed under resolved
operand dependencies; any deleted row is pure; each node enters the queue once;
no retained incoming or metadata use references a deleted value; root/definition
maps are valid for this fixed IR generation; every edge retains the arguments
for precisely the retained destination parameters in the original order.

For N rows, P parameters, V values, U ordinary operand occurrences, A incoming
arguments, the graph walk and setup are O(N+P+V+U+A) once aliases are resolved.
Charge actual alias traversal/flattening separately unless a linear bound is
established. A simple layout costs 4V + 5(N+P) + sizeof(void*)P bytes for the
parameter index, marks, queue and parameter pointers, excluding existing
replacements/removal arrays. Check N+P/offset arithmetic and budget overflow.
Release transient DCE scratch before compaction when lifetime rules permit.
Never make a previously admitted function skip all FAST work merely because
this experimental representation needs more space: use original DCE when the
experimental scratch guard cannot admit C, and record that choice.

The full cost includes admission, purity/type queries, initialized bytes, alias
work, queue traversal locality, parameter unlinking, compaction including dense
builder-local maps, validation, republication, and downstream selection. Arena
compaction does not automatically return old row/payload storage to the OS.

## Decisive adoption experiment (not executed here)

Use one frozen three-arm A/B/C experiment on the pinned compiler, starting with
a read-only differential census. Use the existing frozen throughput corpus and
representative unmodified first-party sources, keeping direct SSA and the
memory-form control separate. The crafted fixture is a positive mechanism
control, not an acceptance workload. If C finds no substantial additional dead
rows/parameters versus B on real inputs, stop before a production rewrite.

For a populated case, implement B and C as reversible experimental alternatives
in the existing DCE slot. Keep the purity function, pass order, compactor,
validation and consumers unchanged. Record row/parameter/incoming residue,
selected-machine residue, emitted code size and runtime checks, not only IR
counts. Validate all selected-pass subsets, the existing allocator/frontend
matrices, debug remapping, self-hosting and supported direct consumers.

After builds and diagnostics finish, run predeclared paired rotated A/B/C and
A/A observations sequentially on the same cloud or GitHub-hosted machine.
Measure total compile wall time and peak RSS with existing native measurement
infrastructure, with identical flags/artifact boundaries. No user hardware.
Do not enable diagnostics in timing builds. Preserve all outcomes, including
failed, neutral, noisy and negative measurements.

Suggested research screening rule (NOT a repository policy change): C should
beat the simplest adequate alternative by at least 2% on aggregate compilation
latency or peak RSS, OR improve generated runtime/code size by at least 5% on a
real workload with no confirmed compile-time/RSS regression. Require correctness
and normal debug behavior. These numerical thresholds are proposed decision
criteria, not predicted effects. Synthetic-only wins, unchanged final code with
no total-cost benefit, or indistinguishable noisy cloud results are no-go.
The repository's separate dedicated-host default-admission policy remains unmet;
this request does not authorize its physical host or a policy change.

## Primary research and license handling

1. Cytron, Ferrante, Rosen, Wegman, Zadeck (1991), *Efficiently Computing Static
   Single Assignment Form and the Control Dependence Graph*, TOPLAS 13(4),
   section 7.1 and figure 17, printed pp. 478-481.
   https://www.cs.utexas.edu/~pingali/CS380C/2010/papers/ssaCytron.pdf
   The paper's DCE follows data and control dependence from observable roots.
   This adaptation deliberately roots all terminators and omits control-
   dependence analysis and branch rewriting. It is weaker, not a verbatim
   implementation of the full algorithm.
2. Wegman and Zadeck (1991), *Constant Propagation with Conditional Branches*,
   TOPLAS 13(2), section 3.4, printed pp. 192-193.
   https://www.cs.utexas.edu/~lin/cs380c/wegman.pdf
   SCCP is the screened alternative, not implemented in this package. Its
   executable-edge/lattice propagation needs user lists and phi-incidence
   handling absent from the current DCE. Full-phi rescans per edge event can
   erase sparse-work benefits on high-fan-in joins.

Both inspected papers have ACM copyright and conditional copying notices, not
permissive source-code licenses. No paper pages or pseudocode are redistributed.
Buster's pinned README and LICENSES/README expressly distinguish third-party
notices from an unresolved first-party grant (#621). No grant is inferred and
no Buster source is redistributed in this package. The probe and fixture were
independently written for this investigation; no LLVM or other vendor code was
imported, and no new project dependency was introduced. This is provenance
reporting, not a legal opinion or repository relicensing action.

## Access and evidence limits

Connected GitHub inspection succeeded for the canonical FAST implementation,
compactor, CFG publication and project contracts. A direct cloud git read failed
DNS; large-file c_gen.c content was empty/rejected by the connector and unavailable
through the attempted web fallback. No full pinned compiler checkout/build or
Buster artifact execution was completed. None of the model results is labeled a
Buster native execution, current-source population census, or production
acceptance result. Actual source-to-IR and IR-to-final-code incidence remain the
most consequential uncertainties.
