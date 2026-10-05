## Observed

During the audit in PR #2627 (2026-10-04, 12:40–13:10 local), three agent sessions shared one checkout of this repository and the documented fixed paths. Concrete effects, all observed from the session that filed this issue:

1. `./build.sh generate --build-directory build-census` issued by another session (parent process: the same `devin acp` host; command visible in `ps`) deleted and recreated `build-census/` while this session's build of the same directory had just finished; `build/` and `build-perf/` were also removed and `build/Release/ide` was rebuilt by another session minutes after this one built it (identical sha256 only because the build is deterministic).
2. `/tmp/ide-base` and `/tmp/lab/*` — the exact paths `docs/agents/benchmarking.md` suggests (`cp build/Release/ide /tmp/ide-base`, `--output /tmp/lab`) — disappeared mid-session, taking a frozen compiler and 1.6 GB of `perf.data` with them.
3. A self-host compile read `src/buster/lib/compiler/frontend/c/c_gen.c` while another session was writing it and failed with `c_gen.c:5678:1: unknown type name 'BU'` and `c_gen.c:16879:1: unterminated function body`; `git status` then showed another session's uncommitted edits (`c_gen.c`, `work_ledger.h`, later `ir.c`, `ir_fast.c`, `ir_internal.h`) and an untracked audit file minted by a third session.

The audit therefore rebuilt everything from a pristine detached worktree (`git worktree add --detach ~/buster-lab/src-pinned 94976ecc`) with session-unique build directories and output paths; nothing built from the shared checkout is cited.

## Expected

AGENTS.md already requires one writer per branch and says `generate` deletes its build directory and must not run beside anything using it, but the guides assume one session per checkout: the benchmarking guide hard-codes `/tmp/ide-base`, `/tmp/ide-cand`, `/tmp/lab`, `/tmp/ab`; `docs/work-ledger.md` hard-codes `build-census`; nothing tells a second session on the same machine to take a worktree. Parallel sessions on one desktop are now the normal case (#1751 recorded duplicate-work and coordination gaps; this is the file-system side of the same problem).

## Affected files

`docs/agents/benchmarking.md` (A/B recipe, lab output paths), `docs/work-ledger.md` (`build-census`), `docs/agents/workflow.md` (session coordination), `build.c` (`generate` deletion policy).

## Validation already run

Observation only: `ps` parent chains, `git status`, the failed compile output and the vanished paths are described in the audit's provenance note (`docs/performance-audits/2026-10-04T111831Z.md`, PR #2627). No source changed.

## Uncertainty

Which session deleted `/tmp/lab` is inferred from the shared fixed path, not observed; the `build-census` deletion is observed (the other session's `generate` command was running with that directory while this session's copy of the binary had already been taken).

## Completion criteria

1. The benchmarking and work-ledger guides recommend a per-session detached worktree for any session that builds or measures, and session-unique paths (for example `${TMPDIR:-/tmp}/<session>/…`, `build-<session>-census`) instead of the fixed `/tmp/ide-base`, `/tmp/lab`, `build-census` spellings; the uarch lab's examples follow.
2. `build.sh generate` refuses to delete a build directory that another live process is using (a lock file or an open-descriptor check under the directory), with a message naming the holder, so a second session fails fast instead of destroying a running build; `build --build-directory` keeps working on an existing tree.
3. A short "parallel sessions on one machine" paragraph in `docs/agents/workflow.md` pointing at both.
