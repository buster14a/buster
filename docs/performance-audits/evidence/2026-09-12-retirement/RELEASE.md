# Native-retirement evidence for `eb1bef2`

This release durably preserves the complete census and strict semantic evidence
from GitHub Actions run 34714527496 before its seven-day source artifacts expire.
It is evidence for compiler commit `eb1bef2f124c6a778a435ae1eda18ae489c489c5`
against direct-oracle commit `3e912a3c5ce9b3e905f3096ff0a0d3e68d80e46e`.

`archive-manifest.json` binds the source commits and trees, original Actions
artifact IDs, original byte counts and SHA-256 digests, compiler binary hashes,
runner/toolchain identity, outcome counts, release asset names and every
published asset's size and SHA-256. `SHA256SUMS` independently covers that
manifest and every payload asset.

The census Actions ZIP is retained byte-for-byte. The strict Actions ZIP is
2,359,006,674 bytes, above GitHub's per-release-asset limit, so it is split at
1,000,000,000-byte boundaries. Concatenating its numbered parts must reproduce
SHA-256 `0351edbcd30062fb72790559396bf771c17b6364b16a408ee83b7c157dbf9670`.
The source snapshots keep the validation workflow, candidate, and removed
direct backend outside ordinary production build targets.

The original census Actions ZIP omitted `tests/.gitignore` from each shard
because `actions/upload-artifact` was invoked with its hidden-file default. The
input is still bound by each `inputs.tsv` ledger and is present in the verified
candidate source snapshot. Replay restores exactly those four copies and fails
if any other input is absent. Failed pre-repair round trips remain in runs
34726417493 and 34726824515 rather than being described as passes.

The `Native retirement durable archive` workflow downloads these release
assets into fresh GitHub-hosted runners. Its census job reconstructs and checks
every archive byte, rebuilds the direct oracle from its exact Git identity,
records both the archived and rebuilt binary hashes, reruns the join and byte
validator, and compares the new joined tree with the recorded tree. Its strict
job reconstructs and checks the original ZIP and packed evidence, then reruns
the 7,776-configuration strict MIR differential with the archived compiler and
sanitizer oracle. Successful replay receipts are published back to this release.

This evidence retains the original limits: inherited host/resource headers and
the process environment were not frozen. It does not close the dependency
closure owned by issue #508, replace final semantic acceptance in #509, permit
direct-backend deletion, or close issue #36.
