# OPUS-CLOUD campaign helpers (scratch, not for merging)

These are the helpers the #880 → #881 → #882 → #36 coordinating session used. They live on the
scratch branch `codex/campaign-cloud-helpers` and are never meant for main.

The campaign state is on GitHub, not here:
- the living plan is issue #36, comment 5862701482;
- #880's closing record is issue #880, comment 5877769421;
- the next workstream is #881 and PR #923.

## Roles and rules (summary; the user's campaign instruction is authoritative)

- **CLOUD** (the coordinating session) owns plan, code, packets and dispatching
  `.github/workflows/9700x-service-dispatch.yml`.
  - Dispatch only after LOCAL's readiness matches the exact packet, and dispatch each key exactly once.
  - Never touch the host path; reading the GitHub run log is fine.
- **LOCAL** owns `benchpress`: installation, host readbacks, recovery.
- **davidgmbb** is the only human, and is asked only about settings (for example
  `BENCH_SERVICE_DISPATCH_ENABLED`), credentials, or acceptance criteria. Ask with one
  question on #36 and the same question in chat.
- **Comments** start with `[OPUS-CLOUD] <session link>` and end with the Claude Code footer.
  - Keep failed attempts on record.
  - Call a subagent's check a "technical review", never an approval.

## Hosted exact-systemd slice (source-exact proof before any installation)

The workflow is `.github/workflows/issue1162-exact-systemd-slice.yml`. It runs on every push to
`codex/1162-exact-systemd-slice-20260925`. The execution commit is the subject plus one pin commit.

1. `REPO=<checkout> WEXEC=<worktree> campaign-helpers/pin_to.sh <commit>` pins it:
   - it sets `subject`, `subject_tree`, `subject_build_blob` and the closure manifest entry in
     `.github/scripts/issue1162_exact_systemd_slice.sh`;
   - it sets `INTEGRATED_*` in `.github/scripts/issue1162_frozen_tree_evidence.py`.
2. If `build.c`'s blob is unchanged since the last admitted one, set `INTEGRATED_REVIEW` to that
   review's PR. It was `pull/1739` for blob `a016a487`.
3. Run `python3 .github/scripts/issue1162_frozen_tree_evidence.py self-test`, commit, and push with
   `--force-with-lease`.
4. **Pin to the merge-queue group commit** (`gh-readonly-queue/main/pr-N-<base>`) to save time.
   - The queue fast-forwards main to exactly that commit.
   - If an earlier group entry fails, the group is rebuilt with a new SHA, and you must re-pin
     (#1747: Attempt 28 → 29).
5. **Verdict lines:**
   - `NORMAL_PATH_EXECUTION_PASS`;
   - `FROZEN_TREE_EVIDENCE verdict=OFFLINE_EVIDENCE_RECONCILED`;
   - `OBSERVATION_INCONCLUSIVE` and `external_consumer_exit=2` are known non-gates, reported
     separately.
6. **Independent replay:**
   - read the artifact back (next section);
   - run `campaign-helpers/replay_slice.sh <dir> <subject> <tree> <build-blob>` from a checkout
     of the subject;
   - it expects the entry consumer 11/11 identical, `unpack_exit=0` with an identical tree, and
     the frozen tree identical.

## Artifact readback (the session can't reach the Actions blob host)

- Write `readback-request.json` on branch `codex/880-evidence-readback`:
  `{"run_id": "<id>", "artifact": "<name>", "branch": "codex/880-evidence-<x>-data"}`.
- Push it. `.github/workflows/campaign-evidence-readback.yml` copies the artifact to that orphan
  branch.
- Then run `git fetch origin <branch> && git archive FETCH_HEAD | tar -x -C <dir>`.

## Physical exports (LOCAL publishes to releases)

Run `campaign-helpers/replay_export.py <release-tag> <asset-basename> <service-binary> [job token base candidate boot]`.
- Build the service binary from a checkout whose `tools/bench_service` matches the installed one
  (`clang … build.c -o build/buster-bench-build && build/buster-bench-build bench_service capabilities`
  gives `build/bench-service-tools/service`).
- Compare every printed hash with LOCAL's receipt.

## Scratch repro guest (real systemd, exact binaries)

Branch `codex/880-p-repro`, workflow `.github/workflows/bq880-p-repro.yml`, reuses the slice script
with scratch-only modes:
- `BQ_P_REPRO=1`: one `SIGTERM` at the prepare boundary, with `strace`.
  - `BQ_P_REPRO_UPGRADE=<commit>` swaps in a fixed service binary.
  - `BQ_P_REPRO_LOOP_SECONDS` first restarts the old binary, then does `systemctl stop`, mirroring
    the host.
- `BQ_L_REPRO=1`: a silent lease-handoff peer, then the stopped-service `workspace-reconcile` if
  the job quarantines.

The subject pin lives in the script. It is set to `6a59bc4f` at `486b1951`.

## Identity blocks for packets

Run `campaign-helpers/identity.sh <rev>` inside a checkout. It prints the commit and tree, the
`build.c`/deploy/profile blobs, and aggregate `ls-tree` digests for `tools/bench_service`,
`tools/throughput` and `src`.
