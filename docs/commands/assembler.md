# assembler

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `src/buster/compiler/assembler.c` is the callable GNU assembly frontend, with
  AVX-512 and scalar paths and shared form matching in `x86_match.c`, whose
  value immediates follow #221's rule (`x86_immediate_encodes`). Its syntax is
  the form table's own (#1134, `x86_match_supported`: every 64-bit form whose
  operands the parser reads), intersected with the target table, which fails
  closed; a new form that the target table classifies is admitted, and
  `assembler_round_trip_test.c` round-trips it.
  `./build/compiler assemble -o output.o input.s` uses it through the existing
  executable. `test-assembler --cross-check` compares both paths and GNU as,
  round-trips every admitted form in Intel and AT&T syntax (against GNU as with
  `--cross-check`), and links/runs fixtures. With `--cross-check` it also
  assembles GCC's and Clang's `-S` output of `examples/assembler/program-*.c`
  (`assembler_programs_test.c`, #1115: 48 sources, at `-O0`, `-O2`, `-O2 -fPIC`
  and `-O3 -march=x86-64-v4` in both syntaxes), compares each object with GNU
  as's, and runs the program linked from it against GNU as's; on a GNU as that
  pads code unlike binutils 2.47, its padding is taken as ours first. It also
  compares 300 random programs of labels, branches, filler and int3-filled
  alignments with GNU as, whose branch sizes follow GNU as's pass order (the
  round-based layout before it differed on about 2 in 100).
  `benchmark-assembler [--external]` reports phases,
  initialization/memory and optionally fresh-process GNU as/Clang comparisons.
  Run these through the shared lock; pin benchmarks to CPU 2. The supported
  syntax, limitations, example and measurement method are in
  [docs/assembler.md](../../docs/assembler.md). `.S` uses the shared preprocessor's
  explicit assembly mode, including `-I`, `-D`, `-U` and source line markers.
