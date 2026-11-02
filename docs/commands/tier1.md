# tier1

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- Tier 1 selection publishes effective uses for `tier1_liveness.c`; the native
  consumer connects required locations, allocation, frames and spill/remat
  records. Its vector path covers the integer core by bottom-up rewriting over
  state tables `tier1_select_initialize` compiles from a grammar of the
  candidates' patterns (`tier1_select_burs.c`, baked; labeled 16 nodes a depth
  level), maximal munch its scalar twin and its fallback; `test-tier1-select`
  checks the tables and the kernels' twins, which `test-tier1-pipeline` and
  `benchmark-tier1-select-source` check on every function they plan. Bounded
  direct scalar calls preserve live caller registers and publish symbolic CALL
  references. Byte/word memory and integer conversions
  use canonical 32-bit storage; terminal chains retain explicit traps or empty
  self-loops. Every other function, general CFGs with loops and phis, goes
  through `tier1_emit_cfg.c` (#140): `tier1_cfg.c` allocates it and its edges
  move values, before a jump, at a successor's start or in a split critical
  edge's stub. Long double, selects of floating values, switches and the
  convention's rarer aggregate shapes still refuse explicitly ([the Tier 1
  guide](../../docs/tier1.md)'s list). AVX-512's
  masks (#179) are selected (`kmov`, `kand`, `kor`, `kxor`, `knot`, and loads,
  stores, constants and bitcasts to and from integers of every width) and
  allocated in the opmask class by the CFG consumer, which moves and spills them
  at their widths and saves them around calls; vectors are refused (#141). Run
  `test-tier1-select`, `test-tier1-liveness`, `test-tier1-emit`,
  `test-tier1-pipeline --cross-check` and `test-tier1-masks` (hand-written mask IR
  natively against the IR's interpreter; Tier 0's mask programs and every `_k*`
  builtin's row through the IR, SSA and the optimizer to Tier 1's native code) for bounded typed-stream/native
  validation; `benchmark-tier1-pipeline [--clang PATH] [--repeat N]` times the
  pipeline's fixtures by Tier 1, Tier 0 and clang `-O0`/`-O2` (bounded kernels,
  not programs; under the 9700X's lock, CPU 2), and `benchmark-tier1-select-source
  [--only select|select-scalar] [-E's options] file...` selection's core cycles
  per IR instruction on real C's functions, as `--tier1` prepares them, by both
  paths (#141). A division by a constant that isn't a power of two is a
  multiplication by its reciprocal (Granlund and Montgomery), in either consumer,
  which saves the values `mul`'s `rax` and `rdx` would clobber (the [selection
  guide](../../docs/tier1-selection.md)). `measure-mask-spills` times a mask's spill and
  reload, to memory against to a general register (`mask_spill_measure.c`, #179's
  measured choice; the 9700X job runs it, `test-mask-spills` checks its programs).
  `test-tier1-model` checks the emitted-case measurement reader;
  `measure-tier1-model --manifest PATH --verify` constructs its eligible
  experiments without reporting timings. See [the Tier 1 guide](../../docs/tier1.md)
  for supported inputs and the complete C-source acceptance still pending.
