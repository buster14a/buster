# #129 row-writer leaf evidence

The source files here are exact copies of the hosted experiment at
`38a9fb11609d59c955272fd6532f52794441e585`. They are preserved diagnostic
material, outside ordinary build/test registration.

Restore `probe.c` and `census.py` under
`tools/research/simd_word_consumer/` in a clean checkout of the compiler
pin `af6b2b100acfdd9c970e35e33d4468cd35883812` before reproducing the
archived workflow's commands. The census script locates the checkout relative
to that original path; running it from this evidence directory is unsupported.

`diagnostic-workflow.yml` records the executed branch-only workflow.
It is retained as evidence; it is not an active workflow at this path.

`evidence.zip` contains the complete textual output, assembly and source
census, with no executables. Its SHA-256 is
`7fd2b4be57f63e346b3f5ebe0f3fe084912c77c9f1a283a06c04f93454c20413`.
`summary.json` inside is a derived summary; `transcript.log`,
`census.json` and the assembly files remain the underlying observations.

Native AVX-512 execution and compiler-throughput acceptance were unrun.
See the [audit](../../2026-09-30T193227Z.md) for results, scope, identities,
source-license reporting and the consumer-first continuation.
