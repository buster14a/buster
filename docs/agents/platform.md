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
  raster points. Compound glyphs align unsigned byte/word indices after
  applying component matrices, including nested compounds. A parent index at or
  past the points accumulated so far, or a child index at or past the child's
  point count, selects an unhinted phantom point (pp1 left origin
  `xMin - lsb`, pp2 `pp1.x + advance`, pp3/pp4 the vertical top/bottom) in that
  glyph's own units, moved by the child's matrix; indices beyond the four
  phantoms are invalid. Phantoms come from the glyph's glyf bounds, `hmtx` and,
  when `vhea`/`vmtx` exist, `yMax + topSideBearing` and `pp3.y - advanceHeight`;
  otherwise hhea ascent/descent. A composite's phantoms come from its own
  metrics, or from the latest component with `USE_MY_METRICS`, untransformed as
  FreeType does. Hinting is never evaluated, so phantoms are never
  instruction-adjusted (tracked by
  [#2138](https://github.com/buster14a/buster/issues/2138)); fonts whose
  anchors depend on hinted phantoms are unsupported. An explicit stack admits
  up to eight component levels; retained
  points are capped at 1,048,576, while glyph visits, anchor searches
  and decoded/translated points share a 9,437,184-unit work budget. Glyph-local reads
  and instruction-payload ranges are checked. Invalid anchors return an
  all-zero bitmap and roll back their arena allocations. A
  conservative limit of 67,108,864 scanline edge-search steps
  further bounds raster work. The headless `truetype_tests` module covers these
  contracts, including point/XY attachment equivalence, phantom-point
  anchors against an in-memory font with and without `vmtx`, transformed/nested
  unsigned indices, scale-sensitive curve goldens, the subdivision cap and
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
- TrueType horizontal kerning uses the legacy version-0 `kern` table's
  format-0 subtables. Matching pair values add in subtable order; the override
  coverage bit replaces the accumulated value, and later subtables can add to
  that replacement. Missing pairs leave the accumulation unchanged. Vertical,
  minimum-distance, cross-stream and other-format subtables are ignored by
  this advance-only API. Pair arrays must fit their declared subtable length;
  malformed arrays contribute nothing. Registered `truetype_tests` cover
  additive order, overrides, absent pairs and coverage filtering using
  synthetic font bytes, with no dependency on installed fonts.
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.

## Host CPU probing

`entry_point` resolves host CPU facts through `target` and the selected
`x86_64` or `aarch64` probe module. Split consumers register that architecture
module; unity `target.c` includes it. The x86-64 probe owns CPUID/XGETBV and
model/feature/brand queries without a compiler dependency. The existing
`x86_64_encode_register_operation` declaration in `x86_64.h` is implemented by
`compiler_assembly_metadata`; instruction-encoding consumers register that
compiler module, while CPU-probe consumers do not.

## Transactional process spawning

`os_get_environment_variable` searches the environment snapshot captured at
entry. Windows names compare without case (`SystemRoot`/`SYSTEMROOT`,
`PATH`/`Path`), using ordinal Unicode mapping for non-ASCII differences;
POSIX names remain distinct. ASCII and byte-equal names need no allocation;
the Unicode fallback uses temporary scratch or a private arena when no thread
context is selected. The first matching captured entry
wins, including an empty value, and the returned slice preserves its original
bytes and pointer. Missing or empty query names return a null-empty slice.
Registered `os_tests` cover these rules with serially replaced snapshots and
restore the original environment before any other test runs.

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
uses a close-from spawn action under glibc, other Linux libcs close an
enumerated `/proc/self/fd` snapshot (a failed `readdir` fails the spawn rather
than yielding a partial set), Apple uses `POSIX_SPAWN_CLOEXEC_DEFAULT` plus
explicit standard-stream inheritance, and Windows passes only duplicated
standard handles and captured pipe ends through
`PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Parent pipe ends are non-inheritable and
all temporary duplicates are closed after `CreateProcessW`. A snapshot cannot
be atomic with a concurrent open, so every first-party POSIX descriptor is
created close-on-exec (`O_CLOEXEC`, `pipe2`, `F_DUPFD_CLOEXEC`); the standard
streams reach the child through `dup2` file actions, which clear the flag on
the target.

GPU tool execution opts into captured-PATH lookup for the tool itself, then
passes a fixed SDK/locale/temporary-directory environment allowlist rather than
the complete compiler environment. Registered `os_tests` inject each setup
failure, compare live descriptor/handle counts, exercise an unrelated
inheritable object, an exact hostile-PATH environment, and a subprocess that
closes descriptors 0-2 before spawning with capture.

Process cancellation state uses `ProcessControlAtomic`: `BUSTER_SINGLE_THREADED`
retains volatile `s32` storage for the existing signal-handler accesses, while
threaded builds retain `AtomicU64`. The load explicitly converts the signed
serial value directly to `u64`, preserving C's numeric conversion modulo 2^64.
Serial stores and set-if-zero already convert their input to `s32`; callers
share zero, one and positive signal numbers representable in that storage.
Registered `os_tests` cover load/store/set-if-zero in both modes, plus serial
negative-value loads with independent numeric expectations. Serial builds use
the same lane path as a one-lane gang, as described in [parallelism](parallelism.md).

## Linux process-group census reads

A procfs task can disappear after its stat/status descriptor opens. Both the
ordinary read and the capacity probe classify native ESRCH as disappearance;
the caller retries its unchanged bounded, complete two-snapshot census.
Open-time ENOENT/ESRCH retain the same meaning. Empty files, oversized content,
malformed records, other read errors and close failures remain failures, and a
failed read never publishes a length. This does not relax leader reservation,
namespace identity, member-state or final ownership checks.

Registered `os_tests` retain an unread stat descriptor for an exited child,
reap that exact child, then exercise the production descriptor reader. Both
normal and capacity-probe reads must observe ESRCH without publishing a length.
Live, empty, oversized and invalid-descriptor controls distinguish disappearance
from other outcomes. The existing synthetic census-churn fixture remains a
separate check. This regression proves the read-after-open defect; the original
#2380 CI failure had no stage detail and is not attributed conclusively to it.

Linux process-group cleanup reads `self/status` from its retained procfs
descriptor. `NSpid` lists the procfs mount's namespace followed by successively
nested namespaces; the final coordinate is the caller's active namespace and
must equal `getpid()`. Numeric IDs may repeat across levels. Context selection
uses that ordered coordinate, retains the procfs and PID-namespace identities,
and rejects missing/malformed/duplicate fields or a mismatching final ID. Leader
identity checks, exact-child reservations and the two matching census snapshots
remain required before successful cleanup.

Registered Linux `os_tests` exercise the production raw-status context selection
with unique/repeated IDs, nonfinal-only matches, invalid identity prerequisites,
zero/negative/mismatching current IDs and malformed fields. A real procfs context
open/close control checks descriptor release; the exited private-group control
uses a three-second deadline. These fixtures do not create nested PID namespaces
or claim attribution for unrelated historical process-group failures. The field
ordering follows the Linux man-pages project's
[`proc_pid_status(5)`](https://man7.org/linux/man-pages/man5/proc_pid_status.5.html)
and namespace-local numbering follows
[`pid_namespaces(7)`](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html).

## Captured-payload storage

The Windows and POSIX capture collectors pack retained stdout and stderr into
16 KiB payload blocks. Each stream fills its last block before another block is
allocated, independently of native read boundaries. Empty reads and bytes past
the capture limits allocate no retained-payload storage. Prefixes, shared-quota
admission order, overflow policy and observed/captured/streamed/dropped counters
keep their existing meanings. Overflow-file descriptors remain caller-owned.

`STREAM_TO_FILE` requires a regular-file descriptor for each captured stdout or
stderr stream, even when its configured quota would avoid overflow. Admission
runs after policy/argv/environment validation and before executable lookup or
platform process setup. POSIX checks descriptor metadata with `fstat`; Windows
requires `GetFileType` disk classification before checked file metadata. Missing
or nonregular sinks fail with `PROCESS_SPAWN_FAILURE_CAPTURE_SINK` and the native
invalid-argument error; failed metadata/type queries preserve their native
errors. Uncaptured sinks and all sink fields under truncate/fail are ignored.
Refusal launches no child and acquires no process pipes or containment objects.

The caller must keep the original borrowed descriptors open, writable and
unrebound through wait, and must not change their flags while the operation is
active. Admission does not seek, write, flush, close or change those flags.
Synchronous regular-file storage and metadata I/O can delay deadline/cancellation
servicing; this policy rejects stream backpressure and does not promise a hard
wall-clock bound on storage or operating-system scheduling. Spill write failures
still preserve native child status, explicit capture failure and exact transferred
and dropped-byte counts while owned process cleanup continues.

Registered desktop `os_tests` run sink-refusal probes inside a first-party helper
with its own 30-second outer deadline and private group/job. Inner probes inherit
that containment. POSIX fills an actual pipe to `EAGAIN`, restores its original
blocking flags, and checks refusal for stdout, stderr and valid-first/bad-second
sinks without a child marker. Actual FIFO/socket and Windows pipe/character
handles supplement missing/invalid/query-error controls. Descriptor/handle census,
identity and flag controls check caller ownership. Separate literal both-stream
regular spill controls cover partial writes, real read-only-descriptor write
failure, caller reuse, platform deadline cleanup and POSIX flag cancellation.
Mobile retains production compilation and existing collector coverage; these
desktop process fixtures do not claim mobile process execution.

For retained stream lengths `R_s`, the collector allocates exactly
`N = sum(ceil(R_s / 16384))` chunk headers and payload blocks. With `R` total
retained bytes, `S` nonempty streams and header size/alignment `H`/`A`, requested
scratch-arena storage, including alignment padding, is at most
`R + S * 16383 + N * (H + A - 1)`. Only each stream's last block can contain
unused payload capacity. Flattening separately allocates `R` bytes in the
caller's arena before capture scratch storage is released. This bounds requested
collector storage rather than arena committed pages, process RSS or all memory
used by the invocation; explicitly unbounded capture limits still admit
unbounded retained output.

The private `BUSTER_INCLUDE_TESTS` collector in `os_internal.h` feeds the
production append/flatten boundary and reports chunk counts and requested
scratch bytes before flattening. Registered `os_tests` compare identical inputs
fed whole, one byte at a time and in 97-byte fragments at lengths 0, 1, 16384,
16385 and 1 MiB. They overwrite released scratch storage before checking caller
output ownership. Literal quota controls cover zero/default and `UINT64_MAX`
limits, the 32 MiB default total, interleaved stream admission, truncation and
failure, and exact overflow-file payloads including a partial write failure and
caller descriptor reuse. Existing live-process drain and transport controls
remain required. These allocation controls make no speed or RSS claim.

## Deterministic captured-pipe replay

Ordinary POSIX `os_process_wait_deadline` drains the blocking pipes created by
`os_process_spawn`. `os_process_capture_step` in `os_internal.h` separates
readiness, byte progress, EOF, abandonment and close observations from native
calls. One readiness observation admits one read. A successful short read
advances by its exact count; an interrupted read contributes no bytes and
returns to polling. Poll timeout/interruption can be retried. EOF stops reads;
one close attempt ends descriptor authority, even if that attempt fails.
A close error conservatively leaves release outcome unknown and is never
retried: it cannot authorize reuse of a possibly recycled descriptor.

Transport failure and abandoned draining remain failed after successful child
termination. Successfully drained prefixes are retained and the child's native
status remains separately available. `capture_failed` reports transport/cleanup
failure; abandonment without EOF remains unsuccessful without setting that bit.
A timeout alone preserves the Wasm consumer's existing pre-readiness retry
eligibility. The reducer does not authorize restarting a child whose external
effects may already have happened.
The reducer does not own process-group identity, cancellation signalling,
descendant enumeration or the quiescent-group buffered-byte snapshot.

Registered `os_tests` replay numeric `capture-replay-v1` event/count traces with
an independent ownership/readiness/accounting oracle after every transition.
The bounded worklist enumerates native-permitted prefixes through six events;
deletion minimization retains admission and failure class, including the
invariant class for discovered counterexamples. Shared poll failure is applied
to every open stream. Traces contain no payloads, paths, command lines or user
environment. Tests reject malformed counts, reads without readiness and reuse
after close. They represent idle polls and EINTR, not read EAGAIN on these
blocking pipes. The recorded `WAIT_FAILED, CLOSE_OK` regression is a minimal
poll-resource-failure witness, conditional on a successful child exit.

Read EBADF and close-after-release EIO injections are separate adapter
robustness controls, excluded from native event generation. They test failure
wiring, prefix preservation and uncertain-close handling; they do not prove
that an exclusively owned anonymous pipe naturally returns these errors.
The native fixture observes a real five-byte child's successful exit without
reaping it, then applies a calling-thread, bounded call-index fault. Healthy
controls before and after check real bytes and descriptor census. Existing
real flood, deadlines and process-tree tests remain independent challenges;
simulated success is not complete OS validation. Native fixture execution is
desktop Linux/macOS; the reducer itself is portable C. Windows capture and
group lifecycle are outside this replay model.

Primary contracts are the Linux man-pages `read(2)`, `poll(2)`, `pipe(7)` and
`close(2)` pages (rendered man-pages 6.19 at
[man7.org](https://man7.org/linux/man-pages/)). Short progress, zero-byte EINTR,
blocking-pipe EOF and poll ENOMEM are modeled; generic-file EIO examples are
not transferred to anonymous pipes. At Apple XNU
`f6217f891ac0bb64f3d375211650a4c1ff8ca1ea` (`xnu-12377.1.9`),
[`pipe_close`](https://github.com/apple-oss-distributions/xnu/blob/f6217f891ac0bb64f3d375211650a4c1ff8ca1ea/bsd/kern/sys_pipe.c)
returns zero. That file's header has APSL-2.0 plus John S. Dyson's custom
redistribution terms; no code is imported. The inspected Linux documentation's
per-page licenses/provenance and any unavailable verification remain recorded
on the owning issue/PR rather than inferred as a repository-wide license.

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

## Arena discard and zeroed reuse

`arena_set_position_and_decommit` discards only complete native pages beyond
the retained position. Legal sub-page granularities and non-page-sized
reservations can leave a partial tail page above the discarded range. If any
previous allocation reached that tail, its dirty watermark remains conservative
through rewind, recommit and pooled reuse, so `arena_allocate_zeroed_bytes`
clears the retained bytes. When no dirty bytes survive above the discarded end,
non-Apple platforms can lower the mark to the retained prefix. Darwin preserves
the mark for discarded pages as well because its discard may preserve contents.
A failed discard leaves the logical cursor, committed extent and dirty mark
unchanged; a rewind with no complete page to discard still succeeds.

Registered `arena_tests` exercise actual create/allocate/decommit/recommit calls
for dirty partial tails, complete-page and untouched-tail controls, sub-page
reservations, no-discard rewinds, repeated cycles and retirement/pool reuse.
The private `arena_internal.h` one-shot seam skips one discard attempt on the
calling thread to check failure-state and payload preservation. It does not
represent an observed native OS failure.

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
`OS_FILE_CREATE_MODE_EXECUTABLE`. Handle access, POSIX creation mode, and Windows
read/write/delete sharing are passed independently. POSIX ignores sharing flags;
Windows creation mode inherits the directory ACL and cannot promise private
permissions or an explicit POSIX mode. Compiler artifact branches report `driver.file-write`;
linker writers retain `link.file-write`. Metadata/import and copy callers check
their existing boolean results, which now include close completion.

Writes are synchronous descriptor transfers, without a userspace output buffer.
Flush (`fsync`/`FlushFileBuffers`) is explicit and reports errors; ordinary
artifact writes do not add a durability flush. A failed write can leave an
empty, partial, or complete destination, and a close failure still fails the
operation. Atomic old-or-new replacement remains the separate #83 contract:
`file_publish_*` and `file_copy_checked` close a staging file and rename it
without flushing either, so publication is atomic but not crash-durable (#2621;
see [artifact publication](driver.md#compiler-output-streams)).
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

## Linux image-browser consumer

The opt-in [native image browser](../projects/image-browser.md) consumes
`window` and `rendering_raster` on Linux x86-64/XCB CPU presentation. The existing
`WINDOW` module options link `xcb`, `xcb-imdkit`, `xcb-util`, `xcb-keysyms`,
`xcb-xkb`, `xkbcommon-x11` and `xkbcommon`; the new target adds no native
library dependency. Its own loader uses existing OS threads, one-lane dispatch
and Linux libc synchronization/filesystem calls where no current generic API
fits. It adds no UI/font/Vulkan module to the compiler. Native Xvfb pixel
readback is software-XCB evidence, not GPU or other-platform product support.

## Native XCB fixture lifecycle

The actual raster fixture owns an independent admitted XCB connection through
allocation refusal, repeated WM initialization and both real XIM provider modes.
Default X servers reset when their last admitted client disconnects; an accepted
next client can be closed before its setup completes. The independent owner
keeps that server lifetime explicit. A server-generation atom must survive every
tested cycle. Production initialization still reports unavailable/lost native
connections without reconnect retries.

The window-arena fault fixture must prove its reserve fault was consumed.
`arena_test_cancel_reserve_failure` in the private tests-only arena header
cancels any unused calling-thread injection before another consumer can
allocate. The unavailable-display control distinguishes native admission
refusal from allocation refusal and requires subsequent unpooled allocations
to succeed.

XIM forward and synchronous masks keep their protocol meanings. Native
subscriptions add only valid core forward bits; synchronous masks determine
protocol event flags, not additional X server subscriptions. Both live provider
modes independently check committed text, refusal/recovery, error-free native
requests, input-context teardown, advertisement/selection removal and restored
environment. A separate real client-loss control preserves the fixture owner.

The native workflow runs 64 complete repetitions per profile, requiring
192 lifecycle cases and 128 real encoding cases, and retains native logs plus
Xvfb stderr even on failure. Local fixture invocations default to one complete
repetition; `BUSTER_NATIVE_CAMPAIGN_REPETITIONS` admits only 1..64. A smaller
executed case count fails independently of the assertion total. Existing
five-second phase deadlines, pass bounds, pixel readback and downstream browser
checks remain required. See [#2499](https://github.com/buster14a/buster/issues/2499)
for the before/after experiments and attribution.
