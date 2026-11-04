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
  and certificate-checks every successful allocation its fixtures make. Its
  multiple-home cases prove register and row facts independently, compare all
  alias orders with the dense oracle, and reject stale rows on edges, joins and
  loops (#1270).
- `tier1_certificate_cfg.c` publishes the certificate of `tier1_cfg.c`'s CFG
  allocations: the source from the input, and the stream, entry facts and edges
  from the allocation, whose shape it bounds before reading through it.
  `test-tier1-cfg` proves every allocation (the oracle every 16th,
  `--oracle-every N`), and its mutants, malformed shapes among them, which
  `tier1_cfg_verify` refuses alike;
  `test-tier1-cfg-source` proves every one of real C.
- `tier1_certificate_emitted.c` certifies the CFG consumer's code: its stream
  decoded from the emitted records (each record's origin, `Tier1Origin`, says
  which decoding applies), each node's records regenerated and compared, the
  control flow walked. Shapes it doesn't decode yet (aggregates, dynamic
  frames, variadic definitions, indirect jumps to several labels) leave the
  code uncertified, by reason; `--tier1-report` counts `code_certified` and
  `code_uncertified`. `test-tier1-pipeline` corrupts certified functions'
  records, every prologue and epilogue record among them: a refused mutant
  counts, an accepted one runs natively and must agree with the interpreter.
  `test-tier1-emit`'s call fixture (`tier1_code_certificate_test.c`) leaves an
  argument register stale (a value passed twice) and leaves out saves and
  spills around calls: each must be refused. Its compare-exchange fixture keeps
  a value in rax across a `lock cmpxchg`, which keeps rax in r11, and must be
  certified; the rule that keeps rax (`tier1_cert_emitted_kept`) is checked on
  its own cases too. The carried-call-row fixture proves a value in both its
  register and row, then corrupts the owner, predecessor store and supplied
  assertions, with and without a frame pointer and through both emitter paths.
  `test-tier1-emit` also requires zero repeat stores for a single earlier
  predecessor, while joins and backward-layout predecessors still store.
- The emitter checks its allocations, and the CFG consumer's code, under
  `Tier1EmitOptions.certify`, which its tests and the driver's `--verify` set. A failure is the emission stage's
  cause `certificate`: a refusal (`TIER1_EMIT_CERTIFICATE`) is internal, and
  the checker's capacity (`TIER1_EMIT_MEMORY`) a resource failure. Either fails
  `--tier1 --verify`'s compile ([the outcomes
  guide](../../docs/tier1-outcomes.md)).
- #938's harness allocates every problem it adapts through
  `tier1_problem_allocate`, by both allocator paths, and reports an allocation
  only past `tier1_problem_certify`:
  - `tests/tier1-problem/report.c` reports problem files;
  - the integration fuzzer certifies every problem it decodes;
  - `tests/tier1-problem/test.c` certifies its fixtures and generated imports.

  The `Tier 1 machine-problem interchange` workflow runs all three.
- [The certificate guide](../../docs/tier1-certificate.md) records the trust
  boundary, supported forms, the CFG producer, why the checker is scalar and the
  emitted code's certificate, with what it doesn't decode yet.

## Benchmark

`benchmark-tier1-certificate [--only VARIANT] [-E's options] file...` times
allocation, publication, check, both, and `tier1_cfg_verify` per IR instruction
on real C's functions, with the certificates' sizes. Run it under the 9700X's
lock on CPU 2. The 9700X job runs it on the frozen `sqlite3.c` where `changes`'
`tier1-certificate` entry says.

`benchmark-tier1-code-certificate [--only emit|decode|check|certify] [-E's
options] file...` times the emitted code's certificate, its decoding, its check
and both, per IR instruction and per record, beside the emission it certifies,
on every function whose code it certifies, with the certificates' sizes and the
uncertified functions by reason. A refusal, or a step out of memory or past a
limit, fails it with no report. The 9700X job runs it on
the frozen `sqlite3.c` where `changes`' `tier1-code-certificate` entry says.
