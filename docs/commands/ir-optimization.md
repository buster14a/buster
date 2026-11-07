# ir-optimization

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- The shared IR optimizers provide SCCP, dead-code/CFG cleanup, value numbering
  and conservative loop-invariant motion, private-field replacement and bounded
  translation-unit inlining. `test-ir-optimize`, `test-ir-gvn`, `test-ir-licm`,
  `test-ir-sroa` and `test-ir-inline` verify transformations against independent
  semantic fixtures and the upstream SSA promoter (`test-ir-inline`'s #1282
  fixtures: SIMD callees' rows, vector constants and slots, and by-value vectors
  run on the IR's interpreter through inlining, SROA's `simd`/`flagged` options
  and promotion); `test-ir-optimize` also runs
  #175's hand-written `tests/ir/optimize/*.ir` before/after pairs in the text form
  through both parser paths, both twins, the canonical printer and the
  independent executor (`--print-text` prints them for review).
  #1450's SIMD fixtures exercise constant lane folding against the lane model
  and interpreter, wide phi/bitcast propagation, exact-bit splats, GVN's complete
  vector payload keys and LICM's integer-lane motion on both paths. A floating
  lane folds exactly where the model gives the same bits under every rounding
  and under FTZ with DAZ (4,096 random lane pairs, and every lane fixture), the
  operation's row staying for its effects.
  `benchmark-ir-optimize`
  and `benchmark-ir-optimize-passes` include preparation and rewriting costs.
  [The optimization guide](../../docs/ir-optimization.md) records supported inputs
  and the source-corpus and target measurements still required for #139.
