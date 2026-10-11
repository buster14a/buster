# Same-run Clang analyzer deduplication: retained evidence

## Disposition

This bundle retains the original per-row analyzer run, its invocation census,
the matched two-row cost probe, and the final plain-O0 reader/self-test run. It
does not qualify a speedup or authorize default adoption. A matched four-arm
two-row campaign took 0.287 seconds on the baseline driver and 131.190 seconds
on the candidate driver. The candidate's context proofs and runtime-closure
hashing dominate this workload. Performance acceptance remains open pending an
approved matched Zen5 workload; this cloud's descendant process and RSS
coverage is unavailable and its coordinator-only samples are not whole-tree
resource evidence.

## License scope

Buster's first-party license grant remains unselected under issue #621; see
[`LICENSES/README.md`](../../../../LICENSES/README.md). The installed LLVM/Clang
toolchain is identified as `Apache-2.0 WITH LLVM-exception` in the retained
[`LICENSES/llvm-LICENSE.txt`](../../../../LICENSES/llvm-LICENSE.txt). This work
used the installed Clang and imported no external implementation. This is not
a package-by-package distribution license audit.

The exact 182-row / 135-group / 47-repeated-row census below is the historical
original artifact at source revision
`44c7ebaa0f7d8900e14c157f0f6243ba7d26a53a`. It is retained as derivation
evidence, not represented as the current candidate census. A fresh census on
the rebased candidate and current authoritative compile database is recorded
separately below.

## Historical original artifact

`original-artifact.zip` is the original per-row result archive. Its SHA-256 is
`c42682fb9453d117f587c05bdd3e068c97c52ed7867c760d761cb646181cb07d` and its
size is 168,213 bytes. `original-derivation.json` records the exact selected
row count, invocation-group count, repeated-row count, source revision,
compile-database SHA-256, and manifest SHA-256. `issue3130-census.json` retains
the per-source derivation. The archived manifest and row results are available
inside the original ZIP.

## Rebased current-source census

The current native census was produced from local code commit
`e99f087d4d87bb31ba7fbf43f2dbfc1afd1613a7`, tree
`501876983e66df54ba9bc9012213c2c1e97ba7b5`. Its selection database was
regenerated with the exact CI options: `--ci --no-sanitize --no-fuzz --no-lto
--linker DEFAULT -- -DBUSTER_UNITY_BUILD=OFF`. Clang was
`/usr/lib/llvm-21/bin/clang`, Ubuntu Clang 21.1.8. The generated database has
364 entries and SHA-256
`4796d6a705ab2eb73e7810f6c9865b1ec4818063cc76226dd285c961dca3a49f`.
The Release plan selected 182 rows, formed 12 candidate groups, proved all 12,
and retained 135 executions plus 47 aliases. Planning took 8.090 seconds and
context proof took 7.922 seconds. `current-source-census.json` records producer,
bootstrap, database and manifest provenance; the compressed PLAN_V2 manifest
is `current-source-plan-v2.txt.gz` and the captured prepare result is
`current-source-prepare.log`.

This used the direct compiler path in the generated database. The hosted
`/usr/bin/clang` spelling was unavailable here, so this does not replace the
exact-head hosted check of that argv0 and symlink path. It is a census of
eligibility, not a performance result. No full Buster run or approved Zen5
matched run was performed.

Context checks establish observed stability under a caller-managed
immutable-run assumption; they are not filesystem locks. Inputs and toolchain
state must not change concurrently between planning and postflight. A transient
out-of-tree negative lookup that changes and is restored between checks is not
ruled out, so this proof does not claim unconditional input authentication or
universal semantic equivalence.

## Matched O0 cost probe

Both arms used a two-row `alias.c` database, four shards, four qualification
samples, and the same installed Clang (`Ubuntu clang version 21.1.8`, target
`x86_64-pc-linux-gnu`). The bootstrap driver was built with the documented
hosted Clang command (`clang -Isrc -Wall -Werror -Wno-unused-function
-Wno-unused-variable -g build.c -o DRIVER`): no optimization switch was set,
so the driver uses Clang's default O0 mode. The saved comparison drivers were:

