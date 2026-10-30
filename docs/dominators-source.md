# Dominator acceptance on source CFGs

`test-ir-source --dominators` connects #174's existing independent graph
oracles to the C-source producer. It complements the synthetic tests described
in [dominators.md](dominators.md); it does not replace them or change the
compiler's production analysis or path-selection policy.

```sh
./build/compiler test-ir-source --dominators [--print NAME] [preprocessor options] file...
```

## What is checked

The command retains the ordinary lowering, SSA promotion, dominance verification
and text-roundtrip checks. For every function that the source preparation emits,
it additionally checks each of three representations: lowered IR, vector-promoted
IR and scalar-promoted IR. Their CFGs need not be identical; each is independently
rebuilt with `ir_build_cfg` and checked on its own.

The adapter borrows that producer's successor and predecessor CSR arrays directly.
It passes them to `dominator_test_case` with the independent oracle enabled. The
existing module test runs both requested solver paths, compares the complete
results, checks padded matrix tails and repeats the vector-requested analysis.
Its independent definitions check dominance by deleted-vertex reachability,
frontiers, natural-loop headers and bodies, nesting and innermost membership,
and whether deleting all dominance backedges leaves an acyclic graph. The oracle
checks the vector-requested result; full equality connects that check to the
scalar result. Host builds that substitute scalar implementations do not establish
AVX-512 execution: target qualification must use the approved AVX-512 build.

The result line reports attempted `forms`, failed forms and individual `checks`.
A successful nonempty run has three checked forms per emitted source function.
A failed form additionally reports the source file, function, representation,
block count and failed-check count. The module's diagnostic names the failed
condition. CFG construction failure, allocation failure, matrix-limit failure,
solver disagreement and failed definitions are failures, never accepted skips.

Unlike the ordinary command, this mode also returns failure when **any input file
is left out**. A source rejection or named unsupported feature remains visible in
the existing per-file report, but cannot reduce the requested corpus and still
produce acceptance. Existing lowering, promotion and text failures still fail the
command. An empty overall function set cannot pass. Omitting `--dominators` keeps
the existing command's behavior and avoids the additional work.

## Cost and lifetime

This is an explicit correctness campaign, not a benchmark or a production path.
There is no block-count sampling or size-based oracle bypass. The independent
oracle allocates a quadratic byte relation and runs repeated reachability searches;
its loop checks can be substantially more expensive than either solver. The
existing 64 MiB matrix budget remains a bound on the solver's bitsets, not a
performance crossover and not a bound on all oracle storage. Exhausting either
storage budget fails the run. Do not include this oracle in compiler throughput
timings or infer a scalar/vector threshold from its execution time.

The adapter reuses the source test's temporary text-output arena after the text
roundtrip has released its results. The IR, temporary output and scratch storage
are distinct. CFG and analysis results live only for one form; output and scratch
marks are restored before the next form. The adapter checks scratch lifetime
before cleanup so a leaked temporary allocation is not silently hidden.

## Focused source fixture

`tests/ir-source/dominators.c` is original project C. It contains a straight-line
function, a diamond, nested loops with continue/break, multiple latch paths,
a two-entry cyclic region, a volatile self-loop and a switch with shared targets.
Its `main` checks bounded native results. The frontend may simplify source shapes;
exact unreachable, duplicate-edge and boundary graph coverage remains the job of
`test-dominators`.

Run builds and each test separately through the approved host's shared lock:

```sh
EXCLUSIVE_OWNER=issue-174-source EXCLUSIVE_TITLE='Issue 174 source CFG checks' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh ./build/compiler test-dominators
EXCLUSIVE_OWNER=issue-174-source EXCLUSIVE_TITLE='Issue 174 source CFG checks' EXCLUSIVE_LINK=https://github.com/buster14a/buster/issues/174 ~/buster-fast-bench/exclusive.sh ./build/compiler test-ir-source --dominators tests/ir-source/dominators.c
```

For a corpus run, add `--dominators` to the existing `test-ir-source` invocations
in `tools/ci/correctness.tests`, preserving their exact pinned preprocessor options,
input lists and corpus versions. Report every requested file and any refusal.
The focused fixture alone is not the full source-corpus acceptance.

## What this change does not establish

Adding the gate is not evidence that it has passed the corpus. Full compiler
builds, the focused source command and all corpus invocations must be executed
on the actual checkout. #174 also still requires controlled repeated real-function
9700X measurements, especially `sqlite3VdbeExec`, a measured selection policy and
consumer-specific acceptance. Neither the original memory fallback nor this test
change supplies that evidence. Keep raw campaign records in
`buster14a/buster-performance`, not in this repository's documentation tree.

The adapter and fixture are original material under the project default terms.
They reuse the existing project CFG and dominator test interfaces; no external
implementation, generated data, or upstream fixture is imported.
