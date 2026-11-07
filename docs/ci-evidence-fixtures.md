# Evidence-job fixture latency

`tests/ci/nightly.sh`, `tests/ci/caches.sh` and
`tests/ci/build-benchmarks.sh` remain the public commands used by `9700x.yml`.
Their existing suite bodies are retained byte-for-byte in the sibling
`*-fixtures.sh` files. The entry points use `evidence-fixtures.sh` for selection
and start/completion logging. The elapsed time includes the child script's
EXIT cleanup. Failures retain their original exit status and stop the workflow
step before admission; neither cleanup nor coverage assertions are removed.

## Selection boundary

Direct/local invocations run full fixtures. `--all` also forces the full suite
inside Actions, for example `bash tests/ci/nightly.sh --all`.

Only `GITHUB_ACTIONS=true` and `GITHUB_EVENT_NAME=pull_request` may skip work.
The event must contain valid base/head commit IDs, checkout HEAD must match
`GITHUB_SHA`, and its two parents must be exactly the event's base and head.
Both parents must be readable and the tracked checkout must be clean. The diff
compares the synthetic merge with its first parent, not just the PR's latest
commit. It uses NUL-separated names and disables rename detection so a deletion
or rename out of infrastructure cannot disappear from selection.

A nonempty diff containing only `src/**/*.c`, `src/**/*.h`, unrelated Markdown
under `docs/`, or `README.md` skips the expensive fixture bodies. CI documents
(`docs/ci*` and `docs/commands/ci*`) still run them. All other paths run the
fixtures: workflows, tools, tests, build scripts, CMake, hooks, generated files
and newly introduced/unclassified paths. Missing or malformed metadata,
incomplete/shallow history, mismatched refs, Git failures and an empty or
incomplete change list also run everything. This is a narrow exclusion, not an
allowlist of infrastructure dependencies that can silently miss a new one.

The lightweight selector/reporter regression suite runs before nightly fixture
selection on every invocation, including compiler-only PRs. It covers real
merge histories, missing parents, wrong metadata, non-PR events, forced full
runs, dirty checkouts, Git failure, infrastructure renames, NUL-delimited names,
unknown paths and failure/cleanup reporting.

This selects **infrastructure regression fixtures only**. The actual
`tools/ci/nightly.sh evidence` command remains unconditional in the workflow.
Hosted compiler correctness, sanitizer scope, hardware admission, required
aggregate checks, host locks and nightly windows are unchanged. No main-push
path filter or prior-run success cache is introduced.

## Download regression

The cache fixture deliberately removes its local source after corrupting its
cached copy. Applying curl's production remote retry policy to that missing
`file://` URL caused approximately 255 seconds of exponential backoff. Local
file URLs now fail immediately; HTTP/HTTPS and other remote protocols retain
`--retry 8 --retry-all-errors --retry-max-time 240`. SHA-256 checks, lock-held
publication and temporary-file cleanup are unchanged.

`tests/ci/cached-download-retry.sh` inspects the production command through a
stand-in transport before the real cache suite runs. It checks local and remote
arguments, partial-transfer rejection, digest rejection, temporary cleanup,
recovery and cache reuse without network access or real retry sleeps. The
existing real file://, Git-mirror and manifest fixtures still run afterwards.

The original observed job is
[42389315754 / 128453332149](https://github.com/buster14a/buster/actions/runs/42389315754/job/128453332149).
Its nightly success message at 16:05:06 UTC was followed by the cache suite's
success at 16:09:22, then build fixtures at 16:09:33. That quiet interval was not
proof of slow nightly cleanup; retaining separate suite timings avoids that
misattribution. Hosted job timings are CI latency observations, not compiler
performance measurements on the 9700X.
