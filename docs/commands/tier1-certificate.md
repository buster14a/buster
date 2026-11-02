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
  and certificate-checks every successful allocation its fixtures make.
- `tier1_certificate_cfg.c` publishes the certificate of `tier1_cfg.c`'s CFG
  allocations: the source from the input, and the stream, entry facts and edges
  from the allocation, whose shape it bounds before reading through it.
  `test-tier1-cfg` proves every allocation (the oracle every 16th,
  `--oracle-every N`), and its mutants, malformed shapes among them;
  `test-tier1-cfg-source` proves every one of real C.
- The emitter checks its allocations under `Tier1EmitOptions.certify`, which its
  tests and the driver's `--verify` set. A refusal is the emission stage's
  cause `certificate`, an internal failure that fails `--tier1 --verify`'s
  compile ([the outcomes guide](../../docs/tier1-outcomes.md)).
- `tier1_problem_certify` is the gate #938's quality publisher must call; no
  publisher exists yet.
- [The certificate guide](../../docs/tier1-certificate.md) records the trust
  boundary, supported forms, the CFG producer, why the checker is scalar and the
  native-emitter bridge still pending for #939.

## Benchmark

`benchmark-tier1-certificate [--only VARIANT] [-E's options] file...` times
allocation, publication, check, both, and `tier1_cfg_verify` per IR instruction
on real C's functions, with the certificates' sizes. Run it under the 9700X's
lock on CPU 2. The 9700X job runs it on the frozen `sqlite3.c` where `changes`'
`tier1-certificate` entry says.
