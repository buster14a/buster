# Retired Forgejo GitHub runner bridge

The source-free Forgejo-to-GitHub broker was removed with the complete
`.forgejo/` implementation in [commit `02c0400a`](https://github.com/buster14a/buster/commit/02c0400a34d04be9e984f29a59291750b3998d3f).
It is no longer a supported execution path. Active CI is defined by
[`.github/workflows/`](../.github/workflows/) and documented in
[GitHub Actions CI](ci-github-actions.md) and the [CI workflow audit](ci-workflow-audit.md).

The former deployment guide and implementation remain available at the last
revision before removal:

- [Historical deployment guide](https://github.com/buster14a/buster/blob/a090d82c70af8da543f7777e75b6e946fed12e19/docs/ci-github-hosted-runners.md).
- [Historical broker template](https://github.com/buster14a/buster/blob/a090d82c70af8da543f7777e75b6e946fed12e19/.forgejo/github-bridge/forgejo-hosted.yml).
- [Historical dispatcher](https://github.com/buster14a/buster/blob/a090d82c70af8da543f7777e75b6e946fed12e19/.forgejo/scripts/github_runner_bridge.py).

Those links describe the retired design. They are not current setup or
validation instructions. There is no bridge to enable, copy, compile or lint
in the current checkout, and no broker credential or live-setting change is
part of this retirement.

## Preserved regression input

The old bridge suite is retained byte-for-byte as
[`tests/retired/github_runner_bridge_test.py.txt`](../tests/retired/github_runner_bridge_test.py.txt).
Its text suffix removes it from Python test discovery. The suite imports the
removed dispatcher and is historical source material, not executable coverage.
Do not run it or report its preservation as a passing bridge regression.

The [native-retirement support inventory](native-retirement-support-v1.tsv)
keeps the same support-file role, dependency-only obligation, byte count and
SHA-256 for the archived path. The move does not remove a compiler subject or
change the declared native compile coverage. Historical evidence keeps its
original support-contract identity and original input paths; its independently
approved replay contracts remain valid.

`tools/ci_time.py` likewise remains a reader for historical Forgejo timing
records. It is separate from the active GitHub timing collector and uses its
recorded timeout fallback when the removed Forgejo workflow is absent.
