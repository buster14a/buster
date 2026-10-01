# CFI prefix lookup: source-count experiment

Related issue: #1309. Draft research: #1346. This follows an existing report; it does not claim the previously reported rescan as a new discovery.

## Identity and scope

Pinned main commit: `ade6ac4b6ecb21f30b61b656439bac476c145e2f`.

Pinned main tree: `4c5306221fdb22fccc929b55e333163742de17d0`.

`src/buster/lib/compiler/object/object.c` blob: `b798889750f58b6605064ffebcc0b376625b978a`.

The research branch adds three C files and a branch-scoped hosted workflow. Production compiler sources are untouched in the branch. `probe.c` patches only disposable, detached worktrees pinned to main. Only the Mach-O writer lookup is replaced experimentally; the Mach-O reader remains unchanged. This is not a complete fix for both call sites in #1309.

The investigation read AGENTS.md, frontend foundations/linkage, machine, testing, build and benchmarking guidance, the 2026-09-25T215313Z audit on `claude/laughing-noether-meufke`, current source, #1309 and its comments, and matching issue/PR searches. The existing audit author's branch was not edited. No independent callable subagent was available after tool/plugin discovery; the alternatives and falsification suite have one author, not independent model review.

## What information is repeatedly rediscovered?

`object_mach_eh_frame_record_start` maps a field offset and width to the start of the containing CFI record. Records are sequential, variable-length, nonoverlapping byte intervals. The source restarts at offset zero for each query, decoding every preceding record header again.

The writer knows the immutable section bytes and makes many lookups while processing relocations. A record end learned for one relocation is discarded before the next lookup. Neither another IR nor a general graph-analysis framework is needed: the useful fact is simply the ordered sequence of already-decoded record ends.

The helper checks null arguments, bounds, 32-bit and extended 64-bit lengths, zero terminators, record overruns and containment of the entire field. Its success depends on the prefix through the matching record, not on every later byte in the section.

## Operation model and minimum causal example

Let F be the number of FDEs after one CIE; let R be the number of lookups. For query i, let k_i be its 1-based FDE ordinal. On valid fixed-size records, the baseline header-decode count is exactly:

    H(F, R, k) = sum_i (k_i + 1)

One query in each FDE gives:

    H(F, F) = F(F + 3) / 2

This is Theta(F^2), while the record bytes and lookup-result count are linear. More generally, queries concentrated near the end cost Theta(FR). Large object output is not the explanation for repeatedly rereading identical headers.

A reduced causal example has a 24-byte CIE followed by two 24-byte FDEs. Query four bytes at offsets 32 and 56. The first query reads two headers; the second reads the same two plus the third. The original does five header decodes where three distinct record boundaries suffice. This two-query count is derived from source, not a separately recorded test row. The hosted grid includes the same F=2 layout with the sequence 32,32,56,56: the recorded baseline count is 10 and both remedies read 3 headers.

The corresponding small legal C source shape is:

```c
unsigned cfi_f0(unsigned x) { return x; }
unsigned cfi_f1(unsigned x) { return x + 1u; }
```

Do not assume a particular compiler emission from this source without checking production counters. Its actual unwind record size, count and order are compiler-output facts; the source-extracted byte-layout experiment is separate.

## Compared algorithms

### 0. Counted original scan

The test driver extracts the exact current helper, renames a copy and inserts one bounded increment immediately before its original record-length load. It does not substitute a handwritten baseline oracle. Both original and counted copy remain available for differential testing.

### 1. Forward hint: the simplest remedy

Retain the last [start,end) interval. A query in that interval returns immediately. A query after it continues scanning at end. An earlier query restarts at zero.

This needs constant index storage and O(P+R) work for monotonically increasing queries, where P is the prefix traversed. It also handles repeated queries to the same record cheaply. However, reverse distinct queries retain Theta(FR) work. The fallback is semantically correct but does not remove the worst-case complexity.

### 2. Demand-built record-end index

