# Platform and backend boundaries

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Platform and backend boundaries

- In rendering and windowing, keep platform-neutral policy and data flow in
  the module front door (`rendering.c`, `window.c`). Native API calls
  belong in the selected backend implementation.
- Rendering backends live in `src/buster/lib/rendering/*.c`; window backends
  live in `src/buster/lib/window/*.c`. These are implementation files
  included by their owning module, not standalone CMake modules, so do not
  register them or add them to the unity-build include list.
- Every backend `.c` includes its directory's `internal.h` before its
  implementation declarations. Those private headers own the shared types,
  helper declarations, and native headers needed for clangd to parse a backend
  independently; do not depend on declarations only earlier in the owning `.c`.
- Share CPU-side draw generation, font/texture orchestration, event-list
  ownership, and lifecycle policy. Keep device resources, synchronization,
  swapchains, native event translation, and native handles backend-specific.
- TrueType bitmaps accept finite scales from zero through
  `BUSTER_TTF_MAX_SCALE` (65536). Zero on either axis, empty glyphs, invalid
  bounds/scales and exceeded budgets return an all-zero bitmap. Admission
  checks ordered glyph bounds, rounded integer endpoints, dimensions of at
  most 4096 per axis and at most 1,048,576 one-byte pixels before allocating
  outline or bitmap storage. Quadratic outlines are flattened iteratively in
  device space: every ordinary chord has at most 0.25 pixel geometric error,
  while ten subdivision levels cap one source curve at 1,024 segments. A
  count-then-emit pass allocates the exact path and rejects more than 1,048,576
  raster points; a conservative limit of 67,108,864 scanline edge-search steps
  further bounds raster work. The headless `truetype_tests` module covers these
  contracts, including scale-sensitive curve goldens, the subdivision cap and
  a deterministic malformed-parameter sweep in sanitizer and fuzz-enabled CI
  configurations.
- The active headless `ide` target has no production TrueType caller. It adds
  the `truetype` module exactly when `BUSTER_INCLUDE_TESTS` is enabled, for the
  registered `truetype_tests` consumer; use value tests (`#if`), because CMake
  always defines this macro as either zero or one. Android and iOS add `window`
  for process lifecycle only and do not add a font consumer. The reusable
  `truetype` module and dormant `font_provider` source remain available for a
  future product target, which must register both dependencies explicitly.
  `tests/truetype_dependency_test.py` checks the generated split graphs,
  builds the tests-disabled split compiler in Debug and Release, and checks
  the actual Release unity preprocessing closure for tests on and off.
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.

## Transactional process spawning

`os_process_spawn` validates all bounded argv and environment strings before it
allocates a pipe or initializes a platform spawn object. Empty argv, embedded
NUL bytes, mismatched key/value counts, empty keys and keys containing `=` are
recoverable failures. `ProcessSpawnResult.failure` identifies the first failed
validation/setup stage and `error` preserves its native error; zero/`NONE` are
reserved for success. Every failure path destroys only objects that completed
initialization and closes every pipe or temporary handle it created.

Executable selection is a separate, explicit policy. With `search_path` clear,
`argv[0]` is made into one exact absolute path and the OS is never asked to
search. With it set, a bare name is resolved once against the environment
captured at program entry, before setup begins; the child environment cannot
redirect that lookup. `use_process_environment` independently selects full
inheritance. Otherwise the supplied key/value slices are the complete child
environment, and an empty Windows block is represented by the required two
UTF-16 NUL code units.

Children inherit only standard streams selected by the caller. Captured pipe
ends are first moved above descriptors 0-2, so a parent with closed standard
streams cannot make `dup2` alias a pipe end that is subsequently closed. Linux
uses a close-from spawn action, Apple uses `POSIX_SPAWN_CLOEXEC_DEFAULT` plus
explicit standard-stream inheritance, and Windows passes only duplicated
standard handles and captured pipe ends through
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Parent pipe ends are non-inheritable and
all temporary duplicates are closed after `CreateProcessW`.

GPU tool execution opts into captured-PATH lookup for the tool itself, then
passes a fixed SDK/locale/temporary-directory environment allowlist rather than
the complete compiler environment. Registered `os_tests` inject each setup
failure, compare live descriptor/handle counts, exercise an unrelated
inheritable object, an exact hostile-PATH environment, and a subprocess that
closes descriptors 0-2 before spawning with capture.

## Virtual memory commitment and prefaulting

`os_commit` reports commitment and nothing else. Its `prefault` argument asks
for best-effort prefaulting of the committed range; that request is issued only
after the commit itself succeeded, and its outcome never reaches the returned
boolean, so an advisory refusal can neither fail a commit that worked nor stand
in for one that did not. A caller that needs the outcome calls `os_prefault`
directly and reads its three documented results: `OS_PREFAULT_POPULATED`,
`OS_PREFAULT_REFUSED` (the platform rejected the request) and
`OS_PREFAULT_UNAVAILABLE` (this build has no prefault facility for its target,
so no request was issued at all).

Prefaulting only populates page table entries for a range that is already
committed. It is not residency, not a page lock, not protection from paging or
swap, and not a latency guarantee; the OS may reclaim a populated page
immediately afterwards. There is no lifetime page-locking option and no
`lock_pages` alias that would imply one. Observed behavior differs per target
and each one refuses for ordinary reasons:

- **Linux** uses `madvise(MADV_POPULATE_WRITE)`, which states exactly this
  intent and locks nothing. Kernels before 5.14 do not know the advice and
  reject it with `EINVAL`, which is a refusal of the request.
