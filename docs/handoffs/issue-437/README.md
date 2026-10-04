# Issue #437 takeover handoff

Prepared on 2026-10-04 at the user's request. This is a source checkpoint and
handoff, not completion of the issue. The user then explicitly requested that
all work be pushed to GitHub. Source branches, incomplete comparison code,
handoff notes and preserved validation receipts are published; there is no
uncommitted implementation left in the session's worktrees.

## Takeover instruction

Continue [#437](https://github.com/buster14a/buster/issues/437) and the
user-requested custom benchmark scope in
[#2648](https://github.com/buster14a/buster/issues/2648). Read this file, the
three [agent notes](agent-notes/), current issue/PR state and `AGENTS.md` before
editing. Reuse the published branches below rather than restarting or
overwriting their work. Review and integrate the independent source slices
serially. Keep unfinished source, local tests, hosted checks, actual installed
host evidence and issue closure separate. Do not merge, enqueue, enable
auto-merge, change repository settings or close the issues without the user's
authorization. Subagents are explicitly allowed.

## User requirements and authorization

- Solve #437 with actual queued execution, exclusive whole-job ownership,
  durable recovery, separate control host/cache, ChatGPT/Codex writes and raw
  result retrieval.
- Support user-defined microbenchmarks, both compiler throughput and program
  runtime. Also support prebuilt native programs and assembly microkernels
  unrelated to compilation; do not force a predefined compiler workload.
- **Ordinary submissions must not require a workflow dispatch.** Direct
  authenticated MCP/CLI submission is the normal path. Automatic repository
  checks on source PRs are a separate validation concern.
- The user authorized host access as `tailscale ssh david@benchpress`. This
  host was inspected read-only; nothing was installed or submitted there.
- The user requested a handoff and then said, "please don't leave anything
  local, push it to github." Incomplete code is therefore preserved in explicit
  source checkpoints rather than silently discarded.

Two unanswered questions remain from this session. Benchpress turned out to
be the actual Ryzen 9700X worker, so it cannot also establish the required
separate control-host acceptance. Ask which separate machine should host the
control service (the local mini PC was offered, but never selected). Also ask
whether the user's ChatGPT account already has a Secure MCP Tunnel connection
or needs setup. Never request an API key or secret in chat. No tunnel client or
relevant credential environment was found locally, and no account settings
were changed. Elapsed time is not an answer or approval.

## Published source checkpoints

All branches are in `buster14a/buster`. SHA values below identify source or
acknowledgement commits; the main handoff branch also contains this documentation
and historical evidence in later commits.

| Branch | Checkpoint | State and purpose |
| --- | --- | --- |
| `codex/437-native-mcp-client` | `73df2f78e66bbdd20c2fb3db00ca8f3b32822217` | Main integration branch. Native execute + nine-tool MCP adapter; runtime/off-host/compare are not integrated. Source-size acknowledgement follows upload source `6ba567d10a487d4f9e498bc5c05548e3a6dc2ef8`. |
| `codex/2648-native-program-execution` | `64b04644980a9d5572cf4bc4c105cdf64e7610ec` | Original independently reviewed native execute checkpoint, draft PR #2650. Native source is `05a50d247a4782a01c4312ad8f54200966394c68`; already integrated in main as `f679fa66e`. |
| `codex/2648-native-benchmark-metrics` | `3ea60463c35afcfecd11ea6bb9f8ee19624267bb` | Native runtime measurements; local normal/sanitizer evidence passes. Independent review and main integration pending. |
| `codex/2648-custom-c-benchmarks` | `3ea60463c35afcfecd11ea6bb9f8ee19624267bb` | Alias for the next scope only. **No custom C implementation exists.** |
| `codex/437-off-host-control` | `9bec5c64b1a32e9fa7cb50ae80792b16b0bef911` | Off-host source `ea0aa6783db21bf5f505ac2511fb921fd2f4a147`, then generated size acknowledgement. Current normal passes; current sanitizer, deployment docs, runtime transfer and independent review pending. |
| `codex/437-compiler-compare` | `ee63d4d78608120f68339852a70cec350ff0ee6e` | **Incomplete comparison checkpoint**, source `03345bfc84a3c99fa029d9365c7ec730dacb840c`, then generated size acknowledgement. Known broker/evidence/provenance/test gaps remain. Do not deploy. |
| `codex/437-mcp-adapter-implementation` | `03d98b959` | Historical isolated six-tool adapter implementation. Its changes are already represented by main's cherry-picks; preserve as a reference, not another integration target. |

The original base was `1d4898682304cc7e50ed4a112d08dd39ef257bce` (tree
`b8df8b52f8fb8227ed9b6faca983941829aae961`). Main has advanced since then.
Refresh remote state before rebasing; no rebase was performed for the handoff.

Existing PRs:

- [#2649](https://github.com/buster14a/buster/pull/2649): main integration PR.
  It was returned to **draft** for the unfinished handoff. At the historical
  published head `f4d14d1a6eaf0fcab36ee002d7f458963402a9c0`, all 47 returned
  checks were complete with no failures and no draft-deferred Apple checks.
  **Those results do not validate the newly pushed combined head.**
- [#2650](https://github.com/buster14a/buster/pull/2650): separate native execute
  draft checkpoint at `64b04644980a9d5572cf4bc4c105cdf64e7610ec`. One workflow
  policy check failed. macOS/iOS entries were deferred for a draft, so their
  green placeholders do not establish real Apple validation. Main already
  includes this source; decide how to consolidate after review, without
  treating both overlapping PRs as independent integration candidates.

No new PR was opened for the unfinished agent branches. Branch publication is
not hosted validation or admission readiness.

## Immediate known failure

The native PR's `Benchmark service workflow policy` check failed in
[run 37229433220](https://github.com/buster14a/buster/actions/runs/37229433220/job/111515792434).
`tools/bench_service/dispatch_recipe_test.py:162` hardcodes the exact string
`service-recipes=validate-buster-v1,zen5-calibration-v1 ` and rejects the added
`native-execute-v1` entry. This also affects main's new combined source.
Retain the [failure log](evidence/handoff-snapshot/native-pr-policy-failure.log).
Update the policy test to check the fixed registry and blocked recipes
correctly as recipes grow, then run the whole policy suite. Do not weaken
dispatch, actor or retirement controls or suppress the failure.

## What is implemented and verified

The main adapter is bounded native C, stdio JSON-RPC, authenticated Unix
SEQPACKET backend. It has initialize/initialized/ping/tool lifecycle, protocol
2025-11-25 and 2025-06-18 negotiation, strict unknown/duplicate fields, explicit
parser stack, valid UTF-8, decimal-string uint64 IDs/cursors, truthful write
annotations and bounded framing. Closing MCP or request cancellation does not
cancel a job; `bench_cancel` performs a durable job mutation. No queue or worker
is opened directly by the adapter.

Main's current tools are `bench_capabilities`, `bench_submit`, `bench_status`,
`bench_result`, `bench_cancel`, `bench_logs`, `bench_program_begin`,
`bench_program_write`, and `bench_program_finish`. Program uploads use SHA256,
declared size, an exact durable offset and 1–432 lowercase-hex bytes per write;
identical retries resume, changed prefixes conflict. Submit the returned
manifest digest in both SHA fields with `native-execute-v1`. No filename or
command crosses the public request boundary. Capability flags become true
only when the installed backend's exact served-recipe token supports them.

Main's latest registered normal and ASan/UBSan service suites each passed
**31,103 assertions / 0 failures**, including real disposable authenticated
socket upload/resume/reconnect/retry/submission cases. The CLI was rebuilt via
`bench_service capabilities` after source commit: SHA256
`090a44403bb7c18b296e2215d822f12ae64e9b81ca143cba61e1f15c710b50c5`.
The combined source-size check passed at `73df2f78e`, production growth 0,
build growth **89,802 bytes**, explicitly acknowledged. The new nine-tool
source still needs independent adapter review/replay. The older independent
six-tool replay (224 processes / 1,082 passing checks) does not cover it.

Native execute accepts immutable static Linux x86-64 ELF programs, with
bounded private CAS, canonical manifest, crash-prefix recovery, private
materialization and a fixed broker/credential gate. Payloads run only under
the candidate identity with fixed argv/environment, no compiler or shell
selection. The reviewed source passed **30,890** normal and sanitizer
assertions each; independent replay passed **998/998** against binary SHA256
`8adaecdec8688bd926aac637993a909a480aa70e04b6177ff5a3b958472cd032`.
Its source review found no actionable issues within the stated fixture scope.
These were not installed production systemd/candidate witnesses.

The separate runtime checkpoint uses the same native CAS, two warmups and nine
fresh process samples. It retains original records in memory/protected pipes,
closes trusted descriptors for payload exec, reaps descendants, and seals
wall/CPU/RSS rows plus derived summaries using existing median helpers. Local
normal and sanitizer suites each passed **31,011/0**; existing throughput
30 focused + **122,930** integration assertions passed. It is a diagnostic
process-launch-through-wait4 measurement with an exit-zero oracle and unchecked
transcript, not a qualified statistical acceptance result. Stable binary
SHA256 is `2e9c8b974dd8cf9b38d882a5db43a9422354ef50d7937fdf6fbf18eae033cd4c`;
rebuild from the exact branch rather than expecting a committed executable.

Off-host current source passed **33,135/0** normal assertions. Its stress
fixture made 100 submission attempts (7 accepted, 93 capacity-full), 1,000
cached reads and **zero ordinary worker frames**, plus an exceptional cancel.
Earlier sanitizer **33,037/0 predates final source**. The control daemon caches
state and sealed results; a fixed operator-configured SSH worker endpoint
preserves immutable assignments, the whole-job quiet boundary, cancellation
intent and terminal ACK. Same-OFD lease custody persists through the custodian
until matching ACK. Losing all custodians/reboot is a recorded lease-gap
quarantine, not a claim that an OFD survived. Only native **execute** CAS
transfer is implemented; runtime/custom C transfer is not yet admitted.

The comparison draft passes existing regressions (throughput **122,930/0**,
service **29,599/0**, broker/gate tests). These do **not** exercise the new
comparison recipe. Its intended path reuses `tp_run`/`tp_compare`, matched
serial source/build roots, frozen baseline inputs, fixed trusted Clang build
argv and protected original evidence. Read the detailed
[comparison blockers](agent-notes/compiler-compare.md) before editing.

## Source coordination and integration hazards

- Main owns MCP translation/upload/doc changes. Off-host also edits MCP
  observation rendering. Preserve both when resolving conflicts.
- Runtime owns native execution/sampling and optional descriptor process
  support in `tools/throughput/platform.h`; its ordinary `tp_process` behavior
  is unchanged. A two-line median-only guard is in `stats.h`.
- Compare owns `tools/throughput/evidence.h` opt-in protected streams and
  trusted compare build/recipe policy; it must use the existing measurement
  and replay code, without a second compiler loop or changed thresholds.
- Reserved values: native execute enum 6 / selector 3 / stage 29; runtime enum
  7 / selector 4 / stage 30; custom C enum 8 / selector 5 / stages 31–32 / public
  upload operations 17–19; compiler compare enum 9 / selector 6 / stages 33–48.
  Native public uploads are operations 14–16; export stays operation 13.
- Current native normal journal is 4. Runtime/custom C/compare share normal
  generation **6**. Off-host assigned journal is **7**. Historical worker
  transition threshold remains **3**. Avoid reintroducing equality checks that
  reject historical replay or assigned requests as later recipes appear.
- Off-host history contains MCP and native intermediate source, including an
  equivalent Windows heap correction; do not blindly cherry-pick its entire
  history over main. Flatten/reconcile its owned slice on a separate integration
  branch, preserving root's already-integrated native and MCP edits.
- Runtime source `3ea60463c` follows native source/ack plus equivalent heap fix
  `fc9f83e0b`. Cherry-pick the runtime source slice, not duplicate native/heap
  history. Perform fresh combined validation after conflict resolution.
- Candidate-writable named files or logs cannot establish trusted benchmark
  samples, compiler evidence or runtime subject identity. Keep the protected
  collector/pipe/sealed original boundary throughout new work.

## Suggested next steps

1. Refresh issues, ownership, branches and repository instructions. Review the
   nine-tool upload adapter independently and extend the preserved blackbox
   harness (it still expects six tools). Keep failed attempts and exact binary
   hashes; `bench_service self-test` rebuilds tests, **not the CLI**.
2. Correct the known recipe-list policy regression. Independently review and
   integrate runtime `3ea60463c` into main; update MCP runtime schemas/capabilities
   only after the backend route is actually available.
3. Review/integrate off-host control, extend unchanged native CAS transfer via
   `bq_recipe_native(execute/runtime)`, run fresh current sanitizers and add
   operator deployment documentation. Account/UID/GID, fixed host/key pins,
   SSHd quiet behavior and actual systemd stop policy must be concrete. New
   deployment reference files were **not** saved by the agent.
4. Finish the comparison draft's broker installed-program identity mapping,
   protected-stream tamper/replay tests, real fixed-Clang subject-build and
   correctness validation, post-collection resource closure, durable schema/
   worker/result/export binding, docs and sanitizers. The detailed agent note
   also records the protected `GITHUB_ACTIONS` stdout annotation edge and
   bounded source-manifest provenance concerns. Independently review it.
5. Implement custom single-C source upload/compiler samples/runtime samples
   in the reserved separate slice. Proposed fixed freestanding source calls
   `benchmark_main`; no caller includes/flags/dependency trees/shell overrides.
   Fresh object/compiler hashes establish compiler samples; link preparation is
   untimed. Transfer generated ELF through a protected pipe into service-owned
   verified read-only storage before runtime. This is a proposal, not source.
6. Add bounded MCP raw evidence retrieval. Current `bench_result` exposes only
   bound digests and `bench_logs` only lifecycle events; **artifact_download is
   false**. Existing CLI export/unpack verifies the original receipt/archive.
   Reuse it with typed receipt and bounded byte-slice tools, no caller paths.
   Parent inspected this seam but wrote no artifact-tool source before stop.
7. Run registered service/broker/gate/policy/harness normal and sanitizer checks
   plus required compiler correctness after build behavior changes; commit,
   regenerate source-size acknowledgements per policy, obtain independent
   review and exact-head hosted validation. Draft placeholders are not gates.
8. With the real separate control host and ChatGPT tunnel selected, prepare a
   concrete reviewable deployment under the user-authorized scope. Record
   installed binary/profile/toolchain identities, effective containment,
   whole-job lease/cleanup/recovery, actual writes/reconnect/result retrieval
   from web and Codex, traffic stress and qualified service-profile evidence.
   No fixture, successful payload execution or published branch closes #437.

MCP implementation limits for artifact work: input/output buffers are 16 KiB,
512 JSON tokens and depth 32. Export op 13 uses job/token/full-result SHA,
cursor and receipt SHA; `UINT64_MAX` requests its 1024-byte canonical receipt,
otherwise the backend serves aligned 64 KiB chunks. A typed 4 KiB base64 slice
can fit duplicated structured/text content, but measure output/token bounds
and preserve strict reply binding. The receipt's archive hash must still be
checked over reconstructed bytes; do not call an arbitrary slice sealed whole
archive evidence. No new artifact implementation or tests exist yet.

## Host and repository facts already checked

`tailscale ssh david@benchpress` succeeded; `david` is UID 1000 with passwordless
administrative access. Benchpress is AMD Ryzen 7 9700X, 8 cores / 16 threads,
Linux x86-64 kernel `6.18.50-2-lts`; CPU 10 was offline in the snapshot. It has
the legacy same-host synchronous service, installed binary SHA256
`416a47dc1046efc8db54a40fc39f5a9bed279f3580b6d3e3f0be7fec7d32de1c`, with only
validate/calibration served. At 18:19:49 UTC the lease inode was stable
(device 41, inode 194974, one link, mode 0640, owner/group `buster-bench`). See
[bounded receipt](evidence/benchpress-receipt.txt). These are historical
read-only facts, not continuous exclusivity/noise proof. No service install,
restart, submit/cancel, benchmark, SSH policy or account change occurred.

GitHub contribution settings were checked in the signed-in UI: **PR creation
is Collaborators only**. `davidgmbb` is the only collaborator with push access;
read-only collaborator `buster14a14a` can still open PRs. It is not literally
"only me can create PRs." Public repository/forking and service-call authority
are separate facts. No repository setting was changed. Current Actions allow
all actions and use a default write token with PR approvals enabled; do not
expand or change settings as part of this handoff.

Historical #880/#1162 and direct lab #422/#426 outcomes do not establish this
new service profile's installed qualification. #881 was superseded; #923's
old retirement recipe is obsolete/blocked. #1216 is the unmerged older
comparison prototype; it was inspected, not overwritten or cherry-picked
wholesale. Current [compare source claim](https://github.com/buster14a/buster/issues/1190#issuecomment-5983926162)
and [integration claim](https://github.com/buster14a/buster/issues/437#issuecomment-5983823179)
identify this session's ownership. Preserve retirement-generated identities
and protected writer rules; don't resurrect obsolete routes to make checks green.

## Reproducibility and evidence

[evidence-index.json](evidence-index.json) hashes the included receipts. The
three agent notes provide detailed implementation and remaining-work context.
Their original local paths are historical provenance; use the published
branches and this repository evidence directory on a new machine. Logs and
JSON may contain temporary fixture paths; those do not imply reusable live
artifacts. No credentials or executable build products are committed.

Read `AGENTS.md`, `docs/agents/{build,testing,workflow,style,benchmarking}.md`,
`docs/source-size.md`, `tools/throughput/{README,DEDICATED}.md` and the service
docs on the relevant branch. C only, no new dependencies, explicit bounded
storage, one return per function, warnings-as-errors and required portability
flags apply. Keep one source writer and one build owner per worktree. Do not
run builds or edits in another live writer's checkout.

Typical validation from a real checkout:

```sh
./build.sh bench_service self-test
./build.sh bench_service self-test --sanitize
./build.sh bench_service_broker self-test
./build.sh bench_throughput self-test
./build.sh bench_service capabilities
python3 -B tools/bench_service/workflow_policy_test.py
python3 -B tools/bench_service/dispatch_recipe_test.py
python3 -B tools/bench_service/zen5_profile_test.py
python3 -B tools/bench_service/deploy/verify_github_actor_policy_test.py
python3 -B tools/bench_service/deploy/verify_github_admission_test.py
python3 -B tools/bench_service/deploy/verify_github_queue_test.py
git diff --check
```

Confirm the exact throughput subcommand against current build guidance before
running expanded suites. Commit source growth before `source_size --base`.
Past the 32 KiB category allowance, generate `--write-baseline` and commit its
output; never hand-edit its numbers. Final combined checks are still required.
The Windows heap correction is already on main: do not regress large `BqQueue`
fixtures onto the default 1 MiB stack. Failed stale-binary/path-bound harness
attempts and the Windows diagnosis are deliberately retained in evidence.

All agents stopped without running builds at handoff. The original workspace
was `/home/david/Documents/Codex/2026-10-04/solve-https-github-com-buster14a-buster-2`,
with separate worktrees under `work/`. No merge, queue integration, issue
closure or actual deployment happened during this session.
