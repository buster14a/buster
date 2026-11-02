# assembler

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/assembler.c` is the callable GNU assembly frontend, with
  AVX-512 and scalar paths and shared form matching in `x86_match.c`, whose
  value immediates follow #221's rule (`x86_immediate_encodes`).
  `./build/compiler assemble -o output.o input.s` uses it through the existing
  executable. `test-assembler --cross-check` compares both paths and GNU as,
  and links/runs fixtures; `benchmark-assembler [--external]` reports phases,
  initialization/memory and optionally fresh-process GNU as/Clang comparisons.
  Run these through the shared lock; pin benchmarks to CPU 2. The supported
  syntax, limitations, example and measurement method are in
  [docs/assembler.md](../../docs/assembler.md). `.S` uses the shared preprocessor's
  explicit assembly mode, including `-I`, `-D`, `-U` and source line markers.
