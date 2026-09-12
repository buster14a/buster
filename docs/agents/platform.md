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
  outline or bitmap storage. After outline extraction, a conservative limit
  of 67,108,864 scanline edge-search steps bounds raster work. The headless
  `truetype_tests` module covers these contracts, including a deterministic
  malformed-parameter sweep in sanitizer and fuzz-enabled CI configurations.
- Renderers consume window-system handles through `WmNativeSurface`; do not
  reach into `WmHandle` or `WmWindowHandle` from a rendering backend.
