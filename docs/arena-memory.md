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

## Tables committed whole (#1521)

An arena of tables (below) is pushed full as it is reserved, so its commit is
its tables' whole size, while a unit writes a few pages of most of them: the
preprocessor's tables are about 10 GB in their full shape, the output's token
columns 2.5 GB, a history table that outgrows its slice up to 2 GB. Under the
kernel's default heuristic overcommit (`vm.overcommit_memory` 0) a single
commit larger than the host's RAM and swap is refused, whatever is touched, so
on a host of 7 GB every unit's preprocessor started with a refused 10 GB
`mprotect`. That refusal used to switch the thread to compact reservations,
which capped every later arena at 64 MB: units of a few hundred thousand tokens
then failed as `can't reserve type table` or `checker input capacity
exhausted`, though they needed far less memory than the host had.

So an arena of tables is mapped `MAP_NORESERVE`. Under heuristic or unlimited
overcommit (modes 0 and 1) its commit is then charged nothing and never
refused, and its pages are allocated as they are first written, as every
arena's are. Where commit charge is bounded nothing changes: the kernel ignores
`MAP_NORESERVE` under strict overcommit (mode 2), and a finite `RLIMIT_DATA`
counts the writable mapping whatever its flags. The other fixed arrays the
preprocessor pushed whole into arenas of their own are tables too: the
arguments' links (256 MB), a history table's full reservation and a shared
file cache's four arrays (568 MiB, one arena of four tables now, where they
took four mappings and four commits).

A refused commit no longer switches the thread to compact reservations: a
smaller reservation charges nothing less, since an ordinary arena commits only
what its pushes reach. It sets `arena_commit_refused` instead, the thread's
from then on, after which `arena_capacity_shift` gives the compact shape to the
tables committed whole (the preprocessor's and its file caches'), and the
thread's other reservations keep their capacities. Compact reservations remain
what a refused mapping (the address space) switches a thread to.

A stage that fails because a push was refused reports its own capacity, as
`checker input capacity exhausted` or `can't reserve type table` do, which
says nothing about memory. So a C-driver unit that fails, without having
reported a memory failure itself, ends with a note where memory may explain
it: that its thread's arenas held 64 MiB each, on a thread in compact
reservations, and that the system refused to commit so many bytes, with the
error, where a commit was refused since the unit started. On the 9700X, the
large unit below under an `RLIMIT_DATA` of 1.75 to 2.5 GiB failed as `can't
reserve type table`, `checker input capacity exhausted` and `code output
capacity exhausted` without saying so; it compiles under 3 GiB.

## Page sizes

