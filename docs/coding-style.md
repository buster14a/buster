# C coding style

This document adapts useful conventions from **buster-legacy** for **buster**. It
is a coding standard, not permission to copy buster-legacy implementations or
inherit its portability layers, build system, or performance-acceptance machinery.

## Language, formatting, and naming

- Write code in **C** unless the goal or action explicitly disallows C or
  requires another language. This applies to supporting tools, tests, and
  automation as well as compiler code.
- Production compiler code is **C**. Use Clang as the host compiler;
  `__attribute__`, `ext_vector_type`, AVX-512 intrinsics, and other deliberate
  Clang/x86-64 extensions are allowed. Do not add compatibility wrappers for
  GCC, MSVC, older host CPUs, or non-Linux host operating systems.
- Use four spaces, no tabs, `snake_case` functions and local variables,
  `PascalCase` named structs/types, and `BUSTER_`-prefixed uppercase macros.
- Put opening braces on their own lines. Use braces for conditionals and loops.
  Prefer single-line declarations and statements, including assignments,
  expressions, and function calls. Keep separate statements on separate lines.
  Split a statement only when doing so improves readability, rather than merely
  to meet a line-length target.
- Prefer single-line function headers, including the return type, function name,
  and parameter list, for both declarations and definitions. Keep the opening
  brace of a definition on its own line. Split a header only when it improves
  readability.
- Put each field in a `struct` or `union` on its own line, with one field per
  declaration. Put each aggregate initializer entry and each enum enumerator on
  its own line as well; apply the same layout to nested aggregates.
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
- Design every stage data-parallel, vectorized, and branch-free first, as
  `src/buster/compiler/c_lexer.c` and `x86_64_encoder.c` are built. The tools
  include masks over many items, prefix sums, in-register permutes as table
  lookups, compress and expand, sorting and segmented scans, bitsets,
  nearest-smaller-values, and pointer jumping. Do not assume that a stage is
  inherently sequential: if it does not vectorize, the right data and
  algorithm have not been found yet. Where C fixes an order (macro state,
  typedef visibility), vectorize within each step and keep the ordered part
  small. Whole compilers have been built this way: Co-dfns (Aaron W. Hsu, "A
  Data Parallel Compiler Hosted on the GPU", 2019) and Pareas (Voetter,
  Huijben, and Rietveld, "Compilation on the GPU? A feasibility study", 2022);
  for expression trees, see Bar-On and Vishkin, "Optimal parallel generation
  of a computation tree form" (TOPLAS, 1985).
- Give each vector path an exact scalar path for the rare cases, which is also
  its twin and test oracle: both must produce identical output or, where the
  two algorithms legitimately differ, pass a semantic check. Compare the two
  on the 9700X: a part that stays scalar says why with those measurements, as
  "SIMD: what pays" in `docs/object-writer-and-linker.md` does.
- Design with Zen 5's measured costs from uops.info. On `zmm`, `vpgatherdd` is
  64 uops, one every 12.6 cycles or so, and `vpscatterdd` is 88 uops, one every
  17, so use scalar loads and stores or in-register permutes instead.
  Register-form `vpcompress*` and `vpexpand*` are 2 uops, but `vpcompress*` to
  memory is 8. `vpermb`, `vpermw`, `vpermd`, and `vpermi2b` (a 128-byte table)
  are 1 uop each, and `vpmullq` is 1 uop with a 2-cycle latency.
- Account for energy per unit of useful work as well as µops and latency in
  hot vector loops. [#199's 9700X measurements](https://github.com/buster14a/buster/issues/199#issuecomment-6297314071)
  are consistent with a per-core power cap slowing heavy AVX-512 work after
  lighter phases, including light vector code, have let the clock boost.
  Reduce unnecessary 512-bit loads and stores, spills and reloads, and repeated
  or overlapping stores. Keep loop-carried state in registers, and choose batch
  widths and unrolling from measured throughput in that workload; extra ILP
  can lose if it adds spills. The inferred mechanism and measured power levels
  are not an AMD specification or a universal threshold.
- Measure vector stages both back to back and immediately after 100–300 ms
  of warm lighter work, using a scalar twin or a representative preceding
  phase. Report nanoseconds as well as cycles, compare across several
  processes, and distinguish the initial transition from sustained execution.
  The encoder and dataflow benchmarks provide `--only` for isolated runs
  and `--after-scalar MS` for conditioning before each timed vector sample;
  their default interleaved runs do not establish either state alone.
  Follow [the encoder's methodology](x86-64-encoder.md#both-states) and
  [the dataflow measurements](dataflow.md#the-power-caps-state). Interpret
  raw event `0x1000008a0` only as an empirical indicator alongside clock,
  energy and temperature measurements; its throttle interpretation is
  inferred, and a zero reading can mean the counter was unavailable.
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
