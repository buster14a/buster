# Branch-only initializer join experiment (#1319)

This branch contains the temporary hosted execution transport, not the production candidate. Concatenate part0 through part4 to recover bundle.tar.gz, SHA-256 6fb5d22b306f8638ef7ea36f3e5f47b66a596305c5cddfd5780832dc063fb828. It contains readable run.py and candidate.patch. The workflow verifies that digest before extraction. Chunking accommodates the connected GitHub text API; these files add no production dependency.

Baseline ade6ac4b6ecb21f30b61b656439bac476c145e2f, tree 4c5306221fdb22fccc929b55e333163742de17d0. Exact patch SHA-256 7534e5970e8e8a08456065714901b63388a5e97996dca2634ab6845c3fb8577a. Expected four-file candidate tree 7faa1067466e9a7b10ae249aa27599cebd2b7e07.

The observer runs unchanged and patched trusted Clang builds in the same standard hosted root, frozen native and independent reference inputs, full registered tests, self-host compile/cmp only (not the benchmark-containing self-host target), and separate diagnostic census builds. No elapsed-time performance acceptance is attempted. A scoped create-only push publishes only the four production files to codex/1319-linear-initializers-20260925-astra; an existing branch is never overwritten. No merge, policy, generated binding, retirement, service, admission or deployment change.
