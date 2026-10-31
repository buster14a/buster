# Arena address space and commit admission

`lane.c` owns the arena allocator. `arena_reserve` and `arena_reserve_small`
reserve anonymous **inaccessible** mappings. They do not make the advertised
capacity writable. `arena_push` checks alignment, multiplication and bounds,
then makes the necessary prefix writable before publishing the new length and
peak. Pages are still physically allocated on first touch. Protection changes
are amortized geometrically; after the small prefix they follow huge-page
boundaries. Failed commitment leaves the allocation descriptor unchanged.

The Linux kernel accounts writable private anonymous mappings against commit;
`MAP_NORESERVE` is ignored in overcommit mode 2. Inaccessible reservations avoid
that charge. See the kernel's [overcommit accounting](https://docs.kernel.org/mm/overcommit-accounting.html).
A reservation and a successful push are therefore distinct operations that can
both fail. The driver reports the failed operation and requested byte count.
The early C-driver checks from #852 remain in place.

## Finite address spaces

`PROT_NONE` still counts toward `RLIMIT_AS`. With a finite limit, each ordinary
arena admits at most one 128th of it, rounded down to a page. This leaves room
for the compiler's many simultaneously live arenas instead of letting the first
64 GiB request greedily consume the available space. A large mapping refused
with `ENOMEM` also activates a 64 MiB per-arena fallback for that thread, for
layouts such as Valgrind's. These are admission ceilings, not guarantees that
an arbitrary workload or number of threads will fit.

**The returned `Arena.capacity` is authoritative**, and can be less than the
requested ceiling. Allocations exceeding it fail; no data is truncated and no
published pointer moves. Fixed-size tables whose producer has already published
an index range use `arena_reserve_exact`, which refuses rather than shortening
that range. Ordinary unbounded processes retain the existing maximum shapes
until the operating system refuses an allocation.

The preprocessor and file cache freeze a power-of-two capacity reduction at
initialization, applying it to both allocations and all their producers' bound
checks. Include-depth and macro-parameter language limits do not change. The
intern table freezes an id ceiling at initialization too; macro-id and bitset
consumers allocate that same range, including when the table later grows. A
cache's record ids retain their complete indexing range in each unit's
`record_data`, independently of the unit's own file capacity.

When a full preprocessor shape is refused by commit accounting, initialization
releases it without pooling and retries the compact shape once. This preserves
the intern producer's existing id range; it never narrows a live table behind
another consumer. Consequently this is not a minimal-memory implementation of
the preprocessor: an intern table initialized before fallback can still require
its full fixed id-indexed arrays. The compact path is bounded admission, not a
new unlimited-growth representation. Its existing capacity diagnostics report
exhaustion rather than corrupting memory.

The deferred checker retains its contiguous per-lane layout, but reserves it
inaccessible and bounds each slice by the same admission policy. Each slice
tracks its own writable prefix. The complete original mapping is unmapped once
at teardown, rather than passing a parent with stale commitment metadata through
the arena pool.

## Ownership, reuse and tests

`mapping_bytes` records the actual owned mapping independently of logical
capacity; `committed` is an offset from its original mapping address. A zero
`mapping_bytes` denotes borrowed writable memory, including the measuring
arena. Such views do not change protections or unmap their owner's storage.

A pooled arena is reprotected inaccessible and discarded with `MADV_DONTNEED`;
reuse starts with zero length/peak and a zero-filled prefix on its first push.
A failed reprotection/discard unmaps the arena instead. Tests that deliberately
poison unused logical capacity explicitly commit that test memory first; a raw
`base` pointer no longer authorizes touching an owned reservation.

`tests/arena/resources.c` checks inaccessible tails, growth and stable pointers,
zero-filled pool reuse, borrowed/measuring arenas, overflow rollback, a real
`mprotect` refusal injected with a child-only seccomp filter, and 32 simultaneous
large logical requests under a 2 GiB address-space limit. With a compiler path
and output directory it runs `-E`, `parse`, `-fsyntax-only`, object compilation
and executable linking, then executes the result. Each command is also refused
by child-only seccomp rules for inaccessible reservations and writable commits,
with a fatal byte-count diagnostic and without usage text. The loader is allowed
to map its ELF segments and make RELRO read-only; the test does not assume that
a small fixed address-space limit must always fail.

The baseline unsanitized hosted CI job runs those commands under 2 GiB,
separately under Callgrind, and without `RLIMIT_AS` under strict overcommit with
4 GiB of additional commit headroom. The sysctls are changed only on that
disposable VM and restored before the ordinary suite. Sanitizer shadow mappings
are not subjected to these artificial limits. CI artifacts retain diagnostics;
no throughput improvement is asserted by this change.

Complete IR verification and SSA analysis borrow only output already pushed
through the owner; temporary work also pushes through that owner. They never
write an uncommitted tail or commit half a large reservation just to split it.