Retain a geometrically grown array of validated record ends, its covered prefix and an invalid/terminal stopping flag. Decode only as far as the next uncached query requires. Forward queries can return directly from extension; queries to the last returned record use a fast path. Other queries use upper_bound(end > field_offset), then check the complete field lies in that record.

For P decoded records, building the prefix and geometric copies cost O(P). Arbitrary queries cost O(P + R log(P+1)) overall; monotone/current-record queries use O(P+R). The implemented per-section metadata adds O(S) space for S object sections. It is intentionally not claimed to be O(1) per arbitrary query.

The cache belongs to one immutable section during one writer call. There is no global, TLS or pointer-identity cache and no cross-invocation invalidation protocol. Allocation lives in the existing enclosing arena; function return does not free old growth buffers.

### Equivalence invariant

After every query, the end array is a prefix of exactly the records the original parser accepts sequentially. Entries are strictly increasing and bounded by section length. The covered endpoint is the final entry, or zero for an empty prefix. A stopped prefix can still answer queries within its valid entries.

A successful result is the unique interval containing the entire queried field. Range arithmetic uses subtraction after bounds checks. A failed query leaves the caller's output unchanged. Empty sections, a query at section end, zero-width fields, malformed headers, zero terminators and extended lengths preserve the original boolean/output behavior.

An eager index that rejects the whole section when a later record is malformed is not equivalent: the old helper may already have returned success for an earlier record. An eager *tolerant-prefix* index could preserve this behavior, but would decode unqueried suffix records unnecessarily. The lazy prototype avoids both problems.

## Experiment dimensions

The source-extracted helper grid independently varies F in {1,2,4,16,64,256,1024,4096} and R in {0,1,4,16,64,256,1024}. It uses five order/position families: ascending, descending, deterministic shuffled, repeated last FDE and repeated first FDE. These are controlled helper query streams, not claims that every stream represents a complete legal object file.

The production experiment instead generates legal C translation units with independent function count and global function-pointer reference count, plus straight-line and branchy bodies. Its configured sizes are 16,64,256,1024,4096 functions and 0 or 1024 global references. These vary code/unwind population separately from unrelated data relocations. It compiles for x86-64 Mach-O, AArch64 Mach-O and an ELF control, records actual output bytes and actual helper calls, and refuses to call a zero-hit experiment a Mach-O count result. It does not assume global references are unwind queries.

Three matched diagnostic variants compare exact object bytes. An existing unrelated C ABI fixture is held out from generation. Baseline self-hosting is attempted before source patching; candidate self-hosting and test_all are also configured. No timing from these hosted diagnostic builds is performance evidence.

## Completed hosted helper evidence

Run: https://github.com/buster14a/buster/actions/runs/36205698566

Research head: `15b0eb0e5a23991f2a2b5cb35d8710c8916d95b7`.

GitHub-hosted Ubuntu 26.04, Clang 21.1.8, ASan/UBSan: **463,206 comparisons passed**, with **840 counter rows**. Checks compare the return value and output sentinel against the exact original helper, not just whether the program crashed. The suite also covers truncated sections, 64-bit record lengths, zero/invalid suffixes, forward and reverse queries on the same cache, null arguments and explicit counter saturation.

| F=R, ascending | Original headers | Hint headers | Index headers |
|---:|---:|---:|---:|
| 4 | 14 | 5 | 5 |
| 16 | 152 | 17 | 17 |
| 64 | 2,144 | 65 | 65 |
| 256 | 33,152 | 257 | 257 |
| 1,024 | 525,824 | 1,025 | 1,025 |

| F | R | Family | Original headers | Hint headers | Index headers | Index comparisons |
|---:|---:|---|---:|---:|---:|---:|
| 1,024 | 1,024 | reverse | 525,824 | 525,824 | 1,025 | 10,233 |
| 1,024 | 1,024 | shuffled | 525,824 | 347,369 | 1,025 | 10,192 |
| 4,096 | 1,024 | reverse | 2,097,152 | 2,097,152 | 4,094 | 12,276 |
| 4,096 | 1,024 | repeated last | 4,195,328 | 4,097 | 4,097 | 0 |
| 4,096 | 1,024 | repeated first | 2,048 | 2 | 2 | 0 |

