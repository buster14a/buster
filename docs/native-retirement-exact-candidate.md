# Exact native-retirement candidate acceptance

Acceptance is a relation over one immutable candidate, not a collection of green branch checks.

## Candidate identity

On `pull_request`, the candidate is `github.event.pull_request.head.sha`; `github.sha` is GitHub's synthetic merge commit and is forbidden as the candidate identity. Manual qualification accepts only a full immutable commit. The checked-out commit must equal the declared candidate, and the identity records its tree, `mir-native-v1` backend, the frozen #508 census digest, the frozen #509 gate-contract digest, and every trusted binary used by a gate.

Compiler digests are platform-specific. The identity therefore carries a closed map for Linux, macOS and Windows on x86-64/AArch64, plus the 9700X comparison compiler and the source reachability verifier. Every binary entry binds its bytes and SHA-256 to the same candidate commit and tree. A platform receipt cannot substitute another platform's compiler.

## Gate receipts and retained bytes

Each gate publishes one `buster.native-retirement.receipt.v1` document and one retained artifact. The receipt names the exact candidate commit/tree, producer commit/tree, expected trusted binary, frozen contract digests, workflow run/attempt, artifact path, byte count and SHA-256. The final verifier opens artifacts descriptor-relatively without following links, rechecks identity after reading, and hashes the actual bytes; a digest string without matching retained bytes cannot pass.

The closed gate set is recorded in `docs/native-retirement-gates-v1.json`: the complete #508 census; strict native execution on Linux, macOS and Windows for x86-64 and AArch64; every supported sanitizer row; at least three repeated self-host stages at a fixed point; archived-direct plus Clang semantic parity; zero production direct-native reachability and fallback; and the exact-candidate #512 final-change comparison.

Unknown or duplicate fields, gates and artifact paths fail closed. So do predecessor commits or trees, stale compiler binaries, stale contract digests, missing sanitizers, incomplete semantic execution, direct fallback and evidence from a different #512 candidate.

## Publication

`tools/native_retirement_candidate.py verify` accepts an identity, receipt directory and artifact root. It publishes canonical acceptance JSON through a sibling temporary file, `fsync`, and atomic replacement only after all gates and bytes validate. Failure does not create a partial acceptance file or replace an existing valid result.

```sh
python3 tools/native_retirement_candidate.py verify \
  --identity evidence/candidate.json \
  --receipts evidence/receipts \
  --artifacts evidence/artifacts \
  --output evidence/native-retirement-acceptance.json
```

The evidence workflow itself must select the exact PR head before gate execution. The companion workflow tests that invariant and the verifier's failure controls. Gate producers still own building their platform binaries and emitting their receipts; no compiler implementation belongs in this acceptance change.
