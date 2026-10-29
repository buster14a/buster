# Structural IR reduction

`test-ir-reduce [--seed N] [--count N]` runs the small correctness-CI suite.
`reduce-ir` accepts a valid canonical function directly, without the C frontend.
For example, with the compiler built from the stated source revision:

```sh
./build/compiler test-ir-reduce --seed 733 --count 64
./build/compiler reduce-ir --revision COMMIT --output reduction \
  --demo phi --inject result-phi --arg 5
BUSTER_COMPILER=/absolute/path/to/compiler sh reduction/replay.sh
```

Replace `COMMIT` with the complete 40- or 64-digit source revision. Use a new
output directory. The memory example uses `--demo memory --inject memory-store`;
`--demo phi --inject verify-phi` injects a disagreement after both real verifiers
approve the graph. These injections change the test observer, never a compiler
implementation. For a real optimizer mismatch, replace `--demo` and `--inject`
with a `.ir` input path and suitable `--arg LOW[:HIGH]` values.

The predicate compares the actual optimizer's scalar and SIMD paths with the
original function through the checked interpreter. It compares the returned
bits and all bytes of one registered 32-byte global, symbol 2. The function is
symbol 1. Unbound calls and other unsupported interpreter forms are unavailable
observations. `--verify` instead rechecks the two verifiers on admitted functions
and supports the explicit `verify-phi` observer injection.
Both input admission and every replacement require both complete verifiers to
accept the function before this observer runs. Consequently, this hook cannot
reduce an ordinary input on which those deterministic verifiers disagree;
reduction of input-verifier disagreements remains separate future work. The
injected fixture qualifies the classified reduction/replay path within that
admission boundary, not that deferred capability.
`--passes 0..7` chooses the existing SCCP/DCE/CFG pass mask.

The reducer rebuilds selected blocks and instructions into fresh columns, with
compact value/block IDs, remapped phi predecessors, call/signature/operand
slices, addresses and referenced slots. Symbol identities and ABI stay stable.
Both complete verifiers, including reachable SSA dominance, approve every
candidate before the predicate runs. Candidate order is stable. The objective
orders block count, instruction count, side-record count, then constant bit
width and population. This finds a useful local reduction, with no claim of a
globally smallest function. Unreachable blocks may contain effects; reachable
stores, calls, volatile transfers and dynamic-stack operations are retained.

Only the same failure kind, pass and detail can replace the original case.
Unsupported observations, resource limits, returned predicate faults and other
failure classes do not match it. The exact-content cache checks every serialized
field after its stable hash; it excludes addresses and C padding. A failure to
establish the original class publishes no result. All allocation failures restore
the caller's output and scratch lengths and leave the result view unchanged.
Borrowed function columns, plans, limits and result storage must avoid writable
arena tails. Opaque predicate context remains caller-owned and must also stay
alive and disjoint from writable storage throughout reduction. Immutable plans
in a retained scratch prefix are supported; output and scratch writes cannot
overlap either arena's retained prefix.

`--max-candidates`, `--memory`, `--steps` and `--frames` bound candidates, arena
capacity and interpreter execution. `--milliseconds` and `--predicate-ms` sample
monotonic time between bounded public consumer calls. Trusted built-in predicates
run in process and must honor their step/resource budgets; these time checks do
not preempt a call or recover from a native process fault. An external-process
predicate adapter is outside this first kernel. The candidate/cache limits may
publish a verified partial reduction after establishing the original failure.
A wall-time boundary publishes the original valid reproducer, avoiding a
scheduling-dependent reduced artifact.

The bundle contains `original.ir`, `reduced.ir`, `result.json` and `replay.sh`.
Before publishing it, the tool reparses and independently observes the reduced
file. The manifest records source revision, executable byte count and FNV-1a
identity, limits, seed, classified failure and reduction counters. Revision is a
caller-supplied provenance assertion. The script uses bundle-relative files and
requires `BUSTER_COMPILER` so the bundle can move between workspaces. Use the
matching source/tool build recorded by the manifest; the script does not verify
an executable's identity itself. Its `--expect-kind`, `--expect-pass` and
`--expect-detail` arguments require the recorded failure class before publishing
a replay. Each replay creates a new `replayed` directory.
Replay selects the scalar parser used by the independent publication check,
regardless of the original input parser. Exit status 1 means invalid input,
a different failure class or an existing destination; status 2 means a resource,
I/O or unavailable-observation boundary, including during publication and replay.
Neither outcome publishes a partial bundle. Unsigned numeric limits require
a digit first and reject signs, leading whitespace and overflow.

This additive test tool does not change IR, verifier, optimizer or interpreter
implementations. Its first structural subset is scalar IR; unfamiliar opcodes,
vector/mask values (including an unused vector-constant pool) and unsupported
signatures refuse before candidate changes. Borrowed vector payloads are covered
by ownership preflight before verifier allocations; neither arena's writable
tail may overlap any part of that pool.
So does function metadata the rebuild doesn't remap: labels, by-value
aggregates' ABI records (the function's own and its calls') and a variadic
definition's incoming frame. `ir_rebuild` returns `IR_ERROR_METADATA` and
`ir_reduce` `IR_REDUCE_INVALID` for such a function, before the predicate
sees it, rather than copying it without them; `test-ir-reduce` checks a
fixture of each that both verifiers accept.
The later #731 adapter and native/backend process predicates remain separate
integration work under #733.
