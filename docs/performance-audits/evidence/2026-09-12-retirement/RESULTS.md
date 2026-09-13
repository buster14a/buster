# Durable native-retirement evidence results

This record closes the preservation and independent-replay scope of
[#510](https://github.com/buster14a/buster/issues/510). It does not accept
native-backend retirement or close parent issue #36.

## Durable destinations

- [`native-retirement-evidence-eb1bef2`](https://github.com/buster14a/buster/releases/tag/native-retirement-evidence-eb1bef2)
  preserves census ZIP `fe48776d3116f1620e940d132965b306a6714c5f86d3ab1a1ada59413650505b`
  (389,211,062 bytes) and reconstructed strict ZIP
  `0351edbcd30062fb72790559396bf771c17b6364b16a408ee83b7c157dbf9670`
  (2,359,006,674 bytes), plus exact source snapshots, manifest, checksums, and
  successful replay receipts.
- [`native-retirement-evidence-2bb4ce9`](https://github.com/buster14a/buster/releases/tag/native-retirement-evidence-2bb4ce9)
  preserves census ZIP `a5f1b5f76bd67401b80b32071386f93c75fd0f2fdd72f517b525b8d9fd76b6eb`
  (396,442,720 bytes) and reconstructed strict ZIP
  `434b9a8ed1a0e52fc53cf084a044de213fbb4cc5d7d482380bbe18f45c937910`
  (2,359,014,398 bytes), plus the same reproduction material.
- [`native-retirement-evidence-history-20260912`](https://github.com/buster14a/buster/releases/tag/native-retirement-evidence-history-20260912)
  preserves the remaining fifteen census, strict, partial, raw, and failed-run
  archives. Its 25 assets total 15,564,921,505 bytes. Together the three
  releases cover every one of the nineteen Actions artifacts in
  `history-catalog.json`.

Each contract binds source commits and trees, original Actions artifact IDs,
original ZIP size/SHA-256, compiler binary identities, runner/toolchain facts,
input ledgers, command lines, recorded outcomes, and release asset hashes.

## Replay commands

The `Native retirement durable replay` workflow performs these commands on a
fresh `ubuntu-26.04` runner after downloading release assets. Matrix variables
select the exact release contract and source snapshot for each evidence pair.

```sh
python3 validation/tools/native_retirement_archive.py verify-census \
  --assets "$RUNNER_TEMP/release-assets" --output "$RUNNER_TEMP/census" --cleanup
python3 validation/tools/native_retirement_archive.py restore-census-inputs \
  --evidence candidate/evidence --source candidate

(
  cd reference
  clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g \
    build.c -o "$RUNNER_TEMP/reference-build"
  "$RUNNER_TEMP/reference-build" generate --cc clang --ci --linker DEFAULT
  "$RUNNER_TEMP/reference-build" build --config Release -t ide
)

clang -O2 -fPIC -shared -Icandidate/src candidate/src/buster/lib/hash.c \
  -o candidate/evidence/libcensus_hash.so
(
  cd candidate/evidence
  python3 "$PWD/join-census.py" census-integrated 4
  python3 "$PWD/validate-census-v2.py" census-integrated 4
)
```

The strict path verifies and reconstructs the original ZIP, extracts only the
bound compiler and result files, restores the published candidate source
snapshot, and then runs:

```sh
clang -Isrc -Wall -Werror -Wno-unused-function -Wno-unused-variable -g \
  build.c -o "$RUNNER_TEMP/strict-build"
"$RUNNER_TEMP/strict-build" test_differential \
  --ide "$GITHUB_WORKSPACE/evidence/strict-binary/ide" --cc clang \
  --strict-mir --sanitize-oracle \
  --out "$GITHUB_WORKSPACE/evidence/strict-differential"
```

Release downloads are retried three times from a clean destination so a
transient GitHub Releases response cannot leave a partial archive behind.

The independent history gate runs no compiler and checks every durable copy
directly against the frozen original Actions ZIP identity:

```sh
python3 tests/native_retirement_inventory_test.py -v
python3 tools/native_retirement_inventory.py \
  --catalog docs/performance-audits/evidence/2026-09-12-retirement/history-catalog.json \
  --output "$RUNNER_TEMP/retirement-history"
```

## Actual CI results

Publication and census replay run
[34730125413](https://github.com/buster14a/buster/actions/runs/34730125413)
used Ubuntu 26.04 and Clang 21.1.8. Both downloaded census archives passed their
destination hashes, independent join, complete row/telemetry/object validation,
and recorded-versus-replayed joined-tree comparison. The `2bb4ce9` strict
archive passed all 18 cases across 432 configurations in that run. The clean
`eb1bef2` strict replay in
[34727739903](https://github.com/buster14a/buster/actions/runs/34727739903)
independently passed the same 7,776 observations with zero failures. Its receipt
creation passed in CI; after its original upload command omitted `--repo`, the
exact emitted receipt was attached to the release and release-side hashed.

| Pair | Census rows / inputs | Candidate binary | Archived / rebuilt direct oracle | Joined tree | Strict compiler |
|---|---:|---|---|---|---|
| `eb1bef2` | 76,416 / 537 | `2bdf15fb6ef4a043d85cb3ed042456cf61ca04eb1e9df3c3f69827ae0c9aca27` | `1190c010537be78a81dc00b85fa6ca8af5573765a278f8ff52c95318d22f16c1` / `33a9987f7d582d9012d2512b48efd0574896575af52fb2269541ca115876f01c` | `4da0f76db7a31f9810886923eefb87fdb217f768bf09899d076a7b8a09206fb5` | `fba419609920611f6df45bb36a79e1d57e014821bef493e965a481acdc1c7a44` |
| `2bb4ce9` | 76,608 / 538 | `71e8879bf5cbf36648cb741147e784c3d855e9188f1f97ddb2480b62b49b3f97` | `33a9987f7d582d9012d2512b48efd0574896575af52fb2269541ca115876f01c` / `1190c010537be78a81dc00b85fa6ca8af5573765a278f8ff52c95318d22f16c1` | `56cd6326fd7e2dc966d034ed5516924e65517e7e6ab5a52a2dc3395983cee4b0` | `60ba044e6c33fb5e9519fa0d860d785be63f5cc1f5824e5ece57454228813a20` |

The clean oracle rebuilds compile and execute successfully but are not
byte-identical to the archived oracle binaries. The receipts preserve both
hashes and set `rebuilt_matches_archived` to false. This is an explicit limit of
the older evidence: inherited host/resource headers and the process environment
were not frozen. Issue #508 owns that closure for future acceptance evidence.

Failed repair attempts remain visible in Actions runs 34727739903, 34728409857,
and 34729266820. They respectively record a receipt-publication mistake, an
incorrect nested workspace, and replay against the wrong source checkout; none
is relabeled as successful evidence.

Pull-request run
[34732963376](https://github.com/buster14a/buster/actions/runs/34732963376)
then exposed two replay-harness reliability defects: GitHub Releases returned a
transient HTTP 500 for one census asset, and the serial `eb1bef2` differential
replay reached the 120-minute job limit. The final workflow retries release
downloads from an empty destination.

A four-worker retry in
[34737929986](https://github.com/buster14a/buster/actions/runs/34737929986)
finished promptly but made every archived compiler invocation terminate by
signal (`signature=101`). The older compiler is therefore replayed only through
the proven serial path; the concurrency experiment is preserved as a failed
negative control.

The final history gate subsequently detected two archives created by later
#504 run 34731220185. CI transfer run
[34732745347](https://github.com/buster14a/buster/actions/runs/34732745347)
preserved and destination-verified their 396,443,308-byte census ZIP and
2,359,011,344-byte strict ZIP. Their producing workflow failure and skipped
census join remain recorded rather than being inferred from the successful
semantic pair above.
