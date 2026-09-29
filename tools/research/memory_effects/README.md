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
