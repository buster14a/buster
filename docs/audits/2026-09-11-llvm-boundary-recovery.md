# Recovered independent LLVM integer-boundary controls

This recovers regression coverage from `fix/llvm-signed-minimum-constants-222`
at `bb25896d405551817be91da5edf56b267ce4b2b3`, not its superseded production fix.
Issue #222 is already fixed on main. Main at `cc25a0d1dba8f1c21447e3712ca5f35678012815`
has internal bitcode-record boundary checks and a basic executable fixture;
the old branch additionally checks neighboring global array values, pointer
logical negation and wide-integer words from a separately compiled consumer.

The original two fixture files are preserved byte-for-byte under new names,
so the current integer fixture is neither replaced nor weakened. The existing
`llvm_bitcode_test_consumers` runner emits only the values translation unit
through Buster. Clang compiles the checker independently and consumes the
bitcode. Explicit source/caller records preserve the five existing fixture
pairings and remove index-dependent pairing; platform guards are unchanged.
The maximum compile command still has six arguments.

The checker covers 24 array elements, five globals, four integer logical-NOT
inputs, null/non-null pointers, four signed minima and both wide-word controls.
These are expectations, not a claim that Buster has passed them. No current
miscompilation or compiler speedup is asserted. Full registered/platform tests,
LLVM consumption and ordinary self-host on the submitted integration revision
remain required before merge. This recovery does not reopen or close #222.

No compiler emitter, build policy, workflow, test module, or dependency changes.
The source-only fixtures are not standalone MSVC targets; the independent
consumer remains Clang under the existing runner's availability policy.
