# Native-retirement evidence history from September 12, 2026

This release preserves thirteen historical census, strict, raw, partial, and
failed-run GitHub Actions archives that were not already covered by the durable
`eb1bef2` and `2bb4ce9` releases. Together, the three releases cover all
seventeen artifacts discovered from the native-retirement evidence branch.

Every asset retains the original Actions artifact name with `.zip` appended.
Archives larger than GitHub's per-asset limit are split into numbered
1,000,000,000-byte parts. The checked-in `history-catalog.json` binds each
original artifact ID, producing run and revision, candidate commit and tree,
byte count, SHA-256, expiry observed before preservation, recorded outcome, and
approved durable release tag.

The read-only `Native retirement history` workflow downloads every release
asset on a fresh GitHub-hosted runner, reconstructs split archives in order, and
checks the resulting byte count and SHA-256 against the original Actions ZIP.
It also compares live origin metadata and rejects unregistered evidence.

Historical failures remain failures. Preservation does not validate unknown
binary identities, replay partial archives, authorize direct-backend retirement,
or close issue #36. Host headers, resource includes, and process environment
remain unfrozen under issue #508.
