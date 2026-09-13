# Native-retirement evidence for `2bb4ce9`

This release durably preserves the complete census and strict semantic evidence
from GitHub Actions run 34718946098 before its seven-day source artifacts expire.
It covers compiler commit `2bb4ce939d99c3956848ca7bc9c347f3ae8db231`,
including the corrected wide-ABI integration, against direct-oracle commit
`3e912a3c5ce9b3e905f3096ff0a0d3e68d80e46e`.

`archive-manifest.json` binds the exact source commits and trees, original
Actions artifact IDs, original byte counts and SHA-256 digests, compiler binary
hashes, runner/toolchain identity, outcome counts, release asset names, and
every published asset's size and SHA-256. `SHA256SUMS` independently covers the
manifest and every payload asset.

The census Actions ZIP is retained byte-for-byte. The 2,359,014,398-byte strict
Actions ZIP exceeds GitHub's per-release-asset limit, so it is split at
1,000,000,000-byte boundaries. Concatenating its numbered parts must reproduce
SHA-256 `434b9a8ed1a0e52fc53cf084a044de213fbb4cc5d7d482380bbe18f45c937910`.
The source snapshots keep the validation workflow, candidate, and removed
direct backend outside ordinary production build targets.

The original census Actions ZIP omits `tests/.gitignore` from each shard because
`actions/upload-artifact` used its hidden-file default. Each `inputs.tsv` binds
that input, and the verified candidate source snapshot retains it. Replay
restores exactly those four copies and fails if any other input is absent.

The `Native retirement durable archive` workflow downloads these release assets
on fresh GitHub-hosted runners. It reconstructs and verifies every archive byte,
rebuilds the direct oracle from its exact Git identity, records both the archived
and rebuilt binary hashes, reruns the census join and byte validator, compares
the recorded and replayed joined trees, and reruns
all 7,776 strict configurations with the archived compiler and sanitizer oracle.

This evidence retains the original limits: inherited host/resource headers and
the process environment were not frozen. It does not close dependency closure
issue #508, replace final semantic acceptance in #509, permit direct-backend
deletion, or close issue #36.
