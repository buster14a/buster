# differential

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `test-differential-harness` validates the Csmith/YARPGen harness with original
  fixtures. `test-differential` requires explicit compiler/tier configuration
  and preserves generators, commands, artifacts and failures. Missing tools or
  executable source lowering return unavailable, never a campaign pass. See
  [the differential guide](../../docs/differential.md). Use the shared 9700X lock per
  run and bounded campaigns that yield to CI.
