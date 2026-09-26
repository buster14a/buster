# Heavy correctness audit — September 25, 2026

## Result

Four newly confirmed defect families were filed after current-source inspection, bounded all-state issue/PR deduplication, two standard GitHub-hosted correctness runs and diagnostic restoration controls. No production repair or acceptance result is claimed.

| Suggested priority | Issue | Independently observed consequence |
|---|---|---|
| P1 | [#1335](https://github.com/buster14a/buster/issues/1335) | Narrow signed call arguments lose ABI extension: `-1` arrives as 255/65535 at Clang callees, including a typed indirect call. |
| P1 | [#1336](https://github.com/buster14a/buster/issues/1336) | Constructor/destructor bodies survive export, but their registrations vanish; the program exits 0 without either required effect. |
| P1 | [#1337](https://github.com/buster14a/buster/issues/1337) | Missing weak functions test present; replaceable weak definitions become strong and fail to link with intended overrides. |
| P2 | [#1338](https://github.com/buster14a/buster/issues/1338) | A defined function alias becomes an unresolved declaration. |

Priorities are recommendations, not formal GitHub priority assignments. The issues contain exact witnesses, source traces, commands, controls, repair boundaries and acceptance criteria.

The separate section-placement finding was already owned by [#1276](https://github.com/buster14a/buster/issues/1276). [Additional evidence](https://github.com/buster14a/buster/issues/1276#issuecomment-5841248991) shows the LLVM writer also drops the requested section, so a native-only repair will not cover that route. It is not counted as a fifth new issue.

## Exact identities

Production/main commit `ade6ac4b6ecb21f30b61b656439bac476c145e2f`, tree `4c5306221fdb22fccc929b55e333163742de17d0`, rechecked unchanged before filing. LLVM writer blob `bfdcda8bcb96f7150a374c5c9260bda3e074a306`.

The exploratory compiler was reused, not freshly rebuilt, from hosted run 36183831330/artifact 10885452801. Its SHA-256 was verified before new executions:

`57cc0f0ec6876bf8d591ea5eddb663da6cfbb4dc84da00e47bad686eceb20e80`

Final tested experiment commit `49d38ddb185ec98ea2ff8a349063d48a947ea577`, tree `c92ab0a4eea85625804fc14c8bcf1860801a5c0c`, is research transport, not production. This document is a later documentation-only publication and was not the tested experiment head.

## Runs and evidence

[Initial run 36202327487](https://github.com/buster14a/buster/actions/runs/36202327487) and [confirmation run 36202538099](https://github.com/buster14a/buster/actions/runs/36202538099) used standard GitHub Ubuntu 26.04 x86-64. Independent tools: Clang 21.1.8, GCC 15.2.0 and GNU Binutils 2.46.

Final artifact **10892113876**, `heavy-audit-36202538099-1`, ZIP SHA-256:

`37f362180814dfa0ba46c23c75e1b52a19bfe901e8d1b84ad1c91ff197bf2198`

The ZIP and all **693 root + 200 follow-up manifest entries** were independently verified after download. Retention ends October 25, 2026. The artifact retains exact source fixtures, argv/cwd, stdout/stderr, exit statuses, original/modified LLVM IR, assembly, objects, executables and provenance. Both generators are committed here: [probe.py](probe.py), [followup.py](followup.py).

All **20 diagnostic-restoration executions pass**: four ABI, four constructor-only, four weak-import, four weak-definition and four alias controls. They restore only the missing fact in copies of the exported IR. They are not production patches. Both frontend forms were exercised; the restoration matrix uses consumer O0/O2. Native controls pass for the four filed families. GCC callees mask the narrow-argument defect by extending their own inputs; the issue does not claim every external callee fails.

The final run records **231 scored stage checks: 191 pass and 40 fail**. This is not a defect count:

| Failed checks | Interpretation |
|---:|---|
| 16 | Repeated manifestations of the four filed roots |
| 3 | Existing section-placement root |
| 18 | Audit harness setup failures: twelve invalid Wasm target spellings and six rejected Node options |
| 3 | Module-assembly refusal/link observations, not promoted to a new issue |

The workflow is red because failures are retained, not suppressed. Successful callback-fixture exit status alone is insufficient: its expected output is missing.

## Unconfirmed and excluded work

No Wasm execution result is established. The initial `wasm32/64-unknown-unknown` spellings were unsupported. Corrected Wasm64 compilation succeeded, but Node 24.21.0 rejected the obsolete `--experimental-wasm-memory64` flag before instantiation. Function-pointer and alignment leads overlap #310/#1196 and #1332 and were not re-filed.

A module-assembly data-directive witness is explicitly rejected on the native path but loses its symbol on the LLVM route. Its admission boundary needs separate characterization; it is not included in the four confirmed new families. Existing frontend truth/eligibility reports #1224/#1225 were also excluded.

## Repair direction and limits

Prioritize scalar-call ABI and weak binding, then lifecycle registration and aliases. Keep one integration writer for overlapping LLVM writer changes; test development can be partitioned by ABI, lifecycle and symbol semantics. Preserve admitted semantics or refuse unsupported requests before publication. Independent consumers and semantic observers are necessary; successful bitcode parsing cannot detect these losses.

No fresh compiler build, trusted TCC bootstrap, full test_all, self-host acceptance, sanitized Buster execution, cross-platform execution or performance qualification was performed. No production source, existing writer branch, generated binding, retirement/service/admission/deployment policy or protection was changed. No benchmark or speedup claim.

Tools: GPT-6 Astra Pro session, connected GitHub, standard hosted jobs, local source/evidence processing and primary documentation. No LLM subagents or independent agent review were used. Independent validation refers to external compilers/linkers and separately compiled observers. Source scouting was broader than LLVM; dynamic confirmation concentrated on the strongest LLVM-boundary lead rather than certifying the whole repository.