For the F=4,096/R=1,024 reverse row, input CFI bytes are 98,328 and query-offset bytes are 8,192. The index's 4,094 rather than 4,097 headers reflect the largest queried ordinal in the evenly spaced family; it does not scan the unqueried suffix. Zero queries produce zero header reads and zero index allocations.

Artifact: `cfi-prefix-index-36205698566-1`, ID `10893344165`.

Archive SHA-256: `c7734e0d5af665dc493eb7bcd018e920e3ab5523eca89903c6028f5dd42c7338`.

Raw helper log SHA-256: `946e7847d85d7947c0c84b18a168a41e2ad3739237b81b9afbb43654662cc315`.

## Allocation and losing cases

The vector starts at 16 entries and doubles. If its final capacity is C, allocated entries retained across growth sum to 2C-16, and copied entries to C-16. For P=1,025 valid records, the hosted run records C=2,048, 4,080 allocated entries and 2,032 copied entries. Thus vector storage retained in the arena is 32,640 bytes, not merely the 8,200 bytes of useful endpoints. For a query into the first FDE, the vector still initially allocates 128 bytes, whereas the hint allocates no index.

The index adds allocation and stores without reducing header work for a single query. On ordered production workloads the hint may be the better implementation: it achieves the same decode count with less state. Reverse/shuffled inputs distinguish the robust index from that simpler choice. No timing establishes which wins end-to-end or the crossover point.

Instrumentation also allocates a per-section stats/cache array for the counted original. These are diagnostic builds, not matched performance candidates. A production change should remove diagnostic counters, retain lifetime checks, choose an allocation strategy based on actual production order and frequency, and separately address the still-unchanged reader.

## Production execution status at report creation

The first production attempt stopped before any compiler source patch because the hosted image lacked the existing mold prerequisite selected by build.c. Commit `579f16ba5a41511b7003038f48d42b93785465b6` installs the distribution mold as repository CI does. Retry: https://github.com/buster14a/buster/actions/runs/36205913976.

At this report's creation that retry is running. **No production-writer count, byte-equivalence pass, self-host pass, full-suite pass or end-to-end speedup is claimed by this version of the report.** Subsequent actual status belongs in the PR evidence update, not inferred from the configured checks.

## Terminal production run (recorded when landing)

