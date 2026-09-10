# Self-host fixed-point audit

The native build driver owns this gate. On a configured Linux x86-64 tree:

```sh
./build.sh self_host_audit_self_test
./build.sh test_self_host_audit --config Release
```

Run `./build.sh generate` once before the second command on a fresh checkout.
Do not regenerate the tree or change compiler sources while an audit is running.
The `.github/workflows/self-host-audit.yml` job uses the documented hosted
Clang bootstrap exception. The existing multi-platform/configuration matrix and
ordinary `test_self_host` gate remain in place.

## What a pass means

Let C0 be the host-built compiler. Independently execute C0 twice to produce
C1a/C1b; C1a twice to produce C2a/C2b; and C2a twice to produce C3a/C3b.
Every child must compile successfully, produce complete evidence and pass its
behavioral probes before it can be used as a producer. Compare both repeats in
each generation, then C1a/C2a and C2a/C3a. This checks repeatability separately
from a three-generation fixed point; equality of only C1/C2 does not establish
repeat-run determinism.

Compare the following in pipeline order, stopping at the first mismatch:

1. Actual preprocessed token kinds and spellings, not just token counts.
2. Semantic canonical IR records and selected MIR records; selected MIR also
   runs the general verifier, including for selector-certified functions. A
   rejected function is never sent to allocation or dereferenced for tracing.
3. Compile stdout, stderr and exit status, then the complete generated binary.
4. Source metrics within every repeated generation and across C2/C3.

Physical lexing metrics are retained and validated for every compile, but are
not required to be equal between C1 and C2: C0 reads host resource headers,
whereas C1 reads Buster's built-in resources. Token evidence must nevertheless
match byte for byte across that boundary. The old comparator's token/byte
counts are now explicitly counts, never proof of identical input. Missing,
truncated, malformed, duplicate or unsupported-version required metrics fail
both the ordinary comparator and this audit.

C0 and every generated compiler also compile and execute the same behavioral
fixture in `none`, `mir-stack`, `fast` and `quality` modes. It covers pointer
joins, integer width/sign handling, floating/integer aggregate arguments and
returns, and register/stack argument boundaries. Variadic checks include named
integer/floating parameters, default integer/float promotions, ordered integer
and floating arguments exhausting both register files, and independent
`va_copy` cursors created before consumption and at an integer-register
boundary. Each copy remains usable after the original list is consumed and
ended. The fixture uses variadic builtins directly: it does not introduce host
resource headers into the exact per-probe token and source-metric comparisons.
Compare those mode-specific phase traces, binaries, outputs and statuses
against C0. An invalid-source fixture must be rejected without crashing,
timing out or producing an executable, with identical diagnostics and status
across generations. Captured sanitizer findings are failures even when the
child exits zero.

This is a finite, same-host, same-source, same-configuration empirical invariant,
not proof of compiler correctness, reproducibility across different SDKs/hosts,
or convergence from arbitrary compilers. The full compiler is bootstrapped in
its default allocator; all four allocators are independently exercised by the
behavioral probes, not falsely described as four full bootstrap fixed points.
The existing ordinary gate additionally exercises its supported alternate
backend builds. The stronger trace audit is currently Linux x86-64 only;
ordinary Linux/Windows x86-64 and macOS self-host coverage is unchanged.

## Independent behavioral evidence

C0 is still Buster, even when built by Clang. Agreement with C0 alone can retain
a semantic error shared by every generation. The dedicated CI job also passes
the same fixture to the existing native differential runner:

```sh
./build.sh test_differential --self-test
./build.sh test_differential --ide build/Release/ide --cc clang --source tests/self_host_bootstrap_probe.c --sanitize-oracle --out build/self-host-audit/probe-oracle
```

Once ordinary bootstrap succeeds, full compiler regressions and this independent
check still run when the enhanced audit fails. Each failure remains a job
failure; there is no `continue-on-error`. This separates semantic reference
evidence from the potentially failing generation comparison without bypassing
the latter. Cancellation does not start another test phase.

