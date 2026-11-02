# determinism

Run commands and resolve source paths from the repository root. Follow the
[shared agent rules](../../AGENTS.md), including the 9700X lock and `cores`
requirements. [Command index](README.md).

## Deterministic output tests

`./build/compiler test-diagnostics` checks the buffered diagnostic merge:
the AVX-512 radix sort against stable insertion sort, guard-page tails,
complete 64-bit keys, grouped notes, the earliest fatal cutoff, and bounded
allocation failures. Jobs run in forward and reverse order with delayed
completion at 1, 2, 3 and 8 lanes; the bytes must agree.

`test-preprocessor` (without files), `test-object-writer`, `test-executable`,
`test-dynamic`, `test-static`, `test-files`, `test-check-lanes`,
`test-checker-deferred` and `test-c-testsuite` also check output after changing
opaque identifier assignments. Each accepts `--id-order N` (default 153). The
object test covers both section modes at every lane count; the preprocessor
compares both `-E` text modes and diagnostics through both kernels and both job
orders; the per-file jobs' test primes the shared table before the walk fills
the cache, and compares every translation unit's `-E` text, diagnostics and
locations and every cached file's tokens at every lane count. The C compile's
tests use the driver's `--id-order N` (`check_driver_id_order`, which primes
the unit's table with its own names in another order): `test-check-lanes`
compares objects, executables and diagnostics at 1 and 8 lanes with one lane's
of the plain compile, `test-checker-deferred` the deferred reader with source
order's, and `test-c-testsuite` each scalar build with its vector one;
`test-checker-corpus --id-order N` checks a corpus so. The 9700X workflow runs
these checks as part of the stage commands.

`./build/compiler benchmark-diagnostics [--only scalar|selected]
[--after-scalar MS]` compares the
ordering paths with 21 interleaved samples, in ns/key and invariant-TSC
ticks/key. Run both isolated `--only selected` and
`--only selected --after-scalar 100`, across several processes pinned to CPU 2
under the host's lock. [The determinism guide](../../docs/determinism.md) specifies
the caller's source-order and lifetime contracts and the integration still
required by #151 and #152.
