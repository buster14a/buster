# C coding style

This document adapts the useful conventions from Buster for **buster-fast**. It
is a coding standard, not permission to copy Buster implementations or inherit
its portability layers, build system, or performance-acceptance machinery.

## Language, formatting, and naming

- Production compiler code is **C**. Use Clang as the host compiler;
  `__attribute__`, `ext_vector_type`, AVX-512 intrinsics, and other deliberate
  Clang/x86-64 extensions are allowed. Do not add compatibility wrappers for
  GCC, MSVC, older host CPUs, or non-Linux host operating systems.
- Use four spaces, no tabs, `snake_case` functions and local variables,
  `PascalCase` named structs/types, and `BUSTER_`-prefixed uppercase macros.
- Put opening braces on their own lines. Use braces for conditionals and loops.
  Keep declarations and statements on one line when readable.
- Declare structs explicitly as `typedef struct Name Name;` followed by
  `struct Name { ... };`, matching the existing codebase.
- Use plain `static` for functions private to a translation unit. Do not
  import `BUSTER_F_DECL`, `BUSTER_GLOBAL_LOCAL`, or unity-build linkage macros.
- Use named constants for shared sizes, limits, and invariants. For example,
  prefer `BUSTER_LEX_VECTOR_COUNT`, `BUSTER_LEX_VECTOR_BYTES`, and a derived
  `BUSTER_LEX_CHUNK_BYTES` to unrelated `VECTOR_COUNT` and `VECTOR_SIZE`
  macros. A truly one-off hardware or format literal can remain literal
  with a comment explaining its meaning.
- Open substantial source files with a short orientation comment documenting
  ownership, main entry points, and important invariants. For large files,
  provide a layout map using greppable symbol names rather than line numbers.
  Comments explain *why* and non-obvious safety constraints, not obvious C.

## Explicit control flow

- Use **one `return` per function**, at the end. Compute a result and converge
  on a single exit so eventual cleanup and instrumentation cannot be skipped.
  Avoid unnecessary result initializers when every path already assigns it.
- Prefer direct calls, `if`, `switch`, and loops over callbacks or
  function-pointer dispatch in compiler execution paths. Function-pointer
  *values* in the C program being compiled are not restricted by this rule.
  An eventual worker-thread entry callback at the OS/runtime boundary is fine;
  keep the work inside the entry point explicit.
- Do not recursively traverse input-dependent structures. Use explicit stacks,
  queues, or worklists for include graphs, syntax trees, types, and IR. Trivial
  statically bounded recursion is an exception.
- Prefer explicit structured error propagation to `goto`. A shared error-unwind
  ladder is acceptable where necessary for several acquired resources; do not
  use `goto` to disguise an early return.
- Do not pass increments, assignments, function calls, volatile/atomic reads,
  or expensive expressions to macros that can evaluate their arguments more
  than once. Compute named temporary values first.

## Memory and representation

- Use bounded pointer-plus-length slices such as `String8` for source bytes,
  paths, and diagnostics. Do not use implicit NUL termination within compiler
  algorithms. Adapt to NUL-terminated strings explicitly at libc/syscall
  boundaries when the API requires them.
- Prefer arenas and region-oriented lifetimes to allocations per token, node,
  symbol, or instruction. Allocation failure, length overflow, and capacity
  exhaustion must have defined behavior; do not silently truncate.
- Favor contiguous arrays, compact records, offsets/indices, and explicit
  capacities. Consider struct-of-arrays layouts for operations that scan only
  some fields, but choose layouts based on measured access patterns.
- Separate frequently accessed data from diagnostics and cold metadata when
  that improves locality. Avoid hidden allocation, unnecessary copies,
  duplicate representations, and repeat full-input traversals.
- Use `restrict` only where non-aliasing is actually guaranteed by callers.
  Keep pointer lifetime, alignment, bounds, and aliasing contracts documented.
- Keep existing intentional build semantics (`-fwrapv`, `-funsigned-char`,
  `-fno-strict-aliasing`) unless changed by a deliberate, tested decision.

## SIMD and Zen 5

- Optimize the host implementation for Zen 5: use AVX-512 intrinsics and
  `ext_vector_type` directly where they pay for themselves. Do not create a
  cross-architecture SIMD abstraction or runtime CPU dispatch by default.
- Choose data layout and batch boundaries before choosing instructions.
  Prefer vectorized classification, masks, population counts, and compaction
  when they eliminate useful work, not just when they look branchless.
- Every vector load must be within readable memory, including on the final
  partial chunk. A `mmap` length or a `String8.length` does **not** authorize
  a full-width load across the end of the logical input. Use a proved padding
  contract, a bounded tail buffer, or masked loads as appropriate.
- Spell out assumptions about sentinel bytes, alignment, vector widths,
  overlap, and inactive lanes. Test empty inputs, short inputs, page edges,
  and lengths around every vector/chunk boundary.
- Keep host ISA, generated-code ISA, and target ABI conceptually separate.
  Targeting Zen 5 for the compiler executable does not force every generated
  program to require AVX-512.
- Performance claims require a repeatable benchmark and, for significant SIMD
  changes, inspection of emitted assembly. Measure single-thread throughput,
  scaling, bytes scanned, allocations, and correctness; do not assume wider
  vectors or fewer branches automatically win.

## Correctness, diagnostics, and dependencies

- Invalid C input should produce diagnostics, not assertions. Assertions are
  for broken compiler invariants. Distinguish recoverable errors from internal
  failures in API result types.
- Compiler/library routines should not print progress or debug traces on
  stdout. Reserve stdout for intentional compiler output; send diagnostics
  to stderr and gate optional instrumentation explicitly.
- Keep work ownership and synchronization explicit when adding threads.
  Outputs and diagnostics should be reproducible regardless of scheduling.
- Build with warnings treated as errors. Add narrow regression tests for
  new behavior, especially malformed inputs, bounds, and integer overflow.
  Use the repository's existing build/smoke commands until it grows tests.
- Do not add dependencies or vendor third-party code casually. Any deliberate
  external import requires license verification, clear ownership, and a
  separate review. Do not change repository licensing without authorization.
- Keep the project a small Linux/Zen 5 C compiler; do not import unrelated
  modules, CI matrices, portability frameworks, or historical Buster rules.