`--source` selects this one explicit input instead of the generated/default
corpus. The driver owns the discovered allocator/optimization/promotion matrix,
including `none`, `mir-stack`, `fast` and `quality`, and keeps code-generation
verification enabled. Independent Clang O0/O2 executions use address and
undefined-behavior sanitizers; their observations must agree before they can
serve as the reference for Buster. The ordinary audit separately requires the
fixture's successful exit and output, so an agreed nonzero exit in the general
differential runner cannot substitute for bootstrap success.

Choose a fresh output directory when repeating the command. Compiler identities
and hashes, configuration inventory, copied source, exact command vectors,
diagnostics and process statuses remain in the differential evidence under the
existing CI artifact. The comparison is behavioral: Clang and Buster binaries
are not expected to be byte-identical. This independent finite test strengthens
semantic evidence but is not a language-wide correctness proof, a public
cross-compiler `va_list` ABI test, or resource-header compatibility coverage.
It adds no worker fan-out. A single compiler invocation is currently serial;
test-worker settings are not evidence of compiler-lane determinism.

## Failure evidence

Each run exclusively claims a fresh directory under
`build/self-host-audit/<Config>/run-<pid>-<clock>-<attempt>/`. No stale binary,
trace or success marker can satisfy a new run. The driver writes `RUNNING`
first and `PASS` only after every gate succeeds. A failure records the earliest
unvalidated child, keeps earlier and partial artifacts, and returns failure.
Each child retains length-prefixed argv in `.command`, raw `.stdout` and
`.stderr`, and portable/native status plus timeout state in `.status`.
Comparisons report the first differing byte offset and both paths/sizes.
Writes and closes are checked; inability to retain evidence is itself failure.
The CI job uploads the evidence on success or failure for seven days.

Replay the exact argument vector from the failed child's `.command` in the
same source checkout/configuration, choosing fresh output/trace prefixes.
Investigate tokens before IR, IR before MIR, and MIR before allocation,
encoding/linking or runtime behavior. Do not disable a verifier or add an
expected-failure entry to turn an audit failure green.

## Trace format and cost

`ide cc -fbootstrap-trace=<prefix>` supports exactly one native C input and
object or executable output. It creates `<prefix>.tokens`, `.ir` and `.mir`.
The v1 envelope is an eight-byte little-endian string length, the string
`BUSTER bootstrap trace v1`, the similarly encoded phase name, explicit
semantic fields, and the eight-byte completion footer `BSTREND1`.
All scalar fields are little-endian u64; byte strings carry u64 lengths.
The producer is the field-order specification in
`src/buster/lib/compiler/codegen/bootstrap_trace.c`.

The snapshots are diagnostic semantic projections, not a replay/interchange
format. They do not serialize pointers, padding, capacity, lazy ABI caches or
physical source byte offsets. Canonical IDs/types/values/blocks/instructions,
edge arguments, symbols/globals/relocations, and selected machine instructions,
registers/blocks/edges/copy sources and side tables are explicit. Line-mark
instruction IDs are included. Debug-bearing compiler executables are compared
in full: executable bytes are not stripped or normalized to obtain a pass.
Code-generation retry boundaries are recorded rather than silently concatenated.

Tracing is opt-in, with a checked 64-KiB buffer per stream. Ordinary compilation
does not walk IR/MIR for evidence or re-run the general MIR verifier. Traces can
be large; they are streamed, compared through read-only mappings, and retained
rather than reduced to collision-prone summary counts.

## Regression ownership

`self_host_audit_self_test` tests the native comparator, metric parser, evidence
completion, missing/empty files, child status, timeout and sanitizer handling.
Driver tests check independent traces, same-length token mutations, object
identity with tracing off/on, invalid flag combinations and writer failures.
Machine tests cover complete ordinary-branch/switch edges on both architectures,
including a minimized ternary-assignment case whose cross-block value had no
edge in the selected MIR graph.
