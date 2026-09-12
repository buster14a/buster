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
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.

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
requires exactly the initial nonzero size. Growth after that snapshot is
excluded; truncation producing early EOF fails. Size-zero descriptors (including
procfs and pipes) instead stream until clean EOF. Failure, including a delayed
close failure, returns no bytes and rolls back the read's arena allocation;
successful empty files retain a nonnull pointer. `file_read` preserves this
success/failure distinction through its slice pointer.

Mappings still require stable input files for their full consumer lifetime;
they do not promise an atomic snapshot under concurrent mutation. When mapping
is unavailable, compiler source/object/include/library loading uses the checked
read contract through `file_map_read`'s existing fallback. The private script
can make a mapping unavailable to test that real fallback. Registered tests
also model stale stat sizes for growth/truncation deterministically, check
prefix errors/interruption/EOF/stat/close failure and allocation rollback, and
exercise actual compiler source and object input diagnostics.