- Baseline driver SHA-256: `2ca1d00a25b2befb78cbb70aff5caa6de899669597e9456385f606e61f50e67c`
- Candidate driver SHA-256: `e0d55f4003bcd90cc4ad10d39877e42b3e295c9cf4164bb45bbf0cb792e0966b`

The baseline was built from `834d0fe665c1723af12df9a1ba44bb19a3670ba6`.
The candidate probe preceded the final private-self-test-root guard; the guard
only changes self-test fixture ownership. The final plain-O0 test driver has
SHA-256 `02d9eccc3b8caefda05e324dc5f4cdedbca2eaf80f1acf40387be07ca11cb050`.
Its full reader suite passed 12 tests in 280.631 seconds. The standalone native
self-test passed 124 checks and 47 expected rejections with zero failures.
After adding the observation-only ordinary-run plan line, a rebuilt Clang 21
default-O0 driver (SHA-256
`784ba3db84e9fc71fef51ee32d26a500fdd8a16d6056f475fd4d8ee6ea533c47`) ran the
same two-row alias fixture in one shard. It reported 2 selected rows, 1 unique
execution and 1 alias; planning/proof took 5.795/5.794 seconds, preflight and
postflight took 5.911/5.603 seconds, and the terminal run passed in 23.179
seconds. This confirms the new ordinary-run timing record and unchanged result
records without repeating the long reader campaign. `ANALYZE_RUN elapsed_us`
includes setup; the new `ANALYZE_PLAN mode=run` line reports it separately.

The candidate four-arm elapsed time was 131.190 seconds versus 0.287 seconds
for baseline. A representative owned worker spent about 5.8 seconds planning
and proving the group, 5.8–6.3 seconds in preflight, and 5.8–6.3 seconds in
postflight. The `ldd` runtime closure is replayed and content-checked to detect
loader-resolution changes; hashing that closure accounts for much of the
repeated proof cost. These measurements demonstrate substantial overhead in
this small fixture. They do not estimate performance on the full Buster
corpus.

Only the real-Clang alias reader fixture timeout was raised to 300 seconds,
above its measured O0 duration. Analyzer, translation-unit, workflow, and
production deadlines remain unchanged.

After the analyzer-specific predefined macro correction, the plain-O0 native
self-test passed 128 checks and 47 expected rejections with zero failures. The
O0 worker-budget reader suite passed all 12 tests in 305.840 seconds. The new
controls prove that analyzer-only `__TIME__` use falls back, analyzer-guarded
headers enter the positive dependency closure and invalidate after content
change, and an analyzer-guarded absolute negative lookup invalidates when its
header appears. Both `-M` and `-E` probes now preserve `--analyze` while keeping
their final action. The option-token control preserves the ordinary Clang
`-Wreserved-module-identifier` warning flag while module inputs remain
unsupported.

## Reproduction and limits

The committed-revision source-size report against `834d0fe665c1723af12df9a1ba44bb19a3670ba6`
records +244,814 ratcheted build bytes and zero production growth. The changed
build files are `tools/clang_analyze.c` (+240,428 bytes) and
`tools/analyzer_worker_budget.py` (+4,386 bytes). This growth covers compiler
and symlink resolution, runtime closure replay, dependency and search-tree
proofs, analyzer-mode preprocessing, conservative eligibility, row-level V2
accounting and independent worker/aggregate verification. The report is
retained as `source-size.txt`. Its status is unacknowledged until the exact
reachable server growth revision is imported and the generated baseline is
regenerated from that commit; no baseline was edited here.

`attempts.zip` retains the two cost campaigns, inputs, manifests, result
records, timing samples, final test logs and earlier failed diagnostic
attempts. It includes the shared-self-test-root investigation, the initial
false-rejection census caused by matching `-Wreserved-module-identifier` as a
module option, and the first failed warning-token control with its corrected
passing self-test. Its inner `attempts/SHA256SUMS` verifies every archived
file. The self-test root race was caused by two concurrent self-tests claiming
an insufficiently unique path while the losing invocation still wrote
fixtures. The source now uses bounded exclusive path claims and guards every
fixture write on successful ownership.

The test emits process-count and sampled RSS fields, but this cloud cannot
confirm descendant enumeration completeness. Do not treat these values as
whole-process-tree resource measurements. No laptop or dedicated Zen5 run was
performed. The implementation's performance and memory acceptance therefore
remain incomplete.
