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

## Reproduction

Use the branch-scoped workflow on an authorized hosted executor. It bootstraps build.c with the documented hosted Clang exception, installs the existing mold prerequisite, and calls the repository's native driver for compiler builds. It verifies the pinned object source blob and retains evidence even on failure.

The executable research driver exposes:

```text
probe extract <object.c> <prototype.h> <generated-probe.h>
probe patch <object.c> <prototype.h> <mode:0|1|2>
probe generate <output.c> <function-count> <global-reference-count> <shape:0|1>
probe hosted
```

For example, on the hosted executor:

```sh
clang -std=c11 -O1 -g -Wall -Wextra -Werror \
  tools/research/cfi-prefix-index/probe.c -o "$RUNNER_TEMP/cfi-probe"
"$RUNNER_TEMP/cfi-probe" generate minimal.c 2 0 0
"$RUNNER_TEMP/cfi-probe" generate references.c 256 1024 1
```

`patch` requires unique exact source anchors and fails rather than guessing after source changes. The hosted path has one integration writer and three disjoint detached worktrees. Generated source, object files, logs, source diffs and compiler hashes are retained by the workflow.

No approved exclusively leased 9700X path was used. No desktop compiler test or benchmark and no ad-hoc SSH was used. Operation-count reductions are not measured speedups. C-only/no-new-production-dependency, canonical validation, portability, self-hosting requirements and lane contracts remain unchanged; the packet is a draft experiment, not admission or acceptance evidence.
