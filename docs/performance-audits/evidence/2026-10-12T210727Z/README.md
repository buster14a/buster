# Evidence for audit 2026-10-12T210727Z

Raw evidence for [audit `2026-10-12T210727Z`](../../2026-10-12T210727Z.md)
(issue #3102, PR #3893). Every file here is stored as produced, and
`SHA256SUMS` pins its bytes. Everything is hosted and diagnostic: a shared
cloud Intel Xeon VM, not the 9700X. It was produced and published by the
same session that wrote the audit, and nobody has reproduced it
independently.

## Revisions and binaries

- **Base:** `main` `f0f84944`.
- **Candidates:** `d306be92` (superseded) and `86ef49d0` (measured). The
  later commits on the branch change only tests and docs.
- **Binaries:** tests-off Release `ide`
  (`generate --cc clang --no-include-tests -- -DBUSTER_NATIVE_TARGET=x86-64-v3`,
  Clang 18.1.3). They are not retained: each is 33 MB, and they rebuild from
  their revisions. `binaries-and-objects.sha256` lists their hashes:
  - `ide-base`: base, arms A and B;
  - `ide-cand`: `d306be92`, superseded arms C and D;
  - `ide-cand2`: `86ef49d0`, arms C2 and D2, which are C and D in the audit;
  - `ide-noreplay` and `ide-noscalar`: the two part variants;
  - `ide-census-counting`: the counting build of the base.
- **Objects:** not retained either (31 MB each). The same file lists their
  hashes:
  - `arm-*.o`: `-c -g0` under Callgrind;
  - `g-*.o`, `q-*.o`: native `-g` and `-g0` runs;
  - `debug*.o`: Debug `ide`s of `d306be92` (`debug`) and `86ef49d0` (`debug2`).

## Files

- **`callgrind-outputs.tar.xz`** holds every Callgrind output. Their
  uncompressed hashes are in `callgrind-outputs.sha256`:
  - `census-A-{syn,c}.out`: the census runs of arm A, made before the
    budgets;
  - `census-A-syn-sep.out`: the same with
    `--separate-callers1=c_parse_infer_initializer_array_count_core`;
  - `arm-{A,B,C,D}-{syn,c}.out`: the first arms; C and D are `d306be92`;
  - `arm-{C2,D2}-{syn,c}.out`: `86ef49d0`;
  - `arm-V{noreplay,noscalar}-{syn,c}.out`: the part variants.

  Annotations come from `callgrind_annotate --inclusive=yes <file>`, and
  per-line costs from `--auto=yes`.
- **`arm.sh`**: the script that ran each arm, from the base checkout.
- **`census-counting-build.patch`**: the throwaway counters applied to the
  base for the census. `logs/census-counters.txt` is its output. It was
  re-run for this bundle with the build that added the last four counters.
  The first run's output, with the first 24 counters only, was not kept, and
  the shared counters match it.
- **`variant-without-replay.patch`** and **`variant-without-inline-end.patch`**:
  the two variants, as diffs against `86ef49d0`.
- **`memory-breakpoint-*.gdb`**: the gdb scripts behind the memory figures.
  - `logs/memory-breakpoint-86ef49d0.txt` is the raw run on a Debug `ide`
    of `ef758612`, whose compiler code is that of `86ef49d0`. It prints one
    `KEEP <elements> <bounds>` line per kept log.
  - The `d306be92` run was summarized and its raw output was not kept. The
    `86ef49d0` run supersedes it, with the same totals: 94 logs, 326,329
    elements and 6,133 bounds.
- **`logs/`**:
  - `arm-*.log` and `census-*.log`: Callgrind's stderr, with the totals;
  - `v3cand*.log`, `v-*.log`: the builds;
  - `debug-tests*.log`: the Debug module tests. The first run was before
    the test-expectation fix, and the second only `c_frontend_tests` after
    it;
  - `debug*-unity*.log`: Debug compiles of the unity input;
  - `selfhost.log`: `test_self_host --config Release`.
- **`test_all.log.xz`**: Release `test_all` on `ef758612`, compressed for
  size. The hash of the uncompressed log is in `test_all.log.sha256`.
