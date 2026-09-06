# Buster audit — 2026-09-06

Four new issue reports, three independent correctness patchsets, focused regression sources, before/after logs, and a detailed audit report. **Nothing in this bundle has been published to GitHub.**

Start with [AUDIT_REPORT.md](AUDIT_REPORT.md). Source revision and environment details are in [evidence/PROVENANCE.md](evidence/PROVENANCE.md). `manifest.json` maps issue drafts, PR drafts, patch hashes, and required production preimages.

## Inspect / apply locally

The patches touch disjoint paths and can be applied independently using `git apply patches/<name>.patch` from a checkout (use an absolute patch path when outside the bundle). They were exported from an older recovered checkout, but all five production preimages match the pinned current-main blobs. Do not push the archived checkout's history. Apply onto a freshly fetched GitHub main and rerun tests.

The patch files add each regression under the repository's `tests/` directory. The `regressions/` copies here are for inspection; run them from their patched repository location, not directly from this bundle.

One-line focused commands, from the patched repository root:

```sh
python3 tests/ebpf_scalar_regression.py
python3 tests/wasm_memory_alignment_regression.py
python3 tests/runtime_boundary_regression.py
CC=clang AUDIT_OPT=-O2 AUDIT_SANITIZE=1 python3 tests/ebpf_scalar_regression.py
```

These focused Linux-host commands do not replace the project's normal module tests, test_all, or Release self-host. Windows canonicalization and kernel eBPF execution remain untested.

## Optional GitHub publication

`publish.py` defaults to an offline, non-mutating plan. Explicit publication requires Linux, Git, an authenticated GitHub CLI, a host Clang, memory64-capable Node, and a local Git checkout. No token should be put into the bundle or command line.

```sh
python3 publish.py
python3 publish.py --checkout /path/to/buster --publish
```

On publication, the helper fetches GitHub main, checks exact production preimages, applies/tests each patch in a separate temporary worktree, and only then creates issues, pushes new branches, and opens **draft** PRs. It does not use or modify the checkout's working files or push archived history, and never force-pushes. It detects existing reports using audit markers or exact titles, and existing PRs using their audit branch/marker. A closed matching item or an unexpected existing branch stops the run for review. Exact-title matching is not semantic duplicate detection: review current issues/PRs before invoking publication.

Live GitHub publication has not been tested here. GitHub writes cannot be atomic across issues/branches/PRs; successful URLs are saved incrementally in publication-result.json if a later step fails. Full CI/self-host and platform-specific gates remain required before merging the drafts. No issues are auto-closed by this script; closing keywords take effect only upon a later merge.

## Symbol growth probe

The probe includes the actual eBPF source and needs the repository include directory. From a repository root, replace /path/to/bundle with this extracted bundle:

```sh
clang -O2 -g -fwrapv -funsigned-char -Wno-unused-function -ffunction-sections -fdata-sections -Isrc /path/to/bundle/evidence/ebpf_symbols.c -Wl,--gc-sections -pthread -lm -o /tmp/buster-ebpf-symbols && /tmp/buster-ebpf-symbols
```

The counts printed as key_comparisons follow N(N-1)/2 analytically. The timings are illustrative, not a benchmark acceptance gate.