Run [36205913976](https://github.com/buster14a/buster/actions/runs/36205913976), job 108302296111, research head `579f16ba5a41511b7003038f48d42b93785465b6`, GitHub-hosted Ubuntu 26.04, completed with conclusion **success** and printed `CFI_PRODUCTION_PASS`. Every step of `probe hosted` exits on the first failed command or contract, so reaching that line means each gate below passed on worktrees detached at pinned main `ade6ac4b6ecb21f30b61b656439bac476c145e2f`:

- baseline `test_self_host --config Release` before any source patch;
- Release `ide` builds of all three patched variants (counted original, forward hint, lazy index);
- 60 generated legal-C cases (5 function counts x 2 reference counts x 2 body shapes x 3 targets), each with byte-identical objects across all three variants, equal Mach-O call counts, index headers not above baseline, and zero calls on the ELF control;
- byte-identical held-out `tests/basic_c_call_abi.c` objects across the three variants;
- `test_self_host --config Release` and `build --config Release -t test_all` of the lazy-index variant.

Production-writer header counts per `__eh_frame` section (identical for x86-64 and AArch64 Mach-O, both body shapes, and 0 or 1,024 global references; calls equal the function count):

| Functions | Writer calls | Original headers | Hint headers | Index headers |
|---:|---:|---:|---:|---:|
| 16 | 16 | 152 | 17 | 17 |
| 64 | 64 | 2,144 | 65 | 65 |
| 256 | 256 | 33,152 | 257 | 257 |
| 1,024 | 1,024 | 525,824 | 1,025 | 1,025 |
| 4,096 | 4,096 | 8,394,752 | 4,097 | 4,097 |

The original counts equal F(F+3)/2 exactly, confirming the source model on real writer output. Global function-pointer references add no `__eh_frame` lookups. The writer issues its queries in ascending FDE order, so the zero-allocation forward hint already reaches the linear minimum there; the lazy index adds allocation without reducing writer header work. The index only matters for non-monotone query streams, which this run did not observe in the writer. The reader call site was not instrumented.

Artifact `cfi-prefix-index-36205913976-1`, ID `10893774988`, 407 files, archive SHA-256 `644287c19a864e6fcee1392619b413e61ac2cd18776a2ae7459f3649b3e1186e`, seven-day retention. The table above is transcribed from the job log's `CFI_CASE` rows.

These are diagnostic operation counts on pinned main, not timings. No end-to-end speedup is claimed, and none of these results was rerun on a later main.

## Landing state

The research record lands without production compiler changes. The branch-scoped hosted workflow `.github/workflows/research-cfi-prefix-index.yml` was removed before landing: it triggered on branch pushes and verifies the pinned `object.c` blob, so it cannot run against a later main. It remains recoverable at commit `76e4a26f873cd793b3f30377d0bf912d834cdb3b`.

## Reproduction

The helper differential runs from the repository root on any revision whose `object_mach_eh_frame_record_start` still has the extraction anchors (`probe extract` fails closed otherwise). It needs a host C compiler with ASan/UBSan runtimes:

```sh
mkdir -p /tmp/cfi
cc -std=c11 -O1 -g -Wall -Wextra -Werror \
  tools/research/cfi-prefix-index/probe.c -o /tmp/cfi/probe
/tmp/cfi/probe extract src/buster/lib/compiler/object/object.c \
  tools/research/cfi-prefix-index/prototype.h /tmp/cfi/generated-probe.h
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  -fno-omit-frame-pointer -I/tmp/cfi \
  tools/research/cfi-prefix-index/differential.c -o /tmp/cfi/differential
/tmp/cfi/differential > /tmp/cfi/helper-counts.log
```

It prints `CFI_HELPER_PASS checks=463206` and 840 `CFI_HELPER` rows. On main `d68265991e874f1151186b7765a82483c51ca14f` (current `object.c` blob `ff2b06feca6b9b3baf306c60356c6e965ff8746e`), GCC 13.3 with ASan/UBSan and Clang 18 (probe only) reproduced that pass and the reverse-order counts above; `probe patch` also still applied all three modes. That checks the helper and anchors only; the production-writer run was not repeated on that revision.

The executable research driver exposes:

```text
probe extract <object.c> <prototype.h> <generated-probe.h>
probe patch <object.c> <prototype.h> <mode:0|1|2>
probe generate <output.c> <function-count> <global-reference-count> <shape:0|1>
probe hosted
```

`patch` requires unique exact source anchors and fails rather than guessing after source changes. `probe hosted` is the production-writer experiment: it refuses to run outside GitHub Actions, checks that the checked-out `object.c` is the pinned blob, and creates three detached worktrees at the pinned commit, so it reproduces only the pinned experiment. To rerun it, restore the workflow from `76e4a26f873cd793b3f30377d0bf912d834cdb3b` on a research branch whose checkout carries the pinned blob and push that branch. To measure a later main, update `PIN`, the blob checks and the anchors together, and record the new revision rather than reusing the numbers above.

No approved exclusively leased 9700X path was used. No desktop compiler test or benchmark and no ad-hoc SSH was used. Operation-count reductions are not measured speedups. C-only/no-new-production-dependency, canonical validation, portability, self-hosting requirements and lane contracts remain unchanged; the packet is a draft experiment, not admission or acceptance evidence.
