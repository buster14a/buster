# tier1-certificate

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Module overview

- `tier1_certificate.c` checks a normalized source/machine-stream allocation
  certificate (#939): block-entry facts, sequential edge edits, simultaneous
  phi renames and concrete exchange/XOR recipes against ABI-backed state, with
  `tier1_certificate_oracle.c`'s dense CFG fixpoint as the independent oracle
  and `tier1_certificate_interval.c` as the current interval allocator's
  fragment adapter. `test-tier1-allocation` runs the stream/mutation suites
  and certificate-checks every successful allocation its fixtures make. [The
  certificate guide](../../docs/tier1-certificate.md) records the trust boundary,
  supported forms and the native-emitter bridge still pending for #939.
