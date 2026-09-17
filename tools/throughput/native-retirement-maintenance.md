# Native retirement ledger maintenance

The native-retirement contract deliberately authenticates both source bytes and
derived population counts. A change to a tracked test or support file therefore
requires one coherent update rather than an isolated expectation change.

1. Recompute the changed row's byte count and SHA-256 in
   `docs/native-retirement-support-v1.tsv`.
2. Move every checked-in support-ledger trust anchor to the resulting ledger
   digest.
3. Keep the C census producer and the independent Python validator bound to the
   same applicability-ledger digest.
4. Refresh the census prose and its independent count tests from the ledger,
   preserving the distinction between supported-object subjects and registered
   non-object controls.
5. Run the Python contract/materializer suites, the native census self-test, and
   a manifest-only census. Acceptance requires `io_failed=0`, all 341
   applicability entries, and the complete authenticated row population.

Do not weaken a fallback test by using source that fails before machine
selection. The fallback fixture must reach canonical code generation and then
exercise the intended permissive machine-selection boundary.