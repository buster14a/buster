# C coding rules

[Agent instructions](../../AGENTS.md) · Paths and commands below are relative to the repository root.

## Core rules

- **Code must stay human-maintainable.** Every major file opens with an
  orientation header — what it owns, its entry points, and, for large files,
  a layout map anchored to greppable definition names, never line numbers.
  Update the header in the same change that moves what it describes. A value
  that must agree across several sites (a chunk size that a capacity
  computation and an emitter both depend on, a limit one file checks and
  another file exploits) is a named constant, not a respelled literal; a
  genuinely one-off format field keeps its literal with a comment naming it.
  Comments state constraints the code cannot show; a claim that has not been
  verified against the code does not get written down.
- **One return per function.** A function has a single exit: compute the
  answer into one result variable, let control flow converge, and `return` it
  once at the end. Guard clauses, mid-loop `return`s, and per-case `return`s in
  a `switch` all become assignments to that variable followed by ordinary
  structured flow — an `else`, a loop condition, a `break`. Prefer restructuring
  the condition over adding nesting; where an early exit skipped work that is
  now merely wasted, keep it skipped with a `done` flag or a loop guard rather
  than reintroducing a second `return`. A single exit is what makes an epilogue
  — a cleanup, a trace point, an arena reset — impossible to leak past. `goto`
  to a shared tail is a second exit spelled differently; it is allowed only for
  the error-unwind ladders that already exist, never as a way to keep an early
  return. Returns in mutually exclusive `#if`/`#elif`/`#else` arms are one
  return — every configuration compiles exactly one of them — so a
  platform-dispatch body already satisfies this rule and does not need a result
  variable threaded through the preprocessor. A `switch` that used to return a
  value per case becomes an undecorated `Type result;`, one `result = ...` per
  case, and `default: BUSTER_TODO();` for the impossible case: `BUSTER_TODO`
  never falls through, which is what leaves `result` definitely assigned without
  a placeholder initializer that a real bug could hide behind. Do not initialize
  the result variable when every path already assigns it: `clang_analyze` reports
  that store as dead and fails the Release tree, and the rule's whole point is
  that the paths, not an initializer, decide the answer.
- `BUSTER_F_DECL` belongs on header declarations only. In a `.c` file, a
  module-local function is `BUSTER_GLOBAL_LOCAL`, and a function that a header
  already declares carries no macro at all — the header declaration is what
  gives it internal linkage in the unity build.

- **C only.** No C++, no exceptions. `-fwrapv`, `-fno-strict-aliasing`,
  `-funsigned-char`.
- **No third-party code.** External code was deliberately removed from the
  tree. Do not add dependencies or vendor libraries.
- **Recursion is forbidden unless it is trivial and statically bounded.**
  Recursive algorithms must represent recursion in data, using an explicit
  stack, queue, or worklist rather than the C call stack. Never use recursive
  calls when the depth depends on source input, runtime data, or another
  unbounded structure.
- **Compiler control flow must not use callbacks or function-pointer dispatch.**
  Use direct calls, loops, switches, and explicit work structures. Function
  pointer values may model the program being compiled, but the compiler itself
  must not execute through them. The one infrastructure exception is the
  uniform SPMD entry passed to `lane_run`; work inside that entry still uses
  direct control flow and an explicit index or range.
- **Warnings are errors** under a very large warning set (see
  `GNU_FAMILY_WARNINGS` in `CMakeLists.txt`), and code must stay clean under
  Clang, GCC, Zig cc, and MSVC. TCC is required only to compile/bootstrap the
  native `build.c` driver through `build.sh`/`build.ps1`; application code is
  not promised to compile with TCC. Avoid compiler-specific extensions unless
  guarded.
- **Idioms**: arena allocation (`<buster/lib/arena.h>`) — no malloc/free churn;
  `String8`/`S8("...")` (defined in `<buster/lib/base.h>`) — no C strings; `STRUCT(Name)`
  declarations; `BUSTER_`-prefixed macros; 4-space indent, snake_case, braces
  on their own line. Prefer function headers, declarations, statements, and
  similar constructs on one line; split them only when doing so is clearer.
  Match the surrounding file.

## String boundaries

String integer parsers take bounded `String8` input. Callers must check
`IntegerParsingU64.status == INTEGER_PARSING_SUCCESS` as well as the consumed
length; overflow consumes the complete digit prefix and saturates the value.
Null-empty slices are valid for string copies. `string_join_arena_attempt`
validates slice pointers, aggregate byte counts, and remaining arena capacity
before allocation, clears its output on failure, and leaves the arena untouched.
The output may alias an input slice. The join and duplicate convenience wrappers
fail the process on invalid input or insufficient reserved capacity. Spelling-space
copies skip empty spans too. String8 characters are single bytes, so joins have
no character-size multiplication.