- **macOS** has no populate advice, so the range is `mlock`ed and immediately
  `munlock`ed; the lock is only what forces the faults and is released before
  returning. `RLIMIT_MEMLOCK` bounds an unprivileged process, so refusing a
  large range is ordinary, and a failed release is reported as a refusal rather
  than as a populated range.
- **Windows** exposes no supported populate call, and `VirtualAlloc(MEM_COMMIT)`
  has already charged the backing store. An MSVC build can force the faults by
  registering the range as a Winsock Registered I/O buffer and deregistering it,
  which requires both extension functions and a length that fits a `DWORD`;
  that table exists only once the process obtained them, so the ordinary
  Windows outcome is `OS_PREFAULT_UNAVAILABLE`.

`ArenaFlags.prefault_pages` forwards that same advisory request for an arena's
initial commitment and every later growth. Ordinary arenas are unaffected: they
ask for nothing and issue no request. A refused or unavailable request still
yields a fully committed, fully usable arena, so no allocation can observe the
difference, while a genuine commit failure stays fatal at the allocation site
with the `arena commit failed` diagnostic. An arena that asked for prefaulting
is neither served from nor parked in the destroy-side reuse pool, because the
pool hands an arena back without reissuing the request.

Registered `arena_tests` and `os_tests` cover this through the `os_internal.h`
prefault seam, which overrides the reported outcome of exactly one request on
the calling thread and counts the requests actually issued. That proves a
disabled flag issues nothing, a refusal leaves commitment and allocation state
intact, and a real commit failure is reported without an advisory request
having been made — none of which needs privileges or real memory exhaustion.
The `commit_prefault` child-process failure mode asserts the fatal commit
diagnostic with prefaulting requested.

## Exclusive directory ownership

`os_make_directory_exclusive` is the namespace-ownership primitive for private
workspaces. It succeeds only when the call created the exact requested name,
uses mode 0700 on POSIX, and distinguishes a pre-existing file, directory, or
link from other errors. Unlike `os_make_directory_attempt`, an existing entry
never counts as success and therefore never authorizes cleanup. Callers retain
the successfully claimed path and delete only that tree.

## File transfer completion

`os_file_open_checked`, `os_file_write_checked`, `os_file_close_checked` and
`os_file_flush` return captured OS errors (zero means success). Write results
retain the exact prefix transferred before an error. Interrupted transfers
retry, partial transfers advance, and zero progress is an error. Close consumes
the handle even on failure and is never retried. `file_write_checked` preserves
a write error over a later close error; `file_write` exposes the same completion
contract as a boolean. Executable/PDB writers use the checked file helper with
execute permission. Compiler artifact branches report `driver.file-write`;
linker writers retain `link.file-write`. Metadata/import and copy callers check
their existing boolean results, which now include close completion.

Writes are synchronous descriptor transfers, without a userspace output buffer.
Flush (`fsync`/`FlushFileBuffers`) is explicit and reports errors; ordinary
artifact writes do not add a durability flush. A failed write can leave an
empty, partial, or complete destination, and a close failure still fails the
operation. Atomic old-or-new replacement remains the separate #83 contract.
Console printing retains its always-on failure wrapper.

Registered file/diagnostic tests use `os_internal.h` scripts scoped to one
calling thread and exact opened path. Short writes perform real bounded writes,
interruptions resume the transfer loop, and injected close failures release
real handles. Scripts are excluded when `BUSTER_INCLUDE_TESTS` is disabled.
The tests cover prefix bytes, first-error precedence, empty/large output,
flush, copying and preprocess/assembly/object/link output in all allocators.

## File read boundaries

`os_file_read_some` returns after one successful transfer; `os_file_read_exact`
fills the buffer or stops at EOF/error. Both expose transferred bytes, an
explicit OK/EOF/ERROR status, and the native error captured before cleanup.
Empty requests succeed without touching the descriptor. POSIX interruptions
retry; Windows broken-pipe/file EOF is clean termination and message-pipe
`ERROR_MORE_DATA` retains successful prefix progress. `os_file_read_attempt`
remains a boolean compatibility wrapper for streaming copy/hash consumers.
The ambiguous count-only `os_file_read` API has been removed.

`os_file_get_stats` exposes `valid` and a captured error. Its size convenience
returns `UINT64_MAX` on failure; size consumers reject it. `file_read_checked`
requires exactly the initial nonzero size. Growth after that size snapshot is
excluded; truncation producing early EOF fails. Size-zero descriptors (including
procfs and pipes) instead stream until clean EOF. Failure, including a delayed
close failure, returns no bytes and rolls back the read's arena allocation;
successful empty files retain a nonnull pointer. `file_read` preserves this
success/failure distinction through its slice pointer.

Successful descriptor-backed `file_read_checked` and `file_map_read` results
also carry `FileIdentity` captured before that same descriptor is closed. POSIX
uses `st_dev`/`st_ino`; Windows uses the volume serial number and 64-bit file
index. Consequently lexical spellings, hard links, followed symbolic links and
case aliases on case-insensitive filesystems compare as one physical file.
Android APK assets have no descriptor identity and retain their normalized asset
path namespace. Identity is cleared with bytes on failure, and mappings retain
the existing stable-input requirement rather than promising isolation from
concurrent replacement.

Mappings still require stable input files for their full consumer lifetime;
they do not promise an atomic snapshot under concurrent mutation. When mapping
is unavailable, compiler source/object/include/library loading uses the checked
read contract through `file_map_read`'s existing fallback. The private script
can make a mapping unavailable to test that real fallback. Registered tests
also model stale stat sizes for growth/truncation deterministically, check
prefix errors/interruption/EOF/stat/close failure and allocation rollback, and
exercise actual compiler source and object input diagnostics.
