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
