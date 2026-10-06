# Isolated memory/effects contract oracle

`memory_effects_oracle.c` is independent research support for Buster main
`8f67df736f13d4edc055110a7a6d619a00a22eaf` (tree
`8810c6603e47e5eecf26e1d73633619a347c70d8`). It does not call Buster, change
production IR, or validate a Buster implementation.

The reference interpreter uses two 16-byte allocations, little-endian byte
loads/stores, permissions, allocation generations, explicit observations,
special-access events, and terminating faults. Ordinary private accesses have
no event beyond value/fault. The interpreter initializes its finite-state memory
to zero, including replacement lifetimes. This chooses concrete bytes for test
witnesses; it does not propose implicit initialization, indeterminate-value,
poison, pointer provenance, concurrency, or full C rules for canonical IR.

Separate proof predicates describe sufficient conditions for redundant-load
elimination, store-to-load forwarding, and exact-interval overwritten-store
deletion. Their proposed initial domain is integer bit-pattern accesses of
32 or 64 bits, fixed private LOCAL roots, definite lifetime and access safety,
matching byte interval/type/generation, and normalized forwarded values.
Unknown operations, escape, lifetimes, special accesses, and possible faults
are conservative barriers. The possible-fault barrier is an intentionally
simple implementation restriction: eliminating a later load without moving
instructions does not in general require every intervening instruction to be
nonfaulting.

The checks comprise 66,394 equivalence comparisons, 71 predicate checks, and
14 counterexamples to weaker rules. The 65,536-case subobject test enumerates
all pairs of values in `[0,255]` stored in disjoint 32-bit fields. Other checks
use 11 boundary bit patterns at every aligned 32/64-bit offset in the model.
This is not exhaustive enumeration of every finite-state program, nor a
mechanized proof of the predicates or a target/backend test.

Counterexamples exercise partial byte overlap, union overlap, unknown calls,
allocation replacement, changing access permissions, narrow normalization,
special load/store events, intermediate observers, escaped storage, partial
overwrites, an overwritten store's own fault, and an unused load's fault.
Atomic checks preserve abstract events; they do not enumerate C11 executions.
No prototype performance measurement is intended.

Run on a standard GitHub-hosted Linux runner:

```sh
cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror memory_effects_oracle.c -o oracle
./oracle
cc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer memory_effects_oracle.c -o oracle-sanitize
./oracle-sanitize
```

Only a completed run establishes actual counts/results. No local or target
performance result is claimed by this artifact.

Hosted evidence is preserved in `evidence/hosted-evidence.zip` (SHA-256
`9704dd93444e666d21bd3b89c8501981fda77567598e9fb280f33827f55dd1e4`).
Its exact execution revision is `da52c15d64f2428c9d314b81ef4ba563f5589294`,
tree `1d09e05301e8b2f1ef703acb5df4af844fdf21c1`, on a standard
`ubuntu-26.04` GitHub runner with Clang 21.1.8 and GCC 15.2.0.
The ordinary and ASan/UBSan oracle runs each completed with the counts above
and zero failures. All ten small-subject census compilations succeeded.
Both pristine compiler-unity inputs completed canonical preparation/census
but downstream code generation refused baseline `xgetbv` (existing #1487).
The workflow therefore failed and does not establish successful object output,
self-hosting, compiler throughput, or performance acceptance.

`run_census.py` instruments only a disposable checkout while building the
diagnostic compiler, then restores the original driver before compiling the
input corpus. No canonical rows are rewritten. Its post-preparation counts
are structural opportunities conditional on the proposed access, lifetime,
initialization and representation contract; they are not certified legal
production rewrites. The counter table has sixteen slots; counts for different
rules are independent. The payload peak is `33V + 5B + 512` bytes per function
plus arena alignment, where V/B are value/block counts. Existing production
budgets and invalidation must be honored by any later implementation.

Reproduce the whole untimed hosted diagnostic with
`python3 tools/research/memory_effects/run_census.py` in a disposable checkout
of the execution revision. Build policy is delegated to the existing C driver.
The branch-only workflow from that exact revision is preserved in history;
it is removed from the final research checkpoint after execution.
The current report, semantic contract, uncertainty and go/no-go live on the
existing [optimizer owner #49](https://github.com/buster14a/buster/issues/49).