An arena's first `small` bytes (`BUSTER_ARENA_SMALL_BYTES`, 64 KB, or
`arena_reserve_small`'s, or a thread's `arena_small_floor`) are base pages and
the rest huge pages. In an arena with huge pages, `base + small` falls on a 2 MB
boundary, and `base` a page or more past the mapping's start. Since the kernel
takes a transparent huge page only where one writable mapping covers its whole
2 MB from such a boundary, the layout itself keeps the base pages base pages
(#652): below `base + small` the writable bytes start past inaccessible ones and
end at the boundary, where the huge pages' differently advised mapping starts,
so fewer than 2 MB of them hold no huge page, nor join a writable neighbour's
mapping, with any setting of transparent huge pages. An arena of 64 KB or less,
base pages alone, has an inaccessible page on each side for the same reason. So
the advice is only where the layout doesn't decide:

- `MADV_NOHUGEPAGE` at the reservation for base pages of 2 MB or more (a units
  lane's 16 MB floor, the tables reserved in base pages throughout, such as
  the preprocessor's arena of tables, the deferred checker's slices);
- `MADV_HUGEPAGE` from `base + small` to the mapping's end at the first commit
  that reaches past `base + small` (`Arena.huge`, which the pool keeps), which a
  host whose transparent huge pages are `madvise` needs, and which makes the
  kernel compact for them where they are `always`.

On Linux 6.7 and later, whose `MAP_STACK` maps a region `VM_NOHUGEPAGE`, the
reservations of the first kind, and a busy host's arenas (#672, base pages
throughout), are mapped `MAP_STACK` rather than advised by a call of their own (#1444,
`arena_stack_base_pages`, which `arena_page_policy` sets from the kernel's
release once at the process's start): the whole mapping is then base pages
until the first commit past `base + small` advises its huge pages, as before,
and no advice call splits the reservation or takes the address space's lock.
The pages are the same either way; on an older kernel, or in a process that
doesn't set the page policy, the reservations advise as before. `test-lanes`
reads both back.

An arena that stays in its base pages, as most of a small unit's do, takes one
`mmap` and one `mprotect` and no advice. An empty file's `-fsyntax-only` made
146 `madvise` calls before, 48 since, and 12 once the preprocessor's tables
were base pages throughout in one arena of tables (below); 11 on `main` at
`92da7275` and 6 with `MAP_STACK` (#1444).

Fewer arenas save more than less advice: on the 9700X an arena costs 2 to 3.4
µs with its teardown, by its size, though it holds a page or none, in its
mapping, its protection change and, once touched, its own page tables (the
measurements in `docs/c-preprocessor.md`'s "Tables of a fixed size"). A unit's
tables that are pushed whole as it starts therefore share an arena of tables
(below).

The compile commands (the C driver and `-E`, `check`, `parse`) also advise the
executable's `.bss` `MADV_NOHUGEPAGE` first (`arena_static_base_pages`). It is
220 MB of statics, mostly the tests' and benchmarks', of which any compile, an
empty file's or sqlite3.c's, writes 11 pages in three 2 MB stretches; with
transparent huge pages `always`, as on the 9700X, two of them took a huge page
each, 4 MB cleared for 44 KB of statics. Tests and benchmarks keep their
`.bss` as the kernel gives it. The advice comes before the process reads or
writes any of `.bss` (`main_driver`), because it doesn't undo a touch before
it: a read maps the huge zero page over its 2 MB, and a write there, even after
the advice, then takes a huge page of its own. Until #1444, `arena_page_policy`'s
statics were written first, and that page took one of an empty file's two huge
pages. `main_driver` advises for a first argument that is an option, as a
compile's usually is, or that holds a byte no command's name has, such as a file
first (`hello.c -o hello`, `main_source_first`), and for `check`, `parse` and
`measure-attested`. A first argument of a name's bytes alone (a file named `a`)
is a source command's only once no command has taken it, and is advised then.

## Finite address spaces

`PROT_NONE` still counts toward `RLIMIT_AS`. With a finite limit, each ordinary
arena admits at most one 128th of it, rounded down to a page. This leaves room
for the compiler's many simultaneously live arenas instead of letting the first
64 GiB request greedily consume the available space. A valid large mapping
refused with `ENOMEM` or `EINVAL` also activates a 64 MiB per-arena fallback for
that thread. Valgrind's address manager can report `EINVAL` for a layout it
cannot represent. These are admission ceilings, not guarantees that an arbitrary
workload or number of threads will fit.

A compile's commands (the driver's, `check` and `parse`) read the limit once,
before they start any thread (`arena_limits_fix`, #652): they never change it,
and without a pool each reservation read it again, about 30 `getrlimit` calls
for an empty file's `-fsyntax-only`. A later `setrlimit` in such a process
changes no reservation's share. Every other process, the tests that lower
`RLIMIT_AS` in-process or in a forked child among them, still reads it at each
reservation, or once per pool.

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

When a full preprocessor shape is refused, by commit accounting
(`arena_commit_refused`) or by the address space (compact reservations),
initialization releases it without pooling and retries the compact shape once;
the thread's later units start in the compact shape. A successful retry leaves
the caller's last recorded failure as it was. This preserves
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

A pooled arena (`arena_pool`, which only the C driver's units lanes set) is
discarded with `MADV_DONTNEED`, and its committed prefix stays writable for the
next reservation that takes it (#1128): every `mprotect` takes the address
space's lock for writing, which the other lanes' reservations, commits and
page faults then wait for, while the discard takes it only for reading. Where
commit charge is bounded, under strict overcommit (`vm.overcommit_memory` 2)
or a finite `RLIMIT_DATA`, release reprotects the prefix inaccessible first,
returning its charge. The pool reads both once, with the `RLIMIT_AS` share its
thread's reservations admit (`arena_pool_limits`), and again after
`arena_pool_release`. A reservation takes the pooled arena with the smallest
mapping that holds its capacity with the same base pages and page layout (huge
pages past `small`, or none; `Arena.reserved` is what the mapping was laid out
for), and gets the capacity it asked for, as from a fresh mapping. Reuse starts
with zero length and peak and zero-filled pages. A failed reprotection or
discard unmaps the arena instead.

An arena of tables (`arena_reserve_tables`, #1128) holds several fixed-size
tables that are reserved and released together in one mapping: a unit's
preprocessor tables, with its three stacks' token columns and its own file
cache (#652), the output's four token columns, and the parser's token, tree
and postorder columns. Each table starts on a page and is laid out as an
arena of its own of its size would be: base pages up to its `small` (with
`arena_reserve_small`'s minimum and the thread's floor) and, past them, huge
pages from a huge-page boundary. The next table starts at its end, or where its
own base pages end on a boundary; the first table with huge pages sets where in
a huge page the arena's base is, so that it needs no gap. So the mapping is no
larger than the tables' own arenas' (their bytes and a huge page each) for two
tables or more, which a finite `RLIMIT_AS` bounds: rounding each table's end to
a boundary, as the first version did, outgrew a 512 MB limit on a small unit's
replay for its expansion history (#1203's review). A mapping refused for its
size past 64 MB switches the thread to compact reservations, as an arena's does,
and a refused commit sets `arena_commit_refused`, so that the preprocessor
retries with smaller tables. The mapping is `MAP_NORESERVE` (#1521, above),
advised `MADV_NOHUGEPAGE` once, each table's huge pages `MADV_HUGEPAGE`, and one
`mprotect` commits it whole.
So a lane's first unit reserves the preprocessor's 27 tables of records, its
stacks' 12 columns and its file cache, all of them base pages (#652), with one
`mmap`, one `mprotect` and one advice call, where they took up to four calls
each, every one of which takes the address space's lock for writing; and each
unit discards them with one `MADV_DONTNEED`, one TLB flush sent to the other
lanes rather than one for each table. The pool matches it by
`Arena.layout`, a hash of its tables' sizes and base pages: only a reservation
of the same tables takes it, and no ordinary reservation does. The parser's
columns follow their unit's token count, so where the address space is
unbounded the parser rounds each to a power of two, which units of similar sizes
then share; under a finite `RLIMIT_AS` or compact reservations each keeps its
own size. A power of two of bytes, past 64 KB of base pages, ends 64 KB short of
a huge-page boundary, so a column's last huge page is base pages: rounding to the
base pages and a power of two past them instead took back 632 of `sqlite3.c`'s
faults, but no time measurable on the 9700X, and was not kept (#1203's review).
`test-lanes` checks the tables' zero, writable and separate bytes, their pages'
advice against an arena of their own's, the pool's reuse for the same tables
alone, not for another layout, the floor's or an ordinary reservation; for 4,096
random sets of tables a layout in order, aligned and no larger than their own
arenas; in a child under `RLIMIT_AS`, tables refused for their size, which
past 64 MB switch the thread to compact reservations; in a child, 2 TiB of
tables, past any host's RAM and swap, committed and writable at both ends,
`nr` in their `VmFlags`, where commit charge isn't bounded, or refused with
`arena_commit_refused` where it is, neither switching the thread; and, in a
child under an `RLIMIT_DATA` of 64 MB more than its data, 96 MB of tables and
an ordinary arena's push of 96 MB refused without switching it, a reservation
of 64 GiB after them keeping its capacity and its pushes within the limit, and
`arena_capacity_shift` giving the compact shape.

Tests that deliberately
poison unused logical capacity explicitly commit that test memory first; a raw
`base` pointer no longer authorizes touching an owned reservation.

`tests/arena/resources.c` checks inaccessible tails, growth and stable pointers,
zero-filled pool reuse (its prefix writable, and inaccessible again in a child
under a finite `RLIMIT_DATA`), the pool's choice of mapping by base pages,
layout and room, the smallest first and the latest released of equal ones,
borrowed/measuring arenas, overflow rollback, a real
`mprotect` refusal injected with a child-only seccomp filter, and 32 simultaneous
large logical requests under a 2 GiB address-space limit. Another isolated child
refuses large inaccessible mappings with `EINVAL`, exercising the compact retry
without relying on Valgrind's current address layout.

With a compiler path and output directory it runs `-E`, `parse`,
`-fsyntax-only`, object compilation and executable linking, then executes the
result. It also compiles a unit of about 540,000 tokens (100,000 declarations
and a function of 30,000 statements) to an object under a 4 GiB `RLIMIT_DATA`,
or with `--strict` under strict overcommit's headroom (#1521): commit charge
below the preprocessor's full tables, which it must take in with their compact
shape while the unit's other arenas keep their capacities. Each command is
also refused by child-only seccomp rules for inaccessible reservations and
writable commits, with a fatal byte-count diagnostic and without usage text.
The large unit runs once more where the address space refuses the
preprocessor's tables' mapping, which switches the thread to compact
reservations, and under a 2 GiB `RLIMIT_DATA`: each must compile, or fail with
the note that its arenas held 64 MiB each, or with a commit's refusal (the
note, or a fatal `can't commit memory`). The loader is allowed to map its ELF
segments and make RELRO read-only; the test does not assume that a small fixed
address-space limit must always fail.

The baseline unsanitized hosted CI job runs those commands under 2 GiB,
separately under Callgrind, and without `RLIMIT_AS` under strict overcommit with
4 GiB of additional commit headroom. The sysctls are changed only on that
disposable VM and restored before the ordinary suite. Sanitizer shadow mappings
are not subjected to these artificial limits. CI artifacts retain diagnostics;
no throughput improvement is asserted by this change.

Complete IR verification and SSA analysis borrow only output already pushed
through the owner; temporary work also pushes through that owner. They never
write an uncommitted tail or commit half a large reservation just to split it.

## Initial qualification

[Hosted qualification run 41196519751](https://github.com/buster14a/buster/actions/runs/41196519751)
built implementation `7be2d8432d12d46c223fa7bd575aa8019707a901` with official
Clang 23.1.4 for baseline x86-64. All five command modes and the linked executable
passed in each of the three environments above. All ten injected command failures
reported a fatal byte-count diagnostic without usage text. Allocator, mapping
layout, SSA, IR verification, IR metamorphic execution and IR reducer regressions
also passed. This focused qualification does not stand in for the full repository
correctness matrix or the approved-host performance measurements.
